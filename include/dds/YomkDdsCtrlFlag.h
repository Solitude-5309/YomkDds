#pragma once
// Maintainer notes: home of the Ctrl+C control flags for YomkDds blocking
// loops (debug publish today; bag record/play to follow). Declared here so
// the embedding process (CLI SIGINT handler / keyboard interaction / library
// host) and the node-layer loops share one definition point — service
// headers stay pure service declarations. This is the process-local control
// plane, deliberately separate from the request-chain protocol surface.
// Contract: written from SIGINT handlers, polled by blocking loops — every
// operation is a lock-free atomic access (async-signal-safe); stopping
// through the yomk request chain is forbidden (locks + allocation in a
// signal handler are UB). Reset before starting a new session to clear any
// residue left by the previous one.

// clang-format off
// cppcheck-suppress unknownMacro
#include <atomic>

namespace yomk::ctrl
{
/**
 * @brief Ctrl+C stop flag for one blocking loop (SIGINT-safe: lock-free ops only)
 *
 * The blocking loop polls stopped(); a SIGINT handler writes it through
 * stop() — an atomic lock-free store. Never stop a session through the yomk
 * request chain: it takes locks and allocates, both banned in a signal
 * handler.
 * @note Call reset() before starting a new session
 */
// @zh: 单个阻塞循环的 Ctrl+C 停止标志（SIGINT 安全：仅无锁操作）
// @zh: 阻塞循环轮询 stopped()；SIGINT handler 经 stop() 写入——atomic 无锁 store。
// @zh: 不可经 yomk 请求链停止：请求链含锁与内存分配，handler 内禁用
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

    /** @brief Poll the flag from the blocking loop (async-signal-safe) */
    // @zh: 阻塞循环轮询标志（async-signal-safe）
    bool stopped() const noexcept
    {
        return m_v.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> m_v{false};
};

// ===== 调试持续发布停止标志（start_pub repeat 模式 / 期望建匹配等待） =====
inline Flag g_debugPubStop;
}  // namespace yomk::ctrl
// clang-format on
