#pragma once
#include <string_view>

/**
 * @brief Service endpoint URL constants for the YomkDds extension
 *
 * Grouped per service: core communication (YomkDdsService), type-agnostic
 * debugging (YomkDdsDebugService), bag record/replay (YomkDdsBagService).
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
// --- YomkDdsService ---

/** @brief Version query endpoint */
// @zh: 版本查询端点
inline constexpr std::string_view ddsVersion = "/YomkDdsService/version";

/** @brief Fast DDS XML profiles load endpoint */
// @zh: 加载 Fast DDS XML profiles 端点
inline constexpr std::string_view ddsLoadXml = "/YomkDdsService/load_xml";

/** @brief Create a DDS node endpoint */
// @zh: 创建 DDS 节点端点
inline constexpr std::string_view ddsCreateNode = "/YomkDdsService/create_node";

/** @brief Delete a DDS node endpoint */
// @zh: 删除 DDS 节点端点
inline constexpr std::string_view ddsDeleteNode = "/YomkDdsService/delete_node";

/** @brief Register a publisher topic endpoint */
// @zh: 注册发布主题端点
inline constexpr std::string_view ddsRegisterPubTopic = "/YomkDdsService/register_pub_topic";

/** @brief Register a subscriber topic endpoint */
// @zh: 注册订阅主题端点
inline constexpr std::string_view ddsRegisterSubTopic = "/YomkDdsService/register_sub_topic";

/** @brief Publish one message endpoint */
// @zh: 发布消息端点
inline constexpr std::string_view ddsPublish = "/YomkDdsService/publish";

/** @brief Loan a publish buffer (zero-copy) endpoint */
// @zh: 借出发布缓冲（零拷贝）端点
inline constexpr std::string_view ddsLoan = "/YomkDdsService/loan";

/** @brief Discard an un-published loaned buffer endpoint */
// @zh: 归还未发布的借出缓冲端点
inline constexpr std::string_view ddsDiscardLoan = "/YomkDdsService/discard_loan";

// --- YomkDdsDebugService ---

/** @brief Create the debug node endpoint (single node per process) */
// @zh: 创建调试节点端点（单节点模型）
inline constexpr std::string_view ddsDebugCreateNode = "/YomkDdsDebugService/create_node";

/** @brief Quit debugging and delete the debug node endpoint */
// @zh: 退出调试（删除调试节点）端点
inline constexpr std::string_view ddsDebugDeleteNode = "/YomkDdsDebugService/delete_node";

/** @brief Register a topic-print observer endpoint */
// @zh: 登记调试主题（逐条投递回调）端点
inline constexpr std::string_view ddsDebugTopicPrint = "/YomkDdsDebugService/topic_print";

/** @brief List discovered topics in the domain endpoint */
// @zh: 列出域内主题端点
inline constexpr std::string_view ddsDebugListTopics = "/YomkDdsDebugService/list_topics";

/** @brief Query topic discovery details endpoint */
// @zh: 查询主题详情端点
inline constexpr std::string_view ddsDebugTopicInfo = "/YomkDdsDebugService/topic_info";

/** @brief Reverse-lookup topics by data type name endpoint */
// @zh: 按数据类型反查主题端点
inline constexpr std::string_view ddsDebugTopicFind = "/YomkDdsDebugService/topic_find";

/** @brief Dump IDL struct of a data type endpoint */
// @zh: 按类型名输出 IDL 结构端点
inline constexpr std::string_view ddsDebugInterfaceShow = "/YomkDdsDebugService/interface_show";

/** @brief List all discovered data type names endpoint */
// @zh: 列出全部类型名端点
inline constexpr std::string_view ddsDebugInterfaceList = "/YomkDdsDebugService/interface_list";

/** @brief Query a publishable example for a topic endpoint */
// @zh: 查询主题发布示例端点
inline constexpr std::string_view ddsDebugTopicExample = "/YomkDdsDebugService/topic_example";

/** @brief Query the message description (type name + JSON template) endpoint */
// @zh: 查询消息描述两要素端点
inline constexpr std::string_view ddsDebugTopicMsg = "/YomkDdsDebugService/topic_msg";

/** @brief Publish a JSON payload to a topic endpoint */
// @zh: 发布 JSON 载荷消息端点
inline constexpr std::string_view ddsDebugTopicPub = "/YomkDdsDebugService/topic_pub";

/** @brief List named participants in the domain endpoint */
// @zh: 列出域内节点端点
inline constexpr std::string_view ddsDebugListNodes = "/YomkDdsDebugService/list_nodes";

/** @brief Query pub/sub topic lists of a named participant endpoint */
// @zh: 查询节点发布/订阅清单端点
inline constexpr std::string_view ddsDebugNodeInfo = "/YomkDdsDebugService/node_info";

// --- YomkDdsBagService ---

/** @brief Create the bag node endpoint (single node per process) */
// @zh: 创建 bag 节点端点（单节点模型）
inline constexpr std::string_view ddsBagCreateNode = "/YomkDdsBagService/create_node";

/** @brief Quit recording and delete the bag node endpoint */
// @zh: 退出录制（删除 bag 节点）端点
inline constexpr std::string_view ddsBagDeleteNode = "/YomkDdsBagService/delete_node";

/** @brief Record topics to an mcap bag endpoint (blocking) */
// @zh: 录制主题端点（阻塞）
inline constexpr std::string_view ddsBagRecord = "/YomkDdsBagService/bag_record";

/** @brief Show bag metadata endpoint */
// @zh: 查看 bag 元信息端点
inline constexpr std::string_view ddsBagInfo = "/YomkDdsBagService/bag_info";

/** @brief Rebuild bag metadata endpoint */
// @zh: 重建 bag 元信息端点
inline constexpr std::string_view ddsBagReindex = "/YomkDdsBagService/bag_reindex";

/** @brief Convert/merge bags endpoint */
// @zh: 转换 bag 端点
inline constexpr std::string_view ddsBagConvert = "/YomkDdsBagService/bag_convert";

/** @brief Replay an mcap bag endpoint (blocking) */
// @zh: 回放 bag 端点（阻塞）
inline constexpr std::string_view ddsBagPlay = "/YomkDdsBagService/bag_play";
}  // namespace yomk::url
