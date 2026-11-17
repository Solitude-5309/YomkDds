#pragma once

#include "YomkDdsPayload.h"

// Maintainer notes: the single home of all YOMK_DEFINE_MSG_NS declarations
// (four-suite Msg piece, same design as the SDK's Yomk{Module}Msg.h).
// New request/response message: define the structure in YomkDdsPayload.h and
// append one YOMK_DEFINE_MSG_NS here — never declare message packages elsewhere.
// All messages register into the dds sub-root (yomk::msgs::dds::...) so the
// suite's message names never collide with other suites in yomk::msgs.
// User payload types (MString/MInt32..., namespace YomkDdsStd) are
// fastddsgen-generated from msg/YomkDdsStdMsgs.idl and installed as the
// separate entry <yomk/dds/YomkDdsStdMsgs.hpp> (types + PubSubTypes); include
// it directly when needed, not through this header.

// clang-format off
// YOMK_DEFINE_MSG_NS is a third-party YomkServer macro; cppcheck needs --library
// to know it (unknownMacro is a tooling config need, not a source defect)
// cppcheck-suppress unknownMacro

// ===== YomkDdsHandler (structures in YomkDdsPayload.h) =====
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::CreateNodeRequest, CreateNodeRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::LoadXmlRequest, LoadXmlRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::RegisterPubTopicRequest, RegisterPubTopicRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::RegisterSubTopicRequest, RegisterSubTopicRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::PublishRequest, PublishRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::LoanRequest, LoanRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DiscardLoanRequest, DiscardLoanRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::LoanSample, LoanSample, msg)

// ===== YomkDdsDebugHandler =====
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DebugCreateNodeRequest, DebugCreateNodeRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DebugPrintTopicRequest, DebugPrintTopicRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DebugListTopicsRequest, DebugListTopicsRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::TopicInfoList, TopicInfoList, d)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DebugGetTopicInfoRequest, DebugGetTopicInfoRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::TopicInfo, TopicInfo, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::InterfaceInfo, InterfaceInfo, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::TopicExampleInfo, TopicExampleInfo, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::TopicMessageInfo, TopicMessageInfo, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::TopicPublishInfo, TopicPublishInfo, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::NodeInfo, NodeInfo, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DebugListNodesRequest, DebugListNodesRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DebugGetNodeInfoRequest, DebugGetNodeInfoRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DebugFindTopicsRequest, DebugFindTopicsRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DebugShowInterfaceRequest, DebugShowInterfaceRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DebugListInterfacesRequest, DebugListInterfacesRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DebugGetTopicExampleRequest, DebugGetTopicExampleRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DebugGetTopicMsgRequest, DebugGetTopicMsgRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::DebugTopicPubRequest, DebugTopicPubRequest, msg)

// ===== YomkDdsBagHandler =====
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::BagCreateNodeRequest, BagCreateNodeRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::BagRecordRequest, BagRecordRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::RecordResult, RecordResult, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::BagInfoRequest, BagInfoRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::BagInfo, BagInfo, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::BagReindexRequest, BagReindexRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::BagConvertRequest, BagConvertRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::ConvertResult, ConvertResult, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::BagPlayRequest, BagPlayRequest, msg)
YOMK_DEFINE_MSG_NS(dds, yomk::payload::dds::PlayResult, PlayResult, msg)
// clang-format on
