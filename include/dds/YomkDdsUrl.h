#pragma once
#include <string_view>

/**
 * @brief Service endpoint URL constants for the YomkDds extension
 *
 * Grouped per service: core communication (YomkDdsHandler), type-agnostic
 * debugging (YomkDdsDebugHandler), bag record/replay (YomkDdsBagHandler).
 * Endpoint behavior is documented by the YOMKDDS_* macros in YomkDdsAPI.h.
 *
 * @note This extension's constants are grouped under yomk::url::dds (the
 *       yomk::url root itself is open — other extensions append their own
 *       domains there). Request chains must reference these constants
 *       instead of raw URL literals.
 */
// @zh: YomkDds 扩展服务端点 URL 常量（归组于 yomk::url::dds 子命名空间——yomk::url
// @zh: 根本身开放，其他扩展在同风格下追加各自的域）；请求链一律引用常量，不裸写 URL 字面量
namespace yomk::url::dds
{
// --- YomkDdsHandler ---

/** @brief Extension-wide version query endpoint (shared by all three suites: one library, one version string) */
// @zh: 全扩展版本查询端点（三套件共用——同一库同一版本串，root yomk::dds().version() 指向此端点）
inline constexpr std::string_view GetVersion = "/YomkDdsHandler/get_version";

/** @brief Fast DDS XML profiles load endpoint */
// @zh: 加载 Fast DDS XML profiles 端点
inline constexpr std::string_view LoadXml = "/YomkDdsHandler/load_xml";

/** @brief Create a DDS node endpoint */
// @zh: 创建 DDS 节点端点
inline constexpr std::string_view CreateNode = "/YomkDdsHandler/create_node";

/** @brief Delete a DDS node endpoint */
// @zh: 删除 DDS 节点端点
inline constexpr std::string_view DeleteNode = "/YomkDdsHandler/delete_node";

/** @brief Register a publisher topic endpoint */
// @zh: 注册发布主题端点
inline constexpr std::string_view RegisterPubTopic = "/YomkDdsHandler/register_pub_topic";

/** @brief Register a subscriber topic endpoint */
// @zh: 注册订阅主题端点
inline constexpr std::string_view RegisterSubTopic = "/YomkDdsHandler/register_sub_topic";

/** @brief Publish one message endpoint */
// @zh: 发布消息端点
inline constexpr std::string_view Publish = "/YomkDdsHandler/publish";

/** @brief Loan a publish buffer (zero-copy) endpoint */
// @zh: 借出发布缓冲（零拷贝）端点
inline constexpr std::string_view Loan = "/YomkDdsHandler/loan";

/** @brief Discard an un-published loaned buffer endpoint */
// @zh: 归还未发布的借出缓冲端点
inline constexpr std::string_view DiscardLoan = "/YomkDdsHandler/discard_loan";

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用；格式惯例见 skills/yomkserver-modular/reference.md「installFunc / weakFunc」）
inline constexpr const char* GetVersionDesc = "Semantic: 返回扩展版本串（三套件共用——同一库同一版本串）；Returns: String 包";
inline constexpr const char* LoadXmlDesc =
        "Semantic: 加载 Fast DDS XML profiles 文件并缓存 raw 串（节点层 QoS 提取链吃 raw 串而非文件路径）；Returns: data 无负载；"
        "Failed=路径空/文件不可读/文件为空/load_XML_profiles_file 失败（rc 整值回显便于对照官方错误码）";
inline constexpr const char* CreateNodeDesc =
        "Semantic: 创建命名 DDS 节点（domainId [0,232]；应用已加载 XML 缓存与 UDP/TCP discovery server 直配，空串=不直配）；Returns: data 无负载；"
        "Failed=节点名空/domainId 越界/节点重名/setDomainId 失败（非法 serverSpec 项回显于 msg）";
inline constexpr const char* DeleteNodeDesc = "Semantic: 按名称删除节点并释放资源；Returns: data 无负载；Failed=节点不存在";
inline constexpr const char* RegisterPubTopicDesc =
        "Semantic: 在节点注册发布主题；type 所有权无条件接管（caller new、服务全路径负责释放，成败皆然）；Returns: data 无负载；"
        "Failed=节点不存在/注册失败";
inline constexpr const char* RegisterSubTopicDesc =
        "Semantic: 在节点注册订阅主题（携带逐条回调）；type 所有权无条件接管（caller new、服务全路径负责释放，成败皆然）；Returns: data 无负载；"
        "Failed=节点不存在/注册失败";
inline constexpr const char* PublishDesc =
        "Semantic: 经节点同步发布一条消息（data 借用非所有权，写入后归 caller 管理）；Returns: data 无负载；Failed=节点不存在/发布失败";
inline constexpr const char* LoanDesc =
        "Semantic: 从 writer 池借出发布缓冲（零拷贝；非 plain 类型或池耗尽即失败，回退普通发布）；Returns: LoanSample 包（sample 从池借出，"
        "caller 经 /publish 发布或 /discard_loan 归还交回中间件）；Failed=节点不存在/借出失败";
inline constexpr const char* DiscardLoanDesc =
        "Semantic: 归还未发布的借出缓冲（sample 从 writer 池借出，归还即交回中间件）；Returns: data 无负载；Failed=节点不存在/归还失败";

// --- YomkDdsDebugHandler ---

/** @brief Create the debug node endpoint (single node per process) */
// @zh: 创建调试节点端点（单节点模型）
inline constexpr std::string_view DebugCreateNode = "/YomkDdsDebugHandler/create_node";

/** @brief Quit debugging and delete the debug node endpoint */
// @zh: 退出调试（删除调试节点）端点
inline constexpr std::string_view DebugDeleteNode = "/YomkDdsDebugHandler/delete_node";

/** @brief Register a topic-print observer endpoint */
// @zh: 登记调试主题（逐条投递回调）端点
inline constexpr std::string_view DebugPrintTopic = "/YomkDdsDebugHandler/print_topic";

/** @brief List discovered topics in the domain endpoint */
// @zh: 列出域内主题端点
inline constexpr std::string_view DebugListTopics = "/YomkDdsDebugHandler/list_topics";

/** @brief Query topic discovery details endpoint */
// @zh: 查询主题详情端点
inline constexpr std::string_view DebugGetTopicInfo = "/YomkDdsDebugHandler/get_topic_info";

/** @brief Reverse-lookup topics by data type name endpoint */
// @zh: 按数据类型反查主题端点
inline constexpr std::string_view DebugFindTopics = "/YomkDdsDebugHandler/find_topics";

/** @brief Dump IDL struct of a data type endpoint */
// @zh: 按类型名输出 IDL 结构端点
inline constexpr std::string_view DebugShowInterface = "/YomkDdsDebugHandler/show_interface";

/** @brief List all discovered data type names endpoint */
// @zh: 列出全部类型名端点
inline constexpr std::string_view DebugListInterfaces = "/YomkDdsDebugHandler/list_interfaces";

/** @brief Query a publishable example for a topic endpoint */
// @zh: 查询主题发布示例端点
inline constexpr std::string_view DebugGetTopicExample = "/YomkDdsDebugHandler/get_topic_example";

/** @brief Query the message description (type name + JSON template) endpoint */
// @zh: 查询消息描述两要素端点
inline constexpr std::string_view DebugGetTopicMsg = "/YomkDdsDebugHandler/get_topic_msg";

/** @brief Publish a JSON payload to a topic endpoint */
// @zh: 发布 JSON 载荷消息端点
inline constexpr std::string_view DebugStartPub = "/YomkDdsDebugHandler/start_pub";

/** @brief Stop a repeating topic_pub session endpoint (protocol bridge to the ctrl flag) */
// @zh: 停止持续发布会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view DebugStopPub = "/YomkDdsDebugHandler/stop_pub";

/** @brief List named participants in the domain endpoint */
// @zh: 列出域内节点端点
inline constexpr std::string_view DebugListNodes = "/YomkDdsDebugHandler/list_nodes";

/** @brief Query pub/sub topic lists of a named participant endpoint */
// @zh: 查询节点发布/订阅清单端点
inline constexpr std::string_view DebugGetNodeInfo = "/YomkDdsDebugHandler/get_node_info";

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用）
inline constexpr const char* DebugCreateNodeDesc =
        "Semantic: 创建调试节点（单节点模型；domainId [0,232]；应用已加载 XML 缓存与 UDP/TCP discovery server 直配，空串=不直配）；Returns: data 无负载；"
        "Failed=domainId 越界/节点已存在/setDomainId 失败";
inline constexpr const char* DebugDeleteNodeDesc =
        "Semantic: 删除调试节点并按节点层顺序清理（停工作线程 → reader → topic → subscriber → participant）；Returns: data 无负载；Failed=节点未建";
inline constexpr const char* DebugPrintTopicDesc =
        "Semantic: 登记主题观察（output 回调经 setOutputSink 原样转交节点，须先于订阅就位，逐条投递收到的消息）；Returns: data 无负载；"
        "Failed=回调空/节点未建/订阅失败（拒绝原因由节点层归类透传）";
inline constexpr const char* DebugListTopicsDesc =
        "Semantic: 列出域内已发现主题（topicName+typeName；收敛等待约 stableRounds*intervalMs）；Returns: TopicInfoList 包";
inline constexpr const char* DebugGetTopicInfoDesc =
        "Semantic: 查询单主题详情（含端点详情，展示与否由调用方决定）；Returns: TopicInfo 包；Failed=节点未建/主题未发现";
inline constexpr const char* DebugFindTopicsDesc =
        "Semantic: 按数据类型名反查主题名清单（find 族语义：无匹配按失败，便于暴露类型名拼写错误）；Returns: StringArray 包；"
        "Failed=节点未建/类型无匹配主题";
inline constexpr const char* DebugShowInterfaceDesc =
        "Semantic: 按类型名经 TypeObject 反射采集 IDL 结构（find 族语义：类型未出现按失败）；Returns: InterfaceInfo 包；"
        "Failed=节点未建/类型未发现/TypeObject 不可得/顶层非 struct/反射采集失败（原因由节点层归类透传）";
inline constexpr const char* DebugListInterfacesDesc =
        "Semantic: 列出域内全部已发现数据类型名（字典序去重；find 族语义：空结果按失败，便于暴露域号或收敛参数配置错误）；Returns: StringArray 包；"
        "Failed=节点未建/域内无任何类型";
inline constexpr const char* DebugGetTopicExampleDesc =
        "Semantic: 查询主题发布示例（info 族语义：主题未发现按失败）；Returns: TopicExampleInfo 包（段头/IDL 语法/发布命令由调用方渲染）；"
        "Failed=节点未建/主题未发现/类型侧/示例生成失败（原因由节点层按步归类透传）";
inline constexpr const char* DebugGetTopicMsgDesc =
        "Semantic: 查询消息描述两要素（类型名+JSON 模板；info 族语义：主题未发现按失败）；Returns: TopicMessageInfo 包；"
        "Failed=节点未建/主题未发现/JSON 模板生成失败（原因由节点层按步归类透传）";
inline constexpr const char* DebugStartPubDesc = R"(
Semantic: 向主题发布 JSON 载荷消息；repeatIntervalMs>0 为持续发布模式，周期发送至 g_debugPubStop 置位（/stop_pub 或 Ctrl+C）或发满 maxTimes 条
Returns: TopicPublishInfo 包（发布计数含首轮，单次发布同样非空）
Precond: 调试节点已创建；主题类型已出现在域内
Idempotent: false —— 每次调用都真实发布
SideEffect: 持锁长驻阻塞（repeat 模式期间本服务其他带锁端点串行化）；向域内发布数据
Fail: Failed=节点未建/发布失败（原因由节点层归类透传；期望建匹配等待期被置位中断同样报错返回）
)";
inline constexpr const char* DebugStopPubDesc =
        "Semantic: 协议桥接置位进程内控制面标志 g_debugPubStop（无锁直置，绝不取 mtx_——start_pub 持锁长驻阻塞期间本端点必须可调度）；"
        "Returns: data 无负载，msg \"stop requested\"；被阻塞的 start_pub 随之收尾带回发布计数";
inline constexpr const char* DebugListNodesDesc =
        "Semantic: 列出域内命名参与者（空列表合法——域内暂无命名参与者）；Returns: StringArray 包";
inline constexpr const char* DebugGetNodeInfoDesc =
        "Semantic: 查询命名参与者的发布/订阅主题清单；Returns: NodeInfo 包；Failed=节点未建/参与者未发现";

// --- YomkDdsBagHandler ---

/** @brief Create the bag node endpoint (single node per process) */
// @zh: 创建 bag 节点端点（单节点模型）
inline constexpr std::string_view BagCreateNode = "/YomkDdsBagHandler/create_node";

/** @brief Quit recording and delete the bag node endpoint */
// @zh: 退出录制（删除 bag 节点）端点
inline constexpr std::string_view BagDeleteNode = "/YomkDdsBagHandler/delete_node";

/** @brief Record topics to an mcap bag endpoint (blocking) */
// @zh: 录制主题端点（阻塞）
inline constexpr std::string_view BagRecord = "/YomkDdsBagHandler/record";

/** @brief Show bag metadata endpoint */
// @zh: 查看 bag 元信息端点
inline constexpr std::string_view BagGetInfo = "/YomkDdsBagHandler/get_info";

/** @brief Rebuild bag metadata endpoint */
// @zh: 重建 bag 元信息端点
inline constexpr std::string_view BagReindex = "/YomkDdsBagHandler/reindex";

/** @brief Convert/merge bags endpoint */
// @zh: 转换 bag 端点
inline constexpr std::string_view BagConvert = "/YomkDdsBagHandler/convert";

/** @brief Replay an mcap bag endpoint (blocking) */
// @zh: 回放 bag 端点（阻塞）
inline constexpr std::string_view BagPlay = "/YomkDdsBagHandler/play";

/** @brief Stop a recording session endpoint (protocol bridge to the ctrl flag) */
// @zh: 停止录制会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view BagStopRecord = "/YomkDdsBagHandler/stop_record";

/** @brief Pause a recording session endpoint (protocol bridge to the ctrl flag) */
// @zh: 暂停录制会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view BagPauseRecord = "/YomkDdsBagHandler/pause_record";

/** @brief Resume a paused recording session endpoint (protocol bridge to the ctrl flag) */
// @zh: 恢复暂停的录制会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view BagResumeRecord = "/YomkDdsBagHandler/resume_record";

/** @brief Stop a playing session endpoint (protocol bridge to the ctrl flag) */
// @zh: 停止回放会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view BagStopPlay = "/YomkDdsBagHandler/stop_play";

/** @brief Pause a playing session endpoint (protocol bridge to the ctrl flag) */
// @zh: 暂停回放会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view BagPausePlay = "/YomkDdsBagHandler/pause_play";

/** @brief Resume a paused playing session endpoint (protocol bridge to the ctrl flag) */
// @zh: 恢复暂停的回放会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view BagResumePlay = "/YomkDdsBagHandler/resume_play";

/** @brief Set the playback rate endpoint (protocol bridge to the ctrl flag) */
// @zh: 设置回放速率端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view BagSetPlayRate = "/YomkDdsBagHandler/set_play_rate";

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用）
inline constexpr const char* BagCreateNodeDesc =
        "Semantic: 创建 bag 节点（单节点模型；domainId [0,232]；应用已加载 XML 缓存与 UDP/TCP discovery server 直配，空串=不直配）；Returns: data 无负载；"
        "Failed=domainId 越界/节点已存在/setDomainId 失败";
inline constexpr const char* BagDeleteNodeDesc =
        "Semantic: 删除 bag 节点并按节点层顺序清理（reader → data → topic → subscriber → participant）；Returns: data 无负载；Failed=节点未建";
inline constexpr const char* BagRecordDesc = R"(
Semantic: 录制主题至 mcap bag（长驻阻塞至 g_bagRecordStop 置位——/stop_record 或 SIGINT——触发节点层收尾返回；/pause_record 冻结订阅写路径）
Returns: RecordResult 包（bag 目录+逐主题统计，通配项已展开，保持录制清单顺序）
Precond: bag 节点已创建；topics 非空且无空主题名
Idempotent: false —— 每次调用产生新录制会话
SideEffect: 持锁长驻阻塞（录制期间本服务其他带锁端点串行化）；向输出目录写 bag 文件
Fail: Failed=topics 空/含空主题名/节点未建/启动校验失败（无端点主题，节点层逐主题列于 msg）
)";
inline constexpr const char* BagGetInfoDesc =
        "Semantic: 读取 bag 元信息（metadata.json 字段+目录递归总字节；静态纯文件操作，不依赖节点不取锁）；Returns: BagInfo 包（行标签/单位/人类可读时间由调用方渲染）；"
        "Failed=bagDir 空/读取失败";
inline constexpr const char* BagReindexDesc =
        "Semantic: 重建 bag 元信息（静态纯文件操作，不依赖节点不取锁）；Returns: data 无负载；Failed=bagDir 空/重建失败";
inline constexpr const char* BagConvertDesc =
        "Semantic: 按 output_bags 配置转换/合并 bag（静态纯文件操作，不依赖节点不取锁）；Returns: ConvertResult 包（逐输出 bag 统计，保持配置序）；"
        "Failed=inputs 空/cfgPath 空/转换失败";
inline constexpr const char* BagPlayDesc = R"(
Semantic: 回放 mcap bag（长驻阻塞至 EOS 或 g_bagPlayStop 置位——/stop_play 或 SIGINT——触发节点层收尾返回；/pause_play 冻结时间轴）
Returns: PlayResult 包（bag 目录+逐主题统计，bag 预扫序，topicName 为发布名）
Precond: bag 节点已创建；bagDir 非空；playbackRate>0；startOffset/delay 非负
Idempotent: false —— 每次调用都真实发布 bag 数据
SideEffect: 持锁长驻阻塞（回放期间本服务其他带锁端点串行化）；向域内发布 bag 数据
Fail: Failed=bagDir 空/playbackRate<=0/startOffset<0/delay<0/节点未建/预扫失败（目录不存在/旧 bag 拒播/qos 非法/topics 未命中 bag 清单，节点层列于 msg）
)";
inline constexpr const char* BagStopRecordDesc =
        "Semantic: 协议桥接置位进程内控制面标志 g_bagRecordStop（无锁直置，绝不取 mtx_——/record 持锁长驻阻塞期间本端点必须可调度）；"
        "Returns: data 无负载，msg \"stop requested\"；录制循环轮询到置位即收尾返回并带回逐主题统计";
inline constexpr const char* BagPauseRecordDesc =
        "Semantic: 置位 g_bagRecordPaused 冻结订阅写路径（无锁直置同 stop_record；恢复由 /resume_record 语义侧控制）；"
        "Returns: data 无负载，msg \"pause requested\"";
inline constexpr const char* BagResumeRecordDesc =
        "Semantic: 恢复 g_bagRecordPaused，订阅写路径继续落盘（无锁直置同 stop_record）；Returns: data 无负载，msg \"resume requested\"";
inline constexpr const char* BagStopPlayDesc =
        "Semantic: 协议桥接置位进程内控制面标志 g_bagPlayStop（无锁直置，绝不取 mtx_——/play 持锁长驻阻塞期间本端点必须可调度）；"
        "Returns: data 无负载，msg \"stop requested\"；回放线程在各阻塞处轮询到置位即收尾返回并带回逐主题统计";
inline constexpr const char* BagPausePlayDesc =
        "Semantic: 置位 g_bagPlayPaused 冻结时间轴（无锁直置同 stop_play；读线程被队列满反压自然停住）；Returns: data 无负载，msg \"pause requested\"";
inline constexpr const char* BagResumePlayDesc =
        "Semantic: 恢复 g_bagPlayPaused 并重锚时间基准（无锁直置同 stop_play；对齐 ros2 PlayerClock 语义）；Returns: data 无负载，msg \"resume requested\"";
inline constexpr const char* BagSetPlayRateDesc =
        "Semantic: 经 bagPlaySetRate 设置回放速率（无锁直置同 stop_play；rate<=0 由节点层校验拒绝并保持原值）；Returns: data 无负载，msg \"rate set\"；"
        "Failed=rate<=0";
}  // namespace yomk::url::dds
