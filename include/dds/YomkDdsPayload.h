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
#include <tuple>
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

// ===== YomkDdsHandler request/response payloads =====

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
struct LoanSample
{
    void* sample = nullptr;
};

// ===== YomkDdsDebugHandler request payloads =====

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
};

/** @brief topic_info request (independent convergence parameters) */
// @zh: topic_info 请求负载（独立收敛参数）
struct DDSDebugInfo
{
    std::string topicName = "";  // topic to query
    uint32_t stableRounds = 0;
    uint32_t intervalMs = 0;
};

// ===== YomkDdsDebugHandler response payloads =====

/** @brief Sentinel of the duration fields: -1 = infinite, other values are nanoseconds */
// @zh: 时长字段哨兵：-1=无限，其余值即纳秒
constexpr int64_t kInfiniteDurationNs = -1;

/**
 * @brief Structured QoS of one discovered endpoint (writer or reader)
 * @note Duration fields are nanoseconds (kInfiniteDurationNs = infinite); the
 *       *Advertised flags tell whether the discovery sample carried that optional
 *       policy at all, so a false flag means the related fields are meaningless
 *       rather than zero-valued
 */
// @zh: 单个已发现端点（writer 或 reader）的结构化 QoS
// @zh: note 时长字段为纳秒（kInfiniteDurationNs = 无限）；*Advertised 标记发现样本是否携带该可选
// @zh: 策略——标志为 false 时相关字段不是"零值"而是"未公告"
struct TopicEndpointQos
{
    bool historyAdvertised = false;  // false: history/depth not carried by the discovery sample
    bool historyKeepAll = false;     // true: KEEP_ALL, false: KEEP_LAST
    uint32_t depth = 0;              // KEEP_LAST depth as advertised
    std::string reliability;         // RELIABLE / BEST_EFFORT / UNKNOWN
    std::string durability;          // VOLATILE / TRANSIENT_LOCAL / TRANSIENT / PERSISTENT / UNKNOWN
    int64_t deadlineNs = kInfiniteDurationNs;
    int64_t lifespanNs = kInfiniteDurationNs;  // writer side
    std::string liveliness;                    // AUTOMATIC / MANUAL_BY_PARTICIPANT / MANUAL_BY_TOPIC / UNKNOWN
    int64_t livelinessLeaseNs = kInfiniteDurationNs;
    int64_t latencyBudgetNs = 0;
    std::string ownership;           // SHARED / EXCLUSIVE / UNKNOWN
    int32_t ownershipStrength = 0;   // writer side
    int64_t timeBasedFilterNs = 0;   // reader side
    std::string destinationOrder;    // BY_RECEPTION_TIMESTAMP / BY_SOURCE_TIMESTAMP / UNKNOWN
    std::vector<std::string> partition;  // partition names, empty when not announced
    bool resourceLimitsAdvertised = false;
    int32_t maxSamples = 0;
    int32_t maxInstances = 0;
    int32_t maxSamplesPerInstance = 0;
    bool publishModeAdvertised = false;  // writer side
    bool publishModeAsync = false;
    bool transportPriorityAdvertised = false;
    int32_t transportPriority = 0;
    bool writerDataLifecycleAdvertised = false;
    bool autodisposeUnregisteredInstances = false;

    // 收敛轮询的快照相等性判定（逐字段覆盖，新增字段须同步入 tie）
    std::tuple<const bool&, const bool&, const uint32_t&, const std::string&, const std::string&,
            const int64_t&, const int64_t&, const std::string&, const int64_t&, const int64_t&,
            const std::string&, const int32_t&, const int64_t&, const std::string&,
            const std::vector<std::string>&, const bool&, const int32_t&, const int32_t&,
            const int32_t&, const bool&, const bool&, const bool&, const int32_t&, const bool&,
            const bool&>
    tie() const
    {
        return std::tie(historyAdvertised, historyKeepAll, depth, reliability, durability,
                deadlineNs, lifespanNs, liveliness, livelinessLeaseNs, latencyBudgetNs, ownership,
                ownershipStrength, timeBasedFilterNs, destinationOrder, partition,
                resourceLimitsAdvertised, maxSamples, maxInstances, maxSamplesPerInstance,
                publishModeAdvertised, publishModeAsync, transportPriorityAdvertised,
                transportPriority, writerDataLifecycleAdvertised,
                autodisposeUnregisteredInstances);
    }

    bool operator==(const TopicEndpointQos& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const TopicEndpointQos& rhs) const { return !(*this == rhs); }
};

/**
 * @brief One discovered endpoint of a topic
 * @note The owning container (publishers / subscriptions) expresses the endpoint
 *       kind, so no endpoint-type string is stored here
 */
// @zh: 主题的一个已发现端点
// @zh: note 端点类型由所在容器（publishers / subscriptions）表达，不重复存字符串
struct TopicEndpointInfo
{
    std::string nodeName;  // owning participant name; empty when the prefix is not matched
    std::string guid;      // FastDDS native GUID text
    TopicEndpointQos qos;

    std::tuple<const std::string&, const std::string&, const TopicEndpointQos&> tie() const
    {
        return std::tie(nodeName, guid, qos);
    }

    bool operator==(const TopicEndpointInfo& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const TopicEndpointInfo& rhs) const { return !(*this == rhs); }
};

/** @brief Structured discovery details of one topic (superset of `topic info -v`) */
// @zh: 单个主题的结构化发现详情（`topic info -v` 全部信息的超集）
struct TopicInfo
{
    std::string topicName;
    std::string typeName;  // raw DDS type name, never style-converted
    uint32_t publisherCount = 0;
    uint32_t subscriptionCount = 0;
    std::vector<TopicEndpointInfo> publishers;     // writers, ordered as discovered
    std::vector<TopicEndpointInfo> subscriptions;  // readers, ordered as discovered

    std::tuple<const std::string&, const std::string&, const uint32_t&, const uint32_t&,
            const std::vector<TopicEndpointInfo>&, const std::vector<TopicEndpointInfo>&>
    tie() const
    {
        return std::tie(topicName, typeName, publisherCount, subscriptionCount, publishers,
                subscriptions);
    }

    bool operator==(const TopicInfo& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const TopicInfo& rhs) const { return !(*this == rhs); }
};

/** @brief list_topics response: one TopicInfo per discovered topic (sorted by topic name) */
// @zh: list_topics 响应：每个已发现主题一项 TopicInfo（按主题名排序）
struct TopicInfoArray
{
    std::vector<TopicInfo> d;
};

/**
 * @brief Reflected member of one IDL struct
 * @note id is the MemberId carried by the type object and only serves as the
 *       declaration-order sort key; MemberIds are sparse and discontinuous, so
 *       they must never be used as an index into members
 */
// @zh: 单个 IDL struct 的反射成员
// @zh: note id 即类型对象携带的 MemberId，只作声明序排序键；MemberId 稀疏且不连续，
// @zh: 严禁当作 members 的下标使用
struct InterfaceMember
{
    uint32_t id = 0;       // MemberId from the type object; sparse sort key, never an index
    std::string name;      // member identifier
    std::string typeName;  // bool / int32 / string / sequence<T[, N]> / T[N] / named type

    std::tuple<const uint32_t&, const std::string&, const std::string&> tie() const
    {
        return std::tie(id, name, typeName);
    }

    bool operator==(const InterfaceMember& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const InterfaceMember& rhs) const { return !(*this == rhs); }
};

/**
 * @brief Reflected definition of one IDL type (root or dependency)
 * @note members keeps declaration order; an empty vector means a struct without
 *       member and is only reachable on the successful resolution path, a failed
 *       build never yields a partially filled type
 */
// @zh: 单个 IDL 类型的反射定义（root 或依赖）
// @zh: note members 保持声明序；空 vector 表示无成员 struct，且只出现在解析成功路径上，
// @zh: 构建失败不会产出半成品类型
struct InterfaceType
{
    std::string typeName;                  // fully qualified name, e.g. YomkDdsStd::MultiArrayLayout
    std::vector<InterfaceMember> members;  // declaration order, empty for a member-less struct

    std::tuple<const std::string&, const std::vector<InterfaceMember>&> tie() const
    {
        return std::tie(typeName, members);
    }

    bool operator==(const InterfaceType& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const InterfaceType& rhs) const { return !(*this == rhs); }
};

/**
 * @brief interface_show response: the root type plus the closure of its struct dependencies
 * @note dependencies is flattened and deduplicated in topological order (a type
 *       comes before the types that reference it) and never contains root
 */
// @zh: interface_show 响应：root 类型及其 struct 依赖闭包
// @zh: note dependencies 平铺去重、拓扑序（被依赖者在前），且不含 root
struct InterfaceInfo
{
    InterfaceType root;                       // queried type
    std::vector<InterfaceType> dependencies;  // topological order, root excluded

    std::tuple<const InterfaceType&, const std::vector<InterfaceType>&> tie() const
    {
        return std::tie(root, dependencies);
    }

    bool operator==(const InterfaceInfo& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const InterfaceInfo& rhs) const { return !(*this == rhs); }
};

/**
 * @brief topic_example response: type identity, reflected struct closure and JSON template
 * @note interfaceInfo.root is the queried type, whose typeName equals typeName; json is the
 *       compact default-value sample and is already a valid publish payload
 */
// @zh: topic_example 响应：类型标识 + struct 反射闭包 + JSON 模板
// @zh: note interfaceInfo.root 为查询类型（其 typeName 与 typeName 相同）；
// @zh: json 为紧凑默认值样本，本身即合法发布载荷
struct TopicExampleInfo
{
    std::string topicName;        // queried topic (exact match)
    std::string typeName;         // raw DDS type name, no style conversion
    InterfaceInfo interfaceInfo;  // root members plus the struct dependency closure
    std::string json;             // compact default-value JSON, a valid publish payload

    std::tuple<const std::string&, const std::string&, const InterfaceInfo&, const std::string&> tie() const
    {
        return std::tie(topicName, typeName, interfaceInfo, json);
    }

    bool operator==(const TopicExampleInfo& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const TopicExampleInfo& rhs) const { return !(*this == rhs); }
};

/**
 * @brief topic_msg response: type name plus the compact default-value JSON sample
 * @note json is single-line and itself a valid publish payload; writing it to a file is a
 *       caller-side concern, no output path travels in the request
 */
// @zh: topic_msg 响应：类型名 + 紧凑默认值 JSON 样本
// @zh: note json 为单行文本，本身即合法发布载荷；是否落盘由调用方决定，请求负载不携带输出路径
struct TopicMessageInfo
{
    std::string topicName;  // queried topic (exact match)
    std::string typeName;   // raw DDS type name
    std::string json;       // compact default-value JSON, a valid publish payload

    std::tuple<const std::string&, const std::string&, const std::string&> tie() const
    {
        return std::tie(topicName, typeName, json);
    }

    bool operator==(const TopicMessageInfo& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const TopicMessageInfo& rhs) const { return !(*this == rhs); }
};

/**
 * @brief topic_pub response counters (reported in both single-shot and repeat modes)
 * @note total includes the first shot (1 for a single-shot publish); failed only counts
 *       repeat-mode write failures
 */
// @zh: topic_pub 响应计数（单次与持续模式均回填）
// @zh: note total 含首轮（单次发布即 1）；failed 仅统计持续模式下的 write 失败
struct TopicPublishInfo
{
    std::string topicName;  // target topic
    uint32_t total = 0;     // published samples, first shot included
    uint32_t failed = 0;    // failed writes in repeat mode

    std::tuple<const std::string&, const uint32_t&, const uint32_t&> tie() const
    {
        return std::tie(topicName, total, failed);
    }

    bool operator==(const TopicPublishInfo& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const TopicPublishInfo& rhs) const { return !(*this == rhs); }
};

/** @brief One topic/type pair of a participant endpoint list */
// @zh: 参与者端点清单中的一条主题/类型对
struct NodeEndpoint
{
    std::string topicName;  // topic name as registered on the endpoint
    std::string typeName;   // raw DDS type name, no style conversion

    std::tuple<const std::string&, const std::string&> tie() const
    {
        return std::tie(topicName, typeName);
    }

    bool operator==(const NodeEndpoint& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const NodeEndpoint& rhs) const { return !(*this == rhs); }
};

/**
 * @brief node_info response: participant name plus its publisher/subscriber topic lists
 * @note each list is deduplicated per topic and sorted by (topicName, typeName); an empty
 *       list still means a discovered participant, only a never-discovered name fails
 */
// @zh: node_info 响应：参与者名 + 发布/订阅主题清单
// @zh: note 两份清单按主题去重、以 (topicName, typeName) 排序；空清单仍表示参与者已被发现，
// @zh: 只有收敛窗口内始终不可见的名字才失败
struct NodeInfo
{
    std::string nodeName;                   // participant_name as discovered
    std::vector<NodeEndpoint> publishers;   // sorted by (topicName, typeName)
    std::vector<NodeEndpoint> subscribers;  // sorted by (topicName, typeName)

    std::tuple<const std::string&, const std::vector<NodeEndpoint>&,
            const std::vector<NodeEndpoint>&> tie() const
    {
        return std::tie(nodeName, publishers, subscribers);
    }

    bool operator==(const NodeInfo& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const NodeInfo& rhs) const { return !(*this == rhs); }
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

// ===== YomkDdsBagService response payloads =====

/**
 * @brief Per-topic statistics of one bag operation (record or play)
 * @note count/bytes cover the whole session across all shards; bytes are the payload
 *       bytes moved (encapsulation header included), not the on-disk file size
 */
// @zh: 单主题的 bag 操作统计（录制或回放）
// @zh: note count/bytes 覆盖跨全部分片的整次会话；bytes 为流转的载荷字节（含 encapsulation
// @zh: header），不是磁盘文件大小
struct BagTopicStat
{
    std::string topicName;  // topic name of the operation (wildcards expanded, remaps applied)
    std::string typeName;   // raw DDS type name stored in / advertised by the bag
    uint64_t count = 0;     // messages recorded or played
    uint64_t bytes = 0;     // payload bytes recorded or played

    std::tuple<const std::string&, const std::string&, const uint64_t&, const uint64_t&> tie() const
    {
        return std::tie(topicName, typeName, count, bytes);
    }

    bool operator==(const BagTopicStat& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const BagTopicStat& rhs) const { return !(*this == rhs); }
};

/**
 * @brief bag_record response: the output bag directory plus per-topic statistics
 * @note Data only: the directory line and the "topic: N messages / M bytes" lines are
 *       rendered by the caller (yomkdds); topics keeps the recording order (wildcard
 *       items already expanded)
 */
// @zh: bag_record 响应：输出 bag 目录 + 逐主题统计
// @zh: note 只回数据：目录行与 "topic: N 条 / M 字节" 行由调用方（yomkdds）渲染；
// @zh: topics 保持录制清单顺序（通配项已展开）
struct RecordResult
{
    std::string bagDir;                // recorded bag directory
    std::vector<BagTopicStat> topics;  // recording order, wildcard items expanded

    std::tuple<const std::string&, const std::vector<BagTopicStat>&> tie() const
    {
        return std::tie(bagDir, topics);
    }

    bool operator==(const RecordResult& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const RecordResult& rhs) const { return !(*this == rhs); }
};

/**
 * @brief Statistics of one converted output bag (one output_bags config entry)
 * @note Bytes are the written payload bytes (encapsulation header included), not the
 *       on-disk file size; topics counts the channels registered for the entry, so
 *       topics with zero written messages are included
 */
// @zh: 单个转换输出 bag 的统计（对应输出配置 output_bags 的一条目）
// @zh: note bytes 为写出的载荷字节（含 encapsulation header），不是磁盘文件大小；
// @zh: topics 为该条目注册的通道数，写入 0 条消息的主题同样计入
struct ConvertedBagStat
{
    std::string uri;             // output bag uri as written by the config
    uint64_t messageCount = 0;   // messages written
    uint64_t byteCount = 0;      // payload bytes written
    uint64_t topicCount = 0;     // channels registered for this entry

    std::tuple<const std::string&, const uint64_t&, const uint64_t&, const uint64_t&> tie() const
    {
        return std::tie(uri, messageCount, byteCount, topicCount);
    }

    bool operator==(const ConvertedBagStat& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const ConvertedBagStat& rhs) const { return !(*this == rhs); }
};

/**
 * @brief bag_convert response: one statistics record per output bag
 * @note Data only: the "converted <uri>: N messages / M bytes / K topics" lines are
 *       rendered by the caller (yomkdds); bags keeps the output_bags config order
 */
// @zh: bag_convert 响应：每个输出 bag 一条统计
// @zh: note 只回数据："converted <uri>: N messages / M bytes / K topics" 行由调用方
// @zh: （yomkdds）渲染；bags 保持 output_bags 配置顺序
struct ConvertResult
{
    std::vector<ConvertedBagStat> bags;  // output_bags config order

    std::tuple<const std::vector<ConvertedBagStat>&> tie() const
    {
        return std::tie(bags);
    }

    bool operator==(const ConvertResult& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const ConvertResult& rhs) const { return !(*this == rhs); }
};

/**
 * @brief Per-topic entry of one bag's metadata
 * @note qos is empty when metadata.json has no qos key (legacy bag) or records an
 *       empty string (subscriber-only topic during recording)
 */
// @zh: 单个 bag 元信息中的逐主题条目
// @zh: note metadata.json 无 qos 键（旧 bag）或记录空串（录制时仅订阅者主题）时 qos 为空
struct BagInfoTopic
{
    std::string topicName;       // metadata.json topics_with_message_count[].topic_metadata.name
    std::string typeName;        // raw DDS type name stored in the bag
    std::string qos;             // "<RELIABILITY>,<DURABILITY>" snapshot, empty when absent
    uint64_t messageCount = 0;   // messages stored for this topic

    std::tuple<const std::string&, const std::string&, const std::string&, const uint64_t&> tie() const
    {
        return std::tie(topicName, typeName, qos, messageCount);
    }

    bool operator==(const BagInfoTopic& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const BagInfoTopic& rhs) const { return !(*this == rhs); }
};

/**
 * @brief bag_info response: the metadata.json fields plus the recursive directory size
 * @note Data only: every line (labels, unit suffixes, human-readable time, 19-column
 *       alignment) is rendered by the caller (yomkdds); times stay in nanoseconds and
 *       bagSizeBytes covers the whole directory, metadata.json included
 */
// @zh: bag_info 响应：metadata.json 字段 + 目录递归总大小
// @zh: note 只回数据：行标签、单位、人类可读时间与 19 列对齐均由调用方（yomkdds）渲染；
// @zh: 时间保留纳秒，bagSizeBytes 为目录递归总字节（含 metadata.json 自身）
struct BagInfo
{
    std::vector<std::string> files;     // relative_file_paths, in bag order
    uint64_t bagSizeBytes = 0;          // recursive directory size, metadata.json included
    std::string storageId;              // storage_identifier, e.g. "mcap"
    uint64_t startTimeNs = 0;           // starting_time.nanoseconds_since_epoch
    uint64_t durationNs = 0;            // duration.nanoseconds
    uint64_t messageCount = 0;          // message_count
    std::string serializationFormat;    // single CDR pipeline, reported as "cdr"
    std::vector<BagInfoTopic> topics;   // topics_with_message_count order

    std::tuple<const std::vector<std::string>&, const uint64_t&, const std::string&, const uint64_t&,
            const uint64_t&, const uint64_t&, const std::string&, const std::vector<BagInfoTopic>&>
    tie() const
    {
        return std::tie(files, bagSizeBytes, storageId, startTimeNs, durationNs, messageCount,
                serializationFormat, topics);
    }

    bool operator==(const BagInfo& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const BagInfo& rhs) const { return !(*this == rhs); }
};

/**
 * @brief bag_play response: the played bag directory plus per-topic statistics
 * @note Data only: the directory line and the "topic: N messages / M bytes" lines are
 *       rendered by the caller (yomkdds); topics keeps the bag scan order, topicName
 *       carries the published name (remaps applied), and a topic that was pre-scanned
 *       but never reached reports 0/0 (playback stopped early via yomk::bagPlayStop)
 */
// @zh: bag_play 响应：回放的 bag 目录 + 逐主题统计
// @zh: note 只回数据：目录行与 "topic: N 条 / M 字节" 行由调用方（yomkdds）渲染；topics 保持
// @zh: bag 预扫顺序，topicName 为发布名（已应用 remap）；预扫命中但未播到的主题（经
// @zh: yomk::bagPlayStop 提前停止）计 0/0
struct PlayResult
{
    std::string bagDir;                 // played bag directory (as requested)
    std::vector<BagTopicStat> topics;   // bag scan order, remap applied to topicName

    std::tuple<const std::string&, const std::vector<BagTopicStat>&> tie() const
    {
        return std::tie(bagDir, topics);
    }

    bool operator==(const PlayResult& rhs) const { return tie() == rhs.tie(); }
    bool operator!=(const PlayResult& rhs) const { return !(*this == rhs); }
};
}  // namespace yomk::payload
