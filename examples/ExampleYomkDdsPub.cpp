/**
 * @file ExampleYomkDdsPub.cpp
 * @brief 最小发布端示例：pub_node 注册发布主题 hello_world（MString），1Hz 持续发布直至
 *       Ctrl+C 退出（退出前 DEL_NODE 清理）；与 ExampleYomkDdsSub 或 yomkdds topic print
 *       配对验证。
 */
#include <yomk/dds/YomkDdsAPI.h>
#include <yomk/server/YomkServerApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include <yomk/dds/YomkDdsStdMsgs.hpp>
#include <yomk/dds/YomkDdsStdMsgsPubSubTypes.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <string>
#include <thread>

using namespace yomk;

static std::atomic<bool> g_stop{false};  // SIGINT（Ctrl+C）退出标志

static void onSignal(int)
{
    g_stop.store(true);
}

// 发布端示例：演示 创建节点 → 注册 MString 发布主题 → 每隔 1s 发布一条 → 收到 Ctrl+C 退出信号后
// 停止发布并销毁节点 的代码流程。
// 跨进程运行方式（与 ExampleYomkDdsSub 配合、启动顺序）见 README。
int main(int argc, char* argv[])
{
    ::yomk::server().init();
    ::yomk::server().srv<YomkDdsService>().create();

    // 1. 创建节点：domainId 缺省取环境变量 YOMKDDS_DDS_DOMAIN_ID（无/非法回落 0；仅同域
    //    节点互通），nodeName 唯一
    auto resp = yomk::dds().node_name("pub_node").create();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsPub").error("create node failed: ", resp.msg);
        return 1;
    }

    // 2. 注册发布主题：type 所有权移交服务端，new 后无需自行 delete
    resp = YOMKDDS_PUB_TOPIC("pub_node", "hello_world", new YomkDdsStd::MStringPubSubType());
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsPub").error("register pub topic failed: ", resp.msg);
        YOMKDDS_DEL_NODE("pub_node");
        return 1;
    }

    // 3. 等待 DDS discovery 完成后，每隔 1s 发布一次，直到收到 Ctrl+C 退出信号
    std::signal(SIGINT, onSignal);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    int total = 0;  // 已发布条数（含失败，作消息序号）
    int failCount = 0;
    while (!g_stop.load())
    {
        YomkDdsStd::MString msg;
        msg.data("hello world " + std::to_string(total));
        resp = YOMKDDS_PUB_MSG("pub_node", "hello_world", &msg);
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            failCount++;
            ::yomk::log().tag("ExampleYomkDdsPub").error("[SEND] publish failed: ", msg.data(), " status=", static_cast<int>(resp.status));
        }
        else
        {
            ::yomk::log().tag("ExampleYomkDdsPub").info("[SEND] ", msg.data());
        }
        total++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 4. 退出前显式销毁节点，确保 DDS 实体在 FastDDS 静态资源销毁前清理
    ::yomk::log().tag("ExampleYomkDdsPub").info("publish done: total=", total, " failed=", failCount);
    resp = YOMKDDS_DEL_NODE("pub_node");
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsPub").error("delete node failed: ", resp.msg);
        return 1;
    }
    return failCount > 0 ? 1 : 0;
}
