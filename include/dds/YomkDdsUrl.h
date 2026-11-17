#pragma once
#include <string_view>

/**
 * @brief Service endpoint URL constants for the YomkDds extension
 *
 * Grouped per service: core communication (YomkDdsHandler), type-agnostic
 * debugging (YomkDdsDebugHandler), bag record/replay (YomkDdsBagHandler).
 * Endpoint behavior is documented by the YOMKDDS_* macros in YomkDdsAPI.h.
 *
 * @note yomk::url is an open namespace: extensions append constants in the
 *       same style. Request chains must reference these constants instead of
 *       raw URL literals.
 */
// @zh: YomkDds 扩展服务端点 URL 常量
// @zh: 按服务分组——核心通信、类型无关调试、Bag 录制回放；端点行为详见 YomkDdsAPI.h 各宏
// @zh: note yomk::url 为开放命名空间，扩展按同风格追加；请求链一律引用常量，不裸写 URL 字面量
namespace yomk::url
{
// --- YomkDdsHandler ---

/** @brief Extension-wide version query endpoint (shared by all three suites: one library, one version string) */
// @zh: 全扩展版本查询端点（三套件共用——同一库同一版本串，root yomk::dds().version() 指向此端点）
inline constexpr std::string_view ddsGetVersion = "/YomkDdsHandler/get_version";

/** @brief Fast DDS XML profiles load endpoint */
// @zh: 加载 Fast DDS XML profiles 端点
inline constexpr std::string_view ddsLoadXml = "/YomkDdsHandler/load_xml";

/** @brief Create a DDS node endpoint */
// @zh: 创建 DDS 节点端点
inline constexpr std::string_view ddsCreateNode = "/YomkDdsHandler/create_node";

/** @brief Delete a DDS node endpoint */
// @zh: 删除 DDS 节点端点
inline constexpr std::string_view ddsDeleteNode = "/YomkDdsHandler/delete_node";

/** @brief Register a publisher topic endpoint */
// @zh: 注册发布主题端点
inline constexpr std::string_view ddsRegisterPubTopic = "/YomkDdsHandler/register_pub_topic";

/** @brief Register a subscriber topic endpoint */
// @zh: 注册订阅主题端点
inline constexpr std::string_view ddsRegisterSubTopic = "/YomkDdsHandler/register_sub_topic";

/** @brief Publish one message endpoint */
// @zh: 发布消息端点
inline constexpr std::string_view ddsPublish = "/YomkDdsHandler/publish";

/** @brief Loan a publish buffer (zero-copy) endpoint */
// @zh: 借出发布缓冲（零拷贝）端点
inline constexpr std::string_view ddsLoan = "/YomkDdsHandler/loan";

/** @brief Discard an un-published loaned buffer endpoint */
// @zh: 归还未发布的借出缓冲端点
inline constexpr std::string_view ddsDiscardLoan = "/YomkDdsHandler/discard_loan";

// --- YomkDdsDebugHandler ---

/** @brief Create the debug node endpoint (single node per process) */
// @zh: 创建调试节点端点（单节点模型）
inline constexpr std::string_view ddsDebugCreateNode = "/YomkDdsDebugHandler/create_node";

/** @brief Quit debugging and delete the debug node endpoint */
// @zh: 退出调试（删除调试节点）端点
inline constexpr std::string_view ddsDebugDeleteNode = "/YomkDdsDebugHandler/delete_node";

/** @brief Register a topic-print observer endpoint */
// @zh: 登记调试主题（逐条投递回调）端点
inline constexpr std::string_view ddsDebugPrintTopic = "/YomkDdsDebugHandler/print_topic";

/** @brief List discovered topics in the domain endpoint */
// @zh: 列出域内主题端点
inline constexpr std::string_view ddsDebugListTopics = "/YomkDdsDebugHandler/list_topics";

/** @brief Query topic discovery details endpoint */
// @zh: 查询主题详情端点
inline constexpr std::string_view ddsDebugGetTopicInfo = "/YomkDdsDebugHandler/get_topic_info";

/** @brief Reverse-lookup topics by data type name endpoint */
// @zh: 按数据类型反查主题端点
inline constexpr std::string_view ddsDebugFindTopics = "/YomkDdsDebugHandler/find_topics";

/** @brief Dump IDL struct of a data type endpoint */
// @zh: 按类型名输出 IDL 结构端点
inline constexpr std::string_view ddsDebugShowInterface = "/YomkDdsDebugHandler/show_interface";

/** @brief List all discovered data type names endpoint */
// @zh: 列出全部类型名端点
inline constexpr std::string_view ddsDebugListInterfaces = "/YomkDdsDebugHandler/list_interfaces";

/** @brief Query a publishable example for a topic endpoint */
// @zh: 查询主题发布示例端点
inline constexpr std::string_view ddsDebugGetTopicExample = "/YomkDdsDebugHandler/get_topic_example";

/** @brief Query the message description (type name + JSON template) endpoint */
// @zh: 查询消息描述两要素端点
inline constexpr std::string_view ddsDebugGetTopicMsg = "/YomkDdsDebugHandler/get_topic_msg";

/** @brief Publish a JSON payload to a topic endpoint */
// @zh: 发布 JSON 载荷消息端点
inline constexpr std::string_view ddsDebugStartPub = "/YomkDdsDebugHandler/start_pub";

/** @brief Stop a repeating topic_pub session endpoint (protocol bridge to the ctrl flag) */
// @zh: 停止持续发布会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view ddsDebugStopPub = "/YomkDdsDebugHandler/stop_pub";

/** @brief List named participants in the domain endpoint */
// @zh: 列出域内节点端点
inline constexpr std::string_view ddsDebugListNodes = "/YomkDdsDebugHandler/list_nodes";

/** @brief Query pub/sub topic lists of a named participant endpoint */
// @zh: 查询节点发布/订阅清单端点
inline constexpr std::string_view ddsDebugGetNodeInfo = "/YomkDdsDebugHandler/get_node_info";

// --- YomkDdsBagHandler ---

/** @brief Create the bag node endpoint (single node per process) */
// @zh: 创建 bag 节点端点（单节点模型）
inline constexpr std::string_view ddsBagCreateNode = "/YomkDdsBagHandler/create_node";

/** @brief Quit recording and delete the bag node endpoint */
// @zh: 退出录制（删除 bag 节点）端点
inline constexpr std::string_view ddsBagDeleteNode = "/YomkDdsBagHandler/delete_node";

/** @brief Record topics to an mcap bag endpoint (blocking) */
// @zh: 录制主题端点（阻塞）
inline constexpr std::string_view ddsBagRecord = "/YomkDdsBagHandler/record";

/** @brief Show bag metadata endpoint */
// @zh: 查看 bag 元信息端点
inline constexpr std::string_view ddsBagGetInfo = "/YomkDdsBagHandler/get_info";

/** @brief Rebuild bag metadata endpoint */
// @zh: 重建 bag 元信息端点
inline constexpr std::string_view ddsBagReindex = "/YomkDdsBagHandler/reindex";

/** @brief Convert/merge bags endpoint */
// @zh: 转换 bag 端点
inline constexpr std::string_view ddsBagConvert = "/YomkDdsBagHandler/convert";

/** @brief Replay an mcap bag endpoint (blocking) */
// @zh: 回放 bag 端点（阻塞）
inline constexpr std::string_view ddsBagPlay = "/YomkDdsBagHandler/play";

/** @brief Stop a recording session endpoint (protocol bridge to the ctrl flag) */
// @zh: 停止录制会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view ddsBagStopRecord = "/YomkDdsBagHandler/stop_record";

/** @brief Pause a recording session endpoint (protocol bridge to the ctrl flag) */
// @zh: 暂停录制会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view ddsBagPauseRecord = "/YomkDdsBagHandler/pause_record";

/** @brief Resume a paused recording session endpoint (protocol bridge to the ctrl flag) */
// @zh: 恢复暂停的录制会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view ddsBagResumeRecord = "/YomkDdsBagHandler/resume_record";

/** @brief Stop a playing session endpoint (protocol bridge to the ctrl flag) */
// @zh: 停止回放会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view ddsBagStopPlay = "/YomkDdsBagHandler/stop_play";

/** @brief Pause a playing session endpoint (protocol bridge to the ctrl flag) */
// @zh: 暂停回放会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view ddsBagPausePlay = "/YomkDdsBagHandler/pause_play";

/** @brief Resume a paused playing session endpoint (protocol bridge to the ctrl flag) */
// @zh: 恢复暂停的回放会话端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view ddsBagResumePlay = "/YomkDdsBagHandler/resume_play";

/** @brief Set the playback rate endpoint (protocol bridge to the ctrl flag) */
// @zh: 设置回放速率端点（协议桥接到进程内控制面标志）
inline constexpr std::string_view ddsBagSetPlayRate = "/YomkDdsBagHandler/set_play_rate";
}  // namespace yomk::url
