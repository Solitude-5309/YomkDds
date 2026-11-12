#ifndef FASTDDSDEBUGNODE_H
#define FASTDDSDEBUGNODE_H

// 引号平铺形式命中 include/，安装树经 yomk/dds/ 前缀提供同一头；缺省实参需完整类型
#include "YomkDdsService.h"
#include "FastDDSXmlHelper.h"

#include <chrono>
#include <cstdint>
#include <fastdds/dds/builtin/topic/PublicationBuiltinTopicData.hpp>
#include <fastdds/dds/builtin/topic/SubscriptionBuiltinTopicData.hpp>
#include <fastdds/rtps/builtin/data/ParticipantBuiltinTopicData.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/xtypes/type_representation/TypeObject.hpp>
#include <fastdds/rtps/common/Guid.hpp>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// 类型无关的调试节点：TypeInformation → TypeObject → DynamicType 自动建订，JSON 输出，
// 全程不依赖 IDL 生成类型。线程模型：发现回调仅缓存发现信息，DataReader 创建统一在工作线程
// 完成（实测发现回调线程内同步建 reader 会导致匹配成功但数据不交付）；公开方法以 mtx_ 串行化；
// 析构按停工作线程 → DataReader → Topic → Subscriber → Participant 顺序清理。
class FastDDSDebugNode
{
    class DebugParticipantListener;
    class DebugSubListener;
    struct DebugSub
    {
        eprosima::fastdds::dds::TypeSupport type;  // 持有 DynamicPubSubType（shared_ptr 所有权）
        eprosima::fastdds::dds::TopicDataType* topicType =
            nullptr;  // 指向 type 所辖同一对象，仅供 listener create/delete_data，不单独持有所有权
        eprosima::fastdds::dds::Topic* topic = nullptr;  // 订阅主题，析构时 delete_topic
        eprosima::fastdds::dds::DataReader* reader = nullptr;
        std::unique_ptr<DebugSubListener> listener;  // reader 的数据监听器，持有样本缓冲与输出 sink
    };

public:
    FastDDSDebugNode();
    ~FastDDSDebugNode();
    FastDDSDebugNode(const FastDDSDebugNode&) = delete;
    FastDDSDebugNode& operator=(const FastDDSDebugNode&) = delete;

public:
    using OutputSink = std::function<void(const std::string&)>;
    // 收敛查询默认参数：连续 5 次快照集合不变即收敛（2 次窗口过短，域内 participant 间歇
    // 上线时易误判提前收敛漏主题）、200ms 快照轮询间隔（默认窗口约 1s）
    static constexpr uint32_t kDefaultStableRounds = 5;
    static constexpr uint32_t kDefaultIntervalMs = 200;
    // 订阅失败告警节流窗口：工作线程每 100ms 重试，不节流刷屏、全静默则调用方永久得不到
    // 失败原因；秒值单列供告警文本引用（毫秒值由秒值换算，避免文本里出现裸数字换算）
    static constexpr uint32_t kMsPerSecond = 1000;
    static constexpr uint32_t kSubWarnIntervalSec = 5;
    static constexpr uint32_t kSubWarnIntervalMs = kSubWarnIntervalSec * kMsPerSecond;
    // 一次性"等待发布者"提示宽限窗：登记时本端 SPDP/EDP 发现尚未完成（公告周期约 3s），
    // 登记即判定会对"发布者早已在线"误报，满窗仍无 writer 才由工作线程提示
    static constexpr uint32_t kPendingHintDelayMs = 3000;
    // 仅可成功一次，失败/重复调用返回 false；spec/err 语义同 FastDDSNode::setDomainId
    // （XML 三级提取链——本节点无用户名，按名 profile 无意义仅默认档可兜底；serverLocator
    // 直配强制 CLIENT，节点唯一名在提取后置入）。
    bool setDomainId(uint32_t domainId, const yomkdds_xml::ParticipantQosSpec& spec =
            yomkdds_xml::ParticipantQosSpec{}, std::string* err = nullptr);
    // 未 setDomainId / 重复登记返回 false；类型解析暂不可用时保持登记，工作线程持续重试；
    // error 出参四类独立文本（未入域 / subscriber 未建 / 主题名为空 / 重复登记）。
    // qos 为动态订阅 reader 的 QoS 描述（可缺省尾参）：全哨兵=Fast DDS reader 默认底座
    // （KEEP_LAST(1)+BEST_EFFORT+VOLATILE）；对接 ROS2 高频流建议
    // DDSQosProfile::ros2DefaultSubscriber()（KEEP_LAST(10) 防丢样本）。建订成功随 pending_
    // 一同清除，失败保留供重试。
    bool subscribeTopic(const std::string& topicName, std::string* error = nullptr,
            const DDSQosProfile& qos = DDSQosProfile{});
    // 须在订阅实际建立前调用（工作线程建订阅时按值捕获），用于测试注入。
    void setOutputSink(OutputSink sink);
    // 收敛查询已发现主题与类型名：轮询发现缓存快照（writer 与 reader 均记录——仅有订阅者
    // 而无发布者的主题同样列出；缓存单调累积），连续 stableRounds 次集合不变即收敛返回；
    // stableRounds=1 即单次快照，等待期间远端新增端点会重置不变计数。0 值钳制默认 5 次/200ms；
    // 阻塞最长约 stableRounds*intervalMs，等待中 Ctrl+C 依赖进程信号处理。
    // 未入域 false（error="debug node not created"）。
    bool listTopics(std::vector<std::pair<std::string, std::string>>& topics,
            uint32_t stableRounds = kDefaultStableRounds,
            uint32_t intervalMs = kDefaultIntervalMs,
            std::string* error = nullptr);
    // topicInfo verbose 模式输出单元：nodeName 经 GUID 前缀归属匹配，未发现对应参与者时为空串；
    // guid 为 FastDDS 原生格式；qosLines 为已缩进的 ROS2 风格键值行
    struct EndpointDetail
    {
        std::string nodeName;
        std::string guid;
        std::vector<std::string> qosLines;

        // 供收敛轮询的快照相等性判定（tuple/vector 比较要求元素可比较）
        bool operator==(const EndpointDetail& rhs) const
        {
            return nodeName == rhs.nodeName && guid == rhs.guid && qosLines == rhs.qosLines;
        }
    };
    // 收敛查询单主题详情（与 listTopics 互不影响）：命中（任一 writer 或 reader 已发现）返回
    // true 并填充 typeName（原始 DDS 类型名）与端点计数；publishers/subscribers 非 nullptr 时
    // （verbose 模式）额外填充各端点详情，快照相等性含端点详情。0 值钳制默认 5 次/200ms；
    // 最长阻塞约 stableRounds*intervalMs。未入域 error="debug node not created"，未发现
    // error="topic [...] not found"。
    bool topicInfo(const std::string& topicName, std::string& typeName,
            size_t& publisherCount, size_t& subscriptionCount,
            uint32_t stableRounds = kDefaultStableRounds,
            uint32_t intervalMs = kDefaultIntervalMs,
            std::vector<EndpointDetail>* publishers = nullptr,
            std::vector<EndpointDetail>* subscribers = nullptr,
            std::string* error = nullptr);
    // 收敛查询域内命名参与者（与 listTopics/topicInfo 互不影响）：仅列出 participant_name 非空
    // 且非 "/" 者——"/" 是 ROS2 参与者默认占位名，rmw_fastrtps 统一置为根 enclave（rcl_init
    // 兜底），节点名走另一通道无辨识价值；同名多参与者各自一行；发现缓存不含自身。
    // 域内无有效命名参与者返回 true 且 names 为空。
    bool nodeList(std::vector<std::string>& names,
            uint32_t stableRounds = kDefaultStableRounds,
            uint32_t intervalMs = kDefaultIntervalMs,
            std::string* error = nullptr);
    // 收敛查询指定 participant_name 参与者的发布/订阅清单（与 listTopics/topicInfo/nodeList
    // 互不影响）。归属判定：RTPS 规范保证端点 GUID 前缀 == 所属参与者 GUID 前缀，与前缀集合
    // 比对即得（不依赖 participant_guid 字段）；同名多参与者的端点合并归入同一清单。
    // "/" 与空名参与者不在此过滤，按名可查——查 "/" 即该名称下全部参与者的端点合并。
    // 未入域 error="debug node not created"，无此名参与者 error="node [...] not found"。
    bool nodeInfo(const std::string& nodeName,
            std::vector<std::pair<std::string, std::string>>& publishers,
            std::vector<std::pair<std::string, std::string>>& subscribers,
            uint32_t stableRounds = kDefaultStableRounds,
            uint32_t intervalMs = kDefaultIntervalMs,
            std::string* error = nullptr);
    // 收敛查询类型 IDL 结构描述（与 listTopics/topicInfo 互不影响；仅支持顶层为 struct 的类型）。
    // error 三态回填：未入域 "debug node not created"、类型未出现 "type [...] not found"、
    // TypeObject 不可得 "type object for [...] not available"（ROS2 rmw_fastrtps 等不参与 XTypes
    // 类型发现的端点即此情形）、顶层非 struct "type [...] is not a struct, ..."。
    bool interfaceShow(const std::string& typeName,
            std::vector<std::string>& lines,
            uint32_t stableRounds = kDefaultStableRounds,
            uint32_t intervalMs = kDefaultIntervalMs,
            std::string* error = nullptr);
    // 收敛查询指定主题的发布示例三段行集（Type / IDL / example，与 listTopics/topicInfo/
    // interfaceShow 互不影响）：依次拿类型名 → IDL 行集 → JSON 发布示例（与 json_deserialize
    // 输入格式对称，可直接作为发布载荷模板）。失败按步骤各自归类回填 error，不统一报 not found。
    // 最长阻塞约 2*stableRounds*intervalMs。
    bool topicExample(const std::string& topicName,
            std::vector<std::string>& lines,
            uint32_t stableRounds = kDefaultStableRounds,
            uint32_t intervalMs = kDefaultIntervalMs,
            std::string* error = nullptr);
    // 收敛查询指定主题的消息描述两要素（typeName + JSON 发布模板，供导出消息描述文件用）。
    // 失败按步骤各自归类回填 error；最长阻塞约 stableRounds*intervalMs。
    bool topicMsgJson(const std::string& topicName,
            std::string& typeName,
            std::string& json,
            uint32_t stableRounds = kDefaultStableRounds,
            uint32_t intervalMs = kDefaultIntervalMs,
            std::string* error = nullptr);
    // 向指定主题发布消息（唯一写入型操作，与查询类互不影响）。四阶段链路：
    // ①发现收敛 → ②类型重建 + JSON 解析（json_deserialize EPROSIMA 格式，与 topic pub -e
    // 模板对称；失败不建 writer）→ ③建临时发布链（qos 全哨兵=Fast DDS writer 默认底座
    // KEEP_LAST(1)+RELIABLE+TRANSIENT_LOCAL，≡既有固定值，无参行为不变；ROS2 对接建议
    // DDSQosProfile::ros2DefaultPublisher()。requiredSubscribers>0 时先阻塞等待 matched 计数
    // 达标，不设超时，yomk::g_debugPubStop 置位即中断 error="waiting for subscribers
    // interrupted"）→ ④write 恰一次 + ack 自适应：发现缓存该主题存在 RELIABLE 订阅者时
    // wait_for_acknowledgments(2s)，超时 error="not all subscribers acknowledged"；全
    // BEST_EFFORT 或无订阅者退化 intervalMs 保底窗（无 ack 协议）。⑤repeatIntervalMs>0 持续
    // 发布：write 失败仅计入 failed 不返回、不再等 ack；maxTimes>0 发满即停并在销毁发布链前
    // 保底排空 200ms（末条消息失去协议重传窗口；Ctrl+C 中断不排空）。成败均清理临时发布链
    // （类型注册幂等不注销）。未入域 error="debug node not created"；最长阻塞约
    // 2*stableRounds*intervalMs + 2s（ack 可用时），requiredSubscribers>0 时匹配等待不设上限。
    // total/failed 仅持续模式回填（单次发布成功隐含 total=1）。
    bool topicPub(const std::string& topicName, const std::string& json,
            std::string& error,
            uint32_t stableRounds = kDefaultStableRounds,
            uint32_t intervalMs = kDefaultIntervalMs,
            uint32_t repeatIntervalMs = 0,
            uint32_t requiredSubscribers = 0,
            uint32_t maxTimes = 0,
            uint32_t* total = nullptr,
            uint32_t* failed = nullptr,
            const DDSQosProfile& qos = DDSQosProfile{});

private:
    // Precondition: 调用方已完成入域检查
    bool waitForTopicsStable(std::vector<std::pair<std::string, std::string>>& topics,
            uint32_t stableRounds, uint32_t intervalMs);
    // Precondition: 调用方已完成入域检查
    bool waitForTopicInfoStable(const std::string& topicName, std::string& typeName,
            size_t& publisherCount, size_t& subscriptionCount,
            uint32_t stableRounds, uint32_t intervalMs,
            std::vector<EndpointDetail>* publishers = nullptr,
            std::vector<EndpointDetail>* subscribers = nullptr);
    // Precondition: 调用方已完成入域检查
    bool waitForNodesStable(std::vector<std::string>& names,
            uint32_t stableRounds, uint32_t intervalMs);
    // Precondition: 调用方已完成入域检查
    bool waitForNodeInfoStable(const std::string& nodeName,
            std::vector<std::pair<std::string, std::string>>& publishers,
            std::vector<std::pair<std::string, std::string>>& subscribers,
            uint32_t stableRounds, uint32_t intervalMs);
    // Precondition: 调用方已完成入域检查；失败按收敛末轮实际原因归类写 error
    // （类型名未出现 / TypeObject 不可得 / IDL 拼装失败三态）。
    bool waitForInterfaceStable(const std::string& typeName,
            std::vector<std::string>& lines,
            uint32_t stableRounds, uint32_t intervalMs,
            std::string* error = nullptr);
    // TypeObject → DynamicType → IDL 行集拼装；仅支持顶层为 struct，否则 false 并经 error
    // 回填 "type [...] is not a struct, interface display unsupported"。仅读取入参，不触及节点状态。
    bool buildInterfaceLines(const eprosima::fastdds::dds::xtypes::TypeObject& type_object,
            const std::string& typeName, std::vector<std::string>& lines,
            std::string* error = nullptr);
    // 锁内仅查发现缓存与拷贝 TypeObject，锁外重建 DynamicType 取默认值样本序列化
    // （不注册类型不建订阅，纯类型内省）；失败分支各自经 error 回填具体原因。
    bool jsonExampleOfType(const std::string& typeName, std::string& out,
            std::string* error = nullptr);
    // 仅拿 seenMtx_ 叶子锁做缓存查询与 TypeObject 拷贝，锁外重建留给调用方；
    // topicPub（阶段②类型重建）与 jsonExampleOfType 共用。
    bool fetchTypeObject(const std::string& typeName, eprosima::fastdds::dds::xtypes::TypeObject& type_object);
    // 发现线程回调入口（DebugParticipantListener 转发）：缓存供登记晚于发现时回放，回调内
    // 不建订阅。运行于 Fast DDS 发现锁临界区内，仅拿 seenMtx_ 叶子锁（锁序倒置防护见成员注释）。
    bool onWriterDiscovered(const std::string& topicName,
                            const eprosima::fastdds::rtps::PublicationBuiltinTopicData& info);
    // 发现线程回调入口：不唤醒工作线程（订阅建立仅由 writer 驱动）。仅拿 seenMtx_ 叶子锁。
    bool onReaderDiscovered(const std::string& topicName,
                            const eprosima::fastdds::rtps::SubscriptionBuiltinTopicData& info);
    // 发现线程回调入口：reader 离线同步移除，使 topicPub 的 GUID 校验只对"缓存仍有记录却无一
    // 匹配"（挂起/异常死亡）生效。仅拿 seenMtx_ 叶子锁。
    bool onReaderRemoved(const std::string& topicName,
                         const eprosima::fastdds::rtps::SubscriptionBuiltinTopicData& info);
    // 发现线程回调入口：不唤醒工作线程（订阅建立链路不依赖参与者发现）。仅拿 seenMtx_ 叶子锁。
    void onParticipantDiscovered(const eprosima::fastdds::rtps::ParticipantBuiltinTopicData& info);
    // 建订阅链路（自加锁，仅由工作线程调用），任一步失败放弃本次订阅；四类失败告警统一经
    // warnAt_ 按（主题, 告警类型）节流（首次立即打印，之后距上次 ≥ kSubWarnIntervalMs 才重复）。
    bool tryStartSubscription(const std::string& topicName,
                              const eprosima::fastdds::rtps::PublicationBuiltinTopicData& info);
    // 订阅建立工作线程主体；等待刻意不用 cv（GCC 11.4 libtsan 对该模式报确定性假阳性，
    // 见 .cpp 实现注释），并对登记满 kPendingHintDelayMs 仍无 writer 的主题打一次性提示。
    void workerLoop();

private:
    eprosima::fastdds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::fastdds::dds::Subscriber* subscriber_ = nullptr;  // 订阅者，拥有全部 reader
    std::unique_ptr<DebugParticipantListener> listener_;        // participant 的发现监听器
    OutputSink sink_;
    std::set<std::string> pending_;         // 已登记、待发现的主题（含解析失败等待重试者）
    // 已发现 writer 缓存（topic → 发现信息列表）：发现事件对同一端点不重发，供登记晚于发现
    // 时回放；列表长度即该主题远端发布者数
    std::map<std::string, std::vector<eprosima::fastdds::rtps::PublicationBuiltinTopicData>> seen_;
    // 已发现 reader 缓存：与 seen_ 并列，供 listTopics 列出仅有订阅者的主题、topicInfo 统计
    // 订阅者数（订阅建立链路不依赖此缓存）
    std::map<std::string, std::vector<eprosima::fastdds::rtps::SubscriptionBuiltinTopicData>> seenReaders_;
    // GUID 前缀 → participant_name：前缀仅作幂等去重与 nodeInfo 归属匹配（RTPS 规范保证端点
    // GUID 前缀 == 所属参与者 GUID 前缀），不对外输出
    std::map<eprosima::fastdds::rtps::GuidPrefix_t, std::string> seenParticipants_;
    std::map<std::string, DebugSub> subs_;  // 已建立订阅
    // 订阅告警节流记录（"<主题名>|<告警类型>" → 上次打印时刻），受 mtx_ 保护，无需逐出
    std::map<std::string, std::chrono::steady_clock::time_point> warnAt_;
    // 主题登记时刻：满 kPendingHintDelayMs 仍无 writer 打一次性提示，打过后置 time_point::max()
    // 作已提示标记；订阅建立成功随 pending_ 一同清除，受 mtx_ 保护
    std::map<std::string, std::chrono::steady_clock::time_point> pendingSince_;
    // 已登记主题的 QoS 暂存：tryStartSubscription 入口取用（无条目=全哨兵兜底），建订成功随
    // pending_ 一同清除，失败保留供重试；受 mtx_ 保护（与 pending_/pendingSince_ 同锁域）
    std::map<std::string, DDSQosProfile> pendingQos_;
    std::mutex mtx_;  // 串行化公开方法与工作线程（participant_/pending_/subs_/workerRunning_）
    // 发现缓存专用叶子锁（保护 seen_/seenReaders_/seenParticipants_）：发现回调运行于 Fast DDS
    // 持有 PDP/EDP 内部锁的临界区内，只允许拿此锁——拿 mtx_ 会与工作线程（持 mtx_ 建 reader
    // 内部等 PDP 锁）形成锁序倒置死锁（实测 AB-BA）。锁序固定：mtx_ → seenMtx_，永不反向嵌套。
    std::mutex seenMtx_;
    std::thread worker_;          // 订阅建立工作线程（setDomainId 成功后启动）
    bool workerRunning_ = false;  // 工作线程生命周期标志（mtx_ 保护）
};

#endif  // FASTDDSDEBUGNODE_H
