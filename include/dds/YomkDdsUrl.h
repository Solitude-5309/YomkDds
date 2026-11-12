#pragma once
#include <string_view>

// YomkDds 扩展服务 URL 常量（四件套之 Url，与 SDK 各域 Yomk{Module}Url.h 同一设计）：
// 与 SDK 内置模块同风格（参考 yomk/fn_pool/YomkFunctionPoolUrl.h），inline constexpr
// std::string_view 编译期常量，请求链全程 string_view 透传，调用路径零堆分配。
// yomk::url 为开放命名空间：扩展的 url 常量按同风格直接追加。
// 请求链一律引用本头常量，不裸写 URL 字符串字面量（含宏 API、示例与测试）。
namespace yomk::url
{
// ===== YomkDdsService 核心通信（宏 API 见 YomkDdsAPI.h，服务声明见 YomkDdsService.h） =====
inline constexpr std::string_view ddsVersion           = "/YomkDdsService/version";             // 版本查询
inline constexpr std::string_view ddsLoadXml           = "/YomkDdsService/load_xml";            // 加载 Fast DDS XML profiles
inline constexpr std::string_view ddsCreateNode        = "/YomkDdsService/create_node";         // 创建 DDS 节点
inline constexpr std::string_view ddsDeleteNode        = "/YomkDdsService/delete_node";         // 删除 DDS 节点
inline constexpr std::string_view ddsRegisterPubTopic  = "/YomkDdsService/register_pub_topic";  // 注册发布主题
inline constexpr std::string_view ddsRegisterSubTopic  = "/YomkDdsService/register_sub_topic";  // 注册订阅主题
inline constexpr std::string_view ddsPublish           = "/YomkDdsService/publish";             // 发布消息
inline constexpr std::string_view ddsLoan              = "/YomkDdsService/loan";                // 借出发布缓冲（loan 零拷贝）
inline constexpr std::string_view ddsDiscardLoan       = "/YomkDdsService/discard_loan";        // 归还借出缓冲

// ===== YomkDdsDebugService 类型无关调试（服务声明见 YomkDdsDebugService.h） =====
inline constexpr std::string_view ddsDebugCreateNode    = "/YomkDdsDebugService/create_node";    // 创建调试节点
inline constexpr std::string_view ddsDebugDeleteNode    = "/YomkDdsDebugService/delete_node";    // 退出调试（删除调试节点）
inline constexpr std::string_view ddsDebugTopicPrint    = "/YomkDdsDebugService/topic_print";    // 登记调试主题（逐条投递回调）
inline constexpr std::string_view ddsDebugListTopics    = "/YomkDdsDebugService/list_topics";    // 列出域内主题
inline constexpr std::string_view ddsDebugTopicInfo     = "/YomkDdsDebugService/topic_info";     // 查询主题详情
inline constexpr std::string_view ddsDebugTopicFind     = "/YomkDdsDebugService/topic_find";     // 按数据类型反查主题
inline constexpr std::string_view ddsDebugInterfaceShow = "/YomkDdsDebugService/interface_show"; // 按类型名输出 IDL 结构
inline constexpr std::string_view ddsDebugInterfaceList = "/YomkDdsDebugService/interface_list"; // 列出全部类型名
inline constexpr std::string_view ddsDebugTopicExample  = "/YomkDdsDebugService/topic_example";  // 查询主题发布示例
inline constexpr std::string_view ddsDebugTopicMsg      = "/YomkDdsDebugService/topic_msg";      // 查询消息描述两要素
inline constexpr std::string_view ddsDebugTopicPub      = "/YomkDdsDebugService/topic_pub";      // 发布 JSON 载荷消息
inline constexpr std::string_view ddsDebugListNodes     = "/YomkDdsDebugService/list_nodes";     // 列出域内节点
inline constexpr std::string_view ddsDebugNodeInfo      = "/YomkDdsDebugService/node_info";      // 查询节点发布/订阅清单

// ===== YomkDdsBagService Bag 录制/回放（服务声明见 YomkDdsBagService.h） =====
inline constexpr std::string_view ddsBagCreateNode = "/YomkDdsBagService/create_node";  // 创建 bag 节点
inline constexpr std::string_view ddsBagDeleteNode = "/YomkDdsBagService/delete_node";  // 退出录制（删除 bag 节点）
inline constexpr std::string_view ddsBagRecord     = "/YomkDdsBagService/bag_record";   // 录制主题
inline constexpr std::string_view ddsBagInfo       = "/YomkDdsBagService/bag_info";     // 查看 bag 元信息
inline constexpr std::string_view ddsBagReindex    = "/YomkDdsBagService/bag_reindex";  // 重建 bag 元信息
inline constexpr std::string_view ddsBagConvert    = "/YomkDdsBagService/bag_convert";  // 转换 bag
inline constexpr std::string_view ddsBagPlay       = "/YomkDdsBagService/bag_play";     // 回放 bag
}  // namespace yomk::url
