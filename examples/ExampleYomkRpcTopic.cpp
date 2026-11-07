/**
 * @file ExampleYomkRpcTopic.cpp
 * @brief 同节点同主题自发自收示例（rpc_demo_topic，MString）：单进程单节点先注册发布主题
 *       再注册订阅主题，等待发现后发布 5 条并打印接收统计，退出前 DEL_NODE 清理。
 */
#include <YomkRpc/YomkRpcAPI.h>
#include <YomkServer/YomkApi.h>

#include <YomkRpcMsg/YomkRpcMsg.hpp>
#include <YomkRpcMsg/YomkRpcMsgPubSubTypes.hpp>
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
    ::yomk::server().srv<YomkRpcService>().create();

    // 查看版本（宏内部自动解包并打印）
    ::yomk::log().tag("ExampleYomkRpcTopic").info("=== YomkRpcService::getVersion ===");
    YOMKRPC_VERSION();

    // 1. 创建节点（每节点一个独立 DDS 参与者）
    ::yomk::log().tag("ExampleYomkRpcTopic").info("=== YomkRpcService::createNode ===");
    auto resp = YOMKRPC_NODE("node0");
    if (resp.m_status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkRpcTopic").error("create node failed: ", resp.m_msg);
        return 1;
    }

    // 2. 注册发布主题（type 所有权移交 FastDDSNode）
    ::yomk::log().tag("ExampleYomkRpcTopic").info("=== YomkRpcService::registerPubTopic ===");
    resp = YOMKRPC_PUB_TOPIC("node0", "rpc_demo_topic", new YomkRpc::MStringPubSubType());
    if (resp.m_status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkRpcTopic").error("register pub topic failed: ", resp.m_msg);
        YOMKRPC_DEL_NODE("node0");
        return 1;
    }

    // 3. 注册订阅主题：回调中累加计数并记录最后一条消息
    ::yomk::log().tag("ExampleYomkRpcTopic").info("=== YomkRpcService::registerSubTopic ===");
    std::atomic<int> received{0};
    std::mutex msgMtx;
    std::string lastMsg;
    auto onMessage = [&](const void* data)
    {
        // data 为交付的消息实例指针（仅回调期间有效），转型为具体消息类型后读取
        auto* msg = static_cast<const YomkRpc::MString*>(data);
        std::lock_guard<std::mutex> lock(msgMtx);
        lastMsg = msg->data();
        received++;
        ::yomk::log().tag("ExampleYomkRpcTopic").info("[RECV] 收到消息: ", msg->data());
    };
    resp = YOMKRPC_SUB_TOPIC("node0", "rpc_demo_topic", new YomkRpc::MStringPubSubType(), onMessage);
    if (resp.m_status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkRpcTopic").error("register sub topic failed: ", resp.m_msg);
        YOMKRPC_DEL_NODE("node0");
        return 1;
    }

    // 4. 等待 DDS discovery 完成后发布 5 条消息
    ::yomk::log().tag("ExampleYomkRpcTopic").info("=== YomkRpcService::publish ===");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    for (int i = 0; i < 5; ++i)
    {
        YomkRpc::MString msg;
        msg.data("Hello YomkRpcService " + std::to_string(i));
        resp = YOMKRPC_PUB_MSG("node0", "rpc_demo_topic", &msg);
        if (resp.m_status != yomk::payload::ResStatus::Succeeded)
        {
            ::yomk::log().tag("ExampleYomkRpcTopic").error("[SEND] 发布失败: ", msg.data(), " status=", static_cast<int>(resp.m_status));
            break;
        }
        ::yomk::log().tag("ExampleYomkRpcTopic").info("[SEND] 发布消息: ", msg.data());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 5. 等待接收完成（首条消息可能因 discovery 时序丢失），打印接收统计
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::string lastMsgCopy;
    {
        std::lock_guard<std::mutex> lock(msgMtx);
        lastMsgCopy = lastMsg;
    }
    ::yomk::log().tag("ExampleYomkRpcTopic").info("[RESULT] 接收=", received.load(), "/5 最后一条=", lastMsgCopy);

    // 6. 退出前销毁节点，确保 DDS 实体在 FastDDS 静态资源销毁前清理
    ::yomk::log().tag("ExampleYomkRpcTopic").info("=== YomkRpcService::deleteNode ===");
    resp = YOMKRPC_DEL_NODE("node0");
    if (resp.m_status != yomk::payload::ResStatus::Succeeded)
    {
        ::yomk::log().tag("ExampleYomkRpcTopic").error("delete node failed: ", resp.m_msg);
        return 1;
    }
    return 0;
}
