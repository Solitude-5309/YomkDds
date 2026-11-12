#pragma once
#include <string>

#include "yomk/server/YomkServerPayload.h"

// YomkDds 扩展专属载荷（四件套之 Payload，namespace yomk::payload，与 SDK 各
// Yomk{Module}Payload.h 同一设计）：YOMKDDS_* 宏（YomkDdsAPI.h）所打包的请求/响应结构、
// 回调类型、端点 QoS 描述与公共配置辅助，全部集中于此。消息包声明见 YomkDdsMsg.h，
// 服务类声明见各服务头（YomkDdsService.h / YomkDdsDebugService.h / YomkDdsBagService.h）。
// 公共头零 fastdds 依赖。

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <vector>

namespace yomk::payload
{
// ===== 公共配置辅助（载荷缺省初始化器与调用方共用；纯头文件内联，不引入日志依赖） =====

// 缺省域 id 解析辅助常量：十进制解析基数与 DDS 域号上限（与 src 各服务 createNode 校验一致）
constexpr unsigned long kDomainEnvBase = 10;
constexpr unsigned long kDomainIdEnvMax = 232;

// 缺省域 id 解析：读环境变量 YOMKDDS_DDS_DOMAIN_ID（与 yomkdds CLI 同源同语义）。
// 未设置/为空/非法（非纯十进制）/超出 [0,232] 时静默回落 0。
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

// 发现服务器直配总开关：读环境变量 YOMKDDS_USE_DISCOVER_SERVER。大小写不敏感的
// "true"/"1" 视为开启；未设置/空/"false"/"0"/其余任意值一律视为关闭，静默不打印
// 日志。关闭时两个 YomkDdsDefault*DiscoverServers 恒返回空串（=不直配），server 串
// 环境变量被忽略。
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

// 缺省 UDP 通道发现服务器聚合串：总开关开启时原样返回 YOMKDDS_UDP_DISCOVER_SERVERS
// （未设置/空 → 空串=不直配），关闭时恒空串。串合法性不在本层校验——由节点创建时的
// serverLocator 解析校验，非法串（空项/空 ip/越界 port）创建失败并回显，不静默回落，
// 避免误配退化成多播发现而"看似正常实则不通"。
inline std::string YomkDdsDefaultUdpDiscoverServers()
{
    if (!YomkDdsUseDiscoverServer())
    {
        return {};
    }
    const char* v = std::getenv("YOMKDDS_UDP_DISCOVER_SERVERS");
    return v ? std::string(v) : std::string();
}

// 缺省 TCP 通道发现服务器聚合串：同上，读 YOMKDDS_TCP_DISCOVER_SERVERS。
inline std::string YomkDdsDefaultTcpDiscoverServers()
{
    if (!YomkDdsUseDiscoverServer())
    {
        return {};
    }
    const char* v = std::getenv("YOMKDDS_TCP_DISCOVER_SERVERS");
    return v ? std::string(v) : std::string();
}

// XML profiles 自动加载总开关：读环境变量 YOMKDDS_USE_XML_PROFILE。大小写不敏感的
// "true"/"1" 视为开启；未设置/空/"false"/"0"/其余任意值一律视为关闭，静默不打印日志。
// 关闭时 YomkDdsDefaultXmlProfilePath 恒返回空串（=不自动加载），XML 路径环境变量被忽略。
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

// 缺省 XML profiles 路径：总开关开启时原样返回 YOMKDDS_XML_PROFILE_PATH（未设置/空 →
// 空串=不加载），关闭时恒空串。路径合法性不在本层校验——由 load_xml 端点校验，路径
// 不存在/文件为空/格式非法失败回显；空串由调用方静默跳过（同发现服务器空串=不直配
// 语义，避免开关误开退化成"看似生效实则未加载"）。
inline std::string YomkDdsDefaultXmlProfilePath()
{
    if (!YomkDdsUseXmlProfile())
    {
        return {};
    }
    const char* v = std::getenv("YOMKDDS_XML_PROFILE_PATH");
    return v ? std::string(v) : std::string();
}

// fastdds discovery 默认监听端口（server 侧 -p 缺省值；serverLocator 串缺省 port 同源）
constexpr uint32_t kDdsDiscoveryDefaultPort = 11811;

// bag play 预读内存队列深度（--read-ahead-queue-size N，对齐 ros2 同名选项：读侧吸收
// 磁盘 IO 抖动保证确定性回放，大→占内存但防播放延迟；<1 拒播，writer QoS 不随其联动；
// 缺省对齐参考实现同款默认 1000）
constexpr size_t kBagPlayReadAheadQueueSize = 1000;

// ===== 回调类型 =====

// 订阅回调：收到消息时被调用，参数为交付的消息实例指针（const void*），static_cast 为
// 自己的消息类型后读取。指针仅回调期间有效，须同步消费、勿跨调用持有（return_loan
// 后失效）。不同类型的安全消费方式见 README。
using DDSCallbackFunc = std::function<void(const void*)>;

// 调试输出回调：每条消息投递一次格式化 JSON 文本，由 /topic_print 调用方自定义（服务层
// 不打印）。回调在 DDS 数据接收线程被调用，须线程安全且快速返回。
using DDSDebugOutputFunc = std::function<void(const std::string&)>;

// ===== 端点 QoS 描述 =====

// 端点 QoS 枚举：enum class 禁止隐式整型转换，用户只能取枚举项（裸数字编译期报错）；
// Unset=0 为哨兵=未设置（默认构造即全哨兵，保持 Fast DDS 端点默认底座）。
enum class DDSHistoryKind : uint8_t
{
    Unset = 0,
    KEEP_LAST,
    KEEP_ALL,
};

enum class DDSReliability : uint8_t
{
    Unset = 0,
    RELIABLE,
    BEST_EFFORT,
};

enum class DDSDurability : uint8_t
{
    Unset = 0,
    TRANSIENT_LOCAL,
    VOLATILE,
};

enum class DDSLiveliness : uint8_t
{
    Unset = 0,
    AUTOMATIC,
    MANUAL_BY_TOPIC,
};

// 端点 QoS 描述（writer/reader 通用）：未设置项（枚举 Unset / 数值 -1）保持 Fast DDS
// 端点默认底座，非哨兵项逐项覆盖（与 rclcpp::QoS "底座+逐项覆盖"语义同构）。
// 时间类 -1=未设置=底座默认 ∞，与 ROS2 默认 ∞ 等值。
struct DDSQosProfile
{
    DDSHistoryKind historyKind = DDSHistoryKind::Unset;
    int32_t depth = -1;              // KEEP_LAST 的 history depth；KEEP_ALL 时忽略
    DDSReliability reliability = DDSReliability::Unset;
    DDSDurability durability = DDSDurability::Unset;
    DDSLiveliness liveliness = DDSLiveliness::Unset;
    int64_t deadlineNs = -1;         // deadline 周期纳秒
    int64_t lifespanNs = -1;         // 消息生存期纳秒（writer 侧策略，reader 翻译时忽略）
    int64_t livelinessLeaseNs = -1;  // liveliness 租约纳秒

    // ROS2 底座工厂（8 项全非哨兵 = 等价整包替换为 ROS2 底座）。分端点裁定依据
    // requested≤offered 匹配方向：
    // publisher=KEEP_LAST(10)+RELIABLE+VOLATILE+AUTOMATIC+∞×3（offered 对任意 ROS2 侧
    // requested 可匹配）；subscriber=KEEP_LAST(10)+BEST_EFFORT+VOLATILE+AUTOMATIC+∞×3
    // （requested 对任意 offered 可匹配，RELIABLE 版订阅 BEST_EFFORT 话题收不到）；
    // sensorData=KEEP_LAST(5)+BEST_EFFORT+VOLATILE+AUTOMATIC+∞×3（两端通用）。
    static DDSQosProfile ros2DefaultPublisher();
    static DDSQosProfile ros2DefaultSubscriber();
    static DDSQosProfile ros2SensorData();
};

// 发现服务器连接描述（client 角色）：address 为 server 监听 IP；port 缺省 11811（fastdds
// discovery 默认端口）。3.6.1 client 连接 server 只需 ip:port + CLIENT 协议（server 响应自报
// GUID），两字段足够；作为节点层 serverLocator 聚合串解析产物使用。
struct DDSServerInfo
{
    std::string address;
    uint32_t port = kDdsDiscoveryDefaultPort;
};

// ===== YomkDdsService 请求/响应负载（端点见 YomkDdsService.h） =====

// create_node 请求负载。
struct DDSNode
{
    std::string nodeName;  // 节点唯一名，不可为空
    // DDS 域号，合法范围 [0,232]；缺省（YOMKDDS_NODE 单参形态）经类内缺省初始化器读
    // 环境变量 YOMKDDS_DDS_DOMAIN_ID（无/非法/超范围回落 0）。
    uint32_t domainId = YomkDdsDefaultDomainId();
    // 发现服务器直配聚合串（可缺省尾参，UDP 通道）：空串=不直配；格式 "ip[:port]"，
    // 多 server 分号分隔，port 缺省 11811。非空时 participant 以 CLIENT 发现协议直配
    // （覆盖 XML discoveryServersList）；非法串（空项/空 ip/越界 port）创建失败回显。
    // 缺省（YOMKDDS_NODE 省略该尾参）经类内缺省初始化器读环境变量：总开关
    // YOMKDDS_USE_DISCOVER_SERVER=true 时取 YOMKDDS_UDP_DISCOVER_SERVERS，否则空串。
    std::string udpDiscoverServerLocator = YomkDdsDefaultUdpDiscoverServers();
    // 发现服务器直配聚合串（可缺省尾参，TCP 通道）：格式同上，port 为 server 的 TCP 监听口。
    // 非空时 participant 补注入 TCPv4 传输（与内置 UDPv4/SHM 共存）并以 TCPv4 locator 直配，
    // 可与 UDP 通道同时使用（任一通道 server 在场即完成发现）。缺省读 YOMKDDS_TCP_DISCOVER_SERVERS，
    // 门控同 UDP 通道。
    std::string tcpDiscoverServerLocator = YomkDdsDefaultTcpDiscoverServers();
};

// load_xml 请求负载。
struct DDSXmlLoad
{
    // Fast DDS XML profiles 文件路径（相对/绝对均可）。缺省（YOMKDDS_LOAD_XML 无参形态）
    // 经类内缺省初始化器读环境变量：总开关 YOMKDDS_USE_XML_PROFILE=true 时取
    // YOMKDDS_XML_PROFILE_PATH，否则空串（服务端报 "xml path is empty"）。
    std::string xmlPath = YomkDdsDefaultXmlProfilePath();
};

// register_pub_topic 请求负载。
struct DDSTopic
{
    std::string nodeName;
    std::string topicName;
    void* type = nullptr;  // TopicDataType*；所有权移交服务端：无论成功或失败，调用后 caller 不再持有/释放
    // 端点 QoS 描述（缺省全哨兵=Fast DDS 默认底座；尾部缺省字段）
    DDSQosProfile qos;
};

// register_sub_topic 请求负载。
struct DDSSubRequest
{
    std::string nodeName;
    std::string topicName;
    void* type = nullptr;  // TopicDataType*；所有权移交服务端：无论成功或失败，调用后 caller 不再持有/释放
    DDSCallbackFunc callback;  // 收到消息时回调
    // 端点 QoS 描述（缺省全哨兵=Fast DDS 默认底座；尾部缺省字段）
    DDSQosProfile qos;
};

// publish 请求负载。
struct DDSPublish
{
    std::string nodeName;
    std::string topicName;
    void* data = nullptr;  // 借用（非所有权）：publish 同步写入，caller 保留并在调用后自行管理 data 生命周期
};

// loan / discard_loan 请求负载。
struct DDSLoan
{
    std::string nodeName;
    std::string topicName;
    void* sample = nullptr;  // loan 请求忽略；discard 请求携带待归还的借出指针
};

// loan 成功响应负载。
struct DDSLoanResult
{
    // /loan 借出的待发样本指针；填值后经 YOMKDDS_PUB_MSG 发布，write 后指针失效。
    void* sample = nullptr;
};

// ===== YomkDdsDebugService 请求负载（端点见 YomkDdsDebugService.h） =====

// create_node 请求负载。
struct DDSDebugNode
{
    // DDS 域号，合法范围 [0,232]；缺省（YOMKDDS_DEBUG_NODE 无参形态）经类内缺省初始化器读
    // 环境变量 YOMKDDS_DDS_DOMAIN_ID（无/非法/超范围回落 0），语义同 DDSNode。
    uint32_t domainId = YomkDdsDefaultDomainId();
    // 发现服务器直配聚合串（可缺省尾参，UDP 通道）：空串=不直配；格式 "ip[:port]"，
    // 多 server 分号分隔，port 缺省 11811。非空时 participant 以 CLIENT 发现协议直配
    // （覆盖 XML discoveryServersList）；非法串（空项/空 ip/越界 port）创建失败回显。
    // 缺省（YOMKDDS_DEBUG_NODE 省略该尾参）经类内缺省初始化器读环境变量：总开关
    // YOMKDDS_USE_DISCOVER_SERVER=true 时取 YOMKDDS_UDP_DISCOVER_SERVERS，否则空串。
    std::string udpDiscoverServerLocator = YomkDdsDefaultUdpDiscoverServers();
    // 发现服务器直配聚合串（可缺省尾参，TCP 通道）：格式同上，port 为 server 的 TCP 监听口，
    // 非空时 participant 补注入 TCPv4 传输并以 TCPv4 locator 直配。语义同 DDSNode。
    // 缺省读 YOMKDDS_TCP_DISCOVER_SERVERS，门控同 UDP 通道。
    std::string tcpDiscoverServerLocator = YomkDdsDefaultTcpDiscoverServers();
};

// topic_print 请求负载。
struct DDSDebugTopic
{
    std::string topicName;      // 待调试主题
    DDSDebugOutputFunc output;  // 用户自定义输出回调；空回调拒绝
    DDSQosProfile qos;          // 动态订阅 reader QoS（可缺省尾参）；全哨兵=Fast DDS reader 默认底座
                                // （KEEP_LAST(1)+BEST_EFFORT+VOLATILE）；对接 ROS2 高频流
                                // 建议 DDSQosProfile::ros2DefaultSubscriber()（KEEP_LAST(10) 防丢样本）
};

// list_topics 请求负载：自适应收敛参数（0 值由节点层钳制为默认 5 次/200ms）
struct DDSDebugList
{
    uint32_t stableRounds = 0;  // 缺省 0=节点层钳制为默认  // 连续不变快照次数阈值；1 即单次快照免等待
    uint32_t intervalMs = 0;    // 快照轮询间隔毫秒
    bool types = false;     // 类型名模式：true 时每行输出 "主题名 [类型名]"
};

// topic_info 请求负载：单主题详情查询（独立收敛参数，语义同 list_topics，0 值由节点层钳制为默认）
struct DDSDebugInfo
{
    std::string topicName;  // 待查询主题
    uint32_t stableRounds = 0;  // 缺省 0=节点层钳制为默认  // 连续不变快照次数阈值；1 即单次快照免等待
    uint32_t intervalMs = 0;    // 快照轮询间隔毫秒
    bool verbose = false;   // 端点详情模式：true 时逐端点输出 Node name/GUID/QoS profile
};

// list_nodes 请求负载：域内命名参与者列表查询（独立收敛参数，语义同 topic_info，0 值由节点层钳制为默认）
struct DDSNodeList
{
    uint32_t stableRounds = 0;  // 缺省 0=节点层钳制为默认  // 连续不变快照次数阈值；1 即单次快照免等待
    uint32_t intervalMs = 0;    // 快照轮询间隔毫秒
};

// node_info 请求负载：指定节点名（participant_name）的发布/订阅主题清单查询（独立收敛参数，
// 语义同 list_nodes，0 值由节点层钳制为默认）
struct DDSNodeInfo
{
    std::string nodeName;   // 待查询节点名（participant_name 精确匹配）
    uint32_t stableRounds = 0;  // 缺省 0=节点层钳制为默认  // 连续不变快照次数阈值；1 即单次快照免等待
    uint32_t intervalMs = 0;    // 快照轮询间隔毫秒
};

// topic_find 请求负载：按数据类型名反查主题列表（独立收敛参数，语义同 list_topics，
// 0 值由节点层钳制为默认）
struct DDSDebugFind
{
    std::string typeName;   // 待查询数据类型名（精确匹配）
    uint32_t stableRounds = 0;  // 缺省 0=节点层钳制为默认  // 连续不变快照次数阈值；1 即单次快照免等待
    uint32_t intervalMs = 0;    // 快照轮询间隔毫秒
};

// interface_show 请求负载：按数据类型名输出 IDL 结构描述（独立收敛参数，语义同 topic_find，
// 0 值由节点层钳制为默认）
struct DDSDebugInterfaceShow
{
    std::string typeName;   // 待查询数据类型名（精确匹配）
    uint32_t stableRounds = 0;  // 缺省 0=节点层钳制为默认  // 连续不变快照次数阈值；1 即单次快照免等待
    uint32_t intervalMs = 0;    // 快照轮询间隔毫秒
};

// interface_list 请求负载：列出发现缓存中出现的全部数据类型名（去重排序，interface show
// 的配套导航；独立收敛参数，0 值由节点层钳制为默认）
struct DDSDebugInterfaceList
{
    uint32_t stableRounds = 0;  // 缺省 0=节点层钳制为默认  // 连续不变快照次数阈值；1 即单次快照免等待
    uint32_t intervalMs = 0;    // 快照轮询间隔毫秒
};

// topic_example 请求负载：按主题名查询发布示例三段行集（Type/IDL/example 命令；独立收敛
// 参数，0 值由节点层钳制为默认）
struct DDSDebugTopicExample
{
    std::string topicName;  // 待查询主题名（精确匹配）
    uint32_t stableRounds = 0;  // 缺省 0=节点层钳制为默认  // 连续不变快照次数阈值；1 即单次快照免等待
    uint32_t intervalMs = 0;    // 快照轮询间隔毫秒
};

// topic_msg 请求负载：按主题名查询消息描述两要素（供导出消息描述文件；StringArray 恰
// 2 行：d[0]=类型名、d[1]=紧凑 msg JSON；独立收敛参数，0 值由节点层钳制为默认）
struct DDSDebugTopicMsg
{
    std::string topicName;  // 待查询主题名（精确匹配）
    uint32_t stableRounds = 0;  // 缺省 0=节点层钳制为默认  // 连续不变快照次数阈值；1 即单次快照免等待
    uint32_t intervalMs = 0;    // 快照轮询间隔毫秒
};

// topic_pub 请求负载：按主题名发布 JSON 载荷消息（首轮完整强校验：节点层先发现收敛+
// 匹配收敛再发布，存在 RELIABLE 订阅者时以 wait_for_acknowledgments 确认送达，全
// BEST_EFFORT 或无订阅者退化尽力而为；收敛参数 0 值由节点层钳制为默认）。requiredSubscribers
// >0 为期望建匹配订阅端数门槛（CLI -w N）：matched 计数达到该值才继续发布，否则一直等待
// （节点层打印等待进度日志）直至达标或停止标志置位中断（不设超时）；=0 自动收敛。repeatIntervalMs
// >0 为持续发布模式：首轮校验成功后保留发布链按该周期重复 write（尽力而为：write 失败仅
// 计数不返回，不再等 ack），直到停止标志 g_debugPubStop 置位（CLI Ctrl+C 经 debugPubStop）
// 或发满 maxTimes 条（>0 时）才退出并清理：发满达标路径在销毁发布链前保底排空 200ms（进程
// 即将退出、writer 即将销毁，末条消息失去重传窗口），发布条数统计经出参 total/failed 带回响应
struct DDSDebugTopicPub
{
    std::string topicName;  // 目标主题名（须已在域内被发现）
    std::string json;       // 发布载荷（JSON 格式，与 topic_example 输出模板同构）
    uint32_t stableRounds = 0;  // 缺省 0=节点层钳制为默认  // 连续不变快照次数阈值；1 即单次快照免等待
    uint32_t intervalMs = 0;    // 快照轮询间隔毫秒
    uint32_t repeatIntervalMs = 0;  // 持续发布间隔毫秒；0=仅发一次（尾部缺省字段）
    uint32_t requiredSubscribers = 0;  // 期望建匹配订阅端数门槛；0=自动收敛（尾部缺省字段）
    uint32_t maxTimes = 0;  // 发布条数上限（含首轮）；0=不限（尾部缺省字段）
    DDSQosProfile qos;  // 临时发布 writer QoS（可缺省尾参）；全哨兵=Fast DDS writer 默认底座
                        // （KEEP_LAST(1)+RELIABLE+TRANSIENT_LOCAL）；
                        // ROS2 底座建议 DDSQosProfile::ros2DefaultPublisher()
};

// ===== YomkDdsBagService 请求负载（端点见 YomkDdsBagService.h） =====

// create_node 请求负载。
struct DDSBagNode
{
    // DDS 域号，合法范围 [0,232]；缺省（YOMKDDS_BAG_NODE 无参形态）经类内缺省初始化器读
    // 环境变量 YOMKDDS_DDS_DOMAIN_ID（无/非法/超范围回落 0），语义同 DDSNode。
    uint32_t domainId = YomkDdsDefaultDomainId();
    // 发现服务器直配聚合串（可缺省尾参，UDP 通道）：空串=不直配；格式 "ip[:port]"，
    // 多 server 分号分隔，port 缺省 11811。非空时 participant 以 CLIENT 发现协议直配
    // （覆盖 XML discoveryServersList）；非法串（空项/空 ip/越界 port）创建失败回显。
    // 缺省（YOMKDDS_BAG_NODE 省略该尾参）经类内缺省初始化器读环境变量：总开关
    // YOMKDDS_USE_DISCOVER_SERVER=true 时取 YOMKDDS_UDP_DISCOVER_SERVERS，否则空串。
    std::string udpDiscoverServerLocator = YomkDdsDefaultUdpDiscoverServers();
    // 发现服务器直配聚合串（可缺省尾参，TCP 通道）：格式同上，port 为 server 的 TCP 监听口，
    // 非空时 participant 补注入 TCPv4 传输并以 TCPv4 locator 直配。语义同 DDSNode。
    // 缺省读 YOMKDDS_TCP_DISCOVER_SERVERS，门控同 UDP 通道。
    std::string tcpDiscoverServerLocator = YomkDdsDefaultTcpDiscoverServers();
};

// bag_record 请求负载。
struct DDSBagRecord
{
    std::vector<std::string> topics;  // 待录制主题清单（至少 1 个，清单内重复/空名拒绝）
    std::string outputDir;            // bag 目录名/路径（相对/绝对均可，父目录自动创建，
                                      // 已存在报错）；空 = 缺省时间戳名 bag_<YYYY-MM-DD_HH-MM-SS_mmm>
    uint64_t maxBagSize = 0;          // 单分片最大字节数；0=不分片（尾部缺省字段）
    uint64_t maxBagDurationSec = 0;   // 单分片最大时长秒；0=不分片（尾部缺省字段，同用先到先分）
    uint64_t maxCacheSize = 0;        // 写缓存双缓冲字节数；0=直写（尾部缺省字段，CLI 默认传 100MiB）
};

// bag_info 请求负载。
struct DDSBagInfo
{
    std::string bagDir;  // bag 目录名/路径（相对/绝对均可，须含 metadata.json）
};

// bag_reindex 请求负载。
struct DDSBagReindex
{
    std::string bagDir;  // bag 目录名/路径（相对/绝对均可；metadata.json 缺失/损坏均可重建）
};

// bag_convert 请求负载。
struct DDSBagConvert
{
    std::vector<std::string> inputs;  // 输入 bag 目录清单（至少 1 个，相对/绝对均可，
                                      // 重复目录拒绝；多输入按 logTime 全局归并）
    std::string cfgPath;              // 输出配置文件路径（JSON，顶层 output_bags 键为
                                      // 对象序列，每条目独立一组输出参数）
};

// bag_play 请求负载。
struct DDSBagPlay
{
    std::string bagDir;  // bag 目录名/路径（相对/绝对均可，须含 metadata.json）
    double playbackRate = 1.0;  // 回放速率倍率（>0；2.0 两倍速，0.5 半倍速，对齐 ros2 --rate；
                                // 尾部缺省字段）
    std::vector<std::string> topics;  // 只回放清单内主题（空=全部，对齐 ros2 --topics 缺省；
                                      // 精确项须在 bag 清单否则报错；元素含一个 '*' 为通配，
                                      // 按 bag 内主题清单匹配展开，对齐 convert topics 语义）
    bool loop = false;  // 循环回放（对齐 ros2 -l/--loop：到尾即回开头无限播放，Ctrl+C 终止；
                        // 尾部缺省字段）
    double startOffset = 0.0;  // 起始偏移秒（对齐 ros2 --start-offset：从 bag 开始第 SEC 秒
                               // 起播，读侧跳过前段消息不入队不计收尾统计；负值非法；
                               // 尾部缺省字段）
    double delay = 0.0;  // 开播延迟秒（对齐 ros2 -d/--delay "Will delay at the beginning of
                         // each loop"：每次开播含每轮回卷前先睡 SEC 秒等订阅端匹配就绪；
                         // 负值非法；尾部缺省字段）
    int64_t waitForAllAckedMs = -1;  // 退出前等全部 RELIABLE 订阅端确认收齐毫秒（对齐 ros2
                                     // --wait-for-all-acked MS：-1 不等待（缺省），0 永等，
                                     // >0 超时毫秒逐主题各计；尾部缺省字段）
    std::vector<std::string> topicRemaps;  // 主题重映射规则（"old:=new" 原始串，对齐 ros2
                                           // --remap old:=new ...：发布主题名精确映射，QoS/类型
                                           // 按 bag 快照照旧，--topics 过滤按原名；尾部缺省字段）
    size_t readAheadQueueSize = kBagPlayReadAheadQueueSize;  // 预读内存队列深度（尾部缺省
                                           // 字段；深度语义见常量注释）
    std::string qosOverridesPath;  // QoS 覆盖文件路径（JSON，键=bag 原主题名，值={reliability,
                                   // durability} 枚举名可独立给——文件命中>快照>回退三档优先
                                   // 级；空=缺省不加载；尾部缺省字段）
};
}  // namespace yomk::payload
