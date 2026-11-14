#pragma once
// Maintainer notes: type-agnostic debugging service; FastDDSDebugNode rebuilds
// subscriptions dynamically from remote XTypes TypeObject, so no message type
// dependency exists anywhere in this service (messages are delivered to the
// user callback as JSON text). Single debug node per process; node lifecycle
// and entry calls are serialized by mtx_.
#include <yomk/server/YomkServerApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include "YomkDdsPayload.h"  // flat quoted include: install tree, test tree and main tree all resolve same-dir

#include <atomic>
#include <memory>
#include <mutex>
class FastDDSDebugNode;
using namespace yomk::payload;

/** @brief Type-agnostic debugging service: discovery inspection, JSON message tap and probe publish */
// @zh: 类型无关调试服务：发现观察、消息 JSON 旁路与探针发布
class YomkDdsDebugService : public YomkService
{
public:
    YomkDdsDebugService(YomkServer* server);
    virtual ~YomkDdsDebugService();
    virtual int init() override;

private:
    yomk::payload::Response getVersion(YomkMsgPtr pkg);
    yomk::payload::Response createNode(YomkMsgPtr pkg);
    yomk::payload::Response topicPrint(YomkMsgPtr pkg);
    yomk::payload::Response listTopics(YomkMsgPtr pkg);
    yomk::payload::Response topicInfo(YomkMsgPtr pkg);
    yomk::payload::Response topicFind(YomkMsgPtr pkg);
    yomk::payload::Response interfaceShow(YomkMsgPtr pkg);
    yomk::payload::Response interfaceList(YomkMsgPtr pkg);
    yomk::payload::Response topicExample(YomkMsgPtr pkg);
    yomk::payload::Response topicMsg(YomkMsgPtr pkg);
    yomk::payload::Response topicPub(YomkMsgPtr pkg);
    yomk::payload::Response listNodes(YomkMsgPtr pkg);
    yomk::payload::Response nodeInfo(YomkMsgPtr pkg);
    yomk::payload::Response deleteNode(YomkMsgPtr pkg);

private:
    // Single debug node: at most one per process, re-create requires delete
    // first; destructor destroys the node and all its DDS entities in the
    // node's internal order (explicit /delete_node before exit recommended)
    std::unique_ptr<FastDDSDebugNode> node_;
    std::mutex mtx_;  // serializes node_ lifetime and all node entry calls
};

/**
 * @brief Stop flag for a repeating debug publish
 *        (yomk::dds().debug().topic(name).payload(json)[.repeat(ms)].publish())
 *
 * The publish loop (repeat(ms) > 0) only reads the flag; SIGINT handlers
 * write it through debugPubStop — an atomic lock-free store. Never stop a
 * publish through the yomk request chain: it takes locks and allocates, both
 * banned in a signal handler.
 * @note Call debugPubReset() before starting a new publish session
 */
// @zh: 调试持续发布（yomk::dds().debug().topic(name).payload(json)[.repeat(ms)].publish()）的停止标志
// @zh: （async-signal-safe）
// @zh: 发布循环（repeat(ms) > 0）只读；SIGINT handler 经 debugPubStop 写入——
// @zh: atomic 无锁 store。不可经 yomk 请求链停止：请求链含锁与内存分配，handler 内禁用
// @zh: note 发起持续发布前先 debugPubReset() 复位，避免上次会话残留置位导致循环秒退
namespace yomk
{
inline std::atomic<bool> g_debugPubStop{false};
inline void debugPubStop()
{
    g_debugPubStop.store(true);
}
inline void debugPubReset()
{
    g_debugPubStop.store(false);
}
}  // namespace yomk
