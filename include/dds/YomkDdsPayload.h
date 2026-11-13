#pragma once
// Maintainer notes: single home for every YOMKDDS_* request/response structure,
// callback type, QoS description and config helper (namespace yomk::payload).
// Public headers keep zero fastdds dependency; message declarations live in
// YomkDdsMsg.h, service classes in the per-service headers.

#include <string>

#include "yomk/server/YomkServerPayload.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <vector>

namespace yomk::payload
{
// ===== Config helpers (used by payload default initializers and callers) =====

/** @brief Parse base for decimal domain-id environment values */
// @zh: 域号环境值的十进制解析基数
constexpr unsigned long kDomainEnvBase = 10;

/** @brief Upper bound of valid DDS domain ids */
// @zh: DDS 域号合法上限
constexpr unsigned long kDomainIdEnvMax = 232;

/**
 * @brief Default DDS domain id from environment
 * @return Parsed YOMKDDS_DDS_DOMAIN_ID, or 0 if unset/invalid/out of range
 * @note Silent fallback to 0; no logging
 */
// @zh: 从环境变量读取缺省 DDS 域号
// @zh: return 解析 YOMKDDS_DDS_DOMAIN_ID，未设置/非法/越界时回落 0
// @zh: note 静默回落，不打印日志
inline uint32_t YomkDdsDefaultDomainId()
{
    const char* v = std::getenv("YOMKDDS_DDS_DOMAIN_ID");
    if (!v || !*v)
    {
        return 0;
    }
    char* end = nullptr;
    unsigned long n = std::strtoul(v, &end, kDomainEnvBase);
    if (end == v || *end != '\0' || n > kDomainIdEnvMax)
    {
        return 0;
    }
    return static_cast<uint32_t>(n);
}

/**
 * @brief Discovery-server direct-configuration master switch
 * @return True only when YOMKDDS_USE_DISCOVER_SERVER is "true"/"1" (case-insensitive)
 * @note Silent on any other value; no logging
 */
// @zh: 发现服务器直配总开关
// @zh: return 仅当 YOMKDDS_USE_DISCOVER_SERVER 为 "true"/"1"（大小写不敏感）时为真
// @zh: note 其余取值一律视为关闭，静默不打印日志
inline bool YomkDdsUseDiscoverServer()
{
    const char* v = std::getenv("YOMKDDS_USE_DISCOVER_SERVER");
    if (!v)
    {
        return false;
    }
    std::string s(v);
    for (char& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s == "true" || s == "1";
}

/**
 * @brief Default UDP discovery-server locator list
 * @return YOMKDDS_UDP_DISCOVER_SERVERS when the master switch is on, otherwise empty
 * @note Empty string means no direct configuration; locator validity is
 *       validated at node creation — invalid input fails loudly instead of
 *       silently degrading to multicast discovery
 */
// @zh: 缺省 UDP 通道发现服务器聚合串
// @zh: return 总开关开启时取 YOMKDDS_UDP_DISCOVER_SERVERS，否则空串
// @zh: note 空串=不直配；串合法性由节点创建时校验，非法串显式报错而非静默退化成多播发现
inline std::string YomkDdsDefaultUdpDiscoverServers()
{
    if (!YomkDdsUseDiscoverServer())
    {
        return {};
    }
    const char* v = std::getenv("YOMKDDS_UDP_DISCOVER_SERVERS");
    return v ? std::string(v) : std::string();
}

/**
 * @brief Default TCP discovery-server locator list
 * @return YOMKDDS_TCP_DISCOVER_SERVERS when the master switch is on, otherwise empty
 */
// @zh: 缺省 TCP 通道发现服务器聚合串
// @zh: return 总开关开启时取 YOMKDDS_TCP_DISCOVER_SERVERS，否则空串
inline std::string YomkDdsDefaultTcpDiscoverServers()
{
    if (!YomkDdsUseDiscoverServer())
    {
        return {};
    }
    const char* v = std::getenv("YOMKDDS_TCP_DISCOVER_SERVERS");
    return v ? std::string(v) : std::string();
}

/**
 * @brief XML profiles auto-load master switch
 * @return True only when YOMKDDS_USE_XML_PROFILE is "true"/"1" (case-insensitive)
 */
// @zh: XML profiles 自动加载总开关
// @zh: return 仅当 YOMKDDS_USE_XML_PROFILE 为 "true"/"1"（大小写不敏感）时为真
inline bool YomkDdsUseXmlProfile()
{
    const char* v = std::getenv("YOMKDDS_USE_XML_PROFILE");
    if (!v)
    {
        return false;
    }
    std::string s(v);
    for (char& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s == "true" || s == "1";
}

/**
 * @brief Default XML profiles path
 * @return YOMKDDS_XML_PROFILE_PATH when the master switch is on, otherwise empty
 * @note Path validity is validated by the load_xml endpoint; empty string
 *       skips loading silently
 */
// @zh: 缺省 XML profiles 路径
// @zh: return 总开关开启时取 YOMKDDS_XML_PROFILE_PATH，否则空串
// @zh: note 路径合法性由 load_xml 端点校验；空串由调用方静默跳过加载
inline std::string YomkDdsDefaultXmlProfilePath()
{
    if (!YomkDdsUseXmlProfile())
    {
        return {};
    }
    const char* v = std::getenv("YOMKDDS_XML_PROFILE_PATH");
    return v ? std::string(v) : std::string();
}

/** @brief Default fastdds discovery server listening port */
// @zh: fastdds discovery 默认监听端口
constexpr uint32_t kDdsDiscoveryDefaultPort = 11811;

/** @brief Default bag-play read-ahead queue depth (ros2-compatible default) */
// @zh: bag play 预读内存队列深度缺省值（对齐 ros2 参考实现）
constexpr size_t kBagPlayReadAheadQueueSize = 1000;

// ===== Callback types =====

/**
 * @brief Subscription callback invoked once per received message
 * @warning The sample pointer is only valid during the callback; consume it
 *          synchronously and never hold it across calls
 */
// @zh: 订阅回调，每收到一条消息调用一次
// @zh: warning 消息实例指针仅回调期间有效，须同步消费，勿跨调用持有
using DDSCallbackFunc = std::function<void(const void*)>;

/**
 * @brief Debug output callback receiving one formatted JSON text per message
 * @warning Invoked on the DDS reception thread; must be thread-safe and return quickly
 */
// @zh: 调试输出回调，每条消息投递一次格式化 JSON 文本
// @zh: warning 在 DDS 数据接收线程被调用，须线程安全且快速返回
using DDSDebugOutputFunc = std::function<void(const std::string&)>;

// ===== Endpoint QoS =====

/** @brief Endpoint history kind (Unset keeps the Fast DDS default base) */
// @zh: 端点 history 种类（Unset=保持 Fast DDS 默认底座）
enum class DDSHistoryKind : uint8_t
{
    Unset = 0,  // sentinel: not set
    KEEP_LAST,
    KEEP_ALL,
};

/** @brief Endpoint reliability (Unset keeps the Fast DDS default base) */
// @zh: 端点可靠性（Unset=保持 Fast DDS 默认底座）
enum class DDSReliability : uint8_t
{
    Unset = 0,  // sentinel: not set
    RELIABLE,
    BEST_EFFORT,
};

/** @brief Endpoint durability (Unset keeps the Fast DDS default base) */
// @zh: 端点持久性（Unset=保持 Fast DDS 默认底座）
enum class DDSDurability : uint8_t
{
    Unset = 0,  // sentinel: not set
    TRANSIENT_LOCAL,
    VOLATILE,
};

/** @brief Endpoint liveliness kind (Unset keeps the Fast DDS default base) */
// @zh: 端点 liveliness 种类（Unset=保持 Fast DDS 默认底座）
enum class DDSLiveliness : uint8_t
{
    Unset = 0,  // sentinel: not set
    AUTOMATIC,
    MANUAL_BY_TOPIC,
};

/**
 * @brief Endpoint QoS description, common to writers and readers
 *
 * Unset fields (enum Unset / numeric -1) keep the Fast DDS endpoint default
 * base; set fields override per-item, matching the rclcpp "base + override"
 * model. Time fields are nanoseconds, -1 = base default (infinite).
 */
// @zh: 端点 QoS 描述（writer/reader 通用）
// @zh: 未设置项（枚举 Unset / 数值 -1）保持 Fast DDS 端点默认底座，非哨兵项逐项覆盖，
// @zh: 与 rclcpp "底座+逐项覆盖"语义同构；时间类 -1=未设置=底座默认 ∞
struct DDSQosProfile
{
    DDSHistoryKind historyKind = DDSHistoryKind::Unset;
    int32_t depth = -1;              // KEEP_LAST history depth; ignored for KEEP_ALL
    DDSReliability reliability = DDSReliability::Unset;
    DDSDurability durability = DDSDurability::Unset;
    DDSLiveliness liveliness = DDSLiveliness::Unset;
    int64_t deadlineNs = -1;         // deadline period in nanoseconds
    int64_t lifespanNs = -1;         // message lifespan in nanoseconds (writer side)
    int64_t livelinessLeaseNs = -1;  // liveliness lease in nanoseconds

    /** @brief ROS2 base: KEEP_LAST(10)+RELIABLE+VOLATILE+AUTOMATIC (offered matches any ROS2 request) */
    // @zh: ROS2 发布端底座（8 项全非哨兵，等价整包替换）
    static DDSQosProfile ros2DefaultPublisher();

    /**
     * @brief ROS2 base: KEEP_LAST(10)+BEST_EFFORT+VOLATILE+AUTOMATIC (requested matches any offer)
     * @note RELIABLE subscribers receive nothing from BEST_EFFORT topics
     */
    // @zh: ROS2 订阅端底座（8 项全非哨兵，等价整包替换）
    // @zh: note RELIABLE 版订阅 BEST_EFFORT 话题收不到
    static DDSQosProfile ros2DefaultSubscriber();

    /** @brief ROS2 sensor-data base: KEEP_LAST(5)+BEST_EFFORT+VOLATILE+AUTOMATIC, both endpoints */
    // @zh: ROS2 sensor-data 底座（两端通用）
    static DDSQosProfile ros2SensorData();
};

/**
 * @brief Discovery server connection info (client role)
 * @note Parsed product of a serverLocator aggregate string; ip:port + CLIENT
 *       protocol is sufficient
 */
// @zh: 发现服务器连接描述（client 角色）
// @zh: 节点层 serverLocator 聚合串解析产物；ip:port + CLIENT 协议足够
struct DDSServerInfo
{
    std::string address;                       // server listening IP
    uint32_t port = kDdsDiscoveryDefaultPort;  // server listening port
};

// ===== YomkDdsService request/response payloads =====

/**
 * @brief create_node request
 * @note Unset optional fields fall back to environment variables; locator
 *       strings ("ip[:port]", semicolon-separated for multiple servers) are
 *       validated at creation — invalid input fails with the reason echoed
 */
// @zh: create_node 请求负载
// @zh: note 可缺省字段经类内缺省初始化器读环境变量；直配聚合串格式 "ip[:port]"，
// @zh: 多 server 分号分隔，非法串创建失败并回显原因
struct DDSNode
{
    std::string nodeName;  // unique node name, must not be empty
    uint32_t domainId = YomkDdsDefaultDomainId();                               // DDS domain id [0,232]
    std::string udpDiscoverServerLocator = YomkDdsDefaultUdpDiscoverServers();  // UDP channel
    std::string tcpDiscoverServerLocator = YomkDdsDefaultTcpDiscoverServers();  // TCP channel
};

/**
 * @brief load_xml request
 * @note Success only proves the file parses and is cached; the three-level
 *       extraction chain applies when nodes are created afterwards
 */
// @zh: load_xml 请求负载
// @zh: note 成功仅证明文件可解析并缓存；后续 create_node 按三级提取链叠加
struct DDSXmlLoad
{
    std::string xmlPath = YomkDdsDefaultXmlProfilePath();  // relative or absolute
};

/**
 * @brief register_pub_topic request
 * @warning Ownership of type (TopicDataType*) is transferred to the service;
 *          the caller must not hold or free it afterwards regardless of result
 */
// @zh: register_pub_topic 请求负载
// @zh: warning type（TopicDataType*）所有权移交服务端，无论成败调用后 caller 不再持有/释放
struct DDSTopic
{
    std::string nodeName;
    std::string topicName;
    void* type = nullptr;  // TopicDataType*; ownership transferred to the service
    DDSQosProfile qos;     // all-sentinel default keeps the Fast DDS base
};

/**
 * @brief register_sub_topic request
 * @warning Same type ownership transfer as DDSTopic
 */
// @zh: register_sub_topic 请求负载
// @zh: warning type 所有权语义同 DDSTopic
struct DDSSubRequest
{
    std::string nodeName;
    std::string topicName;
    void* type = nullptr;      // TopicDataType*; ownership transferred to the service
    DDSCallbackFunc callback;  // invoked once per received message
    DDSQosProfile qos;         // all-sentinel default keeps the Fast DDS base
};

/**
 * @brief publish request
 * @note data is borrowed, not owned: publish writes synchronously, the caller
 *       keeps managing its lifetime
 */
// @zh: publish 请求负载
// @zh: note data 为借用（非所有权）：publish 同步写入，caller 保留并自行管理其生命周期
struct DDSPublish
{
    std::string nodeName;
    std::string topicName;
    void* data = nullptr;  // borrowed (non-owning) message instance pointer
};

/**
 * @brief loan / discard_loan request
 * @note sample is ignored for loan; carries the pointer to return for discard
 */
// @zh: loan / discard_loan 请求负载
// @zh: note loan 请求忽略 sample；discard 请求携带待归还的借出指针
struct DDSLoan
{
    std::string nodeName;
    std::string topicName;
    void* sample = nullptr;
};

/**
 * @brief loan success response payload
 * @warning The loaned sample belongs to the writer pool: fill it and publish
 *          via the chained data()...pub(); the pointer is reclaimed after each write
 */
// @zh: loan 成功响应负载
// @zh: warning 借出样本属 writer 池：填值后经链式 data()...pub() 发布，每次 write 后指针失效
struct DDSLoanResult
{
    void* sample = nullptr;
};

// ===== YomkDdsDebugService request payloads =====

/** @brief debug create_node request (same env fallbacks as DDSNode) */
// @zh: debug create_node 请求负载（环境变量回落语义同 DDSNode）
struct DDSDebugNode
{
    uint32_t domainId = YomkDdsDefaultDomainId();                               // DDS domain id [0,232]
    std::string udpDiscoverServerLocator = YomkDdsDefaultUdpDiscoverServers();  // UDP channel
    std::string tcpDiscoverServerLocator = YomkDdsDefaultTcpDiscoverServers();  // TCP channel
};

/**
 * @brief topic_print request
 * @warning Empty output callback is rejected
 */
// @zh: topic_print 请求负载
// @zh: warning 空输出回调拒绝
struct DDSDebugTopic
{
    std::string topicName;      // topic to observe
    DDSDebugOutputFunc output;  // user-defined output callback
    DDSQosProfile qos;          // dynamic subscription reader QoS
};

/** @brief list_topics request (0 values are clamped to defaults 5 rounds / 200ms) */
// @zh: list_topics 请求负载（0 值由节点层钳制为默认 5 次/200ms）
struct DDSDebugList
{
    uint32_t stableRounds = 0;  // consecutive unchanged-snapshot rounds required
    uint32_t intervalMs = 0;    // snapshot polling interval in milliseconds
    bool types = false;         // true: each line is "topic [type]"
};

/** @brief topic_info request (independent convergence parameters) */
// @zh: topic_info 请求负载（独立收敛参数）
struct DDSDebugInfo
{
    std::string topicName = "";  // topic to query
    uint32_t stableRounds = 0;
    uint32_t intervalMs = 0;
    bool verbose = false;        // true: per-endpoint node name/GUID/QoS lines
};

/** @brief list_nodes request (independent convergence parameters) */
// @zh: list_nodes 请求负载（独立收敛参数）
struct DDSNodeList
{
    uint32_t stableRounds = 0;
    uint32_t intervalMs = 0;
};

/** @brief node_info request (independent convergence parameters) */
// @zh: node_info 请求负载（独立收敛参数）
struct DDSNodeInfo
{
    std::string nodeName = "";  // participant_name exact match
    uint32_t stableRounds = 0;
    uint32_t intervalMs = 0;
};

/** @brief topic_find request (type name exact match) */
// @zh: topic_find 请求负载（类型名精确匹配）
struct DDSDebugFind
{
    std::string typeName = "";
    uint32_t stableRounds = 0;
    uint32_t intervalMs = 0;
};

/** @brief interface_show request (IDL struct dump by type name) */
// @zh: interface_show 请求负载（按类型名输出 IDL 结构）
struct DDSDebugInterfaceShow
{
    std::string typeName = "";
    uint32_t stableRounds = 0;
    uint32_t intervalMs = 0;
};

/** @brief interface_list request (all discovered type names, deduplicated) */
// @zh: interface_list 请求负载（全部已发现类型名，去重排序）
struct DDSDebugInterfaceList
{
    uint32_t stableRounds = 0;
    uint32_t intervalMs = 0;
};

/** @brief topic_example request (Type/IDL/example command three-part lines) */
// @zh: topic_example 请求负载（Type/IDL/example 命令三段行集）
struct DDSDebugTopicExample
{
    std::string topicName = "";  // exact match
    uint32_t stableRounds = 0;
    uint32_t intervalMs = 0;
};

/** @brief topic_msg request (type name + compact msg JSON, exactly two lines) */
// @zh: topic_msg 请求负载（类型名 + 紧凑 msg JSON，恰两行）
struct DDSDebugTopicMsg
{
    std::string topicName = "";  // exact match
    uint32_t stableRounds = 0;
    uint32_t intervalMs = 0;
};

/**
 * @brief topic_pub request (JSON payload publish with full first-shot validation)
 * @note requiredSubscribers > 0 gates delivery on that many matched
 *       subscribers; repeatIntervalMs > 0 keeps the publish chain alive and
 *       repeats until the stop flag or maxTimes is reached
 */
// @zh: topic_pub 请求负载（JSON 载荷发布，首轮完整强校验）
// @zh: note requiredSubscribers>0 为期望建匹配订阅端数门槛；repeatIntervalMs>0 为持续发布
// @zh: 模式，直至停止标志置位或发满 maxTimes 条
struct DDSDebugTopicPub
{
    std::string topicName = "";        // target topic, must be discovered
    std::string json = "";             // payload, same shape as the topic_example template
    uint32_t stableRounds = 0;
    uint32_t intervalMs = 0;
    uint32_t repeatIntervalMs = 0;     // 0 = publish once
    uint32_t requiredSubscribers = 0;  // 0 = auto convergence
    uint32_t maxTimes = 0;             // publish count cap including the first shot
    DDSQosProfile qos;                 // temporary writer QoS
};

// ===== YomkDdsBagService request payloads =====

/** @brief bag create_node request (same env fallbacks as DDSNode) */
// @zh: bag create_node 请求负载（环境变量回落语义同 DDSNode）
struct DDSBagNode
{
    uint32_t domainId = YomkDdsDefaultDomainId();                               // DDS domain id [0,232]
    std::string udpDiscoverServerLocator = YomkDdsDefaultUdpDiscoverServers();  // UDP channel
    std::string tcpDiscoverServerLocator = YomkDdsDefaultTcpDiscoverServers();  // TCP channel
};

/** @brief bag_record request (blocking until recording finishes) */
// @zh: bag_record 请求负载（阻塞至录制收尾）
struct DDSBagRecord
{
    std::vector<std::string> topics;  // at least 1; wildcard "*" item supported
    std::string outputDir;            // empty = timestamped default bag_<...>
    uint64_t maxBagSize = 0;          // per-file split threshold in bytes; 0 = no split
    uint64_t maxBagDurationSec = 0;   // per-file split threshold in seconds; 0 = no split
    uint64_t maxCacheSize = 0;        // write cache double-buffer bytes; 0 = direct write
};

/** @brief bag_info request (pure file read, no bag node needed) */
// @zh: bag_info 请求负载（纯文件读，不经 bag 节点）
struct DDSBagInfo
{
    std::string bagDir = "";  // must contain metadata.json
};

/** @brief bag_reindex request (pure file op, rebuilds metadata.json) */
// @zh: bag_reindex 请求负载（纯文件操作，重建 metadata.json）
struct DDSBagReindex
{
    std::string bagDir = "";  // metadata.json may be missing/corrupted
};

/** @brief bag_convert request (filter/merge inputs into new bags) */
// @zh: bag_convert 请求负载（按配置过滤合并输入为新 bag）
struct DDSBagConvert
{
    std::vector<std::string> inputs;  // at least 1, duplicates rejected
    std::string cfgPath = "";         // JSON config, top-level output_bags array
};

/** @brief bag_play request (blocking until all messages are played) */
// @zh: bag_play 请求负载（阻塞至全部消息播毕）
struct DDSBagPlay
{
    std::string bagDir = "";              // must contain metadata.json
    double playbackRate = 1.0;            // >0; 2.0 double speed
    std::vector<std::string> topics;      // empty = all; one '*' wildcard supported
    bool loop = false;                    // replay from start at end, forever
    double startOffset = 0.0;             // seconds to skip from bag start
    double delay = 0.0;                   // per-loop start delay in seconds
    int64_t waitForAllAckedMs = -1;       // -1 no wait, 0 forever, >0 per-topic timeout
    std::vector<std::string> topicRemaps; // "old:=new" raw strings
    size_t readAheadQueueSize = kBagPlayReadAheadQueueSize;
    std::string qosOverridesPath = "";    // JSON: bag topic -> reliability/durability
};
}  // namespace yomk::payload
