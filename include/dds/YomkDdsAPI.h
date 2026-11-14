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
// chained Api (per-suite, see tmp/链式api改造-调试套件.md). The core
// communication suite is fully chained (yomk::dds()); the debug suite is
// mid-migration (the debug-node lifecycle create()/quit() is chained, the
// remaining endpoints are still macros); the bag suite is still macros. Each
// unit — chained terminator or macro — wraps one unified request:
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
 * @brief Debug topic facet: carries the topicName opened by debug().topic(), the
 *        out_cb() output callback and qos() accumulator of the print() surface, and
 *        the rounds()/interval()/verbose() accumulators of the info() surface
 *        (all optional accumulators are order-free); terminated by print() or info(),
 *        the remaining topic sub-facets join it in later migration rounds
 */
// @zh: 调试主题分面：承载 debug().topic() 起始的主题名、print() 面的 out_cb() 输出回调与
// @zh: qos() 累积器，以及 info() 面的 rounds()/interval()/verbose() 累积器（可选累积器次序不限），
// @zh: 由 print() 或 info() 终结；其余 topic 子分面在后续迁移批次并入
class DdsDebugTopicBuilder
{
    std::string m_topicName;
    yomk::payload::DDSDebugOutputFunc m_output;
    yomk::payload::DDSQosProfile m_qos;
    uint32_t m_stableRounds = 0;  // info() 收敛次数；0 由节点层钳制为默认 5 次
    uint32_t m_intervalMs = 0;    // info() 轮询间隔；0 由节点层钳制为默认 200ms
    bool m_verbose = false;       // info() 端点详情模式：verbose() 置真
public:
    explicit DdsDebugTopicBuilder(std::string topicName)
        : m_topicName(std::move(topicName))
    {
    }
    /**
     * @brief Accumulate the output callback receiving one JSON text per message
     * @param[in] callback DDSDebugOutputFunc; delivered on the DDS reader thread, so
     *            it must be thread-safe and return quickly
     * @return Reference to self for chaining
     * @warning An empty callback is rejected by print()
     */
    // @zh: 累积输出回调，每收到一条消息投递一次 JSON 文本
    // @zh: callback DDSDebugOutputFunc，投递发生在 DDS reader 线程，须线程安全且快速返回
    // @zh: return 自身链式引用
    // @zh: warning 空回调由 print() 拒绝
    DdsDebugTopicBuilder& out_cb(yomk::payload::DDSDebugOutputFunc callback)
    {
        m_output = std::move(callback);
        return *this;
    }
    /**
     * @brief Accumulate the dynamic subscription reader QoS
     * @param[in] qos Reader QoS DDSQosProfile; all-sentinel default keeps
     *            BEST_EFFORT+VOLATILE depth 1
     * @return Reference to self for chaining
     */
    // @zh: 累积动态订阅 reader QoS
    // @zh: qos reader QoS DDSQosProfile；全哨兵缺省保持 BEST_EFFORT+VOLATILE depth 1
    // @zh: return 自身链式引用
    DdsDebugTopicBuilder& qos(yomk::payload::DDSQosProfile qos)
    {
        m_qos = std::move(qos);
        return *this;
    }
    /**
     * @brief Accumulate the convergence round count of info()
     * @param[in] rounds Consecutive unchanged discovery snapshots required; 0 is
     *            clamped by the node layer to the default 5
     * @return Reference to self for chaining
     * @note Applies to info() only; print() is a continuous subscription and
     *       ignores the convergence accumulators
     */
    // @zh: 累积 info() 的收敛轮数
    // @zh: rounds 连续多少次快照不变即认为收敛；0 由节点层钳制为默认 5 次
    // @zh: return 自身链式引用
    // @zh: note 仅作用于 info()；print() 为持续订阅，不看收敛累积器
    DdsDebugTopicBuilder& rounds(uint32_t rounds)
    {
        m_stableRounds = rounds;
        return *this;
    }
    /**
     * @brief Accumulate the discovery snapshot polling interval of info()
     * @param[in] intervalMs Polling interval in milliseconds; 0 is clamped by the
     *            node layer to the default 200ms
     * @return Reference to self for chaining
     * @note Applies to info() only, same as rounds()
     */
    // @zh: 累积 info() 的发现快照轮询间隔
    // @zh: intervalMs 轮询间隔（毫秒）；0 由节点层钳制为默认 200ms
    // @zh: return 自身链式引用
    // @zh: note 仅作用于 info()，同 rounds()
    DdsDebugTopicBuilder& interval(uint32_t intervalMs)
    {
        m_intervalMs = intervalMs;
        return *this;
    }
    /**
     * @brief Accumulate the verbose mode of info()
     * @param[in] verbose true: list per-endpoint details (node name / endpoint type /
     *            GUID / QoS profile) on top of the counts; false: the three-line form
     * @return Reference to self for chaining
     * @note Applies to info() only, same as rounds()/interval(); default false
     */
    // @zh: 累积 info() 的 verbose 模式
    // @zh: verbose true：在计数之外逐端点输出节点名/端点类型/GUID/QoS profile；false：三行形态
    // @zh: return 自身链式引用
    // @zh: note 仅作用于 info()，同 rounds()/interval()；缺省 false
    DdsDebugTopicBuilder& verbose(bool verbose)
    {
        m_verbose = verbose;
        return *this;
    }
    /**
     * @brief Register the topic-print observer, texts are delivered per message (request terminator)
     * @return Response; Invalid when YomkServer is not initialized or out_cb()
     *         was not accumulated
     * @note Discovery-driven deferred behavior: succeeds even when the topic has
     *       no writer yet and auto-subscribes once discovered; re-registering the
     *       same topic fails
     */
    // @zh: 登记调试主题，消息 JSON 文本逐条投递 out_cb() 回调（请求终结）
    // @zh: return Response；未初始化或未累积 out_cb() 回调时返回 Invalid
    // @zh: note 发现驱动的延迟行为：主题尚无发布者仍返回成功，发现后自动建订；重复登记同一主题返回错误
    yomk::payload::Response print()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        if (!m_output)
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "out_cb() callback is required");
        }
        return yomk::server().url(yomk::url::ddsDebugTopicPrint)
            .msg(yomk::msgs::DDSDebugTopic::shared(
                yomk::payload::DDSDebugTopic{std::move(m_topicName), std::move(m_output), std::move(m_qos)}))
            .request();
    }
    /**
     * @brief Query one topic's discovery details (request terminator)
     * @return Response carrying a StringArray packet; without verbose() it is exactly
     *         three lines: "Type: <raw DDS type name>", "Publisher count: N",
     *         "Subscription count: N"; with verbose(true) it adds per-endpoint lines
     *         (node name / endpoint type / GUID / QoS profile), and an endpoint type
     *         with count 0 contributes no section
     * @note Independent convergence query (rounds()/interval(), 0 clamped to
     *       5 rounds / 200ms) that does not depend on the query facet topics();
     *       fails when the topic has not been discovered
     */
    // @zh: 查询单主题的发现详情（请求终结）
    // @zh: return Response 携带 StringArray 包；未累积 verbose() 时恰三行：Type: 原始 DDS 类型名、
    // @zh: Publisher count: N、Subscription count: N；verbose(true) 时在计数外追加逐端点行
    // @zh: （节点名/端点类型/GUID/QoS profile），count 为 0 的端点类型无清单段
    // @zh: note 独立收敛查询（rounds()/interval()，0 钳制为 5 次/200ms），
    // @zh: 不依赖查询分面的 topics()；未发现主题返回错误
    yomk::payload::Response info()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsDebugTopicInfo)
            .msg(yomk::msgs::DDSDebugInfo::shared(
                yomk::payload::DDSDebugInfo{std::move(m_topicName), m_stableRounds, m_intervalMs, m_verbose}))
            .request();
    }
};

/**
 * @brief Debug type facet: carries the typeName opened by debug().type() and the
 *        rounds()/interval() convergence accumulators (order-free), terminated by
 *        find_topics() or interface_show(); the remaining type sub-facets join it
 *        in later migration rounds
 */
// @zh: 调试类型分面：承载 debug().type() 起始的类型名与 rounds()/interval() 收敛累积器
// @zh: （次序不限），find_topics() 或 interface_show() 终结；其余 type 子分面在后续迁移批次并入
class DdsDebugTypeBuilder
{
    std::string m_typeName;
    uint32_t m_stableRounds = 0;  // 0 由节点层钳制为默认 5 次
    uint32_t m_intervalMs = 0;    // 0 由节点层钳制为默认 200ms
public:
    explicit DdsDebugTypeBuilder(std::string typeName)
        : m_typeName(std::move(typeName))
    {
    }
    /**
     * @brief Accumulate the convergence round count
     * @param[in] rounds Consecutive unchanged discovery snapshots required; 0 is
     *            clamped by the node layer to the default 5
     * @return Reference to self for chaining
     */
    // @zh: 累积收敛轮数
    // @zh: rounds 连续多少次快照不变即认为收敛；0 由节点层钳制为默认 5 次
    // @zh: return 自身链式引用
    DdsDebugTypeBuilder& rounds(uint32_t rounds)
    {
        m_stableRounds = rounds;
        return *this;
    }
    /**
     * @brief Accumulate the discovery snapshot polling interval
     * @param[in] intervalMs Polling interval in milliseconds; 0 is clamped by the
     *            node layer to the default 200ms
     * @return Reference to self for chaining
     */
    // @zh: 累积发现快照轮询间隔
    // @zh: intervalMs 轮询间隔（毫秒）；0 由节点层钳制为默认 200ms
    // @zh: return 自身链式引用
    DdsDebugTypeBuilder& interval(uint32_t intervalMs)
    {
        m_intervalMs = intervalMs;
        return *this;
    }
    /**
     * @brief Reverse-lookup topics by data type name, exact match (request terminator)
     * @return Response carrying a StringArray packet, one topicName per line sorted by
     *         name; Invalid when YomkServer is not initialized
     * @note Independent convergence query (rounds()/interval(), 0 clamped to 5 rounds /
     *       200ms); fails when no topic matches the type, which surfaces type-name typos
     */
    // @zh: 按数据类型名反查主题列表，精确匹配（请求终结）
    // @zh: return Response 携带 StringArray 包，每行一个主题名（按名排序）；未初始化返回 Invalid
    // @zh: note 独立收敛查询（rounds()/interval()，0 钳制为 5 次/200ms）；
    // @zh: 无匹配主题返回错误，可据此发现类型名拼写错误
    yomk::payload::Response find_topics()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsDebugTopicFind)
            .msg(yomk::msgs::DDSDebugFind::shared(
                yomk::payload::DDSDebugFind{std::move(m_typeName), m_stableRounds, m_intervalMs}))
            .request();
    }
    /**
     * @brief Dump the IDL struct description of the accumulated data type (request terminator)
     * @return Response carrying a StringArray packet of IDL source lines (struct header,
     *         one line per field, closing brace); Invalid when YomkServer is not
     *         initialized
     * @note Pure type introspection through TypeInformation -> TypeObject ->
     *       DynamicType, no subscription is created; only top-level struct types are
     *       supported, and the peer must publish XTypes TypeObject data (ROS2
     *       rmw_fastrtps endpoints do not). Same convergence semantics as
     *       find_topics(): rounds()/interval(), 0 clamped to 5 rounds / 200ms
     */
    // @zh: 按累积的类型名输出其 IDL 结构描述（请求终结）
    // @zh: return Response 携带 StringArray 包，多行 IDL 源语法（struct 头 + 字段行 + 结尾）；未初始化返回 Invalid
    // @zh: note 经 TypeInformation → TypeObject → DynamicType 纯类型内省，不建订阅；
    // @zh: 仅支持顶层为 struct 的类型，且需对端提供 XTypes TypeObject（ROS2 rmw_fastrtps 端点不提供）；
    // @zh: 收敛语义同 find_topics()：rounds()/interval()，0 钳制为 5 次/200ms
    yomk::payload::Response interface_show()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsDebugInterfaceShow)
            .msg(yomk::msgs::DDSDebugInterfaceShow::shared(
                yomk::payload::DDSDebugInterfaceShow{std::move(m_typeName), m_stableRounds, m_intervalMs}))
            .request();
    }
};

/**
 * @brief Debug facet: carries the optional domain()/udp_ds()/tcp_ds() creation
 *        accumulators plus the rounds()/interval()/with_type() query accumulators
 *        (order-free), terminated by create()/quit() or by the query terminators
 *        topics()/interfaces(), or hands over to the topic()/type() sub-facets; the
 *        remaining query/node sub-facets join it in later rounds
 */
// @zh: 调试分面：承载可选的 domain()/udp_ds()/tcp_ds() 建点累积器与 rounds()/interval()/with_type()
// @zh: 查询累积器（次序不限），create()/quit() 或查询终结 topics()/interfaces() 收束，或交出 topic()/type() 子分面；
// @zh: 其余查询/node 子分面在后续迁移批次并入
class DdsDebugBuilder
{
    uint32_t m_domainId = yomk::payload::YomkDdsDefaultDomainId();                               // 缺省读 YOMKDDS_DDS_DOMAIN_ID
    std::string m_udpDiscoverServerLocator = yomk::payload::YomkDdsDefaultUdpDiscoverServers();  // 缺省经总开关读 YOMKDDS_UDP_DISCOVER_SERVERS
    std::string m_tcpDiscoverServerLocator = yomk::payload::YomkDdsDefaultTcpDiscoverServers();  // 缺省经总开关读 YOMKDDS_TCP_DISCOVER_SERVERS
    uint32_t m_stableRounds = 0;                                                                 // 0 由节点层钳制为默认 5 次
    uint32_t m_intervalMs = 0;                                                                   // 0 由节点层钳制为默认 200ms
    bool m_types = false;                                                                        // 查询分面 types 标志：with_type() 置真
public:
    /**
     * @brief Accumulate the DDS domain id
     * @param[in] domainId DDS domain id [0,232]
     * @return Reference to self for chaining
     */
    // @zh: 累积 DDS 域号（调试节点仅可观察同域端点）
    // @zh: domainId DDS 域号 [0,232]
    // @zh: return 自身链式引用
    DdsDebugBuilder& domain(uint32_t domainId)
    {
        m_domainId = domainId;
        return *this;
    }
    /**
     * @brief Accumulate the UDP channel discovery-server locator aggregate
     * @param[in] locator "ip[:port]" aggregate, semicolon-separated for multiple servers
     * @return Reference to self for chaining
     */
    // @zh: 累积 UDP 通道发现服务器直配聚合串（语义同核心套件建点分面）
    // @zh: locator "ip[:port]" 聚合串，多 server 分号分隔
    // @zh: return 自身链式引用
    DdsDebugBuilder& udp_ds(std::string locator)
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
    DdsDebugBuilder& tcp_ds(std::string locator)
    {
        m_tcpDiscoverServerLocator = std::move(locator);
        return *this;
    }
    /**
     * @brief Accumulate the convergence rounds of the query terminator
     * @param[in] rounds Consecutive unchanged-snapshot rounds required; 0 is
     *            clamped to the default 5 by the node layer
     * @return Reference to self for chaining
     */
    // @zh: 累积查询终结的收敛轮次
    // @zh: rounds 连续快照不变所需轮次；0 由节点层钳制为默认 5 次
    // @zh: return 自身链式引用
    DdsDebugBuilder& rounds(uint32_t rounds)
    {
        m_stableRounds = rounds;
        return *this;
    }
    /**
     * @brief Accumulate the snapshot polling interval of the query terminator
     * @param[in] intervalMs Polling interval in milliseconds; 0 is clamped to the
     *            default 200ms by the node layer
     * @return Reference to self for chaining
     */
    // @zh: 累积查询终结的快照轮询间隔
    // @zh: intervalMs 轮询间隔（毫秒）；0 由节点层钳制为默认 200ms
    // @zh: return 自身链式引用
    DdsDebugBuilder& interval(uint32_t intervalMs)
    {
        m_intervalMs = intervalMs;
        return *this;
    }
    /**
     * @brief Enable the type-name mode of the query terminator ("topic [type]" per line)
     * @return Reference to self for chaining
     * @note Boolean switch — no argument; the types flag of the discovery query
     */
    // @zh: 开启查询终结的类型名模式（每行 "主题名 [类型名]"）
    // @zh: return 自身链式引用
    // @zh: note 布尔开关，无参；对应发现查询的 types 标志
    DdsDebugBuilder& with_type()
    {
        m_types = true;
        return *this;
    }
    /**
     * @brief Create the debug node, one type-agnostic domain observer per process (request terminator)
     * @return Response; invalid locator input or out-of-range domainId fails with the reason echoed back
     * @note Single node model: at most one per process, delete it before re-creating.
     *       Unset accumulators keep the environment-variable defaults
     */
    // @zh: 创建调试节点（类型无关域观察者，请求终结）
    // @zh: return Response；domainId 越界 [0,232] 或直配串非法创建失败并回显原因
    // @zh: note 单节点模型：一个进程至多一个，重复创建须先删除；未累积项保持环境变量缺省
    yomk::payload::Response create()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsDebugCreateNode)
            .msg(yomk::msgs::DDSDebugNode::shared(
                yomk::payload::DDSDebugNode{m_domainId, std::move(m_udpDiscoverServerLocator), std::move(m_tcpDiscoverServerLocator)}))
            .request();
    }
    /**
     * @brief Delete the debug node and destroy all its DDS entities (request terminator)
     * @return Response; fails when the debug node was not created
     * @note Destroy counterpart of create() on the same facet; named quit() rather
     *       than remove() because the debug node carries no name, which keeps the
     *       entry clear of the core suite's node(nodeName).remove() facet
     */
    // @zh: 删除调试节点并销毁其全部 DDS 实体（请求终结）
    // @zh: return Response；未创建时返回错误
    // @zh: note 与 create() 同分面的销毁对；命名 quit() 而非 remove()——调试节点无名，
    // @zh: 避免与核心套件 node(nodeName).remove() 入口混同
    yomk::payload::Response quit()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsDebugDeleteNode).msg(nullptr).request();
    }
    /**
     * @brief Switch into the topic facet with the observed topic name
     * @param[in] topicName Topic to observe, must not be empty
     * @return Topic facet, terminated by print() or info()
     */
    // @zh: 切入主题分面并累积被观察的主题名
    // @zh: topicName 被观察主题名，不可为空
    // @zh: return 主题分面，经 print() 或 info() 终结
    DdsDebugTopicBuilder topic(std::string topicName)
    {
        return DdsDebugTopicBuilder(std::move(topicName));
    }
    /**
     * @brief Switch into the type facet with the queried data type name
     * @param[in] typeName Data type name to reverse-look up, exact match
     * @return Type facet, terminated by find_topics()
     */
    // @zh: 切入类型分面并累积被反查的数据类型名
    // @zh: typeName 反查用数据类型名，精确匹配
    // @zh: return 类型分面，经 find_topics() 终结
    DdsDebugTypeBuilder type(std::string typeName)
    {
        return DdsDebugTypeBuilder(std::move(typeName));
    }
    /**
     * @brief List discovered topics in the domain, sorted (request terminator)
     * @return Response carrying a StringArray packet, one line per topic; names only
     *         unless with_type() is accumulated, in which case each line is
     *         "topicName [typeName]"; remote writers and readers both recorded, the
     *         list may be empty
     * @note Convergence query: polls discovery snapshots every interval() until
     *       rounds() consecutive snapshots are unchanged; unset accumulators are
     *       clamped to 5 rounds / 200ms. Independent of the topic() sub-facet
     */
    // @zh: 列出域内已发现的主题（按名排序，请求终结）
    // @zh: return Response 携带 StringArray 包，每行一个主题；未累积 with_type() 时仅主题名，
    // @zh: 否则每行为 "主题名 [类型名]"；远端 writer 与 reader 均记录，列表可为空
    // @zh: note 收敛查询：每 interval() 轮询一次发现快照，连续 rounds() 次不变即返回；
    // @zh: 未累积时钳制为默认 5 次/200ms；与 topic() 子分面互不依赖
    yomk::payload::Response topics()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsDebugListTopics)
            .msg(yomk::msgs::DDSDebugList::shared(
                yomk::payload::DDSDebugList{m_stableRounds, m_intervalMs, m_types}))
            .request();
    }
    /**
     * @brief List all discovered data type names, deduplicated and sorted (request terminator)
     * @return Response carrying a StringArray packet, one typeName per line; Invalid when
     *         YomkServer is not initialized
     * @note Independent convergence query (rounds()/interval(), 0 clamped to 5 rounds /
     *       200ms), the navigation companion of type(name).interface_show(); fails when
     *       the domain holds no type at all
     */
    // @zh: 列出发现缓存中出现的全部数据类型名（去重字典序，请求终结）
    // @zh: return Response 携带 StringArray 包，每行一个类型名；未初始化返回 Invalid
    // @zh: note 独立收敛查询（rounds()/interval()，0 钳制为 5 次/200ms），
    // @zh: 是 type(name).interface_show() 的导航配套；域内无任何类型返回错误
    yomk::payload::Response interfaces()
    {
        if (!requireServer())
        {
            return yomk::payload::Response(yomk::payload::ResStatus::Invalid, "YomkServer is not initialized");
        }
        return yomk::server().url(yomk::url::ddsDebugInterfaceList)
            .msg(yomk::msgs::DDSDebugInterfaceList::shared(
                yomk::payload::DDSDebugInterfaceList{m_stableRounds, m_intervalMs}))
            .request();
    }
};

/**
 * @brief Domain entry builder: core communication and debug facets live here during the macro-to-chain migration
 */
// @zh: 域入口 Builder：宏链迁移期内核心通信与调试分面挂载于此
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
     * @brief Switch into the debug facet (type-agnostic domain observer)
     * @return Debug facet, terminated by create()/quit() or by the query terminator topics()
     * @note The debug node itself carries no name — node(name) on that facet
     *       refers to remote participants of the domain in later rounds
     */
    // @zh: 切入调试分面（类型无关域观察者）
    // @zh: return 调试分面，经 create()/quit() 或查询终结 topics() 收束
    // @zh: note 调试节点自身无名——该分面上的 node(name) 指域内远端参与者（后续批次并入）
    DdsDebugBuilder debug()
    {
        return DdsDebugBuilder{};
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
 * Every macro returns yomk::payload::Response and requires a debug node created
 * first via yomk::dds().debug()[.domain/.udp_ds/.tcp_ds()].create(), deleted via
 * yomk::dds().debug().quit(); topic registration is chained as
 * yomk::dds().debug().topic(name).out_cb(cb)[.qos(qos)].print(), the topic list as
 * yomk::dds().debug()[.rounds(n)][.interval(ms)][.with_type()].topics(), and the
 * single-topic details as
 * yomk::dds().debug().topic(name)[.rounds(n)][.interval(ms)][.verbose(true)].info(), and the
 * reverse lookup as yomk::dds().debug().type(name)[.rounds(n)][.interval(ms)].find_topics(),
 * and the IDL struct dump as
 * yomk::dds().debug().type(name)[.rounds(n)][.interval(ms)].interface_show(), and the
 * type-name list as yomk::dds().debug()[.rounds(n)][.interval(ms)].interfaces().
 * list/info/find share the convergence semantics: consecutive stableRounds
 * unchanged snapshots, 0 clamped to defaults 5 rounds / 200ms. Full behavior:
 * docs/features/debug-service.md
 */
// @zh: 调试宏（YomkDdsDebugService 端点，等价 CLI：yomkdds）
// @zh: 均返回 yomk::payload::Response，且须先经
// @zh: yomk::dds().debug()[.domain/.udp_ds/.tcp_ds()].create() 创建调试节点，
// @zh: 退出经 yomk::dds().debug().quit()；主题登记已链式化为
// @zh: yomk::dds().debug().topic(name).out_cb(cb)[.qos(qos)].print()，主题清单为
// @zh: yomk::dds().debug()[.rounds(n)][.interval(ms)][.with_type()].topics()，单主题详情为
// @zh: yomk::dds().debug().topic(name)[.rounds(n)][.interval(ms)][.verbose(true)].info()，类型反查为
// @zh: yomk::dds().debug().type(name)[.rounds(n)][.interval(ms)].find_topics()，类型内省为
// @zh: yomk::dds().debug().type(name)[.rounds(n)][.interval(ms)].interface_show()，类型清单为
// @zh: yomk::dds().debug()[.rounds(n)][.interval(ms)].interfaces()；
// @zh: list/info/find 组共用收敛语义：
// @zh: 连续 stableRounds 次快照不变即返回，0 钳制为默认 5 次/200ms；详见 docs/features/debug-service.md

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
