#ifndef FASTDDSBAGNODE_H
#define FASTDDSBAGNODE_H

// 引 ParticipantQosSpec（XML 配置通道 + serverLocator 直配 spec；缺省实参需完整类型）
#include "FastDDSXmlHelper.h"

#include <cstdint>
#include <fastdds/dds/builtin/topic/PublicationBuiltinTopicData.hpp>
#include <fastdds/dds/builtin/topic/SubscriptionBuiltinTopicData.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// bag 录制/回放节点，模块级设计取舍：
// 透传机制：PassthroughPubSubType 不解析数据，原始 CDR 字节（含 encapsulation header）
// 原样落盘 mcap（schema_id=0 无 schema 通道、encoding="cdr"、thirdparty/mcap v2.1.3，
// 实现仅在本节点 .cpp 单译元编译）；发现层仅按主题名 + 类型名字符串匹配，与远端类型
// 定义零耦合。停止/暂停标志由 YomkDdsBagService 提供（record: yomk::g_bagRecordStop/
// g_bagRecordPaused；play: yomk::g_bagPlayStop/g_bagPlayPaused，暂停冻结时间轴）。
// 线程模型：发现回调仅缓存发现信息（seenMtx_ 叶子锁，锁序倒置防护同 FastDDSDebugNode）；
// 数据回调两路：maxCacheSize=0 同步直写（DDS 接收线程逐条写盘，payloadMtx_ 串行多
// reader 并发写同一 McapWriter），>0 写缓存双缓冲（回调深拷贝入队即返回、独立消费线程
// 写盘——仅把写盘 IO 挪出接收线程）。公开方法以 mtx_ 串行化；record/play 均为长驻阻塞
// 调用（record 成功返回后节点定格须重建，play 播完实体全清可再次调用）。
class FastDDSBagNode
{
    class BagParticipantListener;
    class BagSubListener;
    struct BagSub
    {
        eprosima::fastdds::dds::TypeSupport type;  // 持有透传 TopicDataType（shared_ptr 所有权，按类型名共享）
        eprosima::fastdds::dds::TopicDataType* topicType =
            nullptr;  // 指向 type 所辖同一对象，仅供 create/delete_data，不单独持有所有权
        void* data = nullptr;  // create_data() 创建的 Blob 接收缓冲（收尾/回滚经 topicType->delete_data 释放）
        eprosima::fastdds::dds::Topic* topic = nullptr;
        eprosima::fastdds::dds::DataReader* reader = nullptr;
        std::unique_ptr<BagSubListener> listener;  // reader 的数据监听器，引用 data 缓冲并持写盘上下文
        std::string qos;  // 落盘 QoS 串 "<RELIABILITY>,<DURABILITY>"（首见 writer offered；
                          // 仅订阅者主题空串）——Channel.metadata 与 metadata.json 同源单次取值
    };

public:
    FastDDSBagNode();
    ~FastDDSBagNode();
    FastDDSBagNode(const FastDDSBagNode&) = delete;
    FastDDSBagNode& operator=(const FastDDSBagNode&) = delete;

public:
    // record 校验收敛默认参数：连续 15 次快照不变即收敛 × 200ms 轮询
    static constexpr uint32_t kDefaultStableRounds = 15;
    static constexpr uint32_t kDefaultIntervalMs = 200;
    // 校验最短总窗（毫秒）：rounds*intervalMs 不足时向上提升 rounds。SPDP 参与者公告周期
    // 默认 3s，bag 节点与远端端点近同时上线时，校验窗须覆盖 SPDP+EDP 一整轮互通，否则
    // "刚上线的发布者"会被误判为无端点——取 2 倍公告周期
    static constexpr uint32_t kMinValidateWindowMs = 6000;
    // play 预读队列缺省深度（读侧 PlayQueue 背压边界；writer QoS 独立常量不随其联动）
    static constexpr size_t kPlayReadAheadQueueSizeDefault = 1000;
    struct BagTopicStat
    {
        std::string topic;     // 用户输入原样
        std::string type;      // 远端端点公告的原始 DDS 类型名，无转换
        uint64_t count = 0;
        uint64_t bytes = 0;    // CDR 全量，含 encapsulation header
    };
    // 仅可成功一次，失败/重复调用返回 false。spec/err 语义同 FastDDSNode::setDomainId
    // （XML 三级提取链——本节点无用户名，仅默认档可兜底；serverLocator 直配强制 CLIENT）。
    bool setDomainId(uint32_t domainId, const yomkdds_xml::ParticipantQosSpec& spec =
            yomkdds_xml::ParticipantQosSpec{}, std::string* err = nullptr);
    // 录制主题列表（长驻阻塞：调用线程阻塞至 yomk::g_bagRecordStop 置位并完成收尾）。
    // 清单项支持单 '*' 通配，模式项按发现缓存全表展开为实际录制清单；任一精确主题既无
    // 发布者也无订阅者、或模式未命中即整体报错 false（不建目录不产生任何文件；仅订阅者
    // 主题同样通过）。maxBagSize/maxBagDurationSec 先到先分片，maxCacheSize>0 走写缓存
    // 双缓冲（缓存满丢弃新消息并按主题计数，收尾统一告警）。reader QoS 跟随远端 writer
    // offered；每主题一个 mcap Channel（schema_id=0，metadata 落 type+qos）。
    // g_bagRecordPaused 置位期间照常 take 但丢弃（不计入时间统计）。
    // 成功返回 true 且节点定格（再次 record 报错须重建）；失败返回 false 并经 error 出参
    // 回填原因，未定格可修正输入后重试。
    bool record(const std::vector<std::string>& topics, std::vector<BagTopicStat>& stats,
            std::string* error = nullptr,
            uint32_t stableRounds = kDefaultStableRounds,
            uint32_t intervalMs = kDefaultIntervalMs,
            const std::string& outputDir = "",
            uint64_t maxBagSize = 0,
            uint64_t maxBagDurationSec = 0,
            uint64_t maxCacheSize = 0);
    // 最近一次成功录制的 bag 目录名（当前路径相对名，如 bag_2026-01-01_12-00-00_000）；未录制过为空。
    const std::string& bagDir() const
    {
        return bagDir_;
    }
    // 回放 bag 目录（长驻阻塞：阻塞至播毕或停止标志置位；不引入定格——播完发布实体全清，
    // 可再次调用）。读播解耦：读线程经 mcap 索引逐条预读原始 CDR 入有界队列（满则背压，
    // loop 到尾回卷重预读），播放线程按录制时间轴还原节奏，writer 按 bag 内快照按需创建
    // （暂停置位冻结时间轴，恢复后重锚继续）。预扫 metadata.json：type 空串的旧格式 bag
    // 拒播，qos 空/缺失回退 RELIABLE+VOLATILE。读流中途失败记错误推 EOS，已读消息照常
    // 播完后返回 false。参数语义：
    // playbackRate 速率倍率：>0 生效（2.0 两倍速、0.5 半倍速），<=0 报 "invalid playback
    //   rate"；topics 空=全部，精确项不在 bag 报错、单 '*' 按 bag 清单展开、未命中报错，
    //   入选集合=精确∪通配命中（去重升序）；loop 到尾回卷无限播放，轮边界重锚无倾泻。
    // startOffset 读侧跳过早于全场首条（过滤前基准）+偏移的消息（bag 级偏移不受 topics
    //   过滤影响，与 loop 叠加每轮保持），负值报错；delay 落播放侧每轮轮边界前分段轮询
    //   睡（大延迟下 Ctrl+C 即退），负值报错；waitForAllAckedMs 播毕/停止后对 RELIABLE
    //   writer 分片等 ACK，0 永等（片间可中断）、>0 超时仅告警、-1 不等待（缺省）。
    // topicRemaps "old:=new" 精确映射发布名（QoS/类型按 bag 快照照旧，--topics 按原名先
    //   过滤后改名，old 不在 bag 清单静默无效，回显/统计显示新名）；readAheadQueueSize
    //   预读队列深度（读侧吸收磁盘 IO 抖动），<1 报错，writer history 上限独立不联动，
    //   队列空致播放持续等待打一次性 starved 告警；qosOverridesPath QoS 覆盖 JSON（键=
    //   bag 原主题名；三档优先级文件>bag 快照>回退，非法枚举报错、清单外键静默无效），
    //   空串=不加载。
    bool play(const std::string& bagDir, std::vector<BagTopicStat>& stats,
            std::string* error = nullptr, double playbackRate = 1.0,
            const std::vector<std::string>& topics = {}, bool loop = false,
            double startOffset = 0.0, double delay = 0.0,
            int64_t waitForAllAckedMs = -1,
            const std::vector<std::string>& topicRemaps = {},
            size_t readAheadQueueSize = kPlayReadAheadQueueSizeDefault,
            const std::string& qosOverridesPath = {});
    // 读 bag 目录 metadata.json 组装 info 文本（纯文件操作 DDS-free，不经节点实例；输出
    // 形态对齐 ros2 bag info）。outLines 每项一行（不含换行符，首项为空行）；失败返回
    // false 并经 error 出参回填原因（路径不存在/无 metadata.json/解析失败）。bagDir 相对/
    // 绝对路径均可。
    static bool bagInfoText(const std::string& bagDir, std::vector<std::string>& outLines,
            std::string* error = nullptr);
    // 从 bag 目录内 mcap 分片文件重建 metadata.json（纯文件操作 DDS-free，不经节点实例；
    // metadata.json 缺失/损坏均可重建，无条件覆盖写出）。失败返回 false 并经 error 出参
    // 回填原因。bagDir 相对/绝对路径均可。
    static bool bagReindex(const std::string& bagDir, std::string* error = nullptr);
    // 将输入 bag 目录集按输出配置文件转换合并为新 bag（纯文件操作 DDS-free，不经节点实
    // 例；输出配置语义对齐 ros2 bag convert：顶层 output_bags 键为对象序列，每条目独立
    // uri/topics/start_time/end_time/max_bagfile_size/max_bagfile_duration，未知字段宽容
    // 忽略；start_time/end_time 支持非负整数纳秒或可读时间串——本地时区，与产物 metadata
    // starting_time 的 `_format` 伴生键同款，拷贝即用）。逐输入收集分片预扫主题与类型名
    // （同主题跨输入类型冲突报错；入选主题无类型名的旧格式 bag 拒转），逐条目 k-way 归并
    // （logTime 全局升序，多输入按 logTime 全局归并；inputs 至少 1 个，重复目录拒绝）。
    // 任一条目失败返回 false 并经 error 回填原因（已产生的输出目录保留不回滚）。
    static bool bagConvert(const std::vector<std::string>& inputs, const std::string& cfgPath,
            std::vector<std::string>& outLines, std::string* error = nullptr);

private:
    // 发现线程回调入口（BagParticipantListener 转发）：新见 writer/reader 追加进发现缓存。
    // 仅拿 seenMtx_ 叶子锁：回调在 Fast DDS 持有 PDP/EDP 内部锁的临界区内被调用，拿 mtx_ 会
    // 与 record（持 mtx_ 建 reader 内部等 PDP 锁）形成锁序倒置死锁（防护同 FastDDSDebugNode）。
    bool onWriterDiscovered(const std::string& topicName,
                            const eprosima::fastdds::rtps::PublicationBuiltinTopicData& info);
    bool onReaderDiscovered(const std::string& topicName,
                            const eprosima::fastdds::rtps::SubscriptionBuiltinTopicData& info);

private:
    eprosima::fastdds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::fastdds::dds::Subscriber* subscriber_ = nullptr;  // 订阅者，拥有全部 reader
    std::unique_ptr<BagParticipantListener> listener_;
    // 已发现 writer/reader 缓存（topic → 发现信息列表，同主题每端点各占一项，首项供类型名
    // 与 QoS 读取；发现事件不重发，缓存单调累积——录制校验只看有无，不处理端点离线）
    std::map<std::string, std::vector<eprosima::fastdds::rtps::PublicationBuiltinTopicData>> seen_;
    std::map<std::string, std::vector<eprosima::fastdds::rtps::SubscriptionBuiltinTopicData>> seenReaders_;
    std::map<std::string, BagSub> subs_;
    std::mutex mtx_;  // 串行化公开方法（record 长驻阻塞期间本节点全部入口排队）
    // 发现缓存专用叶子锁（保护 seen_/seenReaders_）：锁序固定 mtx_ → seenMtx_，seenMtx_
    // 永不反向嵌套（锁序倒置防护同 FastDDSDebugNode::seenMtx_）
    std::mutex seenMtx_;
    bool recorded_ = false;   // 录制定格标志（成功收尾后置位；mtx_ 保护）
    std::string bagDir_;      // 最近一次录制的 bag 目录名（recorded_ 置位后有效）
};

#endif  // FASTDDSBAGNODE_H
