#include "FastDDSDebugNode.h"

#include "YomkDdsDebugService.h" // yomk::g_debugPubStop 持续发布停止标志（API 头内联定义）

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <utility>

#include <fastdds/dds/core/policy/QosPolicies.hpp>
#include <fastdds/dds/core/status/StatusMask.hpp>
#include <fastdds/dds/core/status/SubscriptionMatchedStatus.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicData.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicDataFactory.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicPubSubType.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicType.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicTypeBuilderFactory.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicTypeMember.hpp>
#include <fastdds/dds/xtypes/dynamic_types/MemberDescriptor.hpp>
#include <fastdds/dds/xtypes/dynamic_types/TypeDescriptor.hpp>
#include <fastdds/dds/xtypes/type_representation/TypeObject.hpp>
#include <fastdds/dds/xtypes/utils.hpp>
#include <fastdds/rtps/reader/ReaderDiscoveryStatus.hpp>
#include <fastdds/rtps/writer/WriterDiscoveryStatus.hpp>

using namespace eprosima::fastdds::dds;
// using-directive 不注入兄弟命名空间：文件作用域内以别名保持 rtps:: 前缀与 FastDDS 头内写法一致
namespace rtps = eprosima::fastdds::rtps;

namespace
{
std::string nowStamp()
{
    using Clock = std::chrono::system_clock;
    constexpr int kMicroDigits = 6;
    constexpr long long kMicrosPerSec = 1000000;
    const auto now = Clock::now();
    std::time_t sec = Clock::to_time_t(now);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count() % kMicrosPerSec;
    std::tm local {};
    localtime_r(&sec, &local);
    std::ostringstream os;
    os << std::put_time(&local, "%H:%M:%S") << '.'
       << std::setw(kMicroDigits) << std::setfill('0') << us;
    return os.str();
}

// 种子混入 random_device 与单调时钟，同进程重复调用不复现；uid 后缀使多进程各自的调试/
// 录制节点在 node list/node info 视角可区分（固定名参与者会同名列出、端点被合并）
std::string makeUidSuffix()
{
    constexpr int kUidHexChars = 12;
    constexpr std::uint64_t kUidMask = (std::uint64_t{1} << (kUidHexChars * 4)) - 1;
    constexpr int kRdBits = 32;
    std::random_device rd;
    std::uint64_t seed = (static_cast<std::uint64_t>(rd()) << kRdBits) | rd();
    seed ^= static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::mt19937_64 gen(seed);
    std::ostringstream os;
    os << std::hex << std::setw(kUidHexChars) << std::setfill('0')
       << (gen() & kUidMask);
    return os.str();
}

// ---- topic info verbose 的端点详情辅助（EndpointDetail.qosLines 的行来源） ----
// 展示名对齐 ros2 topic info -v 的输出习惯（去 _QOS 后缀），枚举拼写对齐 FastDDS 3.6.1 QosPolicies.hpp
std::string formatDuration(const Duration_t& duration)
{
    if (Time_t::is_infinite(duration))
    {
        return "Infinite";
    }
    std::ostringstream os;
    os << duration.seconds;
    if (duration.nanosec != 0)
    {
        constexpr int kNanosDigits = 9;
        os << '.' << std::setw(kNanosDigits) << std::setfill('0') << duration.nanosec;
    }
    os << " s";
    return os.str();
}

std::string guidString(const rtps::GUID_t& guid)
{
    std::ostringstream os;
    os << guid;
    return os.str();
}

// Precondition: 调用方须已持 seenMtx_；归属判定依赖 RTPS 规范保证的端点 GUID 前缀 == 所属参与者 GUID 前缀
std::string participantNameOf(
        const std::map<rtps::GuidPrefix_t, std::string>& participants, const rtps::GUID_t& guid)
{
    const auto it = participants.find(guid.guidPrefix);
    return (it != participants.end()) ? it->second : std::string();
}

const char* reliabilityKindName(ReliabilityQosPolicyKind kind)
{
    switch (kind)
    {
        case RELIABLE_RELIABILITY_QOS:
            return "RELIABLE";
        case BEST_EFFORT_RELIABILITY_QOS:
            return "BEST_EFFORT";
        default:
            return "UNKNOWN";
    }
}

const char* durabilityKindName(DurabilityQosPolicyKind kind)
{
    switch (kind)
    {
        case TRANSIENT_LOCAL_DURABILITY_QOS:
            return "TRANSIENT_LOCAL";
        case TRANSIENT_DURABILITY_QOS:
            return "TRANSIENT";
        case PERSISTENT_DURABILITY_QOS:
            return "PERSISTENT";
        case VOLATILE_DURABILITY_QOS:
            return "VOLATILE";
        default:
            return "UNKNOWN";
    }
}

const char* livelinessKindName(LivelinessQosPolicyKind kind)
{
    switch (kind)
    {
        case MANUAL_BY_PARTICIPANT_LIVELINESS_QOS:
            return "MANUAL_BY_PARTICIPANT";
        case MANUAL_BY_TOPIC_LIVELINESS_QOS:
            return "MANUAL_BY_TOPIC";
        case AUTOMATIC_LIVELINESS_QOS:
            return "AUTOMATIC";
        default:
            return "UNKNOWN";
    }
}

const char* ownershipKindName(OwnershipQosPolicyKind kind)
{
    switch (kind)
    {
        case EXCLUSIVE_OWNERSHIP_QOS:
            return "EXCLUSIVE";
        case SHARED_OWNERSHIP_QOS:
            return "SHARED";
        default:
            return "UNKNOWN";
    }
}

const char* destinationOrderKindName(DestinationOrderQosPolicyKind kind)
{
    switch (kind)
    {
        case BY_SOURCE_TIMESTAMP_DESTINATIONORDER_QOS:
            return "BY_SOURCE_TIMESTAMP";
        case BY_RECEPTION_TIMESTAMP_DESTINATIONORDER_QOS:
            return "BY_RECEPTION_TIMESTAMP";
        default:
            return "UNKNOWN";
    }
}

// Partition_t 名字为长度前缀 + 非 NUL 结尾字符串，须以 (name, size) 追加
std::string partitionNames(const PartitionQosPolicy& partition)
{
    std::string out = "[";
    bool first = true;
    for (auto it = partition.begin(); it != partition.end(); ++it)
    {
        if (!first)
        {
            out += ", ";
        }
        out.append(it->name(), it->size());
        first = false;
    }
    out += "]";
    return out;
}

// ROS2 对齐组：KEEP_ALL 时发现数据 Depth 为原值 0，照实输出；未携带时显式标 not advertised 而非静默跳过
void appendHistoryDepthLines(std::vector<std::string>& lines,
        const eprosima::fastcdr::optional<HistoryQosPolicy>& history)
{
    if (history.has_value())
    {
        lines.emplace_back(std::string("  History: ") +
                (history->kind == KEEP_ALL_HISTORY_QOS ? "KEEP_ALL" : "KEEP_LAST"));
        lines.emplace_back("  Depth: " + std::to_string(history->depth));
    }
    else
    {
        lines.emplace_back("  History: not advertised");
        lines.emplace_back("  Depth: not advertised");
    }
}

void appendResourceLimitsLine(std::vector<std::string>& lines,
        const ResourceLimitsQosPolicy& limits)
{
    lines.push_back("  Resource Limits: max_samples=" + std::to_string(limits.max_samples) +
            ", max_instances=" + std::to_string(limits.max_instances) +
            ", max_samples_per_instance=" + std::to_string(limits.max_samples_per_instance));
}

// writer 端点 QoS 行集：ROS2 对齐组 8 行恒前置，DDS 扩展组按发现数据携带情况追加（optional 一行不删）
std::vector<std::string> writerQosLines(const rtps::PublicationBuiltinTopicData& writer)
{
    std::vector<std::string> lines;
    appendHistoryDepthLines(lines, writer.history);
    lines.emplace_back(std::string("  Reliability: ") +
            reliabilityKindName(writer.reliability.kind));
    lines.emplace_back(std::string("  Durability: ") + durabilityKindName(writer.durability.kind));
    lines.emplace_back("  Deadline: " + formatDuration(writer.deadline.period));
    lines.emplace_back("  Lifespan: " + formatDuration(writer.lifespan.duration));
    lines.emplace_back(std::string("  Liveliness: ") + livelinessKindName(writer.liveliness.kind));
    lines.emplace_back(
        "  Liveliness lease duration: " + formatDuration(writer.liveliness.lease_duration));
    lines.emplace_back("  Latency Budget: " + formatDuration(writer.latency_budget.duration));
    lines.emplace_back(std::string("  Ownership: ") + ownershipKindName(writer.ownership.kind));
    lines.emplace_back("  Ownership Strength: " + std::to_string(writer.ownership_strength.value));
    lines.emplace_back(std::string("  Destination Order: ") +
            destinationOrderKindName(writer.destination_order.kind));
    lines.emplace_back("  Partition: " + partitionNames(writer.partition));
    if (writer.resource_limits.has_value())
    {
        appendResourceLimitsLine(lines, *writer.resource_limits);
    }
    if (writer.publish_mode.has_value())
    {
        lines.emplace_back(std::string("  Publish Mode: ") +
                (writer.publish_mode->kind == ASYNCHRONOUS_PUBLISH_MODE ?
                "ASYNCHRONOUS" : "SYNCHRONOUS"));
    }
    if (writer.transport_priority.has_value())
    {
        lines.emplace_back(
            "  Transport Priority: " + std::to_string(writer.transport_priority->value));
    }
    if (writer.writer_data_lifecycle.has_value())
    {
        lines.emplace_back(
            std::string("  Writer Data Lifecycle: autodispose_unregistered_instances=") +
            (writer.writer_data_lifecycle->autodispose_unregistered_instances ? "true" : "false"));
    }
    return lines;
}

// reader 端点 QoS 行集：同 writer 构成同序（无 Ownership Strength、多 Time Based Filter），optional 一行不删
std::vector<std::string> readerQosLines(const rtps::SubscriptionBuiltinTopicData& reader)
{
    std::vector<std::string> lines;
    appendHistoryDepthLines(lines, reader.history);
    lines.emplace_back(std::string("  Reliability: ") +
            reliabilityKindName(reader.reliability.kind));
    lines.emplace_back(std::string("  Durability: ") + durabilityKindName(reader.durability.kind));
    lines.emplace_back("  Deadline: " + formatDuration(reader.deadline.period));
    lines.emplace_back("  Lifespan: " + formatDuration(reader.lifespan.duration));
    lines.emplace_back(std::string("  Liveliness: ") + livelinessKindName(reader.liveliness.kind));
    lines.emplace_back(
        "  Liveliness lease duration: " + formatDuration(reader.liveliness.lease_duration));
    lines.emplace_back("  Latency Budget: " + formatDuration(reader.latency_budget.duration));
    lines.emplace_back(std::string("  Ownership: ") + ownershipKindName(reader.ownership.kind));
    lines.emplace_back(
        "  Time Based Filter: " + formatDuration(reader.time_based_filter.minimum_separation));
    lines.emplace_back(std::string("  Destination Order: ") +
            destinationOrderKindName(reader.destination_order.kind));
    lines.emplace_back("  Partition: " + partitionNames(reader.partition));
    if (reader.resource_limits.has_value())
    {
        appendResourceLimitsLine(lines, *reader.resource_limits);
    }
    return lines;
}

FastDDSDebugNode::EndpointDetail makeWriterDetail(
        const rtps::PublicationBuiltinTopicData& writer, const std::string& nodeName)
{
    FastDDSDebugNode::EndpointDetail detail;
    detail.nodeName = nodeName;
    detail.guid = guidString(writer.guid);
    detail.qosLines = writerQosLines(writer);
    return detail;
}

FastDDSDebugNode::EndpointDetail makeReaderDetail(
        const rtps::SubscriptionBuiltinTopicData& reader, const std::string& nodeName)
{
    FastDDSDebugNode::EndpointDetail detail;
    detail.nodeName = nodeName;
    detail.guid = guidString(reader.guid);
    detail.qosLines = readerQosLines(reader);
    return detail;
}

// ===== DDSQosProfile 翻译（与 FastDDSNode.cpp 版本同构，独立持有不跨 cpp 共享） =====

constexpr int64_t kNsPerSec = 1000000000LL;

// Precondition: ns >= 0
Duration_t toDuration(int64_t ns)
{
    return Duration_t(static_cast<int32_t>(ns / kNsPerSec), static_cast<uint32_t>(ns % kNsPerSec));
}

// 翻译到端点 QoS：起点为 *_QOS_DEFAULT 底座，非哨兵项（枚举非 Unset / 数值 >= 0）逐项覆盖，
// 哨兵项保持底座默认；枚举映射在此显式完成，不做数值直译（公共头枚举与 Fast DDS 内部枚举解耦）
void toWriterQos(const DDSQosProfile& p, DataWriterQos& qos)
{
    if (p.historyKind != DDSHistoryKind::Unset)
    {
        qos.history().kind =
            (p.historyKind == DDSHistoryKind::KEEP_LAST) ? KEEP_LAST_HISTORY_QOS : KEEP_ALL_HISTORY_QOS;
    }
    // depth 在 KEEP_ALL 下忽略；未设 historyKind（底座即 KEEP_LAST）时同样生效
    if (p.depth >= 0 && p.historyKind != DDSHistoryKind::KEEP_ALL)
    {
        qos.history().depth = p.depth;
    }
    if (p.reliability != DDSReliability::Unset)
    {
        qos.reliability().kind =
            (p.reliability == DDSReliability::RELIABLE) ? RELIABLE_RELIABILITY_QOS : BEST_EFFORT_RELIABILITY_QOS;
    }
    if (p.durability != DDSDurability::Unset)
    {
        qos.durability().kind =
            (p.durability == DDSDurability::TRANSIENT_LOCAL) ? TRANSIENT_LOCAL_DURABILITY_QOS : VOLATILE_DURABILITY_QOS;
    }
    if (p.liveliness != DDSLiveliness::Unset)
    {
        qos.liveliness().kind =
            (p.liveliness == DDSLiveliness::AUTOMATIC) ? AUTOMATIC_LIVELINESS_QOS : MANUAL_BY_TOPIC_LIVELINESS_QOS;
    }
    if (p.deadlineNs >= 0)
    {
        qos.deadline().period = toDuration(p.deadlineNs);
    }
    if (p.lifespanNs >= 0)
    {
        qos.lifespan().duration = toDuration(p.lifespanNs);
    }
    if (p.livelinessLeaseNs >= 0)
    {
        qos.liveliness().lease_duration = toDuration(p.livelinessLeaseNs);
    }
}

// reader 版：无 lifespan（writer 侧策略，reader 翻译时忽略），其余同构。
void toReaderQos(const DDSQosProfile& p, DataReaderQos& qos)
{
    if (p.historyKind != DDSHistoryKind::Unset)
    {
        qos.history().kind =
            (p.historyKind == DDSHistoryKind::KEEP_LAST) ? KEEP_LAST_HISTORY_QOS : KEEP_ALL_HISTORY_QOS;
    }
    if (p.depth >= 0 && p.historyKind != DDSHistoryKind::KEEP_ALL)
    {
        qos.history().depth = p.depth;
    }
    if (p.reliability != DDSReliability::Unset)
    {
        qos.reliability().kind =
            (p.reliability == DDSReliability::RELIABLE) ? RELIABLE_RELIABILITY_QOS : BEST_EFFORT_RELIABILITY_QOS;
    }
    if (p.durability != DDSDurability::Unset)
    {
        qos.durability().kind =
            (p.durability == DDSDurability::TRANSIENT_LOCAL) ? TRANSIENT_LOCAL_DURABILITY_QOS : VOLATILE_DURABILITY_QOS;
    }
    if (p.liveliness != DDSLiveliness::Unset)
    {
        qos.liveliness().kind =
            (p.liveliness == DDSLiveliness::AUTOMATIC) ? AUTOMATIC_LIVELINESS_QOS : MANUAL_BY_TOPIC_LIVELINESS_QOS;
    }
    if (p.deadlineNs >= 0)
    {
        qos.deadline().period = toDuration(p.deadlineNs);
    }
    if (p.livelinessLeaseNs >= 0)
    {
        qos.liveliness().lease_duration = toDuration(p.livelinessLeaseNs);
    }
}
}  // namespace

class FastDDSDebugNode::DebugParticipantListener : public DomainParticipantListener
{
public:
    explicit DebugParticipantListener(FastDDSDebugNode& node) : node_(node) {}

    // 仅处理 DISCOVERED_WRITER；回调内仅缓存发现信息，不建订阅（回调线程内同步建 reader
    // 会导致匹配成功但数据不交付）
    void on_data_writer_discovery(
            DomainParticipant* /*participant*/,
            rtps::WriterDiscoveryStatus reason,
            const PublicationBuiltinTopicData& info,
            bool& should_be_ignored) override
    {
        should_be_ignored = false;
        if (reason == rtps::WriterDiscoveryStatus::DISCOVERED_WRITER)
        {
            node_.onWriterDiscovered(std::string(info.topic_name.to_string()), info);
        }
    }

    // 处理 DISCOVERED_READER 与 REMOVED_READER；回调内仅缓存/移除发现信息，
    // 供 listTopics 列出仅有订阅者的主题（不参与订阅建立链路）。
    void on_data_reader_discovery(
            DomainParticipant* /*participant*/,
            rtps::ReaderDiscoveryStatus reason,
            const SubscriptionBuiltinTopicData& info,
            bool& should_be_ignored) override
    {
        should_be_ignored = false;
        if (reason == rtps::ReaderDiscoveryStatus::DISCOVERED_READER)
        {
            node_.onReaderDiscovered(std::string(info.topic_name.to_string()), info);
        }
        else if (reason == rtps::ReaderDiscoveryStatus::REMOVED_READER)
        {
            node_.onReaderRemoved(std::string(info.topic_name.to_string()), info);
        }
    }

    // 仅处理 DISCOVERED_PARTICIPANT；回调内仅缓存 GUID 与名称，供 nodeList 列出域内
    // 命名参与者（不参与订阅建立链路）。
    void on_participant_discovery(
            DomainParticipant* /*participant*/,
            rtps::ParticipantDiscoveryStatus reason,
            const ParticipantBuiltinTopicData& info,
            bool& should_be_ignored) override
    {
        should_be_ignored = false;
        if (reason == rtps::ParticipantDiscoveryStatus::DISCOVERED_PARTICIPANT)
        {
            node_.onParticipantDiscovered(info);
        }
    }

private:
    FastDDSDebugNode& node_;
};

class FastDDSDebugNode::DebugSubListener : public DataReaderListener
{
public:
    DebugSubListener(std::string topicName, std::string typeName, TopicDataType* topicType,
                     OutputSink sink)
        : topic_(std::move(topicName))
        , typeName_(std::move(typeName))
        , topicType_(topicType)
        , sink_(std::move(sink))
        // DynamicPubSubType::create_data() 返回堆上 DynamicData::_ref_type（shared_ptr），
        // take_next_sample 经 deserialize 直接反序列化进该缓冲
        , sample_(createSample(topicType))
    {
    }

    ~DebugSubListener()
    {
        if (topicType_ != nullptr && sample_ != nullptr)
        {
            topicType_->delete_data(sample_);
        }
    }

    void on_subscription_matched(DataReader* /*reader*/,
                                 const SubscriptionMatchedStatus& info) override
    {
        if (info.current_count_change != 0)
        {
            std::cout << "topic=" << topic_
                      << " matched writers=" << info.current_count << std::endl;
        }
    }

    void on_data_available(DataReader* reader) override
    {
        SampleInfo info;
        while (RETCODE_OK == reader->take_next_sample(sample_, &info))
        {
            if (!info.valid_data)
            {
                continue;
            }
            std::ostringstream os;
            os << "\n[" << nowStamp() << "] topic=" << topic_ << " type=" << typeName_
               << " seq=" << ++seq_ << "\n";
            // json_serialize 直接声明于 eprosima::fastdds::dds（3.6.1 无 xtypes:: 内层）
            if (RETCODE_OK != json_serialize(*sample_, DynamicDataJsonFormat::EPROSIMA, os))
            {
                os << "<json_serialize failed>";
            }
            os << "\n";
            deliver(os.str());
        }
    }

private:
    // 样本缓冲分配失败即抛，由 tryStartSubscription 的回滚路径承接
    static DynamicData::_ref_type* createSample(TopicDataType* topicType)
    {
        auto* sample = static_cast<DynamicData::_ref_type*>(topicType->create_data());
        if (sample == nullptr)
        {
            throw std::runtime_error("FastDDSDebugNode: create dynamic sample failed");
        }
        return sample;
    }

    void deliver(const std::string& text)
    {
        if (sink_)
        {
            sink_(text);
        }
        else
        {
            std::cout << text << std::flush;
        }
    }

    std::string topic_;
    std::string typeName_;
    TopicDataType* topicType_;        // 仅供 create/delete_data 样本缓冲，不持有所有权
    OutputSink sink_;                 // 建订阅时按值捕获（订阅建立后换 sink 不生效）
    DynamicData::_ref_type* sample_;  // 堆上 shared_ptr 样本缓冲，析构经 delete_data 释放
    uint64_t seq_ = 0;
};

FastDDSDebugNode::FastDDSDebugNode()
    : listener_(std::make_unique<DebugParticipantListener>(*this))
{
}

FastDDSDebugNode::~FastDDSDebugNode()
{
    // 析构顺序约束：先停工作线程（不再触碰 subs_/participant_），再按
    // DataReader → Topic → Subscriber → Participant 清理；停止由工作线程下一轮轮询感知
    {
        std::lock_guard<std::mutex> lock(mtx_);
        workerRunning_ = false;
    }
    if (worker_.joinable())
    {
        worker_.join();
    }

    if (participant_ == nullptr)
    {
        return;
    }

    if (subscriber_ != nullptr)
    {
        // 先删 reader（停回调）再删主题；监听器与样本缓冲随 subs_/listener_ 成员析构释放
        for (auto& kv : subs_)
        {
            if (kv.second.reader != nullptr)
            {
                subscriber_->delete_datareader(kv.second.reader);
            }
        }
        for (auto& kv : subs_)
        {
            if (kv.second.topic != nullptr)
            {
                participant_->delete_topic(kv.second.topic);
            }
        }
        participant_->delete_subscriber(subscriber_);
    }

    DomainParticipantFactory::get_instance()->delete_participant(participant_);
}

bool FastDDSDebugNode::setDomainId(uint32_t domainId,
    const yomkdds_xml::ParticipantQosSpec& spec, std::string* err)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (participant_ != nullptr)
    {
        return false;
    }

    // mask 用 none() 对齐 FastDDSNode——实测 all() 会使本节点上的动态 reader 匹配成功但数据不交付
    // （listener 仅服务发现回调，RTPS 层直接派发不受 mask 控制）；uid 唯一名使多进程调试节点互不相同。
    // XML 提取先于置名执行：本节点无用户名，按名 profile 无意义仅默认档可兜底，落空保持 DEFAULT 底座
    DomainParticipantQos qos = PARTICIPANT_QOS_DEFAULT;
    yomkdds_xml::participantQosFromXml(qos, "", spec.xml);
    // 发现服务器直配（仅 client 角色）：合并语义为直配 > XML 提取打底，TCP 通道自动补注入
    // TCPv4 传输；任一 spec 非法则整体失败
    if (!spec.udpServerSpec.empty() || !spec.tcpServerSpec.empty())
    {
        std::string serverErr;
        if (!yomkdds_xml::applyDiscoveryServers(qos, spec.udpServerSpec, spec.tcpServerSpec, serverErr))
        {
            if (err != nullptr)
            {
                *err = serverErr;
            }
            return false;
        }
    }
    qos.name(std::string("yomkdds_debug_") + makeUidSuffix());
    participant_ = DomainParticipantFactory::get_instance()->create_participant(
        domainId, qos, listener_.get(), StatusMask::none());
    if (participant_ == nullptr)
    {
        return false;
    }

    subscriber_ = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT, nullptr, StatusMask::none());
    if (subscriber_ == nullptr)
    {
        DomainParticipantFactory::get_instance()->delete_participant(participant_);
        participant_ = nullptr;
        return false;
    }

    // 订阅建立全部由工作线程承担（回调内同步建 reader 会匹配成功但数据不交付）
    workerRunning_ = true;
    try
    {
        worker_ = std::thread(&FastDDSDebugNode::workerLoop, this);
    }
    catch (...)
    {
        workerRunning_ = false;
        participant_->delete_subscriber(subscriber_);
        subscriber_ = nullptr;
        DomainParticipantFactory::get_instance()->delete_participant(participant_);
        participant_ = nullptr;
        return false;
    }
    return true;
}

bool FastDDSDebugNode::subscribeTopic(const std::string& topicName, std::string* error,
        const DDSQosProfile& qos)
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (participant_ == nullptr || subscriber_ == nullptr)
    {
        if (error != nullptr)
        {
            *error = (participant_ == nullptr) ? "debug node not created" : "subscriber not created";
        }
        return false;
    }
    if (topicName.empty())
    {
        if (error != nullptr)
        {
            *error = "empty topic name";
        }
        return false;
    }
    if (pending_.count(topicName) > 0 || subs_.count(topicName) > 0)
    {
        if (error != nullptr)
        {
            *error = "topic [" + topicName + "] already registered";
        }
        return false;
    }
    pending_.insert(topicName);
    pendingQos_[topicName] = qos;
    // 宽限起点记录在工作线程：不在此处直接查发现缓存判定——调用方通常在 setDomainId 之后
    // 立即登记，本端 SPDP/EDP 发现尚未完成，会对"发布者早已在线"误报
    pendingSince_[topicName] = std::chrono::steady_clock::now();
    // 此处仅登记，建订由工作线程轮询取件（刻意不用 cv 即时唤醒：GCC 11.4 libtsan 死锁检测器
    // 对该 cv 等待模式报确定性 double lock 假阳性，定罪链见 test/tsan.supp 头注）
    return true;
}

void FastDDSDebugNode::setOutputSink(OutputSink sink)
{
    std::lock_guard<std::mutex> lock(mtx_);
    sink_ = std::move(sink);
}

bool FastDDSDebugNode::listTopics(std::vector<std::pair<std::string, std::string>>& topics,
        uint32_t stableRounds, uint32_t intervalMs, std::string* error)
{
    if (stableRounds == 0)
    {
        stableRounds = kDefaultStableRounds;
    }
    if (intervalMs == 0)
    {
        intervalMs = kDefaultIntervalMs;
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (participant_ == nullptr)
        {
            if (error != nullptr)
            {
                *error = "debug node not created";
            }
            return false;
        }
    }
    return waitForTopicsStable(topics, stableRounds, intervalMs);
}

bool FastDDSDebugNode::topicInfo(const std::string& topicName, std::string& typeName,
        size_t& publisherCount, size_t& subscriptionCount,
        uint32_t stableRounds, uint32_t intervalMs,
        std::vector<EndpointDetail>* publishers, std::vector<EndpointDetail>* subscribers,
        std::string* error)
{
    if (stableRounds == 0)
    {
        stableRounds = kDefaultStableRounds;
    }
    if (intervalMs == 0)
    {
        intervalMs = kDefaultIntervalMs;
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (participant_ == nullptr)
        {
            if (error != nullptr)
            {
                *error = "debug node not created";
            }
            return false;
        }
    }
    if (!waitForTopicInfoStable(
            topicName, typeName, publisherCount, subscriptionCount, stableRounds, intervalMs,
            publishers, subscribers))
    {
        if (error != nullptr)
        {
            *error = "topic [" + topicName + "] not found";
        }
        return false;
    }
    return true;
}

bool FastDDSDebugNode::nodeList(std::vector<std::string>& names,
        uint32_t stableRounds, uint32_t intervalMs, std::string* error)
{
    if (stableRounds == 0)
    {
        stableRounds = kDefaultStableRounds;
    }
    if (intervalMs == 0)
    {
        intervalMs = kDefaultIntervalMs;
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (participant_ == nullptr)
        {
            if (error != nullptr)
            {
                *error = "debug node not created";
            }
            return false;
        }
    }
    return waitForNodesStable(names, stableRounds, intervalMs);
}

bool FastDDSDebugNode::nodeInfo(const std::string& nodeName,
        std::vector<std::pair<std::string, std::string>>& publishers,
        std::vector<std::pair<std::string, std::string>>& subscribers,
        uint32_t stableRounds, uint32_t intervalMs, std::string* error)
{
    if (stableRounds == 0)
    {
        stableRounds = kDefaultStableRounds;
    }
    if (intervalMs == 0)
    {
        intervalMs = kDefaultIntervalMs;
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (participant_ == nullptr)
        {
            if (error != nullptr)
            {
                *error = "debug node not created";
            }
            return false;
        }
    }
    if (!waitForNodeInfoStable(nodeName, publishers, subscribers, stableRounds, intervalMs))
    {
        if (error != nullptr)
        {
            *error = "node [" + nodeName + "] not found";
        }
        return false;
    }
    return true;
}

bool FastDDSDebugNode::interfaceShow(const std::string& typeName,
        std::vector<std::string>& lines,
        uint32_t stableRounds, uint32_t intervalMs, std::string* error)
{
    if (stableRounds == 0)
    {
        stableRounds = kDefaultStableRounds;
    }
    if (intervalMs == 0)
    {
        intervalMs = kDefaultIntervalMs;
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (participant_ == nullptr)
        {
            if (error != nullptr)
            {
                *error = "debug node not created";
            }
            return false;
        }
    }
    return waitForInterfaceStable(typeName, lines, stableRounds, intervalMs, error);
}

bool FastDDSDebugNode::topicExample(const std::string& topicName,
        std::vector<std::string>& lines,
        uint32_t stableRounds, uint32_t intervalMs, std::string* error)
{
    if (stableRounds == 0)
    {
        stableRounds = kDefaultStableRounds;
    }
    if (intervalMs == 0)
    {
        intervalMs = kDefaultIntervalMs;
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (participant_ == nullptr)
        {
            if (error != nullptr)
            {
                *error = "debug node not created";
            }
            return false;
        }
    }
    std::string typeName;
    size_t publisherCount = 0;
    size_t subscriptionCount = 0;
    if (!waitForTopicInfoStable(topicName, typeName, publisherCount, subscriptionCount,
                stableRounds, intervalMs))
    {
        if (error != nullptr)
        {
            *error = "topic [" + topicName + "] not found";
        }
        return false;
    }
    std::vector<std::string> idl;
    if (!waitForInterfaceStable(typeName, idl, stableRounds, intervalMs, error))
    {
        return false;
    }
    // 默认值模板与 json_deserialize 输入格式对称；example 命令的 JSON 整体包单引号，防 shell
    // 剥内层双引号（裸传会被剥成 {data:123} 解析失败）
    std::string json;
    if (!jsonExampleOfType(typeName, json, error))
    {
        return false;
    }
    lines.clear();
    lines.push_back("Type: " + typeName);
    lines.push_back("IDL:");
    lines.insert(lines.end(), std::make_move_iterator(idl.begin()),
            std::make_move_iterator(idl.end()));
    lines.push_back("example:");
    lines.push_back("yomkdds topic pub " + topicName + " '" + json + "'");
    return true;
}

bool FastDDSDebugNode::topicMsgJson(const std::string& topicName,
        std::string& typeName,
        std::string& json,
        uint32_t stableRounds, uint32_t intervalMs, std::string* error)
{
    if (stableRounds == 0)
    {
        stableRounds = kDefaultStableRounds;
    }
    if (intervalMs == 0)
    {
        intervalMs = kDefaultIntervalMs;
    }
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (participant_ == nullptr)
        {
            if (error != nullptr)
            {
                *error = "debug node not created";
            }
            return false;
        }
    }
    size_t publisherCount = 0;
    size_t subscriptionCount = 0;
    if (!waitForTopicInfoStable(topicName, typeName, publisherCount, subscriptionCount,
                stableRounds, intervalMs))
    {
        if (error != nullptr)
        {
            *error = "topic [" + topicName + "] not found";
        }
        return false;
    }
    return jsonExampleOfType(typeName, json, error);
}

bool FastDDSDebugNode::waitForTopicsStable(std::vector<std::pair<std::string, std::string>>& topics,
        uint32_t stableRounds, uint32_t intervalMs)
{
    // 快照仅拿 seenMtx_ 叶子锁（不持锁睡眠，发现事件线程可并行写入）
    auto snapshot = [this]()
    {
        std::lock_guard<std::mutex> lock(seenMtx_);
        std::vector<std::pair<std::string, std::string>> items;
        items.reserve(seen_.size() + seenReaders_.size());
        for (const auto& kv : seen_)
        {
            items.emplace_back(kv.first, kv.second.front().type_name.to_string());
        }
        for (const auto& kv : seenReaders_)
        {
            if (seen_.count(kv.first) == 0)
            {
                items.emplace_back(kv.first, kv.second.front().type_name.to_string());
            }
        }
        std::sort(items.begin(), items.end());
        return items;
    };
    std::vector<std::pair<std::string, std::string>> prev = snapshot();
    uint32_t unchanged = 1;
    while (unchanged < stableRounds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        std::vector<std::pair<std::string, std::string>> cur = snapshot();
        if (cur == prev)
        {
            ++unchanged;
        }
        else
        {
            prev = std::move(cur);
            unchanged = 1;
        }
    }
    topics = std::move(prev);
    return true;
}

bool FastDDSDebugNode::waitForTopicInfoStable(const std::string& topicName, std::string& typeName,
        size_t& publisherCount, size_t& subscriptionCount,
        uint32_t stableRounds, uint32_t intervalMs,
        std::vector<EndpointDetail>* publishers, std::vector<EndpointDetail>* subscribers)
{
    // 快照仅拿 seenMtx_ 叶子锁（不持锁睡眠，发现事件线程可并行写入）；端点详情纳入快照
    // 相等性：等待期间远端端点新增（含 QoS 重发）会重置不变计数
    auto snapshot = [this, &topicName, publishers, subscribers]()
    {
        std::lock_guard<std::mutex> lock(seenMtx_);
        auto w = seen_.find(topicName);
        auto r = seenReaders_.find(topicName);
        if (w == seen_.end() && r == seenReaders_.end())
        {
            return std::make_tuple(false, std::string(), size_t{0}, size_t{0},
                std::vector<EndpointDetail>(), std::vector<EndpointDetail>());
        }
        // else-if 守卫：消除 cppcheck 对 r 迭代器有效性的路径分析盲区
        std::string name;
        if (w != seen_.end())
        {
            name = w->second.front().type_name.to_string();
        }
        else if (r != seenReaders_.end())
        {
            name = r->second.front().type_name.to_string();
        }
        const size_t pubCount = (w != seen_.end()) ? w->second.size() : size_t{0};
        const size_t subCount = (r != seenReaders_.end()) ? r->second.size() : size_t{0};
        std::vector<EndpointDetail> pubDetails;
        if (publishers != nullptr && w != seen_.end())
        {
            pubDetails.reserve(w->second.size());
            for (const auto& writer : w->second)
            {
                pubDetails.push_back(makeWriterDetail(writer,
                    participantNameOf(seenParticipants_, writer.guid)));
            }
        }
        std::vector<EndpointDetail> subDetails;
        if (subscribers != nullptr && r != seenReaders_.end())
        {
            subDetails.reserve(r->second.size());
            for (const auto& reader : r->second)
            {
                subDetails.push_back(makeReaderDetail(reader,
                    participantNameOf(seenParticipants_, reader.guid)));
            }
        }
        return std::make_tuple(true, name, pubCount, subCount, std::move(pubDetails),
            std::move(subDetails));
    };
    auto prev = snapshot();
    uint32_t unchanged = 1;
    while (unchanged < stableRounds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        auto cur = snapshot();
        if (cur == prev)
        {
            ++unchanged;
        }
        else
        {
            prev = std::move(cur);
            unchanged = 1;
        }
    }
    const bool found = std::get<0>(prev);
    if (found)
    {
        constexpr size_t kSubDetailsIdx = 5;
        typeName = std::get<1>(prev);
        publisherCount = std::get<2>(prev);
        subscriptionCount = std::get<3>(prev);
        if (publishers != nullptr)
        {
            *publishers = std::move(std::get<4>(prev));
        }
        if (subscribers != nullptr)
        {
            *subscribers = std::move(std::get<kSubDetailsIdx>(prev));
        }
    }
    return found;
}

bool FastDDSDebugNode::waitForNodesStable(std::vector<std::string>& names,
        uint32_t stableRounds, uint32_t intervalMs)
{
    // 快照仅拿 seenMtx_ 叶子锁（不持锁睡眠，发现事件线程可并行写入）；过滤 "/"——ROS2 参与
    // 者默认占位名，rmw_fastrtps 统一置为根 enclave（rcl_init 兜底），节点名走另一通道无辨识价值
    auto snapshot = [this]()
    {
        std::lock_guard<std::mutex> lock(seenMtx_);
        std::vector<std::string> items;
        items.reserve(seenParticipants_.size());
        for (const auto& kv : seenParticipants_)
        {
            if (!kv.second.empty() && kv.second != "/")
            {
                items.push_back(kv.second);
            }
        }
        std::sort(items.begin(), items.end());
        return items;
    };
    std::vector<std::string> prev = snapshot();
    uint32_t unchanged = 1;
    while (unchanged < stableRounds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        std::vector<std::string> cur = snapshot();
        if (cur == prev)
        {
            ++unchanged;
        }
        else
        {
            prev = std::move(cur);
            unchanged = 1;
        }
    }
    names = std::move(prev);
    return true;
}

bool FastDDSDebugNode::waitForNodeInfoStable(const std::string& nodeName,
        std::vector<std::pair<std::string, std::string>>& publishers,
        std::vector<std::pair<std::string, std::string>>& subscribers,
        uint32_t stableRounds, uint32_t intervalMs)
{
    // 快照仅拿 seenMtx_ 叶子锁（不持锁睡眠，发现事件线程可并行写入）；归属判定基于 RTPS 规范
    // 保证的"端点 GUID 前缀 == 所属参与者 GUID 前缀"，不依赖 participant_guid 字段
    auto snapshot = [this, &nodeName]()
    {
        std::lock_guard<std::mutex> lock(seenMtx_);
        std::set<rtps::GuidPrefix_t> prefixes;
        for (const auto& kv : seenParticipants_)
        {
            if (kv.second == nodeName)
            {
                prefixes.insert(kv.first);
            }
        }
        std::vector<std::pair<std::string, std::string>> pubs;
        std::vector<std::pair<std::string, std::string>> subs;
        for (const auto& kv : seen_)
        {
            for (const auto& w : kv.second)
            {
                if (prefixes.count(w.guid.guidPrefix) > 0)
                {
                    pubs.emplace_back(kv.first, w.type_name.to_string());
                    break;  // 同主题去重：任一归属 writer 命中即计一条
                }
            }
        }
        for (const auto& kv : seenReaders_)
        {
            for (const auto& r : kv.second)
            {
                if (prefixes.count(r.guid.guidPrefix) > 0)
                {
                    subs.emplace_back(kv.first, r.type_name.to_string());
                    break;  // 同主题去重：任一归属 reader 命中即计一条
                }
            }
        }
        std::sort(pubs.begin(), pubs.end());
        std::sort(subs.begin(), subs.end());
        return std::make_tuple(!prefixes.empty(), std::move(pubs), std::move(subs));
    };
    auto prev = snapshot();
    uint32_t unchanged = 1;
    while (unchanged < stableRounds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        auto cur = snapshot();
        if (cur == prev)
        {
            ++unchanged;
        }
        else
        {
            prev = std::move(cur);
            unchanged = 1;
        }
    }
    if (!std::get<0>(prev))
    {
        return false;
    }
    publishers = std::move(std::get<1>(prev));
    subscribers = std::move(std::get<2>(prev));
    return true;
}

// XTypes 无界 string/sequence 的 bound 专用表示（等同无界，非实际长度）
constexpr uint32_t kUnboundedBound = 0xFFFFFFFFu;

// DynamicType → IDL 类型名文本：基础标量映射 ROS2/IDL4 风格名，sequence/array 递归展开元素
// 类型，嵌套命名类型仅显示子类型名不展开；kind 无对应 IDL 文本时返回 false，宁可失败不输出非法 IDL
bool idlTypeNameOf(const traits<DynamicType>::ref_type& type, std::string& out)
{
    if (type == nullptr)
    {
        return false;
    }
    switch (type->get_kind())
    {
        case xtypes::TK_BOOLEAN: out = "bool"; return true;
        case xtypes::TK_BYTE: out = "octet"; return true;
        case xtypes::TK_INT16: out = "int16"; return true;
        case xtypes::TK_INT32: out = "int32"; return true;
        case xtypes::TK_INT64: out = "int64"; return true;
        case xtypes::TK_UINT16: out = "uint16"; return true;
        case xtypes::TK_UINT32: out = "uint32"; return true;
        case xtypes::TK_UINT64: out = "uint64"; return true;
        case xtypes::TK_FLOAT32: out = "float32"; return true;
        case xtypes::TK_FLOAT64: out = "float64"; return true;
        case xtypes::TK_FLOAT128: out = "float128"; return true;
        case xtypes::TK_INT8: out = "int8"; return true;
        case xtypes::TK_UINT8: out = "uint8"; return true;
        case xtypes::TK_CHAR8: out = "char"; return true;
        case xtypes::TK_CHAR16: out = "wchar"; return true;
        default: break;
    }
    // 容器与命名类型需读 TypeDescriptor；descriptor 须预分配非空实例（traits<>::make_shared()），
    // get_descriptor 以 overwrite 语义填充
    auto desc = traits<TypeDescriptor>::make_shared();
    if (RETCODE_OK != type->get_descriptor(desc) || desc == nullptr)
    {
        return false;
    }
    switch (type->get_kind())
    {
        case xtypes::TK_STRING8:
        case xtypes::TK_STRING16:
        {
            out = (xtypes::TK_STRING16 == type->get_kind()) ? "wstring" : "string";
            const auto& bound = desc->bound();
            if (!bound.empty() && bound[0] > 0 && bound[0] != kUnboundedBound)
            {
                out += "<" + std::to_string(bound[0]) + ">";
            }
            return true;
        }
        case xtypes::TK_SEQUENCE:
        {
            std::string elem;
            if (!idlTypeNameOf(desc->element_type(), elem))
            {
                return false;
            }
            const auto& bound = desc->bound();
            if (!bound.empty() && bound[0] > 0 && bound[0] != kUnboundedBound)
            {
                out = "sequence<" + elem + ", " + std::to_string(bound[0]) + ">";
            }
            else
            {
                out = "sequence<" + elem + ">";
            }
            return true;
        }
        case xtypes::TK_ARRAY:
        {
            std::string elem;
            if (!idlTypeNameOf(desc->element_type(), elem))
            {
                return false;
            }
            out = elem;
            for (uint32_t dim : desc->bound())
            {
                out += "[" + std::to_string(dim) + "]";
            }
            return true;
        }
        default:
        {
            std::string name = desc->name().to_string();
            if (name.empty())
            {
                return false;
            }
            out = name;
            return true;
        }
    }
}

bool FastDDSDebugNode::waitForInterfaceStable(const std::string& typeName,
        std::vector<std::string>& lines,
        uint32_t stableRounds, uint32_t intervalMs, std::string* error)
{
    // 失败原因归类仅用于收敛后回填 error，不参与快照相等性；每轮开头重置，端点离线使类型名
    // 消失时能回退到"未发现"（1=TypeObject 不可得：ROS2 rmw_fastrtps 等不参与 XTypes 类型发现）
    int lastReason = 0;
    std::string lastDetail;
    // 快照仅拿 seenMtx_ 叶子锁，registry 查询与类型拼装不持锁；TypeObject 未就绪（TypeLookup
    // 尚未取回）视为本轮未命中继续等待
    auto snapshot = [this, &typeName, &lastReason, &lastDetail]()
            -> std::pair<bool, std::vector<std::string>>
    {
        lastReason = 0;
        std::lock_guard<std::mutex> lock(seenMtx_);
        auto buildFromInfo = [this, &typeName, &lastReason, &lastDetail](const auto& info,
                std::vector<std::string>& out) -> bool
        {
            const auto& ti = info.type_information.type_information;
            const auto& tid =
                    (xtypes::TK_NONE != ti.complete().typeid_with_size().type_id()._d())
                    ? ti.complete().typeid_with_size().type_id()
                    : ti.minimal().typeid_with_size().type_id();
            xtypes::TypeObject type_object;
            if (RETCODE_OK != DomainParticipantFactory::get_instance()->type_object_registry()
                        .get_type_object(tid, type_object))
            {
                lastReason = 1;
                return false;
            }
            if (!buildInterfaceLines(type_object, typeName, out, &lastDetail))
            {
                lastReason = 2;
                return false;
            }
            return true;
        };
        for (const auto& entry : seen_)
        {
            for (const auto& info : entry.second)
            {
                if (info.type_name.to_string() == typeName)
                {
                    std::vector<std::string> out;
                    if (buildFromInfo(info, out))
                    {
                        return {true, std::move(out)};
                    }
                    // 命中但 TypeObject 未就绪：不回退 reader（同类型同一 TypeObject）
                    return {false, {}};
                }
            }
        }
        for (const auto& entry : seenReaders_)
        {
            for (const auto& info : entry.second)
            {
                if (info.type_name.to_string() == typeName)
                {
                    std::vector<std::string> out;
                    if (buildFromInfo(info, out))
                    {
                        return {true, std::move(out)};
                    }
                    return {false, {}};
                }
            }
        }
        return {false, {}};
    };
    auto prev = snapshot();
    uint32_t unchanged = 1;
    while (unchanged < stableRounds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        auto cur = snapshot();
        if (cur == prev)
        {
            ++unchanged;
        }
        else
        {
            prev = std::move(cur);
            unchanged = 1;
        }
    }
    if (!prev.first)
    {
        // 按收敛末轮实际原因归类回填，避免统一报 "type not found" 误导调用方以为类型名拼错
        if (error != nullptr)
        {
            if (1 == lastReason)
            {
                *error = "type object for [" + typeName + "] not available";
            }
            else if (2 == lastReason)
            {
                *error = lastDetail.empty()
                        ? "interface build failed for [" + typeName + "]" : lastDetail;
            }
            else
            {
                *error = "type [" + typeName + "] not found";
            }
        }
        return false;
    }
    lines = std::move(prev.second);
    return true;
}

bool FastDDSDebugNode::buildInterfaceLines(const xtypes::TypeObject& type_object,
        const std::string& typeName, std::vector<std::string>& lines, std::string* error)
{
    // TypeObject → DynamicType（shared_ptr 持有；不注册类型不建订阅，纯类型内省）
    auto dyn_type = DynamicTypeBuilderFactory::get_instance()->create_type_w_type_object(
                type_object)->build();
    if (dyn_type == nullptr || xtypes::TK_STRUCTURE != dyn_type->get_kind())
    {
        if (error != nullptr)
        {
            *error = "type [" + typeName + "] is not a struct, interface display unsupported";
        }
        return false;
    }
    lines.clear();
    lines.push_back("struct " + typeName + " {");
    const uint32_t count = dyn_type->get_member_count();
    DynamicTypeMembersById members;
    if (RETCODE_OK != dyn_type->get_all_members(members) || members.size() != count)
    {
        if (error != nullptr)
        {
            *error = "interface introspection failed for [" + typeName + "]";
        }
        return false;
    }
    for (const auto& pair : members)  // map 按 MemberId 升序 == struct 成员声明序
    {
        auto desc = traits<MemberDescriptor>::make_shared();
        if (RETCODE_OK != pair.second->get_descriptor(desc) || desc == nullptr)
        {
            return false;
        }
        std::string field;
        if (!idlTypeNameOf(desc->type(), field))
        {
            return false;
        }
        lines.push_back("    " + field + " " + desc->name().to_string() + ";");
    }
    lines.push_back("};");
    return true;
}

bool FastDDSDebugNode::fetchTypeObject(const std::string& typeName,
        xtypes::TypeObject& type_object)
{
    // 仅拿 seenMtx_ 叶子锁做缓存查询与 TypeObject 拷贝，锁外重建留给调用方
    std::lock_guard<std::mutex> lock(seenMtx_);
    auto fetch = [&type_object, &typeName](const auto& entries) -> bool
    {
        for (const auto& entry : entries)
        {
            for (const auto& info : entry.second)
            {
                if (info.type_name.to_string() != typeName)
                {
                    continue;
                }
                const auto& ti = info.type_information.type_information;
                const auto& tid =
                        (xtypes::TK_NONE != ti.complete().typeid_with_size().type_id()._d())
                        ? ti.complete().typeid_with_size().type_id()
                        : ti.minimal().typeid_with_size().type_id();
                return RETCODE_OK == DomainParticipantFactory::get_instance()
                                ->type_object_registry().get_type_object(tid, type_object);
            }
        }
        return false;
    };
    return fetch(seen_) || fetch(seenReaders_);
}

bool FastDDSDebugNode::jsonExampleOfType(const std::string& typeName, std::string& out,
        std::string* error)
{
    xtypes::TypeObject type_object;
    if (!fetchTypeObject(typeName, type_object))
    {
        if (error != nullptr)
        {
            *error = "type object for [" + typeName + "] not available";
        }
        return false;
    }
    // 锁外重建 DynamicType，不注册类型不建订阅（纯类型内省）
    auto dyn_type = DynamicTypeBuilderFactory::get_instance()->create_type_w_type_object(
                type_object)->build();
    if (dyn_type == nullptr)
    {
        if (error != nullptr)
        {
            *error = "type rebuild failed for [" + typeName + "]";
        }
        return false;
    }
    auto data = DynamicDataFactory::get_instance()->create_data(dyn_type);
    if (data == nullptr)
    {
        if (error != nullptr)
        {
            *error = "example generation failed for [" + typeName + "]: create_data";
        }
        return false;
    }
    std::ostringstream os;
    const bool ok = RETCODE_OK == json_serialize(data, DynamicDataJsonFormat::EPROSIMA, os);
    if (ok)
    {
        out = os.str();
    }
    else if (error != nullptr)
    {
        *error = "example generation failed for [" + typeName + "]: json_serialize";
    }
    DynamicDataFactory::get_instance()->delete_data(data);
    return ok;
}

bool FastDDSDebugNode::topicPub(const std::string& topicName, const std::string& json,
        std::string& error, uint32_t stableRounds, uint32_t intervalMs,
        uint32_t repeatIntervalMs, uint32_t requiredSubscribers,
        uint32_t maxTimes, uint32_t* total, uint32_t* failed,
        const DDSQosProfile& qos)
{
    if (total != nullptr)
    {
        *total = 0;
    }
    if (failed != nullptr)
    {
        *failed = 0;
    }
    yomk::debugPubReset();  // 复位上次会话残留的停止请求，此后 SIGINT 置位才对本轮生效
    if (stableRounds == 0)
    {
        stableRounds = kDefaultStableRounds;
    }
    if (intervalMs == 0)
    {
        intervalMs = kDefaultIntervalMs;
    }
    error.clear();
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (participant_ == nullptr)
        {
            error = "debug node not created";
            return false;
        }
    }
    // ⓪ requiredSubscribers>0：主题未被发现时无法重建类型，先无限轮询等主题出现（"先发布后
    // 订阅"场景）；g_debugPubStop 置位即中断报错返回，此时未创建任何实体
    if (requiredSubscribers > 0)
    {
        const uint32_t topicLogEveryRounds = std::max<uint32_t>(1, 5000 / intervalMs);
        uint32_t topicQuietRounds = 0;
        bool topicInterrupted = false;
        bool topicFound = false;
        while (true)
        {
            if (yomk::g_debugPubStop.load())
            {
                topicInterrupted = true;
                break;
            }
            {
                std::lock_guard<std::mutex> lock(seenMtx_);
                topicFound = seen_.find(topicName) != seen_.end() ||
                        seenReaders_.find(topicName) != seenReaders_.end();
            }
            if (topicFound)
            {
                break;
            }
            if (topicQuietRounds == 0 || topicQuietRounds >= topicLogEveryRounds)
            {
                std::cout << "waiting for topic " << topicName
                          << " to be discovered" << std::endl;
                topicQuietRounds = 0;
            }
            ++topicQuietRounds;
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
        if (topicInterrupted)
        {
            error = "waiting for subscribers interrupted";
            return false;
        }
    }
    // ① 发现收敛
    std::string typeName;
    size_t publisherCount = 0;
    size_t subscriptionCount = 0;
    if (!waitForTopicInfoStable(topicName, typeName, publisherCount, subscriptionCount,
                stableRounds, intervalMs))
    {
        error = "topic [" + topicName + "] not found";
        return false;
    }
    // ② 类型重建 + JSON 解析（失败不建任何发布实体）
    xtypes::TypeObject type_object;
    if (!fetchTypeObject(typeName, type_object))
    {
        error = "type object for [" + typeName + "] not available";
        return false;
    }
    auto dyn_type = DynamicTypeBuilderFactory::get_instance()->create_type_w_type_object(
                type_object)->build();
    if (dyn_type == nullptr)
    {
        error = "type rebuild failed for [" + typeName + "]";
        return false;
    }
    // json_deserialize 要求传入空引用（内部自行分配填充样本），不能预 create_data
    DynamicData::_ref_type data;
    if (RETCODE_OK != json_deserialize(json, dyn_type, DynamicDataJsonFormat::EPROSIMA, data))
    {
        error = "invalid json for type [" + typeName + "]";
        return false;
    }
    // ③ 临时发布链（QoS 语义：请求 ≤ 提供的最大兼容）
    TypeSupport ts(new DynamicPubSubType(dyn_type));
    if (RETCODE_OK != ts.register_type(participant_))
    {
        DynamicDataFactory::get_instance()->delete_data(data);
        error = "register type [" + typeName + "] failed";
        return false;
    }
    Topic* topic = participant_->create_topic(topicName, ts.get_type_name(), TOPIC_QOS_DEFAULT);
    Publisher* publisher = topic != nullptr ?
        participant_->create_publisher(PUBLISHER_QOS_DEFAULT) : nullptr;
    DataWriterQos wqos = DATAWRITER_QOS_DEFAULT;
    toWriterQos(qos, wqos);  // 无参=全哨兵零覆盖，底座值≡原写死 RELIABLE+TRANSIENT_LOCAL（等值替换）
    DataWriter* writer = publisher != nullptr ?
        publisher->create_datawriter(topic, wqos) : nullptr;
    if (writer == nullptr)
    {
        DynamicDataFactory::get_instance()->delete_data(data);
        if (publisher != nullptr)
        {
            participant_->delete_publisher(publisher);
        }
        if (topic != nullptr)
        {
            participant_->delete_topic(topic);
        }
        error = "create datawriter failed";
        return false;
    }
    // requiredSubscribers>0：阻塞等待 matched 计数达标，不设超时（防订阅者未全部上线即漏发）；
    // 计数口径 PublicationMatchedStatus.current_count 含 BEST_EFFORT（与 ros2 topic pub -w 一致），
    // 调试节点自身不订阅目标主题故无 intra-participant 干扰；状态查询失败视为匹配未知，放弃等待
    if (requiredSubscribers > 0)
    {
        int32_t lastLogged = -1;
        uint32_t quietRounds = 0;
        const uint32_t logEveryRounds = std::max<uint32_t>(1, 5000 / intervalMs);
        bool interrupted = false;
        while (true)
        {
            if (yomk::g_debugPubStop.load())
            {
                interrupted = true;
                break;
            }
            PublicationMatchedStatus status;
            if (RETCODE_OK != writer->get_publication_matched_status(status))
            {
                break;
            }
            if (status.current_count >= static_cast<int32_t>(requiredSubscribers))
            {
                break;
            }
            if (status.current_count != lastLogged || quietRounds >= logEveryRounds)
            {
                std::cout << "waiting for subscribers: " << status.current_count << "/"
                          << requiredSubscribers << " matched on topic " << topicName
                          << std::endl;
                lastLogged = status.current_count;
                quietRounds = 0;
            }
            else
            {
                ++quietRounds;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
        if (interrupted)
        {
            error = "waiting for subscribers interrupted";
            publisher->delete_datawriter(writer);
            participant_->delete_publisher(publisher);
            participant_->delete_topic(topic);
            DynamicDataFactory::get_instance()->delete_data(data);
            return false;
        }
    }
    // 匹配收敛：无订阅者恒 0 也收敛照发
    uint32_t unchanged = 0;
    int32_t lastMatched = -1;
    while (unchanged < stableRounds)
    {
        PublicationMatchedStatus status;
        if (RETCODE_OK != writer->get_publication_matched_status(status))
        {
            break;  // 状态查询失败视为未知，放弃收敛直接发布
        }
        unchanged = (status.current_count == lastMatched) ? unchanged + 1 : 1;
        lastMatched = status.current_count;
        if (unchanged < stableRounds)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    }
    // 校验：发现缓存中该主题的 RELIABLE 订阅者须全部出现在实际匹配集（缓存有记录却无一匹配
    // = 订阅者挂起/异常死亡，wait_for_all_acked 无法发现它们），否则不发布避免"订阅者收不到
    // 却报成功"。BEST_EFFORT 不参与 ack 不纳入比对；正常退出者已清出缓存不误报，异常死亡在
    // lease 过期前窗口内可能误报（重试即可）；枚举失败视为未知放弃校验仍尽力而为发布
    std::vector<rtps::GUID_t> expectGuids{};
    {
        std::lock_guard<std::mutex> lock(seenMtx_);
        auto cached = seenReaders_.find(topicName);
        if (cached != seenReaders_.end())
        {
            for (const auto& rd : cached->second)
            {
                if (RELIABLE_RELIABILITY_QOS == rd.reliability.kind)
                {
                    expectGuids.push_back(rd.guid);
                }
            }
        }
    }
    bool subscriberMissing = false;
    if (!expectGuids.empty())
    {
        std::vector<InstanceHandle_t> handles;
        if (RETCODE_OK == writer->get_matched_subscriptions(handles))
        {
            for (const auto& guid : expectGuids)
            {
                bool found = false;
                for (const auto& handle : handles)
                {
                    SubscriptionBuiltinTopicData matched;
                    if (RETCODE_OK == writer->get_matched_subscription_data(matched, handle) &&
                            matched.guid == guid)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    subscriberMissing = true;
                    break;
                }
            }
        }
    }
    if (subscriberMissing)
    {
        error = "subscribers exist but not all matched (suspended or offline)";
        publisher->delete_datawriter(writer);
        participant_->delete_publisher(publisher);
        participant_->delete_topic(topic);
        DynamicDataFactory::get_instance()->delete_data(data);
        return false;
    }
    // ④ write 恰一次 + ack 自适应确认
    // DynamicPubSubType 约定 void* 指向 DynamicData::_ref_type（智能指针地址，非裸指针，
    // 见源码 calculate_serialized_size/serialize 的 static_cast），故传 &data 而非 data.get()。
    // 回执内容取实际待发样本序列化（所见即所发，与 topic print 可对账），发前一次复用全程
    std::ostringstream payloadOs;
    if (RETCODE_OK != json_serialize(data, DynamicDataJsonFormat::EPROSIMA, payloadOs))
    {
        payloadOs << "<json_serialize failed>";
    }
    const std::string payloadJson = payloadOs.str();
    bool ok = RETCODE_OK == writer->write(&data);
    if (ok)
    {
        std::cout << "publishing #1 to topic " << topicName << ": " << payloadJson << std::endl;
        // 仅缓存中存在 RELIABLE 订阅者才启用 ack（仅 RELIABLE reader 参与 ackNack）
        bool ackUsable = false;
        {
            std::lock_guard<std::mutex> lock(seenMtx_);
            auto it = seenReaders_.find(topicName);
            if (it != seenReaders_.end())
            {
                for (const auto& info : it->second)
                {
                    if (RELIABLE_RELIABILITY_QOS == info.reliability.kind)
                    {
                        ackUsable = true;
                        break;
                    }
                }
            }
        }
        if (ackUsable)
        {
            // 全部 RELIABLE 订阅者确认才算成功；超时有限不卡死，如实报失败
            static const Duration_t kAckTimeout{2, 0};
            if (RETCODE_OK != writer->wait_for_acknowledgments(kAckTimeout))
            {
                error = "not all subscribers acknowledged";
                ok = false;
            }
        }
        else
        {
            // 无 ack 协议保证，保底窗覆盖异步发送
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    }
    else
    {
        error = "write failed";
    }
    // ⑤ 持续发布模式：首轮成功后按周期重复 write，尽力而为（RELIABLE 的 NACK 重传由协议
    // 自主完成，不再同步等 ack）；发布节拍为绝对时刻网格，对齐 ros2 rcl_timer 使平均频率贴合 -r
    if (ok && repeatIntervalMs > 0)
    {
        uint32_t sent = 1;
        uint32_t failCount = 0;
        const auto interval = std::chrono::milliseconds(repeatIntervalMs);
        auto next = std::chrono::steady_clock::now() + interval;
        while (!yomk::g_debugPubStop.load() && (maxTimes == 0 || sent < maxTimes))
        {
            std::this_thread::sleep_until(next);
            if (yomk::g_debugPubStop.load())
            {
                break;
            }
            if (RETCODE_OK == writer->write(&data))
            {
                ++sent;
                std::cout << "publishing #" << sent << " to topic " << topicName
                          << ": " << payloadJson << std::endl;
            }
            else
            {
                ++failCount;
            }
            // 绝对网格推进：write 耗时与过睡不累积进周期；下一网格点已过期时跳到未来最近
            // 网格点，不连续追发防唤醒风暴
            next += interval;
            const auto now = std::chrono::steady_clock::now();
            if (next <= now)
            {
                next += interval * (1 + (now - next) / interval);
            }
        }
        // 发满 maxTimes 退出（非 Ctrl+C）：writer 即将销毁、末条消息失去重传窗口，销毁前保底
        // 排空 200ms 等异步传输落地；排空必须在 delete_datawriter 之前（writer 销毁即切断重传路径）
        static constexpr uint32_t kPubDrainMs = 200;
        if (maxTimes > 0 && !yomk::g_debugPubStop.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(kPubDrainMs));
        }
        if (total != nullptr)
        {
            *total = sent;
        }
        if (failed != nullptr)
        {
            *failed = failCount;
        }
    }
    // 清理临时发布链；类型注册幂等不注销
    publisher->delete_datawriter(writer);
    participant_->delete_publisher(publisher);
    participant_->delete_topic(topic);
    DynamicDataFactory::get_instance()->delete_data(data);
    return ok;
}

// 发现线程回调入口：仅缓存，不建订阅。仅拿 seenMtx_ 叶子锁——本回调在 Fast DDS EDP 锁
// 临界区内被调用，拿 mtx_ 会与工作线程（持 mtx_ 建 reader 内部等 PDP/EDP 锁）锁序倒置死锁。
bool FastDDSDebugNode::onWriterDiscovered(const std::string& topicName,
        const rtps::PublicationBuiltinTopicData& info)
{
    std::lock_guard<std::mutex> lock(seenMtx_);
    seen_[topicName].push_back(info);
    return true;
}

// 发现线程回调入口：不唤醒工作线程（订阅建立仅由 writer 驱动）。仅拿 seenMtx_ 叶子锁
// （本回调在 Fast DDS PDP 大锁临界区内被调用，锁序倒置防护同上）。
bool FastDDSDebugNode::onReaderDiscovered(const std::string& topicName,
        const rtps::SubscriptionBuiltinTopicData& info)
{
    std::lock_guard<std::mutex> lock(seenMtx_);
    seenReaders_[topicName].push_back(info);
    return true;
}

// 发现线程回调入口：reader 离线同步移除，使 topicPub 的 GUID 校验只对"缓存仍有记录却无一
// 匹配"（挂起/异常死亡）生效，正常退出者已清出缓存不误报。仅拿 seenMtx_ 叶子锁（同上锁序防护）。
bool FastDDSDebugNode::onReaderRemoved(const std::string& topicName,
        const rtps::SubscriptionBuiltinTopicData& info)
{
    std::lock_guard<std::mutex> lock(seenMtx_);
    auto it = seenReaders_.find(topicName);
    if (it == seenReaders_.end())
    {
        return false;
    }
    for (auto eit = it->second.begin(); eit != it->second.end(); ++eit)
    {
        if (eit->guid == info.guid)
        {
            it->second.erase(eit);
            if (it->second.empty())
            {
                seenReaders_.erase(it);
            }
            return true;
        }
    }
    return false;
}

// 发现线程回调入口：不唤醒工作线程（订阅建立链路不依赖参与者发现）。仅拿 seenMtx_ 叶子锁
// （本回调在 Fast DDS PDP 大锁临界区内被调用，锁序倒置防护同上）。
void FastDDSDebugNode::onParticipantDiscovered(const rtps::ParticipantBuiltinTopicData& info)
{
    std::lock_guard<std::mutex> lock(seenMtx_);
    seenParticipants_.emplace(info.guid.guidPrefix,
            std::string(info.participant_name.to_string()));
}

// 官方文档 15.16 接收端形态：TypeInformation → TypeObject → DynamicType → DynamicPubSubType
// → Topic → DataReader。失败告警统一经 warnThrottled 节流（本函数每 100ms 重试一次：全静默
// 会使调用方永久得不到失败原因，不节流则持续失败刷屏）。
bool FastDDSDebugNode::tryStartSubscription(const std::string& topicName,
        const rtps::PublicationBuiltinTopicData& info)
{
    std::lock_guard<std::mutex> lock(mtx_);
    // 未登记/已建立订阅 → 忽略（同名 writer 重复发现由此去重）
    if (pending_.count(topicName) == 0 || subs_.count(topicName) > 0 ||
            participant_ == nullptr || subscriber_ == nullptr)
    {
        return false;
    }

    // 登记时暂存的 QoS 描述；无条目=全哨兵兜底（防御：正常路径登记与建订一一对应）
    DDSQosProfile qos;
    const auto qosIt = pendingQos_.find(topicName);
    if (qosIt != pendingQos_.end())
    {
        qos = qosIt->second;
    }

    // warnAt_ 受 mtx_ 保护（本函数入口即持 mtx_）
    auto warnThrottled = [this, &topicName](const char* kind, const std::string& text)
    {
        const std::string key = topicName + "|" + kind;
        const auto now = std::chrono::steady_clock::now();
        const auto it = warnAt_.find(key);
        if (warnAt_.end() != it &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count() <
                static_cast<int64_t>(kSubWarnIntervalMs))
        {
            return;
        }
        warnAt_[key] = now;
        std::cout << "warning: " << text << std::endl;
    };

    // TypeInformation → TypeObject：complete 优先、未设（TK_NONE）时回退 minimal，
    // 对齐 DynamicPubSubType 构造的类型标识选择逻辑
    const auto& ti = info.type_information.type_information;
    const auto& tid = (xtypes::TK_NONE != ti.complete().typeid_with_size().type_id()._d())
            ? ti.complete().typeid_with_size().type_id()
            : ti.minimal().typeid_with_size().type_id();
    xtypes::TypeObject type_object;
    if (RETCODE_OK != DomainParticipantFactory::get_instance()->type_object_registry().get_type_object(
                tid, type_object))
    {
        // TypeLookup 尚未取回远端 TypeObject：保留登记，等待轮询/后续重复发现事件重试
        warnThrottled("type-object",
                "TypeObject not ready, keep pending. topic=" + topicName +
                " type=" + info.type_name.to_string() +
                " -- the remote endpoint does not publish XTypes TypeInformation/TypeObject" +
                " (ROS2 rmw_fastrtps endpoints are built with compile-time type mapping and" +
                " usually provide none), or TypeLookup has not completed yet; this topic cannot" +
                " be subscribed dynamically. Retrying every " +
                std::to_string(kSubWarnIntervalSec) + "s.");
        return false;
    }

    auto dyn_type = DynamicTypeBuilderFactory::get_instance()->create_type_w_type_object(
                type_object)->build();
    if (dyn_type == nullptr)
    {
        warnThrottled("dyn-type",
                "build DynamicType failed. topic=" + topicName +
                " type=" + info.type_name.to_string() +
                " -- TypeObject was fetched but DynamicType construction failed" +
                " (the type contains constructs unsupported by the dynamic type builder).");
        return false;
    }

    TypeSupport ts(new DynamicPubSubType(dyn_type));
    ts.register_type(participant_);

    Topic* topic = participant_->create_topic(topicName, ts.get_type_name(), TOPIC_QOS_DEFAULT);
    if (topic == nullptr)
    {
        warnThrottled("create-topic",
                "create_topic failed. topic=" + topicName + " type=" + ts.get_type_name() +
                " -- a topic with the same name is already registered with a different type.");
        return false;
    }

    // reader 建立前任何失败仅回滚本次新建资源，主题保持 pending 等待重试
    try
    {
        DebugSub& entry = subs_[topicName];
        entry.type = ts;
        entry.topicType = ts.get();
        entry.topic = topic;
        entry.listener = std::make_unique<DebugSubListener>(topicName, ts.get_type_name(),
                    entry.topicType, sink_);
        DataReaderQos rqos = DATAREADER_QOS_DEFAULT;
        toReaderQos(qos, rqos);  // 无参=全哨兵零覆盖，底座值≡原 DATAREADER_QOS_DEFAULT（等值替换）
        entry.reader = subscriber_->create_datareader(topic, rqos, entry.listener.get());
        if (entry.reader == nullptr)
        {
            warnThrottled("datareader",
                    "create_datareader failed. topic=" + topicName +
                    " type=" + ts.get_type_name() +
                    " -- QoS incompatibility or resource limit.");
            subs_.erase(topicName);
            participant_->delete_topic(topic);
            return false;
        }
    }
    catch (...)
    {
        warnThrottled("exception",
                "subscribe setup threw an exception. topic=" + topicName +
                " type=" + ts.get_type_name() + " -- rolled back, keep pending for retry.");
        subs_.erase(topicName);
        participant_->delete_topic(topic);
        return false;
    }

    pending_.erase(topicName);
    pendingSince_.erase(topicName);
    pendingQos_.erase(topicName);
    std::cout << "subscribed topic=" << topicName
              << " type=" << ts.get_type_name() << std::endl;
    return true;
}

// 订阅建立工作线程：sleep 有界轮询兼顾"新发现/新登记响应"与"TypeObject 稍后就绪的重试"，
// 查交集按锁序 mtx_ → seenMtx_ 嵌套。等待刻意不用 condition_variable：GCC 11.4 libtsan 死锁
// 检测器对该模式报确定性 double lock 假阳性并级联假 data race（定罪链见 test/tsan.supp 头注）。
// 附带职责：登记满 kPendingHintDelayMs 仍无 writer 的主题打一次性等待提示（仅提示不改状态）。
void FastDDSDebugNode::workerLoop()
{
    constexpr auto kWorkerInterval = std::chrono::milliseconds(100);
    while (true)
    {
        std::string todo;
        std::string undiscovered;
        rtps::PublicationBuiltinTopicData info;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (!workerRunning_)
            {
                return;
            }
            {
                std::lock_guard<std::mutex> seenLock(seenMtx_);
                for (const auto& name : pending_)
                {
                    auto it = seen_.find(name);
                    if (it != seen_.end())
                    {
                        todo = name;
                        info = it->second.front();  // 取任一同主题 writer 快照，解锁后使用
                        break;
                    }
                    // 无 writer（含仅见 reader：建订只由 writer 驱动），登记满宽限窗且未提示过
                    // （已提示者值为 time_point::max()）
                    auto since = pendingSince_.find(name);
                    if (pendingSince_.end() != since &&
                            std::chrono::steady_clock::time_point::max() != since->second &&
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - since->second).count() >=
                            static_cast<int64_t>(kPendingHintDelayMs))
                    {
                        undiscovered = name;
                    }
                }
            }
            if (!undiscovered.empty())
            {
                // 先置 max() 再打印（打印在锁外），保证同一主题只提示一次
                pendingSince_[undiscovered] = std::chrono::steady_clock::time_point::max();
            }
        }
        if (!undiscovered.empty())
        {
            std::cout << "topic [" << undiscovered
                      << "] not discovered yet, waiting for publisher..." << std::endl;
        }
        if (!todo.empty())
        {
            tryStartSubscription(todo, info);
        }
        std::this_thread::sleep_for(kWorkerInterval);
    }
}
