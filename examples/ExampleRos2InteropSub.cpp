/**
 * @file ExampleRos2InteropSub.cpp
 * @brief ROS2 互通订阅端示例（run_ros2_interop.sh 方向 2/3）：set_name 直改类型名为 ROS2
 *       约定名 std_msgs::msg::dds_::String_ + "rt/" 话题前缀 + ros2DefaultSubscriber 底座，
 *       逐条打印直至 Ctrl+C/SIGTERM（退出打印总收条数）；机制详见
 *       docs/manual/ros2-interop-tutorial.md。
 */

#include <yomk/dds/YomkDdsAPI.h>
#include <yomk/server/YomkServerApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include <yomk/dds/YomkDdsStdMsgs.hpp>
#include <yomk/dds/YomkDdsStdMsgsPubSubTypes.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

using namespace yomk;

static std::atomic<bool> g_stop{false};  // SIGINT/SIGTERM 退出标志

static void onSignal(int)
{
    g_stop.store(true);
}

int main()
{
    ::yomk::server().init();
    ::yomk::server().srv<YomkDdsService>().create();

    // 创建节点（域 0 对齐 ROS2 默认域）
    auto resp = yomk::dds().node_name("interop_sub").domain_id(0).create();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleRos2InteropSub").error("create node failed: ", resp.msg);
        return 1;
    }

    // 类型对象 new 后、移交前 set_name 成 ROS2 约定名（宏实参位内联 new 无法先改名）
    std::atomic<int> received{0};
    auto* type = new YomkDdsStd::MStringPubSubType();
    type->set_name("std_msgs::msg::dds_::String_");
    auto onMessage = [&](const void* data)
    {
        auto* msg = static_cast<const YomkDdsStd::MString*>(data);
        received++;
        ::yomk::log().tag("ExampleRos2InteropSub").info("[RECV] ", msg->data());
    };
    // QoS 用 ROS2 订阅端默认底座：BEST_EFFORT requested 对任意 offered（含 ros2 topic
    // pub 与 YomkDds pub 的 RELIABLE）均可匹配（create_sub() 头文件注释同口径）
    resp =
        yomk::dds().node_name("interop_sub").topic("rt/chatter_interop").type_ptr(type).sub_cb(onMessage).qos(DDSQosProfile::ros2DefaultSubscriber()).create_sub();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleRos2InteropSub").error("register sub topic failed: ", resp.msg);
        YOMKDDS_DEL_NODE("interop_sub");
        return 1;
    }

    // 持续监听：收流回调逐条打印 [RECV]（不退出），Ctrl+C/SIGTERM 置位 g_stop 后收尾
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    while (!g_stop.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // 退出前显式销毁节点，打印总收条数
    resp = YOMKDDS_DEL_NODE("interop_sub");
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleRos2InteropSub").error("delete node failed: ", resp.msg);
        return 1;
    }
    std::cout << "[QUIT] received total=" << received.load() << std::endl;
    return 0;
}
