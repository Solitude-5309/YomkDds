#pragma once
// Maintainer notes: home of the Ctrl+C / interactive control flags for all
// YomkDds blocking loops (debug publish, bag record, bag play). Declared here
// so the embedding process (CLI SIGINT handler / keyboard interaction /
// library host) and the node-layer loops share one definition point — service
// headers stay pure service declarations. This is the process-local control
// plane, deliberately separate from the request-chain protocol surface.
// Contract: written from SIGINT handlers, polled by blocking loops — every
// bool operation is a lock-free atomic access (async-signal-safe); stopping
// through the yomk request chain is forbidden (locks + allocation in a
// signal handler are UB). Reset before starting a new session to clear any
// residue left by the previous one.

// clang-format off
// cppcheck-suppress unknownMacro
#include <atomic>

namespace yomk::ctrl
{
/**
 * @brief Ctrl+C stop/pause flag for one blocking loop (SIGINT-safe: lock-free ops only)
 *
 * The blocking loop polls stopped()/paused(); a SIGINT handler or the CLI
 * keyboard thread writes through stop()/pause()/resume() — atomic lock-free
 * stores. Never stop a session through the yomk request chain: it takes
 * locks and allocates, both banned in a signal handler.
 * @note Call reset() (stop semantics) before starting a new session
 */
// @zh: 单个阻塞循环的 Ctrl+C 停止/暂停标志（SIGINT 安全：仅无锁操作）
// @zh: 阻塞循环轮询 stopped()/paused()；SIGINT handler 或 CLI 键盘线程经
// @zh: stop()/pause()/resume() 写入——atomic 无锁 store。不可经 yomk 请求链停止：
// @zh: 请求链含锁与内存分配，handler 内禁用
// @zh: note 发起新会话前先 reset() 复位，避免上次会话残留置位导致循环秒退
class Flag
{
public:
    Flag() = default;

    /** @brief Set the stop flag (async-signal-safe) */
    // @zh: 置位停止标志（async-signal-safe）
    void stop() noexcept
    {
        m_v.store(true, std::memory_order_relaxed);
    }

    /** @brief Clear the flag before starting a new session (async-signal-safe) */
    // @zh: 复位标志，新会话发起前调用（async-signal-safe）
    void reset() noexcept
    {
        m_v.store(false, std::memory_order_relaxed);
    }

    /** @brief Poll the stop flag from the blocking loop (async-signal-safe) */
    // @zh: 阻塞循环轮询停止标志（async-signal-safe）
    bool stopped() const noexcept
    {
        return m_v.load(std::memory_order_relaxed);
    }

    /** @brief Set the pause flag (async-signal-safe) */
    // @zh: 置位暂停标志（async-signal-safe）
    void pause() noexcept
    {
        m_v.store(true, std::memory_order_relaxed);
    }

    /** @brief Clear the pause flag (async-signal-safe) */
    // @zh: 清除暂停标志（async-signal-safe）
    void resume() noexcept
    {
        m_v.store(false, std::memory_order_relaxed);
    }

    /** @brief Poll the pause flag from the blocking loop (async-signal-safe) */
    // @zh: 阻塞循环轮询暂停标志（async-signal-safe）
    bool paused() const noexcept
    {
        return m_v.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> m_v{false};
};

// ===== 调试持续发布停止标志（start_pub repeat 模式 / 期望建匹配等待） =====
inline Flag g_debugPubStop;

// ===== bag 录制控制标志（record 阻塞期间停止/暂停）。入口只复位 stop 不动 paused——
// ===== paused 是调用方启动意图（CLI --start-paused），handler 清除会使暂停态静默失效 =====
inline Flag g_bagRecordStop;
inline Flag g_bagRecordPaused;

// ===== bag 回放控制标志（play 阻塞期间停止/暂停/调速）。暂停冻结时间轴——读线程被
// ===== 队列满反压自然停住，恢复后重锚时间基准；rate 缩放消息间调度间隔（2.0 两倍速、
// ===== 0.5 半倍速，对齐 ros2 PlayerClock set_rate 语义），由 CLI 键盘交互或库调用方设置 =====
inline Flag g_bagPlayStop;
inline Flag g_bagPlayPaused;
inline std::atomic<double> g_bagPlayRate{1.0};  // 初始值由 play 入口注入

/** @brief Set the playback rate multiplier (takes effect on subsequent scheduling); rejects rate <= 0 and keeps the previous value */
// @zh: 设置回放速率倍率（即时作用于后续消息调度）；拒绝 rate<=0 并保持原值
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

/** @brief Combined session reset for record: clears stop and paused */
// @zh: 录制会话整体复位：清 stop 与 paused（残留 paused 表现为"什么都不录"）
inline void bagRecordReset() noexcept
{
    g_bagRecordStop.reset();
    g_bagRecordPaused.resume();
}

/** @brief Combined session reset for play: clears stop/paused and restores rate 1.0 */
// @zh: 回放会话整体复位：清 stop 与 paused，rate 复位 1.0（残留速率跨会话延续）
inline void bagPlayReset() noexcept
{
    g_bagPlayStop.reset();
    g_bagPlayPaused.resume();
    g_bagPlayRate.store(1.0, std::memory_order_relaxed);
}
}  // namespace yomk::ctrl
// clang-format on
