/**
 * @file ExampleYomkDdsTopic.cpp
 * @brief 同节点同主题自发自收示例（dds_demo_topic，MString）：单进程单节点先注册发布主题
 *       再注册订阅主题，等待发现后发布 5 条并打印接收统计，退出前 DEL_NODE 清理。
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

// 单进程完整流程示例：同一节点内注册发布与订阅主题，发布 5 条 MString 并在订阅回调中接收打印，最后销毁节点。
// 覆盖 create_node / register_pub_topic / register_sub_topic / publish / delete_node 全流程。
int main(int argc, char* argv[])
{
    ::yomk::server().init();
    ::yomk::server().srv<YomkDdsService>().create();

    // 查看版本（链式 version() 返回 Response，解包/打印由调用方处理）
    ::yomk::log().tag("ExampleYomkDdsTopic").info("=== YomkDdsService::getVersion ===");
    auto vresp = yomk::dds().version();
    if (vresp.status == yomk::payload::ResStatus::Succeeded)
    {
        auto ver = ::yomk::msgs::unpack<::yomk::msgs::String>(vresp.data);
        if (ver)
        {
            ::yomk::log().tag("YomkDdsService").info(ver->d);
        }
    }
    else
    {
        ::yomk::log().tag("YomkDdsService").error("getVersion failed: ", vresp.msg);
    }

    // 1. 创建节点（每节点一个独立 DDS 参与者）
    ::yomk::log().tag("ExampleYomkDdsTopic").info("=== YomkDdsService::createNode ===");
    auto resp = yomk::dds().node_name("node0").create();
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsTopic").error("create node failed: ", resp.msg);
        return 1;
    }

    // 2. 注册发布主题（type 所有权移交 FastDDSNode）
    ::yomk::log().tag("ExampleYomkDdsTopic").info("=== YomkDdsService::registerPubTopic ===");
    resp = YOMKDDS_PUB_TOPIC("node0", "dds_demo_topic", new YomkDdsStd::MStringPubSubType());
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsTopic").error("register pub topic failed: ", resp.msg);
        YOMKDDS_DEL_NODE("node0");
        return 1;
    }

    // 3. 注册订阅主题：回调中累加计数并记录最后一条消息
    ::yomk::log().tag("ExampleYomkDdsTopic").info("=== YomkDdsService::registerSubTopic ===");
    std::atomic<int> received{0};
    std::mutex msgMtx;
    std::string lastMsg;
    auto onMessage = [&](const void* data)
    {
        // data 为交付的消息实例指针（仅回调期间有效），转型为具体消息类型后读取
        auto* msg = static_cast<const YomkDdsStd::MString*>(data);
        std::lock_guard<std::mutex> lock(msgMtx);
        lastMsg = msg->data();
        received++;
        ::yomk::log().tag("ExampleYomkDdsTopic").info("[RECV] 收到消息: ", msg->data());
    };
    resp = YOMKDDS_SUB_TOPIC("node0", "dds_demo_topic", new YomkDdsStd::MStringPubSubType(), onMessage);
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsTopic").error("register sub topic failed: ", resp.msg);
        YOMKDDS_DEL_NODE("node0");
        return 1;
    }

    // 4. 等待 DDS discovery 完成后发布 5 条消息
    ::yomk::log().tag("ExampleYomkDdsTopic").info("=== YomkDdsService::publish ===");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    for (int i = 0; i < 5; ++i)
    {
        YomkDdsStd::MString msg;
        msg.data("Hello YomkDdsService " + std::to_string(i));
        resp = YOMKDDS_PUB_MSG("node0", "dds_demo_topic", &msg);
        if (resp.status != yomk::payload::ResStatus::Succeeded)
        {
            ::yomk::log().tag("ExampleYomkDdsTopic").error("[SEND] 发布失败: ", msg.data(), " status=", static_cast<int>(resp.status));
            break;
        }
        ::yomk::log().tag("ExampleYomkDdsTopic").info("[SEND] 发布消息: ", msg.data());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 5. 等待接收完成（首条消息可能因 discovery 时序丢失），打印接收统计
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::string lastMsgCopy;
    {
        std::lock_guard<std::mutex> lock(msgMtx);
        lastMsgCopy = lastMsg;
    }
    ::yomk::log().tag("ExampleYomkDdsTopic").info("[RESULT] 接收=", received.load(), "/5 最后一条=", lastMsgCopy);

    // 6. 退出前销毁节点，确保 DDS 实体在 FastDDS 静态资源销毁前清理
    ::yomk::log().tag("ExampleYomkDdsTopic").info("=== YomkDdsService::deleteNode ===");
    resp = YOMKDDS_DEL_NODE("node0");
    if (resp.status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkDdsTopic").error("delete node failed: ", resp.msg);
        return 1;
    }
    return 0;
}
