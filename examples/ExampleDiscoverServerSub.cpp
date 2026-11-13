/**
 * @file ExampleDiscoverServerSub.cpp
 * @brief 发现服务器直配订阅端示例：ds_sub_node 订阅 ds_hello_world（MString，回调打印），
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

// 发现服务器示例·订阅端：演示 跨域经发现服务器互通 的订阅侧代码流程——节点域 id 固定 200，
// 以 CLIENT 角色直配发现服务器，与域 id 100 的 ExampleDiscoverServerPub 互通（DS 模式下
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

    // 1. 创建节点：域 id 显式 200（DS 模式下可与对端异域）；省略双通道尾参 →
    //    直配串自动取环境变量（总开关开启时生效，participant 以 CLIENT 发现协议运行）
    auto resp = yomk::dds().node("ds_sub_node").domain(200).create();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleDiscoverServerSub").error("create node failed: ", resp.msg);
        return 1;
    }

    // 2. 注册订阅主题：type 所有权移交服务端；收到消息时回调 onMessage
    std::atomic<int> received{0};
    auto onMessage = [&](const void* data)
    {
        // data 为交付的消息实例指针（仅回调期间有效），转型为具体消息类型后读取
        auto* msg = static_cast<const YomkDdsStd::MString*>(data);
        received++;
        ::yomk::log().tag("ExampleDiscoverServerSub").info("[RECV] ", msg->data());
    };
    resp = yomk::dds().node("ds_sub_node").topic("ds_hello_world").type_ptr(new YomkDdsStd::MStringPubSubType()).sub_cb(onMessage).create_sub();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleDiscoverServerSub").error("register sub topic failed: ", resp.msg);
        yomk::dds().node("ds_sub_node").remove();
        return 1;
    }

    // 3. 监听 Ctrl+C，主循环等待退出信号
    std::signal(SIGINT, onSignal);
    ::yomk::log().tag("ExampleDiscoverServerSub").info("subscribing ds_hello_world, press Ctrl+C to exit");
    while (!g_stop.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 4. 退出前显式销毁节点，确保 DDS 实体在 FastDDS 静态资源销毁前清理
    ::yomk::log().tag("ExampleDiscoverServerSub").info("received total=", received.load(), " messages");
    resp = yomk::dds().node("ds_sub_node").remove();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleDiscoverServerSub").error("delete node failed: ", resp.msg);
        return 1;
    }
    return 0;
}
