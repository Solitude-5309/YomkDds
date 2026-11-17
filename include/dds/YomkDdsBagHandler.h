#pragma once
// Maintainer notes: mcap bag record/replay service; FastDDSBagNode writes raw
// CDR bytes straight to mcap (schema_id=0, no-schema channels) and replays by
// rebuilding pass-through writers on the recorded timeline — no message type
// dependency. Single bag node per process; record/play block the calling
// thread under mtx_, so long-running stops rely on the yomk::ctrl flags (YomkDdsCtrlFlag.h).
#include <yomk/server/YomkServerApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include "YomkDdsPayload.h"  // flat quoted include: install tree, test tree and main tree all resolve same-dir
#include "YomkDdsCtrlFlag.h"  // yomk::ctrl bag 控制面标志（迁移自本头，服务头保持纯服务声明）

#include <memory>
#include <mutex>
class FastDDSBagNode;
using namespace yomk::payload;
using namespace yomk::payload::dds;

/** @brief Bag record/replay service: mcap recording, inspection, conversion and timeline replay */
// @zh: Bag 录制回放服务：mcap 录制、元信息查看、转换与按时间轴回放
class YomkDdsBagHandler : public YomkService
{
public:
    YomkDdsBagHandler(YomkServer* server);
    virtual ~YomkDdsBagHandler();
    virtual int init() override;

private:
    yomk::payload::Response getVersion(YomkMsgPtr pkg);
    yomk::payload::Response createNode(YomkMsgPtr pkg);
    yomk::payload::Response record(YomkMsgPtr pkg);
    yomk::payload::Response deleteNode(YomkMsgPtr pkg);
    yomk::payload::Response getInfo(YomkMsgPtr pkg);
    yomk::payload::Response reindex(YomkMsgPtr pkg);
    yomk::payload::Response convert(YomkMsgPtr pkg);
    yomk::payload::Response play(YomkMsgPtr pkg);
    yomk::payload::Response stopRecord(YomkMsgPtr pkg);
    yomk::payload::Response pauseRecord(YomkMsgPtr pkg);
    yomk::payload::Response resumeRecord(YomkMsgPtr pkg);
    yomk::payload::Response stopPlay(YomkMsgPtr pkg);
    yomk::payload::Response pausePlay(YomkMsgPtr pkg);
    yomk::payload::Response resumePlay(YomkMsgPtr pkg);
    yomk::payload::Response setPlayRate(YomkMsgPtr pkg);

private:
    // Single bag node: at most one per process, re-create requires delete
    // first; destructor destroys the node and all its DDS entities in the
    // node's internal order (explicit /delete_node before exit recommended)
    std::unique_ptr<FastDDSBagNode> node_;
    std::mutex mtx_;  // serializes node_ lifetime and all node entry calls
};
