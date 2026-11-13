/**
 * @file TestRos2InteropPub.cpp
 * @brief 真机互通回归判定程序·发布端（run_ros2_interop.sh 驱动，ctest 树不含）
 *
 * 测试设施定位（examples 的 ExampleRos2InteropPub 为面向用户的教学示例，判定职责归本目录）：
 * 域 0 建节点 + MStringPubSubType set_name 成 ROS2 约定名（std_msgs::msg::dds_::String_）
 * 注册 rt/chatter_interop（ros2DefaultPublisher 底座），发布内容 "hello ros2 N"（N 递增），
 * 1Hz 发布 30 条后正常退出（脚本 timeout 兜底）。
 *
 * 对跑协议（run_ros2_interop.sh）：
 *   方向 1：本程序 → ros2 topic echo /chatter_interop --once（判定 ROS2 收到）
 *
 * 话题名口径：rmw_fastrtps 对 ROS2 语义话题名加 rt/ 前缀生成实际 DDS 话题名，
 * YomkDds 端直接用 rt/ 形态注册（README 5.6 互通三要素之一）。
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

static std::atomic<bool> g_stop{false}; // SIGINT/SIGTERM 退出标志

static void onSignal(int)
{
    g_stop.store(true);
}

int main()
{
    ::yomk::server().init();
    ::yomk::server().srv<YomkDdsService>().create();

    // 创建节点（域 0 对齐 ROS2 默认域）
    auto resp = yomk::dds().node("interop_pub").domain(0).create();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("TestRos2InteropPub").error("create node failed: ", resp.msg);
        return 1;
    }

    // 类型对象 new 后、移交前 set_name 成 ROS2 约定名（宏实参位内联 new 无法先改名）
    auto *type = new YomkDdsStd::MStringPubSubType();
    type->set_name("std_msgs::msg::dds_::String_");
    // QoS 用 ROS2 发布端默认底座：RELIABLE offered 对 ros2 topic echo 任意 requested 可匹配
    resp = yomk::dds().node("interop_pub").topic("rt/chatter_interop").type(type).qos(DDSQosProfile::ros2DefaultPublisher()).create_pub();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("TestRos2InteropPub").error("register pub topic failed: ", resp.msg);
        yomk::dds().node("interop_pub").remove();
        return 1;
    }

    // 1Hz 发布 30 条后正常退出（Ctrl+C 提前中断）
    std::signal(SIGINT, onSignal);
    std::this_thread::sleep_for(std::chrono::seconds(1)); // 等待 discovery
    for (int i = 0; i < 30 && !g_stop.load(); ++i)
    {
        YomkDdsStd::MString msg;
        msg.data("hello ros2 " + std::to_string(i));
        resp = yomk::dds().node("interop_pub").topic("rt/chatter_interop").data(&msg).pub();
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            ::yomk::log().tag("TestRos2InteropPub").error("[SEND] publish failed");
        }
        else
        {
            ::yomk::log().tag("TestRos2InteropPub").info("[SEND] ", msg.data());
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 退出前显式销毁节点
    resp = yomk::dds().node("interop_pub").remove();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("TestRos2InteropPub").error("delete node failed: ", resp.msg);
        return 1;
    }
    return 0;
}
