// /record、/play 长驻阻塞期间持锁——锁保护 node_ 生命周期不被并发 deleteNode 破坏
// （同 YomkDdsDebugHandler 持锁收敛约定）；停止经 yomk::ctrl::dds::g_bagRecordStop / yomk::ctrl::dds::g_bagPlayStop 由
// SIGINT 处理函数异步置位，不依赖本服务其他端点。
#include "YomkDdsBagHandler.h"
#include "YomkDdsMsg.h"
#include "YomkDdsHandler.h"  // YomkDdsHandler::xmlCache()（XML profiles raw 串单例访问；本头未 include 公共头）

#include "FastDDSBagNode.h"
// 显式引入 yomkdds_xml::ParticipantQosSpec（FastDDSBagNode.h 传递亦可得，对齐 IWYU）
#include "FastDDSXmlHelper.h"

#include <utility>

namespace
{
constexpr uint32_t kMaxDomainId = 232;
}  // namespace

YomkDdsBagHandler::YomkDdsBagHandler(YomkServer* server) : YomkService(server)
{
    name("/YomkDdsBagHandler");
}

YomkDdsBagHandler::~YomkDdsBagHandler() = default;

int YomkDdsBagHandler::init()
{
    installFunc("/get_version", &YomkDdsBagHandler::getVersion);
    installFunc("/create_node", &YomkDdsBagHandler::createNode);
    installFunc("/record", &YomkDdsBagHandler::record);
    installFunc("/get_info", &YomkDdsBagHandler::getInfo);
    installFunc("/reindex", &YomkDdsBagHandler::reindex);
    installFunc("/convert", &YomkDdsBagHandler::convert);
    installFunc("/play", &YomkDdsBagHandler::play);
    installFunc("/stop_record", &YomkDdsBagHandler::stopRecord);
    installFunc("/pause_record", &YomkDdsBagHandler::pauseRecord);
    installFunc("/resume_record", &YomkDdsBagHandler::resumeRecord);
    installFunc("/stop_play", &YomkDdsBagHandler::stopPlay);
    installFunc("/pause_play", &YomkDdsBagHandler::pausePlay);
    installFunc("/resume_play", &YomkDdsBagHandler::resumePlay);
    installFunc("/set_play_rate", &YomkDdsBagHandler::setPlayRate);
    installFunc("/delete_node", &YomkDdsBagHandler::deleteNode);
    return 0;
}

yomk::payload::Response YomkDdsBagHandler::getVersion(YomkMsgPtr pkg)
{
    (void)pkg;  // 无参数载荷
    std::string version = "YomkDds v" YOMKDDS_VERSION_STRING;
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(version));
}

yomk::payload::Response YomkDdsBagHandler::createNode(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::BagCreateNodeRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not BagCreateNodeRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::BagCreateNodeRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[BagCreateNodeRequest] is dynamic_pointer_cast failed. ");
    };

    if (p->msg.domainId > kMaxDomainId)
    {
        yomk::log().tag("YomkDdsBagHandler::createNode").error("domainId [", p->msg.domainId, "] out of valid range [0,232]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "domainId [" + std::to_string(p->msg.domainId) + "] out of valid range [0,232]");
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ != nullptr)
    {
        yomk::log().tag("YomkDdsBagHandler::createNode").error("bag node already exists, delete it first");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "bag node already exists, delete it first");
    }

    auto node = std::make_unique<FastDDSBagNode>();
    // xmlCache 经 YomkDdsHandler::xmlCache() 单例取用（load_xml 成功后非空）；
    // 直配串空串=不直配现状
    auto spec = yomkdds_xml::ParticipantQosSpec{};
    spec.xml = YomkDdsHandler::xmlCache();
    spec.udpServerSpec = p->msg.udpDiscoverServerLocator;
    spec.tcpServerSpec = p->msg.tcpDiscoverServerLocator;
    std::string nodeErr;
    if (!node->setDomainId(p->msg.domainId, spec, &nodeErr))
    {
        yomk::log().tag("YomkDdsBagHandler::createNode").error("create bag node failed: ", nodeErr.empty() ? "setDomainId error" : nodeErr);
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed,
            "create bag node failed: " + (nodeErr.empty() ? "setDomainId error" : nodeErr));
    }
    node_ = std::move(node);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsBagHandler::record(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::BagRecordRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not BagRecordRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::BagRecordRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[BagRecordRequest] is dynamic_pointer_cast failed. ");
    };

    // 纯输入校验锁外前置：不触 DDS，未建节点时也可先报出明确的输入问题
    if (p->msg.topics.empty())
    {
        yomk::log().tag("YomkDdsBagHandler::record").error("no topics given");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "no topics given");
    }
    for (const auto& topic : p->msg.topics)
    {
        if (topic.empty())
        {
            yomk::log().tag("YomkDdsBagHandler::record").error("empty topic name in topic list");
            return yomk::payload::Response(yomk::payload::ResStatus::Failed, "empty topic name in topic list");
        }
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsBagHandler::record").error("bag node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "bag node not created");
    }

    // 兜底复位残留 stop 标志（上次 Ctrl+C 残留置位会致录制秒退）；只复位 stop 不动 paused——
    // paused 是调用方启动意图（CLI --start-paused / yomk::ctrl::dds::g_bagRecordPaused），handler 清除会使暂停态静默失效
    yomk::ctrl::dds::g_bagRecordStop.reset();

    // 长驻阻塞：录制至 SIGINT（yomk::ctrl::dds::g_bagRecordStop 置位）触发节点层收尾返回；
    // 启动校验失败（无端点主题）原因由节点层逐主题列于 msg
    std::string error;
    std::vector<FastDDSBagNode::BagTopicStat> stats;
    if (!node_->record(p->msg.topics, stats, &error, 0, 0, p->msg.outputDir, p->msg.maxBagSize,
                       p->msg.maxBagDurationSec, p->msg.maxCacheSize))
    {
        if (error.empty())
        {
            error = "bag record failed";  // 兜底，避免空 msg
        }
        yomk::log().tag("YomkDdsBagHandler::record").error("record failed: ", error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }

    // 回执只回结构化数据：bag 目录 + 逐主题统计（通配项已展开，保持录制清单顺序）；
    // 目录行与 "topic: N 条 / M 字节" 行由调用方（yomkdds）渲染
    yomk::payload::dds::RecordResult result;
    result.bagDir = node_->bagDir();
    result.topics.reserve(stats.size());
    for (const auto& stat : stats)
    {
        result.topics.push_back(
                yomk::payload::dds::BagTopicStat{stat.topic, stat.type, stat.count, stat.bytes});
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
            yomk::msgs::dds::RecordResult::shared(std::move(result)));
}

yomk::payload::Response YomkDdsBagHandler::getInfo(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::BagInfoRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not BagInfoRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::BagInfoRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[BagInfoRequest] is dynamic_pointer_cast failed. ");
    };

    // bagDir 空校验锁外前置（对齐 bagRecord 的纯输入校验先例：不触节点状态）
    if (p->msg.bagDir.empty())
    {
        yomk::log().tag("YomkDdsBagHandler::getInfo").error("no bag dir given");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "no bag dir given");
    }

    // bagInfo 为静态纯文件操作，不依赖 node_，不取 mtx_
    FastDDSBagNode::BagInfo info;
    std::string error;
    if (!FastDDSBagNode::bagInfo(p->msg.bagDir, info, &error))
    {
        if (error.empty())
        {
            error = "bag info failed";  // 兜底，避免空 msg
        }
        yomk::log().tag("YomkDdsBagHandler::getInfo").error("bag info failed: ", error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }

    // 回执只回结构化数据（metadata.json 字段 + 目录递归总字节）；行标签、单位、人类可读
    // 时间与 19 列对齐均由调用方（yomkdds）渲染
    yomk::payload::dds::BagInfo result;
    result.files = info.files;
    result.bagSizeBytes = info.bagSizeBytes;
    result.storageId = info.storageId;
    result.startTimeNs = info.startTimeNs;
    result.durationNs = info.durationNs;
    result.messageCount = info.messageCount;
    result.serializationFormat = info.serializationFormat;
    result.topics.reserve(info.topics.size());
    for (const auto& topic : info.topics)
    {
        result.topics.push_back(yomk::payload::dds::BagMetadataTopic{
                topic.topicName, topic.typeName, topic.qos, topic.messageCount});
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
            yomk::msgs::dds::BagInfo::shared(std::move(result)));
}

yomk::payload::Response YomkDdsBagHandler::reindex(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::BagReindexRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not BagReindexRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::BagReindexRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[BagReindexRequest] is dynamic_pointer_cast failed. ");
    };

    // 纯输入校验锁外前置（同 bagRecord 先例：不触节点状态）
    if (p->msg.bagDir.empty())
    {
        yomk::log().tag("YomkDdsBagHandler::reindex").error("no bag dir given");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "no bag dir given");
    }

    // bagReindex 为静态纯文件操作，不依赖 node_，不取 mtx_
    std::string error;
    if (!FastDDSBagNode::bagReindex(p->msg.bagDir, &error))
    {
        if (error.empty())
        {
            error = "bag reindex failed";  // 兜底，避免空 msg
        }
        yomk::log().tag("YomkDdsBagHandler::reindex").error("bag reindex failed: ", error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsBagHandler::convert(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::BagConvertRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not BagConvertRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::BagConvertRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[BagConvertRequest] is dynamic_pointer_cast failed. ");
    };

    // 纯输入校验锁外前置（同上先例）
    if (p->msg.inputs.empty())
    {
        yomk::log().tag("YomkDdsBagHandler::convert").error("no input bag dir given");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "no input bag dir given");
    }
    if (p->msg.cfgPath.empty())
    {
        yomk::log().tag("YomkDdsBagHandler::convert").error("no config file given");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "no config file given");
    }

    // bagConvert 为静态纯文件操作，不依赖 node_，不取 mtx_
    std::vector<FastDDSBagNode::BagConvertStat> stats;
    std::string error;
    if (!FastDDSBagNode::bagConvert(p->msg.inputs, p->msg.cfgPath, stats, &error))
    {
        if (error.empty())
        {
            error = "bag convert failed";  // 兜底，避免空 msg
        }
        yomk::log().tag("YomkDdsBagHandler::convert").error("bag convert failed: ", error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }

    // 回执只回结构化数据：每个输出 bag 一条统计（保持 output_bags 配置序）；
    // "converted <uri>: N messages / M bytes / K topics" 行由调用方（yomkdds）渲染
    yomk::payload::dds::ConvertResult result;
    result.bags.reserve(stats.size());
    for (const auto& stat : stats)
    {
        result.bags.push_back(yomk::payload::dds::ConvertedBagStat{
                stat.uri, stat.messageCount, stat.byteCount, stat.topicCount});
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
            yomk::msgs::dds::ConvertResult::shared(std::move(result)));
}

yomk::payload::Response YomkDdsBagHandler::play(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::BagPlayRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not BagPlayRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::BagPlayRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[BagPlayRequest] is dynamic_pointer_cast failed. ");
    };

    // 纯输入校验锁外前置（同 bagRecord 先例：不触节点状态）
    if (p->msg.bagDir.empty())
    {
        yomk::log().tag("YomkDdsBagHandler::play").error("no bag dir given");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "no bag dir given");
    }
    // 纯输入校验锁外前置（同上先例）
    if (p->msg.playbackRate <= 0.0)
    {
        yomk::log().tag("YomkDdsBagHandler::play").error("invalid playback rate");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "invalid playback rate");
    }
    // 负值非法：有意偏离 ros2 API 层“告警忽略按 0 播”，快速失败暴露拼写错误
    // （对齐 ros2 CLI check_not_negative_float 拦截形态）
    if (p->msg.startOffset < 0.0)
    {
        yomk::log().tag("YomkDdsBagHandler::play").error("invalid start offset");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "invalid start offset");
    }
    // 只拒负值（0 为合法缺省）；有意偏离 ros2 API 层负值告警按 0 播，快速失败暴露拼写错误
    if (p->msg.delay < 0.0)
    {
        yomk::log().tag("YomkDdsBagHandler::play").error("invalid delay");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "invalid delay");
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsBagHandler::play").error("bag node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "bag node not created");
    }

    // 兜底复位残留 stop 标志（上次 Ctrl+C 残留置位会致回放秒退）；只复位 stop 不动 paused——
    // paused 是运行意图（键盘线程 yomk::ctrl::dds::g_bagPlayPaused 置位后期待播放线程冻结时间轴），清除会静默失效
    yomk::ctrl::dds::g_bagPlayStop.reset();

    // 长驻阻塞：回放至 EOS 或 SIGINT（yomk::ctrl::dds::g_bagPlayStop 置位）触发节点层收尾返回；
    // 预扫失败（目录不存在/旧 bag 拒播/qos 非法/topics 未命中 bag 清单）原因由节点层列于 msg
    std::string error;
    std::vector<FastDDSBagNode::BagTopicStat> stats;
    if (!node_->play(p->msg.bagDir, stats, &error, p->msg.playbackRate, p->msg.topics,
                         p->msg.loop, p->msg.startOffset, p->msg.delay,
                         p->msg.waitForAllAckedMs, p->msg.topicRemaps,
                         p->msg.readAheadQueueSize, p->msg.qosOverridesPath))
    {
        if (error.empty())
        {
            error = "bag play failed";  // 兜底，避免空 msg
        }
        yomk::log().tag("YomkDdsBagHandler::play").error("play failed: ", error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }

    // 回执只回结构化数据：回放目录 + 逐主题统计（bag 预扫序，topicName 为发布名）；
    // 目录行与 "topic: N 条 / M 字节" 行由调用方（yomkdds）渲染
    yomk::payload::dds::PlayResult result;
    result.bagDir = p->msg.bagDir;
    result.topics.reserve(stats.size());
    for (const auto& stat : stats)
    {
        result.topics.push_back(yomk::payload::dds::BagTopicStat{
                stat.topic, stat.type, stat.count, stat.bytes});
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
            yomk::msgs::dds::PlayResult::shared(std::move(result)));
}

yomk::payload::Response YomkDdsBagHandler::deleteNode(YomkMsgPtr pkg)
{
    (void)pkg;  // 单 bag 节点模型无参数载荷

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsBagHandler::deleteNode").error("bag node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "bag node not created");
    }
    node_.reset();  // 析构按节点层内部顺序清理（reader → data → topic → subscriber → participant）
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsBagHandler::stopRecord(YomkMsgPtr pkg)
{
    // 控制桥：无锁直置控制面标志，绝不取 mtx_——/record 持锁长驻阻塞期间本端点必须可调度
    // （取锁即与运行中的录制互锁死）；录制循环轮询到置位即收尾返回并带回逐主题统计
    (void)pkg;
    yomk::ctrl::dds::g_bagRecordStop.stop();
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "stop requested");
}

yomk::payload::Response YomkDdsBagHandler::pauseRecord(YomkMsgPtr pkg)
{
    // 无锁约束同 stopRecord；暂停冻结订阅写路径（逐条检查 paused），恢复由 /resume_record 语义侧控制
    (void)pkg;
    yomk::ctrl::dds::g_bagRecordPaused.pause();
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "pause requested");
}

yomk::payload::Response YomkDdsBagHandler::resumeRecord(YomkMsgPtr pkg)
{
    // 无锁约束同 stopRecord；恢复后订阅写路径继续落盘（paused 逐条检查在写路径）
    (void)pkg;
    yomk::ctrl::dds::g_bagRecordPaused.resume();
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "resume requested");
}

yomk::payload::Response YomkDdsBagHandler::stopPlay(YomkMsgPtr pkg)
{
    // 无锁约束同 stopRecord；回放线程在各阻塞处轮询置位，收尾返回并带回逐主题统计
    (void)pkg;
    yomk::ctrl::dds::g_bagPlayStop.stop();
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "stop requested");
}

yomk::payload::Response YomkDdsBagHandler::pausePlay(YomkMsgPtr pkg)
{
    // 无锁约束同 stopPlay；暂停冻结时间轴（读线程被队列满反压自然停住）
    (void)pkg;
    yomk::ctrl::dds::g_bagPlayPaused.pause();
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "pause requested");
}

yomk::payload::Response YomkDdsBagHandler::resumePlay(YomkMsgPtr pkg)
{
    // 无锁约束同 stopPlay；恢复后重锚时间基准（对齐 ros2 PlayerClock 语义）
    (void)pkg;
    yomk::ctrl::dds::g_bagPlayPaused.resume();
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "resume requested");
}

yomk::payload::Response YomkDdsBagHandler::setPlayRate(YomkMsgPtr pkg)
{
    if (!pkg || pkg->name() != yomk::msgs::dds::BagPlayRateRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not BagPlayRateRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::BagPlayRateRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[BagPlayRateRequest] is dynamic_pointer_cast failed. ");
    }
    // 无锁约束同 stopPlay；rate<=0 由 bagPlaySetRate 校验拒绝并保持原值
    if (!yomk::ctrl::dds::bagPlaySetRate(p->msg.rate))
    {
        yomk::log().tag("YomkDdsBagHandler::setPlayRate").error("invalid playback rate [", p->msg.rate, "]");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "invalid playback rate: must be > 0");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "rate set");
}
