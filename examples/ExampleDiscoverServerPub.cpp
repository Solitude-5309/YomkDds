/**
 * @file ExampleDiscoverServerPub.cpp
 * @brief 发现服务器直配发布端示例：ds_pub_node 发布 ds_hello_world（MString，1Hz），
 *       配合 LaunchDiscoverServer.sh 与发现服务器环境变量实现多播受限场景互通；
 *       步骤详见 docs/manual/discovery-server-tutorial.md。
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

// 发现服务器示例·发布端：演示 跨域经发现服务器互通 的发布侧代码流程——节点域 id 固定 100，
// 以 CLIENT 角色直配发现服务器，与域 id 200 的 ExampleDiscoverServerSub 互通（DS 模式下
// server 不校验域 id，原理见 README 5.7/5.8）。
// 直配串经环境变量自动填入（node().create() 缺省形态，domain/udp/tcp 全缺省读 env）：
//   export YOMKDDS_USE_DISCOVER_SERVER=true
//   export YOMKDDS_UDP_DISCOVER_SERVERS=127.0.0.1:11811   // 须与 LaunchDiscoverServer.sh
//                                                         // 启动的 server 一致；TCP 场景改用
//                                                         // YOMKDDS_TCP_DISCOVER_SERVERS
// 未配置时回落多播发现，本示例跨域 100/200 场景将无法互通，运行前务必先 export。
// 三终端运行方式（先启动发现服务器，再 Sub/Pub 任意顺序）见 README 5.8。
int main()
{
    ::yomk::server().init();
    ::yomk::server().srv<YomkDdsService>().create();

    // 1. 创建节点：域 id 显式 100（DS 模式下可与对端异域）；省略双通道尾参 →
    //    直配串自动取环境变量（总开关开启时生效，participant 以 CLIENT 发现协议运行）
    auto resp = yomk::dds().node("ds_pub_node").domain_id(100).create();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleDiscoverServerPub").error("create node failed: ", resp.msg);
        return 1;
    }

    // 2. 注册发布主题：type 所有权移交服务端，new 后无需自行 delete
    resp = yomk::dds().node("ds_pub_node").topic("ds_hello_world").type_ptr(new YomkDdsStd::MStringPubSubType()).create_pub();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleDiscoverServerPub").error("register pub topic failed: ", resp.msg);
        yomk::dds().node("ds_pub_node").remove();
        return 1;
    }

    // 3. 等待经发现服务器完成 discovery 后，每隔 1s 发布一次，直到收到 Ctrl+C 退出信号
    std::signal(SIGINT, onSignal);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    int total = 0;  // 已发布条数（含失败，作消息序号）
    int failCount = 0;
    while (!g_stop.load())
    {
        YomkDdsStd::MString msg;
        msg.data("hello world " + std::to_string(total));
        resp = yomk::dds().node("ds_pub_node").topic("ds_hello_world").data(&msg).pub();
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            failCount++;
            ::yomk::log().tag("ExampleDiscoverServerPub").error("[SEND] publish failed: ", msg.data(), " status=", static_cast<int>(resp.status));
        }
        else
        {
            ::yomk::log().tag("ExampleDiscoverServerPub").info("[SEND] ", msg.data());
        }
        total++;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 4. 退出前显式销毁节点，确保 DDS 实体在 FastDDS 静态资源销毁前清理
    ::yomk::log().tag("ExampleDiscoverServerPub").info("publish done: total=", total, " failed=", failCount);
    resp = yomk::dds().node("ds_pub_node").remove();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleDiscoverServerPub").error("delete node failed: ", resp.msg);
        return 1;
    }
    return failCount > 0 ? 1 : 0;
}
