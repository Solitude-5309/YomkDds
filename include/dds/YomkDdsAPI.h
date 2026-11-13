#pragma once
#include <string>
#include <utility>

#include <yomk/server/YomkServerApi.h>
#include <yomk/dds/YomkDdsBagService.h>
#include <yomk/dds/YomkDdsDebugService.h>
#include <yomk/dds/YomkDdsMsg.h>
#include <yomk/dds/YomkDdsService.h>
#include <yomk/dds/YomkDdsUrl.h>

// Maintainer notes: this header is migrating from the macro-form Api to the
// chained Api (per-suite, see tmp/链式api改造.md). The core communication
// suite is fully chained (yomk::dds()); the debug/bag suites are still
// macros. Each unit — chained terminator or macro — wraps one unified request:
// yomk::server().url(<yomk::url constant>).msg(<yomk::msgs message>).request().
// URLs come from YomkDdsUrl.h, payload structures from YomkDdsPayload.h,
// message declarations from YomkDdsMsg.h.

// ===== Chained Api — YomkDdsService core communication (migration in progress) =====
// 设计对齐 YomkServer 链式 API（srv_info/log）：域入口按值返回 builder，builder 机制
// 内敛于 yomk::detail，调用方全程以 auto 接收，从不具名引用类型；builder 禁止缓存复用。
namespace yomk::detail
{
/**
 * @brief XML profiles loading facet: carries the explicit path accumulated by dds().xml(), terminated by load_cfg()
 */
// @zh: XML profiles 加载分面：承载 dds().xml() 累积的显式路径，load_cfg() 终结
class DdsXmlBuilder
{
    std::string m_xmlPath;
public:
    explicit DdsXmlBuilder(std::string xmlPath)
        : m_xmlPath(std::move(xmlPath))
    {
    }
    /**
     * @brief Load the Fast DDS XML profiles file given by xml() (request terminator)
     * @return Response; success requires status == Succeeded, failure details in msg;
     *         returns Invalid if YomkServer is not initialized
     * @note Must be called before node creation; a later successful call replaces
     *       the cache wholesale. Success only proves the file parses — profiles
     *       apply via the three-level extraction chain when nodes are created
     */
    // @zh: 加载 xml() 指定的 Fast DDS XML profiles 文件（请求终结）
    // @zh: return Response，成功判定 status == Succeeded，失败原因读 msg；未初始化返回 Invalid
    // @zh: note 须在建点前调用；再次成功调用整体替换缓存；成功仅证明文件可解析，
    // @zh: profiles 在后续建点时按三级提取链叠加
    yomk::payload::Response load_cfg()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsLoadXml)
            .msg(yomk::msgs::DDSXmlLoad::shared(yomk::payload::DDSXmlLoad{std::move(m_xmlPath)}))
            .request();
    }
};

/**
 * @brief Topic registration facet: carries the nodeName/topicName accumulated by
 *        node().topic(), required type() and optional qos() accumulators,
 *        terminated by create_pub() (publisher) or sub_cb()...create_sub() (subscriber);
 *        data()...pub() publishes one message on an already-registered publisher topic
 */
// @zh: 主题注册分面：承载 node().topic() 累积的节点名/主题名，type() 必填与
// @zh: qos() 可选累积器，create_pub()（发布）或 sub_cb()...create_sub()（订阅）终结；
// @zh: data()...pub() 向已注册发布主题发送一条消息
class DdsTopicBuilder
{
    std::string m_nodeName;
    std::string m_topicName;
    void* m_type = nullptr;
    yomk::payload::DDSQosProfile m_qos;
    yomk::payload::DDSCallbackFunc m_callback;
    void* m_data = nullptr;
    void* m_sample = nullptr;
public:
    DdsTopicBuilder(std::string nodeName, std::string topicName)
        : m_nodeName(std::move(nodeName)), m_topicName(std::move(topicName))
    {
    }
    /**
     * @brief Accumulate the message type instance
     * @param[in] type Message type instance (new XxxPubSubType())
     * @return Reference to self for chaining
     * @warning Ownership of type is transferred to the service; do not hold or
     *          free it afterwards regardless of the result
     */
    // @zh: 累积消息类型实例
    // @zh: type 消息类型实例（new XxxPubSubType()）
    // @zh: return 自身链式引用
    // @zh: warning type 所有权移交服务端，无论成败调用后 caller 不再持有/释放
    DdsTopicBuilder& type(void* type)
    {
        m_type = type;
        return *this;
    }
    /**
     * @brief Accumulate the endpoint QoS description
     * @param[in] qos Endpoint QoS description DDSQosProfile; all-sentinel default
     *            keeps the Fast DDS base, DDSQosProfile::ros2DefaultPublisher() for ROS2
     * @return Reference to self for chaining
     */
    // @zh: 累积端点 QoS 描述
    // @zh: qos 端点 QoS 描述 DDSQosProfile；全哨兵=Fast DDS 默认底座，ROS2 用
    // @zh: DDSQosProfile::ros2DefaultPublisher()
    // @zh: return 自身链式引用
    DdsTopicBuilder& qos(yomk::payload::DDSQosProfile qos)
    {
        m_qos = std::move(qos);
        return *this;
    }
    /**
     * @brief Register a publisher topic on the node (request terminator)
     * @return Response; success requires status == Succeeded, failure details in msg
     */
    // @zh: 在节点上注册发布主题（请求终结）
    // @zh: return Response，成功判定 status == Succeeded，失败原因读 msg
    yomk::payload::Response create_pub()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsRegisterPubTopic)
            .msg(yomk::msgs::DDSTopic::shared(
                yomk::payload::DDSTopic{std::move(m_nodeName), std::move(m_topicName), m_type, std::move(m_qos)}))
            .request();
    }
    /**
     * @brief Accumulate the subscriber callback
     * @param[in] callback Invoked once per received message; the sample pointer
     *            is only valid during the callback
     * @return Reference to self for chaining
     */
    // @zh: 累积订阅回调
    // @zh: callback 每收到一条消息调用一次；消息实例指针仅回调期间有效
    // @zh: return 自身链式引用
    DdsTopicBuilder& sub_cb(yomk::payload::DDSCallbackFunc callback)
    {
        m_callback = std::move(callback);
        return *this;
    }
    /**
     * @brief Register a subscriber topic on the node (request terminator)
     * @return Response; success requires status == Succeeded, failure details in msg
     * @note sub_cb() is required — registering without a callback fails
     */
    // @zh: 在节点上注册订阅主题（请求终结）
    // @zh: return Response，成功判定 status == Succeeded，失败原因读 msg
    // @zh: note sub_cb() 必填——未累积回调的注册返回错误
    yomk::payload::Response create_sub()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        if (!m_callback)
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "sub_cb() callback is required");
        }
        return yomk::server().url(yomk::url::ddsRegisterSubTopic)
            .msg(yomk::msgs::DDSSubRequest::shared(
                yomk::payload::DDSSubRequest{std::move(m_nodeName), std::move(m_topicName), m_type, std::move(m_callback), std::move(m_qos)}))
            .request();
    }
    /**
     * @brief Accumulate the message instance pointer to publish
     * @param[in] data Borrowed (non-owning) message instance pointer
     * @return Reference to self for chaining
     */
    // @zh: 累积待发送的消息实例指针
    // @zh: data 借用（非所有权）消息实例指针
    // @zh: return 自身链式引用
    DdsTopicBuilder& data(void* data)
    {
        m_data = data;
        return *this;
    }
    /**
     * @brief Publish one message on the already-registered publisher topic (request terminator)
     * @return Response; success requires status == Succeeded, failure details in msg
     * @note The publish writes synchronously; the topic must have been registered
     *       via create_pub() first
     */
    // @zh: 向已注册发布主题发送一条消息（请求终结）
    // @zh: return Response，成功判定 status == Succeeded，失败原因读 msg
    // @zh: note publish 同步写入；主题须先经 create_pub() 注册，未注册返回错误
    yomk::payload::Response pub()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsPublish)
            .msg(yomk::msgs::DDSPublish::shared(
                yomk::payload::DDSPublish{std::move(m_nodeName), std::move(m_topicName), m_data}))
            .request();
    }
    /**
     * @brief Loan a publish buffer for zero-copy write, plain types only (request terminator)
     * @return Writer-pool sample pointer; nullptr on failure (non-loanable type or
     *         pool exhausted) or when YomkServer is not initialized
     * @note The only terminator that does not return Response — emptiness check
     *       on the return value is the failure test
     * @warning The pointer is reclaimed after each write; loan again per send
     */
    // @zh: 借出发布缓冲（仅 plain 类型，免序列化，请求终结）
    // @zh: return writer 池内待发样本指针；失败（非 loan 类型/池耗尽）或未初始化返回 nullptr
    // @zh: note 唯一非 Response 终结——返回值判空即失败判定
    // @zh: warning 每次 write 后指针即被中间件收回，须重新借出
    void* loan()
    {
        if (!requireServer())
        {
            return nullptr;
        }
        auto resp = yomk::server().url(yomk::url::ddsLoan)
            .msg(yomk::msgs::DDSLoan::shared(
                yomk::payload::DDSLoan{std::move(m_nodeName), std::move(m_topicName), nullptr}))
            .request();
        if (resp.status != yomk::payload::ResStatus::Succeeded || !resp.data)
        {
            return nullptr;
        }
        auto loan = yomk::msgs::unpack<yomk::msgs::DDSLoanResult>(resp.data);
        return loan ? loan->msg.sample : nullptr;
    }
    /**
     * @brief Accumulate the loaned sample pointer to return
     * @param[in] sample Pointer obtained from the chained loan()
     * @return Reference to self for chaining
     */
    // @zh: 累积待归还的借出样本指针
    // @zh: sample 须为链式 loan() 借出的指针
    // @zh: return 自身链式引用
    DdsTopicBuilder& sample(void* sample)
    {
        m_sample = sample;
        return *this;
    }
    /**
     * @brief Return an unpublished loaned sample to avoid writer-pool leak (request terminator)
     * @return Response; success requires status == Succeeded, failure details in msg
     * @note The pointer becomes invalid once returned; do not access it afterwards
     */
    // @zh: 归还未发布的借出样本，避免 writer 池泄漏（请求终结）
    // @zh: return Response，成功判定 status == Succeeded，失败原因读 msg
    // @zh: note 归还成功后指针即失效，不得再访问
    yomk::payload::Response return_loan()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsDiscardLoan)
            .msg(yomk::msgs::DDSLoan::shared(
                yomk::payload::DDSLoan{std::move(m_nodeName), std::move(m_topicName), m_sample}))
            .request();
    }
};

/**
 * @brief Node creation facet: carries the nodeName accumulated by dds().node(),
 *        optional domain()/udp_ds()/tcp_ds() accumulators (order-free), terminated by create()
 */
// @zh: 节点创建分面：承载 dds().node() 累积的节点名，domain()/udp_ds()/tcp_ds()
// @zh: 可选累积器（次序不限），create() 终结
class DdsNodeBuilder
{
    std::string m_nodeName;
    uint32_t m_domainId = yomk::payload::YomkDdsDefaultDomainId();                               // 缺省读 YOMKDDS_DDS_DOMAIN_ID
    std::string m_udpDiscoverServerLocator = yomk::payload::YomkDdsDefaultUdpDiscoverServers();  // 缺省经总开关读 YOMKDDS_UDP_DISCOVER_SERVERS
    std::string m_tcpDiscoverServerLocator = yomk::payload::YomkDdsDefaultTcpDiscoverServers();  // 缺省经总开关读 YOMKDDS_TCP_DISCOVER_SERVERS
public:
    explicit DdsNodeBuilder(std::string nodeName)
        : m_nodeName(std::move(nodeName))
    {
    }
    /**
     * @brief Accumulate the DDS domain id
     * @param[in] domainId DDS domain id [0,232]
     * @return Reference to self for chaining
     */
    // @zh: 累积 DDS 域号
    // @zh: domainId DDS 域号 [0,232]
    // @zh: return 自身链式引用
    DdsNodeBuilder& domain(uint32_t domainId)
    {
        m_domainId = domainId;
        return *this;
    }
    /**
     * @brief Accumulate the UDP channel discovery-server locator aggregate
     * @param[in] locator "ip[:port]" aggregate, semicolon-separated for multiple servers
     * @return Reference to self for chaining
     */
    // @zh: 累积 UDP 通道发现服务器直配聚合串
    // @zh: locator "ip[:port]" 聚合串，多 server 分号分隔
    // @zh: return 自身链式引用
    DdsNodeBuilder& udp_ds(std::string locator)
    {
        m_udpDiscoverServerLocator = std::move(locator);
        return *this;
    }
    /**
     * @brief Accumulate the TCP channel discovery-server locator aggregate
     * @param[in] locator Same form as udp_ds()
     * @return Reference to self for chaining
     */
    // @zh: 累积 TCP 通道发现服务器直配聚合串
    // @zh: locator 形式同 udp_ds()
    // @zh: return 自身链式引用
    DdsNodeBuilder& tcp_ds(std::string locator)
    {
        m_tcpDiscoverServerLocator = std::move(locator);
        return *this;
    }
    /**
     * @brief Switch into the topic registration facet with the topic name
     * @param[in] topicName Topic name to register on this node
     * @return Topic registration facet, terminated by create_pub() or create_sub()
     */
    // @zh: 切入主题注册分面并累积主题名
    // @zh: topicName 本节点待注册的主题名
    // @zh: return 主题注册分面，经 create_pub() 或 create_sub() 终结
    DdsTopicBuilder topic(std::string topicName)
    {
        return DdsTopicBuilder(m_nodeName, std::move(topicName));  // 拷贝节点名：node 分面其后仍可 create()
    }

    /**
     * @brief Create the DDS node, one dedicated DDS participant per node (request terminator)
     * @return Response; invalid locator input fails with the reason echoed back
     * @note nodeName is unique and must not be empty; only same-domain nodes
     *       interoperate. Unset accumulators keep the environment-variable defaults
     */
    // @zh: 创建 DDS 节点（每节点一个独立 DDS 参与者，请求终结）
    // @zh: return Response；非法直配串创建失败并回显原因
    // @zh: note nodeName 唯一且不可为空；仅同域节点可互通；未累积项保持环境变量缺省
    yomk::payload::Response create()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsCreateNode)
            .msg(yomk::msgs::DDSNode::shared(
                yomk::payload::DDSNode{std::move(m_nodeName), m_domainId, std::move(m_udpDiscoverServerLocator), std::move(m_tcpDiscoverServerLocator)}))
            .request();
    }
    /**
     * @brief Delete the node and destroy all its DDS entities (request terminator)
     * @return Response; fails when the node name is unknown
     * @note Destroy counterpart of create() on the same facet
     */
    // @zh: 删除节点并销毁其全部 DDS 实体（请求终结）
    // @zh: return Response；nodeName 不存在时返回错误
    // @zh: note 与 create() 同分面的销毁对
    yomk::payload::Response remove()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsDeleteNode)
            .msg(yomk::msgs::String::shared(std::move(m_nodeName)))
            .request();
    }
};

/**
 * @brief Core communication chained builder: chained endpoints live here during the macro-to-chain migration
 */
// @zh: 核心通信链式 Builder：宏链迁移期内的链式端点挂载于此
class DdsBuilder
{
public:
    /**
     * @brief Switch into the node creation facet with the unique node name
     * @param[in] nodeName Unique node name, must not be empty
     * @return Node creation facet, terminated by create()
     */
    // @zh: 切入节点创建分面并累积节点唯一名
    // @zh: nodeName 节点唯一名，不可为空
    // @zh: return 节点创建分面，经 create() 终结
    DdsNodeBuilder node(std::string nodeName)
    {
        return DdsNodeBuilder(std::move(nodeName));
    }

    /**
     * @brief Query the extension version (read terminator)
     * @return Response carrying a String packet ("YomkDds v<version>");
     *         returns Invalid if YomkServer is not initialized
     * @note Unpacking and printing the version string are the caller's
     *       responsibility (the former macro did both internally)
     */
    // @zh: 查询扩展版本（读终结）
    // @zh: return Response 携带 String 包（"YomkDds v<版本号>"）；未初始化返回 Invalid
    // @zh: note 版本串的解包与打印由调用方自行处理（原宏内部代做）
    yomk::payload::Response version()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsVersion).msg(nullptr).request();
    }

    /**
     * @brief Switch into the XML profiles loading facet with an explicit path
     * @param[in] xmlPath Explicit path, relative or absolute
     * @return XML loading facet, terminated by load_cfg()
     */
    // @zh: 切入 XML profiles 加载分面并累积显式路径
    // @zh: xmlPath 显式路径（相对/绝对均可）
    // @zh: return XML 加载分面，经 load_cfg() 终结
    DdsXmlBuilder xml(std::string xmlPath)
    {
        return DdsXmlBuilder(std::move(xmlPath));
    }

    /**
     * @brief Load the default Fast DDS XML profiles file (request terminator)
     * @return Response; success requires status == Succeeded, failure details in msg;
     *         returns Invalid if YomkServer is not initialized
     * @note Default path reads YOMKDDS_XML_PROFILE_PATH gated by
     *       YOMKDDS_USE_XML_PROFILE=true; empty path skips loading silently.
     *       Must be called before node creation; a later successful call replaces
     *       the cache wholesale. Success only proves the file parses — profiles
     *       apply via the three-level extraction chain when nodes are created
     */
    // @zh: 加载缺省 Fast DDS XML profiles 文件（请求终结）
    // @zh: return Response，成功判定 status == Succeeded，失败原因读 msg；未初始化返回 Invalid
    // @zh: note 缺省路径经总开关 YOMKDDS_USE_XML_PROFILE=true 读取 YOMKDDS_XML_PROFILE_PATH，
    // @zh: 空串由调用方静默跳过加载；须在建点前调用；再次成功调用整体替换缓存；
    // @zh: 成功仅证明文件可解析，profiles 在后续建点时按三级提取链叠加
    yomk::payload::Response load_cfg()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsLoadXml)
            .msg(yomk::msgs::DDSXmlLoad::shared(yomk::payload::DDSXmlLoad{}))
            .request();
    }
};
}  // namespace yomk::detail

namespace yomk
{
/**
 * @brief YomkDds core communication domain entry: starting point for chained calls
 * @return detail::DdsBuilder (by value, for single-expression chained calls only)
 * @note Thread-safe, non-blocking
 */
// @zh: YomkDds 核心通信域入口：链式调用的起点
// @zh: return detail::DdsBuilder（按值，仅用于单表达式链式调用）
// @zh: note 线程安全，非阻塞
inline detail::DdsBuilder dds()
{
    return detail::DdsBuilder{};
}
}  // namespace yomk

// ===== YomkDdsDebugService — type-agnostic debugging =====
/**
 * @brief Debugging macros (YomkDdsDebugService endpoints); equal CLI: yomkdds
 *
 * Every macro returns yomk::payload::Response and requires a created debug
 * node (YOMKDDS_DEBUG_NODE) first. list/info/find share the convergence
 * semantics: consecutive stableRounds unchanged snapshots, 0 clamped to
 * defaults 5 rounds / 200ms. Full behavior: docs/features/debug-service.md
 */
// @zh: 调试宏（YomkDdsDebugService 端点，等价 CLI：yomkdds）
// @zh: 均返回 yomk::payload::Response，且须先创建调试节点；list/info/find 组共用收敛语义：
// @zh: 连续 stableRounds 次快照不变即返回，0 钳制为默认 5 次/200ms；详见 docs/features/debug-service.md

/**
 * @brief Create the debug node (type-agnostic domain observer)
 * @param[in] ... variadic tails, all optional: same form as yomk::dds().node(...).create()
 * @note Single node per process; delete it before re-creating
 */
// @zh: 创建调试节点（类型无关域观察者）
// @zh: 可变尾参语义同 yomk::dds().node(...).create()
// @zh: note 单节点模型：一个进程至多一个，重复创建须先删除

#define YOMKDDS_DEBUG_NODE(...) \
    ::yomk::server().url(::yomk::url::ddsDebugCreateNode).msg(::yomk::msgs::DDSDebugNode::shared(yomk::payload::DDSDebugNode{__VA_ARGS__})).request()

/**
 * @brief Register a topic-print observer; JSON texts are delivered per message
 * @param[in] output User callback (DDSDebugOutputFunc); must be thread-safe and quick
 * @param[in] ... qos variadic tail: dynamic subscription reader QoS
 * @note Discovery-driven deferred behavior: succeeds even when the topic has
 *       no writer yet; repeated registration of the same topic fails
 * @warning Empty output callback is rejected
 */
// @zh: 登记调试主题，消息 JSON 文本逐条投递 output
// @zh: output 用户自定义回调（DDSDebugOutputFunc），须线程安全且快速返回
// @zh: qos 可缺省尾参：动态订阅 reader QoS
// @zh: note 发现驱动的延迟行为：主题尚无发布者仍返回成功；重复登记同一主题返回错误
// @zh: warning 空输出回调拒绝

#define YOMKDDS_DEBUG_TOPIC_PRINT(topicName, output, ...) \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugTopicPrint).msg(\
        ::yomk::msgs::DDSDebugTopic::shared(yomk::payload::DDSDebugTopic{topicName, output, ##__VA_ARGS__})).request()

/**
 * @brief List discovered topics in the domain (names only, sorted)
 */
// @zh: 列出域内已发现的全部主题（仅主题名，按名排序）

#define YOMKDDS_DEBUG_TOPIC_LIST(stableRounds, intervalMs)                   \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugListTopics).msg(\
        ::yomk::msgs::DDSDebugList::shared(yomk::payload::DDSDebugList{stableRounds, intervalMs})).request()

/**
 * @brief List discovered topics with type names ("topic [type]" per line)
 */
// @zh: 列出域内主题并附类型名（每行 "主题名 [类型名]"）

#define YOMKDDS_DEBUG_TOPIC_LIST_T(stableRounds, intervalMs)                 \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugListTopics).msg(\
        ::yomk::msgs::DDSDebugList::shared(yomk::payload::DDSDebugList{stableRounds, intervalMs, true})).request()

/**
 * @brief Query one topic's discovery details (type, publisher/subscriber counts)
 * @note Fails when the topic is not discovered
 */
// @zh: 查询单主题的发现详情（类型、发布/订阅端计数）
// @zh: note 未发现主题返回错误

#define YOMKDDS_DEBUG_TOPIC_INFO(topicName, stableRounds, intervalMs)        \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugTopicInfo).msg(\
        ::yomk::msgs::DDSDebugInfo::shared(yomk::payload::DDSDebugInfo{topicName, stableRounds, intervalMs})).request()

/**
 * @brief Verbose topic details: per-endpoint node name, GUID and QoS profile
 */
// @zh: 单主题端点详情（verbose）：逐端点输出节点名、GUID 与 QoS profile

#define YOMKDDS_DEBUG_TOPIC_INFO_V(topicName, stableRounds, intervalMs)      \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugTopicInfo).msg(\
        ::yomk::msgs::DDSDebugInfo::shared(yomk::payload::DDSDebugInfo{topicName, stableRounds, intervalMs, true})).request()

/**
 * @brief Reverse-lookup topics by data type name (exact match)
 * @note Fails when no topic matches the type
 */
// @zh: 按数据类型名反查主题列表（精确匹配）
// @zh: note 无匹配主题返回错误

#define YOMKDDS_DEBUG_TOPIC_FIND(typeName, stableRounds, intervalMs)         \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugTopicFind).msg(\
        ::yomk::msgs::DDSDebugFind::shared(yomk::payload::DDSDebugFind{typeName, stableRounds, intervalMs})).request()

/**
 * @brief Dump the IDL struct description of a data type
 * @note Requires the peer to provide XTypes TypeObject (ROS2 rmw_fastrtps
 *       endpoints do not)
 */
// @zh: 按数据类型名输出 IDL 结构描述
// @zh: note 需对端提供 XTypes TypeObject（ROS2 rmw_fastrtps 端点不提供）

#define YOMKDDS_DEBUG_INTERFACE_SHOW(typeName, stableRounds, intervalMs)     \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugInterfaceShow).msg(\
        ::yomk::msgs::DDSDebugInterfaceShow::shared(yomk::payload::DDSDebugInterfaceShow{typeName, stableRounds, intervalMs})).request()

/**
 * @brief List all discovered data type names (deduplicated, sorted)
 */
// @zh: 列出发现缓存中出现的全部数据类型名（去重字典序）

#define YOMKDDS_DEBUG_INTERFACE_LIST(stableRounds, intervalMs)              \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugInterfaceList).msg(\
        ::yomk::msgs::DDSDebugInterfaceList::shared(yomk::payload::DDSDebugInterfaceList{stableRounds, intervalMs})).request()

/**
 * @brief Query a copy-pastable publish example for a topic (Type/IDL/example)
 */
// @zh: 按主题名查询发布示例三段行集（Type/IDL/example 命令）

#define YOMKDDS_DEBUG_TOPIC_EXAMPLE(topicName, stableRounds, intervalMs)    \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugTopicExample).msg(\
        ::yomk::msgs::DDSDebugTopicExample::shared(yomk::payload::DDSDebugTopicExample{topicName, stableRounds, intervalMs})).request()

/**
 * @brief Query a topic's message description: type name + compact msg JSON
 */
// @zh: 按主题名查询消息描述两要素：类型名 + 紧凑 msg JSON

#define YOMKDDS_DEBUG_TOPIC_MSG(topicName, stableRounds, intervalMs)        \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugTopicMsg).msg(\
        ::yomk::msgs::DDSDebugTopicMsg::shared(yomk::payload::DDSDebugTopicMsg{topicName, stableRounds, intervalMs})).request()

/**
 * @brief Publish a JSON payload to a topic with full first-shot validation
 * @param[in] requiredSubscribers Delivery gate: publish only after that many
 *            matched subscribers (0 = auto convergence)
 * @param[in] repeatIntervalMs >0 keeps the publish chain alive and repeats
 *            until the stop flag or maxTimes is reached
 * @note Blocking until convergence and (for RELIABLE peers) acknowledgment
 * @warning Stop a repeating publish via yomk::debugPubStop (async-signal-safe)
 */
// @zh: 按主题名发布 JSON 载荷消息（首轮完整强校验）
// @zh: requiredSubscribers 期望建匹配订阅端数门槛（0=自动收敛）
// @zh: repeatIntervalMs >0 为持续发布模式，直至停止标志置位或发满 maxTimes 条
// @zh: note 阻塞至收敛完成（RELIABLE 订阅端还等送达确认）
// @zh: warning 持续发布经 yomk::debugPubStop 停止（async-signal-safe）

#define YOMKDDS_DEBUG_TOPIC_PUB(topicName, json, stableRounds, intervalMs, repeatIntervalMs, requiredSubscribers, maxTimes, ...)  \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugTopicPub).msg(\
        ::yomk::msgs::DDSDebugTopicPub::shared(yomk::payload::DDSDebugTopicPub{topicName, json, stableRounds, intervalMs, repeatIntervalMs, requiredSubscribers, maxTimes, ##__VA_ARGS__})).request()

/**
 * @brief List named participants in the domain (sorted, empty names skipped)
 */
// @zh: 列出当前域内已发现的全部命名参与者（按名排序，空名参与者跳过）

#define YOMKDDS_DEBUG_NODE_LIST(stableRounds, intervalMs)                    \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugListNodes).msg(\
        ::yomk::msgs::DDSNodeList::shared(yomk::payload::DDSNodeList{stableRounds, intervalMs})).request()

/**
 * @brief Query pub/sub topic lists of a named participant (GUID-prefix based)
 * @note Endpoints of same-name participants are merged
 */
// @zh: 查询指定节点名的发布/订阅主题清单（按端点 GUID 前缀归属）
// @zh: note 同名多参与者端点合并

#define YOMKDDS_DEBUG_NODE_INFO(nodeName, stableRounds, intervalMs)          \
    ::yomk::server().url(\
        ::yomk::url::ddsDebugNodeInfo).msg(\
        ::yomk::msgs::DDSNodeInfo::shared(yomk::payload::DDSNodeInfo{nodeName, stableRounds, intervalMs})).request()

/**
 * @brief Quit debugging: delete the debug node and its DDS entities
 * @note Fails when the debug node was not created
 */
// @zh: 退出调试：删除调试节点及其全部 DDS 实体
// @zh: note 未创建时返回错误

#define YOMKDDS_DEBUG_QUIT() ::yomk::server().url(::yomk::url::ddsDebugDeleteNode).msg(nullptr).request()


// ===== YomkDdsBagService — mcap bag record/replay =====
/**
 * @brief Bag record/replay macros (YomkDdsBagService endpoints)
 *
 * Every macro returns yomk::payload::Response. bag_record/bag_play block the
 * calling thread until finished or stopped via the yomk::bag* flags.
 * Full behavior: docs/features/bag-service.md
 */
// @zh: Bag 录制回放宏（YomkDdsBagService 端点）
// @zh: 均返回 yomk::payload::Response；bag_record/bag_play 阻塞调用线程至收尾或经
// @zh: yomk::bag* 标志停止；详见 docs/features/bag-service.md

/**
 * @brief Create the bag node (mcap record/replay)
 * @param[in] ... variadic tails, all optional: same form as yomk::dds().node(...).create()
 * @note Single node per process; delete it before re-creating
 */
// @zh: 创建 bag 节点（mcap 录制回放）
// @zh: 可变尾参语义同 yomk::dds().node(...).create()
// @zh: note 单节点模型：一个进程至多一个，重复创建须先删除

#define YOMKDDS_BAG_NODE(...) \
    ::yomk::server().url(::yomk::url::ddsBagCreateNode).msg(::yomk::msgs::DDSBagNode::shared(yomk::payload::DDSBagNode{__VA_ARGS__})).request()

/**
 * @brief Record topics into an mcap bag (blocks the calling thread until done)
 * @param[in] topics At least 1; one '*' wildcard item expands over the
 *            discovery snapshot; unmatched/wildcard-less-no-publisher items fail wholesale
 * @param[in] outputDir Empty = timestamped default bag_<...>; existing dir fails
 * @param[in] maxBagSize,maxBagDurationSec Per-file split thresholds (0 = no split)
 * @param[in] maxCacheSize Write cache double-buffer bytes; 0 = direct write
 * @note Stop via yomk::bagRecordStop (async-signal-safe); reset before reuse
 */
// @zh: 录制主题到 mcap bag（阻塞调用线程至录制收尾）
// @zh: topics 至少 1 个；恰含一个 '*' 的通配项按发现快照展开；未命中/无效项整体报错
// @zh: outputDir 空 = 缺省时间戳名；已存在报错
// @zh: maxBagSize/maxBagDurationSec 单分片阈值（0=不分片）；maxCacheSize 写缓存字节数（0=直写）
// @zh: note 经 yomk::bagRecordStop 停止（async-signal-safe）；复用前先 bagRecordReset 复位

#define YOMKDDS_BAG_RECORD(topics, outputDir, maxBagSize, maxBagDurationSec, maxCacheSize) \
    ::yomk::server().url(::yomk::url::ddsBagRecord).msg(::yomk::msgs::DDSBagRecord::shared(yomk::payload::DDSBagRecord{(topics), (outputDir), (maxBagSize), (maxBagDurationSec), (maxCacheSize)})).request()

/**
 * @brief Show bag metadata (pure file read, no bag node needed)
 * @note Fails when the path has no readable metadata.json
 */
// @zh: 查看 bag 元信息（纯文件读，不经 bag 节点）
// @zh: note 路径无 metadata.json/解析失败返回错误

#define YOMKDDS_BAG_INFO(bagDir) \
    ::yomk::server().url(::yomk::url::ddsBagInfo).msg(::yomk::msgs::DDSBagInfo::shared(yomk::payload::DDSBagInfo{(bagDir)})).request()

/**
 * @brief Rebuild metadata.json from the bag's mcap files (pure file op)
 * @note Overwrites unconditionally; old bags without type metadata fall back
 *       to an empty type string
 */
// @zh: 重建 bag 元信息（纯文件操作，读全部分片重建 metadata.json）
// @zh: note 无条件覆盖写出；旧格式 bag（无 type 元数据）回退空串

#define YOMKDDS_BAG_REINDEX(bagDir) \
    ::yomk::server().url(::yomk::url::ddsBagReindex).msg(::yomk::msgs::DDSBagReindex::shared(yomk::payload::DDSBagReindex{(bagDir)})).request()

/**
 * @brief Filter/merge input bags into new bags per a JSON config file
 * @param[in] inputs At least 1, duplicates rejected
 * @param[in] cfgPath JSON, top-level output_bags array with per-entry params
 * @note Multi-input merge is globally ordered by logTime; cross-input type
 *       mismatch of the same topic fails; produced outputs are kept on failure
 */
// @zh: 转换 bag（按 JSON 配置过滤合并输入为新 bag）
// @zh: inputs 至少 1 个，重复目录拒绝
// @zh: cfgPath JSON，顶层 output_bags 键为对象序列，每条目独立一组输出参数
// @zh: note 多输入按 logTime 全局归并；同主题跨输入类型不一致报错；失败不回滚已产生输出

#define YOMKDDS_BAG_CONVERT(inputs, cfgPath) \
    ::yomk::server().url(::yomk::url::ddsBagConvert).msg(::yomk::msgs::DDSBagConvert::shared(yomk::payload::DDSBagConvert{(inputs), (cfgPath)})).request()

/**
 * @brief Replay an mcap bag on its recorded timeline (blocks until done)
 * @param[in] playbackRate >0 speed multiplier
 * @param[in] topics Empty = all; one '*' wildcard supported
 * @param[in] loop Endless replay; startOffset skips seconds from bag start;
 *            delay adds per-loop start latency
 * @param[in] waitForAllAckedMs RELIABLE ack wait before exit (-1/0/>0)
 * @param[in] topicRemaps "old:=new" rules; readAheadQueueSize pre-read queue
 *            depth; qosOverridesPath JSON QoS overrides (file > bag snapshot > fallback)
 * @note Pause/resume/rate at runtime via yomk::bagPlayPause/Resume/SetRate;
 *       stop via yomk::bagPlayStop (async-signal-safe); legacy bags without
 *       type metadata are rejected
 */
// @zh: 回放 mcap bag（阻塞至全部消息播毕或停止置位）
// @zh: playbackRate >0 倍率；topics 空=全部（支持恰一个 '*' 通配）；loop 循环回放；
// @zh: startOffset 起始偏移秒；delay 每轮开播延迟；waitForAllAckedMs 退出前等确认（-1/0/>0）；
// @zh: topicRemaps "old:=new" 重映射；readAheadQueueSize 预读队列深度；
// @zh: qosOverridesPath QoS 覆盖文件（文件 > bag 快照 > 回退三档）
// @zh: note 运行时经 yomk::bagPlayPause/Resume/SetRate 控制；经 yomk::bagPlayStop 停止
// @zh: （async-signal-safe）；旧格式 bag（type 空串）拒播

#define YOMKDDS_BAG_PLAY(bagDir, playbackRate, topics, loop, startOffset, delay, waitForAllAckedMs, topicRemaps, readAheadQueueSize, qosOverridesPath) \
    ::yomk::server().url(::yomk::url::ddsBagPlay).msg(\
        ::yomk::msgs::DDSBagPlay::shared(yomk::payload::DDSBagPlay{(bagDir), (playbackRate), (topics), (loop), (startOffset), (delay), (waitForAllAckedMs), (topicRemaps), (readAheadQueueSize), (qosOverridesPath)})).request()

/**
 * @brief Quit bag mode: delete the bag node and its DDS entities
 * @note Fails when the bag node was not created
 */
// @zh: 退出录制：删除 bag 节点及其全部 DDS 实体
// @zh: note 未创建时返回错误

#define YOMKDDS_BAG_DEL_NODE() ::yomk::server().url(::yomk::url::ddsBagDeleteNode).msg(nullptr).request()
