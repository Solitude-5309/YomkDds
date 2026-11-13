/**
 * @file ExampleYomkDdsSub.cpp
 * @brief 最小订阅端示例：sub_node 注册订阅主题 hello_world（MString），回调逐条打印直至
 *       Ctrl+C 退出（退出前 DEL_NODE 清理）；与 ExampleYomkDdsPub 或 yomkdds topic pub
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
#include <thread>

using namespace yomk;

static std::atomic<bool> g_stop{false};  // SIGINT（Ctrl+C）退出标志

static void onSignal(int)
{
    g_stop.store(true);
}

// 订阅端示例：演示 创建节点 → 注册 MString 订阅主题（回调打印每条消息）→ 等待 Ctrl+C 退出 → 销毁节点 的代码流程。
// 跨进程运行方式（与 ExampleYomkDdsPub 配合、启动顺序）见 README。
int main(int argc, char* argv[])
{
    ::yomk::server().init();
    ::yomk::server().srv<YomkDdsService>().create();

    // 1. 创建节点：domainId 缺省取环境变量 YOMKDDS_DDS_DOMAIN_ID，须与发布端同域方可互通
    auto resp = yomk::dds().node_name("sub_node").create();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsSub").error("create node failed: ", resp.msg);
        return 1;
    }

    // 2. 注册订阅主题：type 所有权移交服务端；收到消息时回调 onMessage
    std::atomic<int> received{0};
    auto onMessage = [&](const void* data)
    {
        // data 为交付的消息实例指针（仅回调期间有效），转型为具体消息类型后读取
        auto* msg = static_cast<const YomkDdsStd::MString*>(data);
        received++;
        ::yomk::log().tag("ExampleYomkDdsSub").info("[RECV] ", msg->data());
    };
    resp = yomk::dds().node_name("sub_node").topic("hello_world").type_ptr(new YomkDdsStd::MStringPubSubType()).sub_cb(onMessage).create_sub();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsSub").error("register sub topic failed: ", resp.msg);
        yomk::dds().node_name("sub_node").remove();
        return 1;
    }

    // 3. 监听 Ctrl+C，主循环等待退出信号
    std::signal(SIGINT, onSignal);
    ::yomk::log().tag("ExampleYomkDdsSub").info("subscribing hello_world, press Ctrl+C to exit");
    while (!g_stop.load())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 4. 退出前显式销毁节点，确保 DDS 实体在 FastDDS 静态资源销毁前清理
    ::yomk::log().tag("ExampleYomkDdsSub").info("received total=", received.load(), " messages");
    resp = yomk::dds().node_name("sub_node").remove();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsSub").error("delete node failed: ", resp.msg);
        return 1;
    }
    return 0;
}
