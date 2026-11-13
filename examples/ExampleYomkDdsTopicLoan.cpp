/**
 * @file ExampleYomkDdsTopicLoan.cpp
 * @brief loan 零拷贝发布示例：loan_topic（MInt32，plain 类型）经链式 loan() 借出池内
 *       样本填值后发布 5 条并演示 discard 归还；fallback_topic（MString，非 plain）对照
 *       展示借出返回空指针的回退路径。详见 docs/manual/loan-tutorial.md。
 */
#include <yomk/dds/YomkDdsAPI.h>
#include <yomk/server/YomkServerApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include <yomk/dds/YomkDdsStdMsgs.hpp>
#include <yomk/dds/YomkDdsStdMsgsPubSubTypes.hpp>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

using namespace yomk;

// loan 借出机制示例：演示 借出池内样本免序列化发布（仅 plain 类型可用，非 plain 回退普通发布）与 discard 归还，
// 并对比 plain(MInt32) 与非 plain(MString) 的借出差异；订阅端回调透明接收（指针仅回调期间有效）。
int main(int argc, char* argv[])
{
    ::yomk::server().init();
    ::yomk::server().srv<YomkDdsService>().create();

    // 1. 创建节点
    ::yomk::log().tag("ExampleYomkDdsTopicLoan").info("=== YomkDdsService::createNode ===");
    auto resp = yomk::dds().node("node0").create();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsTopicLoan").error("create node failed: ", resp.msg);
        return 1;
    }

    // 2. 非 plain 类型 loan 回退演示：MString 含 string 成员不可 loan，借出返回 nullptr
    ::yomk::log().tag("ExampleYomkDdsTopicLoan").info("=== loan fallback (non-plain MString) ===");
    resp = yomk::dds().node("node0").topic("fallback_topic").type_ptr(new YomkDdsStd::MStringPubSubType()).create_pub();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsTopicLoan").error("register pub topic failed: ", resp.msg);
        yomk::dds().node("node0").remove();
        return 1;
    }
    void* loanPtr = yomk::dds().node("node0").topic("fallback_topic").loan();
    ::yomk::log().tag("ExampleYomkDdsTopicLoan").info("非 plain MString 借出指针=", (loanPtr != nullptr ? "非空" : "空"), "（预期为空，回退普通发布）");

    // 3. plain 类型 loan 全链路演示：MInt32 为纯基础类型，FINAL 后 is_plain 为 true
    ::yomk::log().tag("ExampleYomkDdsTopicLoan").info("=== loan publish (plain MInt32) ===");
    resp = yomk::dds().node("node0").topic("loan_topic").type_ptr(new YomkDdsStd::MInt32PubSubType()).create_pub();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsTopicLoan").error("register pub topic failed: ", resp.msg);
        yomk::dds().node("node0").remove();
        return 1;
    }

    // 订阅端透明走 reader loan：回调中打印收到的值并累计计数
    std::atomic<int> loanReceived{0};
    std::mutex loanMtx;
    int32_t lastLoanValue = -1;
    auto onLoan = [&](const void* data)
    {
        // data 为交付的消息实例指针；MInt32 是 alignof≤4 的 plain 类型，可直接转型解引用（对齐契约见 src/FastDDSNode.cpp）
        auto* msg = static_cast<const YomkDdsStd::MInt32*>(data);
        std::lock_guard<std::mutex> lock(loanMtx);
        lastLoanValue = msg->data();
        loanReceived++;
        ::yomk::log().tag("ExampleYomkDdsTopicLoan").info("[RECV] 收到借出消息: ", msg->data());
    };
    resp = yomk::dds().node("node0").topic("loan_topic").type_ptr(new YomkDdsStd::MInt32PubSubType()).sub_cb(onLoan).create_sub();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsTopicLoan").error("register sub topic failed: ", resp.msg);
        yomk::dds().node("node0").remove();
        return 1;
    }

    // 4. 等待 DDS discovery 完成
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 借出 → 池内直接填值 → 发布（每次 write 后中间件收回指针，须重新借出）
    for (int i = 0; i < 5; ++i)
    {
        loanPtr = yomk::dds().node("node0").topic("loan_topic").loan();
        if (loanPtr == nullptr)
        {
            ::yomk::log().tag("ExampleYomkDdsTopicLoan").error("[SEND] 借出失败: 第 ", i, " 次 loan 返回 nullptr");
            break;
        }
        static_cast<YomkDdsStd::MInt32*>(loanPtr)->data(100 + i);
        resp = yomk::dds().node("node0").topic("loan_topic").data(loanPtr).pub();
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            ::yomk::log().tag("ExampleYomkDdsTopicLoan").error("[SEND] 借出发布失败: ", 100 + i, " status=", static_cast<int>(resp.status));
            break;
        }
        ::yomk::log().tag("ExampleYomkDdsTopicLoan").info("[SEND] 借出发布: ", 100 + i);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 5. 等待接收完成（首条消息可能因 discovery 时序丢失），打印接收统计
    std::this_thread::sleep_for(std::chrono::seconds(1));
    int32_t lastValueCopy = 0;
    {
        std::lock_guard<std::mutex> lock(loanMtx);
        lastValueCopy = lastLoanValue;
    }
    ::yomk::log().tag("ExampleYomkDdsTopicLoan").info("[RESULT] 接收=", loanReceived.load(), "/5 最后值=", lastValueCopy);

    // 6. discard 演示：借出后不发布须归还缓冲（否则池泄漏）
    ::yomk::log().tag("ExampleYomkDdsTopicLoan").info("=== discard loan ===");
    loanPtr = yomk::dds().node("node0").topic("loan_topic").loan();
    ::yomk::log().tag("ExampleYomkDdsTopicLoan").info("借出指针=", (loanPtr != nullptr ? "非空" : "空"));
    if (loanPtr != nullptr)
    {
        resp = yomk::dds().node("node0").topic("loan_topic").sample(loanPtr).return_loan();
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            ::yomk::log().tag("ExampleYomkDdsTopicLoan").error("discard loan failed: ", resp.msg);
        }
    }

    // 7. 退出前销毁节点，确保 DDS 实体在 FastDDS 静态资源销毁前清理
    ::yomk::log().tag("ExampleYomkDdsTopicLoan").info("=== YomkDdsService::deleteNode ===");
    resp = yomk::dds().node("node0").remove();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsTopicLoan").error("delete node failed: ", resp.msg);
        return 1;
    }
    return 0;
}
