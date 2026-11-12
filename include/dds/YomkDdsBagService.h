#pragma once
// Maintainer notes: mcap bag record/replay service; FastDDSBagNode writes raw
// CDR bytes straight to mcap (schema_id=0, no-schema channels) and replays by
// rebuilding pass-through writers on the recorded timeline — no message type
// dependency. Single bag node per process; record/play block the calling
// thread under mtx_, so long-running stops rely on the yomk::bag* flags below.
#include <yomk/server/YomkServerApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include "YomkDdsPayload.h"  // flat quoted include: install tree, test tree and main tree all resolve same-dir

#include <atomic>
#include <memory>
#include <mutex>
class FastDDSBagNode;
using namespace yomk::payload;

/** @brief Bag record/replay service: mcap recording, inspection, conversion and timeline replay */
// @zh: Bag 录制回放服务：mcap 录制、元信息查看、转换与按时间轴回放
class YomkDdsBagService : public YomkService
{
public:
    YomkDdsBagService(YomkServer* server);
    virtual ~YomkDdsBagService();
    virtual int init() override;

private:
    yomk::payload::Response getVersion(YomkMsgPtr pkg);
    yomk::payload::Response createNode(YomkMsgPtr pkg);
    yomk::payload::Response bagRecord(YomkMsgPtr pkg);
    yomk::payload::Response deleteNode(YomkMsgPtr pkg);
    yomk::payload::Response bagInfo(YomkMsgPtr pkg);
    yomk::payload::Response bagReindex(YomkMsgPtr pkg);
    yomk::payload::Response bagConvert(YomkMsgPtr pkg);
    yomk::payload::Response bagPlay(YomkMsgPtr pkg);

private:
    // Single bag node: at most one per process, re-create requires delete
    // first; destructor destroys the node and all its DDS entities in the
    // node's internal order (explicit /delete_node before exit recommended)
    std::unique_ptr<FastDDSBagNode> node_;
    std::mutex mtx_;  // serializes node_ lifetime and all node entry calls
};

/**
 * @brief Stop/pause flags for YOMKDDS_BAG_RECORD (async-signal-safe)
 *
 * The record loop polls the stop flag every 100ms and the subscriber write
 * path checks the pause flag per message. SIGINT handlers write stop through
 * bagRecordStop — an atomic lock-free store; never stop a record through the
 * yomk request chain (locks and allocation are banned in a signal handler).
 * @note Call bagRecordReset() before starting a new record session; the
 *       record handler resets stop only, never paused — pausing is caller
 *       intent and clearing it silently would look like "records nothing"
 */
// @zh: YOMKDDS_BAG_RECORD 的停止/暂停标志（async-signal-safe）
// @zh: 录制循环每 100ms 轮询 stop，订阅写路径逐条检查 paused。SIGINT handler 经
// @zh: bagRecordStop 写入——atomic 无锁 store；不可经 yomk 请求链停止（handler 内禁锁与分配）
// @zh: note 发起录制前先 bagRecordReset() 复位；录制 handler 只复位 stop 不动 paused
// @zh: ——暂停是调用方启动意图，静默清除表现为“什么都不录”
namespace yomk
{
inline std::atomic<bool> g_bagRecordStop{false};
inline std::atomic<bool> g_bagRecordPaused{false};
inline void bagRecordStop()
{
    g_bagRecordStop.store(true);
}
inline void bagRecordPause()
{
    g_bagRecordPaused.store(true);
}
inline void bagRecordResume()
{
    g_bagRecordPaused.store(false);
}
inline void bagRecordReset()
{
    g_bagRecordStop.store(false);
    g_bagRecordPaused.store(false);  // reset both: leftover pause looks like "records nothing"
}
}  // namespace yomk

/**
 * @brief Stop/pause/rate controls for YOMKDDS_BAG_PLAY (async-signal-safe)
 *
 * Playback threads read the flags at every blocking point. Pause freezes the
 * timeline (the reader thread back-pressures on the full queue) and resume
 * re-anchors the time base; rate scales the inter-message scheduling interval
 * (2.0 = double speed, 0.5 = half speed, ros2 PlayerClock set_rate semantics).
 * Pause/resume/rate are set by CLI keyboard interaction or library callers;
 * none travel through the DDSBagPlay protocol (CLI and bag service share the
 * process, reading the globals directly).
 * @note bagPlaySetRate rejects rate <= 0 and keeps the previous value;
 *       call bagPlayReset() before starting a new playback session
 */
// @zh: YOMKDDS_BAG_PLAY 的停止/暂停/速率控制（async-signal-safe）
// @zh: 播放线程在各阻塞处读标志。暂停冻结时间轴（读线程被队列满反压自然停住），恢复后
// @zh: 重锚时间基准；速率缩放消息间调度间隔（2.0 两倍速、0.5 半倍速，对齐 ros2 PlayerClock
// @zh: set_rate 语义）。暂停/恢复/速率由 CLI 键盘交互或库调用方设置；不经 DDSBagPlay 协议
// @zh: 传递（CLI 与 bag 服务同进程，全局标志直读）
// @zh: note bagPlaySetRate 拒绝 rate<=0 并保持原值；发起回放前先 bagPlayReset() 复位
namespace yomk
{
inline std::atomic<bool> g_bagPlayStop{false};
inline std::atomic<bool> g_bagPlayPaused{false};
inline std::atomic<double> g_bagPlayRate{1.0};  // initial value injected by the play entry
inline void bagPlayStop()
{
    g_bagPlayStop.store(true);
}
inline void bagPlayPause()
{
    g_bagPlayPaused.store(true);
}
inline void bagPlayResume()
{
    g_bagPlayPaused.store(false);
}
/** @brief Set the playback rate multiplier (takes effect on subsequent scheduling) */
// @zh: 设置回放速率倍率（即时作用于后续消息调度）
inline bool bagPlaySetRate(double rate)
{
    if (rate <= 0.0)
    {
        return false;
    }
    g_bagPlayRate.store(rate, std::memory_order_relaxed);
    return true;
}
/** @brief Query the current playback rate multiplier */
// @zh: 查询当前回放速率倍率
inline double bagPlayGetRate()
{
    return g_bagPlayRate.load(std::memory_order_relaxed);
}
inline void bagPlayReset()
{
    g_bagPlayStop.store(false);
    g_bagPlayPaused.store(false);  // reset both: leftover pause looks like "frozen on start"
    g_bagPlayRate.store(1.0, std::memory_order_relaxed);  // leftover rate persists across sessions
}
}  // namespace yomk
