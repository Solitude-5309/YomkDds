#pragma once

#include "YomkDdsPayload.h"

// YomkDds 扩展消息包（四件套之 Msg，与 SDK 各 Yomk{Module}Msg.h 同一设计：YOMK_DEFINE_MSG
// 集中声明于此）。本头只承载 YOMK_DEFINE_MSG 消息包声明；用户载荷类型（MString/MInt32/...，
// namespace YomkDdsStd）为 Fast DDS IDL 经 fastddsgen 生成（msg/YomkDdsStdMsgs.idl），随扩展
// 安装为独立入口 <yomk/dds/YomkDdsStdMsgs.hpp>（+ PubSubTypes），经 DDS 线上传输（注册主题
// 时的 type 实参即 new XxxPubSubType()），按需直接包含，不经本头。
//
// 新增请求/应答消息包：在 yomk::payload 定义数据结构（YomkDdsPayload.h）并在本头追加
// YOMK_DEFINE_MSG；新增用户载荷类型：在 msg/YomkDdsStdMsgs.idl 追加 struct 并重跑 msg 构建
// 重新生成。不在别处手写消息包声明。

// clang-format off
// YOMK_DEFINE_MSG 是 YomkServer 第三方宏，cppcheck 未 --library 配置识别（unknownMacro 属工具配置需求，非自有源码缺陷）
// cppcheck-suppress unknownMacro

// ===== YomkDdsService（请求/响应结构见 YomkDdsPayload.h） =====
YOMK_DEFINE_MSG(yomk::payload::DDSNode, DDSNode, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSXmlLoad, DDSXmlLoad, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSTopic, DDSTopic, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSSubRequest, DDSSubRequest, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSPublish, DDSPublish, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSLoan, DDSLoan, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSLoanResult, DDSLoanResult, msg)

// ===== YomkDdsDebugService =====
YOMK_DEFINE_MSG(yomk::payload::DDSDebugNode, DDSDebugNode, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSDebugTopic, DDSDebugTopic, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSDebugList, DDSDebugList, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSDebugInfo, DDSDebugInfo, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSNodeList, DDSNodeList, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSNodeInfo, DDSNodeInfo, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSDebugFind, DDSDebugFind, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSDebugInterfaceShow, DDSDebugInterfaceShow, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSDebugInterfaceList, DDSDebugInterfaceList, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSDebugTopicExample, DDSDebugTopicExample, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSDebugTopicMsg, DDSDebugTopicMsg, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSDebugTopicPub, DDSDebugTopicPub, msg)

// ===== YomkDdsBagService =====
YOMK_DEFINE_MSG(yomk::payload::DDSBagNode, DDSBagNode, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSBagRecord, DDSBagRecord, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSBagInfo, DDSBagInfo, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSBagReindex, DDSBagReindex, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSBagConvert, DDSBagConvert, msg)
YOMK_DEFINE_MSG(yomk::payload::DDSBagPlay, DDSBagPlay, msg)
// clang-format on
