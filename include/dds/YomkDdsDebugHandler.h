#pragma once
// Maintainer notes: type-agnostic debugging service; FastDDSDebugNode rebuilds
// subscriptions dynamically from remote XTypes TypeObject, so no message type
// dependency exists anywhere in this service (messages are delivered to the
// user callback as JSON text). Single debug node per process; node lifecycle
// and entry calls are serialized by mtx_.
#include <yomk/server/YomkServerApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include "YomkDdsPayload.h"  // flat quoted include: install tree, test tree and main tree all resolve same-dir
#include "YomkDdsCtrlFlag.h"  // yomk::ctrl::g_debugPubStop：停止标志定义移至独立头，服务头保持纯服务声明

#include <memory>
#include <mutex>
class FastDDSDebugNode;
using namespace yomk::payload;
using namespace yomk::payload::dds;

/** @brief Type-agnostic debugging service: discovery inspection, JSON message tap and probe publish */
// @zh: 类型无关调试服务：发现观察、消息 JSON 旁路与探针发布
class YomkDdsDebugHandler : public YomkService
{
public:
    YomkDdsDebugHandler(YomkServer* server);
    virtual ~YomkDdsDebugHandler();
    virtual int init() override;

private:
    yomk::payload::Response getVersion(YomkMsgPtr pkg);
    yomk::payload::Response createNode(YomkMsgPtr pkg);
    yomk::payload::Response printTopic(YomkMsgPtr pkg);
    yomk::payload::Response listTopics(YomkMsgPtr pkg);
    yomk::payload::Response getTopicInfo(YomkMsgPtr pkg);
    yomk::payload::Response findTopics(YomkMsgPtr pkg);
    yomk::payload::Response showInterface(YomkMsgPtr pkg);
    yomk::payload::Response listInterfaces(YomkMsgPtr pkg);
    yomk::payload::Response getTopicExample(YomkMsgPtr pkg);
    yomk::payload::Response getTopicMsg(YomkMsgPtr pkg);
    yomk::payload::Response startPub(YomkMsgPtr pkg);
    yomk::payload::Response stopPub(YomkMsgPtr pkg);
    yomk::payload::Response listNodes(YomkMsgPtr pkg);
    yomk::payload::Response getNodeInfo(YomkMsgPtr pkg);
    yomk::payload::Response deleteNode(YomkMsgPtr pkg);

private:
    // Single debug node: at most one per process, re-create requires delete
    // first; destructor destroys the node and all its DDS entities in the
    // node's internal order (explicit /delete_node before exit recommended)
    std::unique_ptr<FastDDSDebugNode> node_;
    std::mutex mtx_;  // serializes node_ lifetime and all node entry calls
};
