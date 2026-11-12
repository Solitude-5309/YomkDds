#pragma once

#include "YomkDdsPayload.h"

// Maintainer notes: the single home of all YOMK_DEFINE_MSG declarations
// (four-suite Msg piece, same design as the SDK's Yomk{Module}Msg.h).
// New request/response message: define the structure in YomkDdsPayload.h and
// append one YOMK_DEFINE_MSG here — never declare message packages elsewhere.
// User payload types (MString/MInt32..., namespace YomkDdsStd) are
// fastddsgen-generated from msg/YomkDdsStdMsgs.idl and installed as the
// separate entry <yomk/dds/YomkDdsStdMsgs.hpp> (types + PubSubTypes); include
// it directly when needed, not through this header.

// clang-format off
// YOMK_DEFINE_MSG is a third-party YomkServer macro; cppcheck needs --library
// to know it (unknownMacro is a tooling config need, not a source defect)
// cppcheck-suppress unknownMacro

// ===== YomkDdsService (structures in YomkDdsPayload.h) =====
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
