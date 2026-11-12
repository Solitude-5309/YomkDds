#pragma once
// YomkDdsDebugService 对外头：服务类 YomkDdsDebugService 的声明与调试进程级控制标志。
// 用户通常直接使用 YomkDdsAPI.h 中的 YOMKDDS_DEBUG_* 宏；请求结构见 YomkDdsPayload.h
// （yomk::payload），消息包声明见 YomkDdsMsg.h。调试能力由内部的 FastDDSDebugNode 提供
// （类型无关：经 DDS 发现机制取回远端 TypeObject 动态建订阅，消息以 JSON 文本投递给
// 用户回调，零消息类型依赖）。
#include <yomk/server/YomkServerApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include "YomkDdsPayload.h"  // 平铺引号三环境命中：安装树同目录/测试树平铺/主库平铺

#include <atomic>
#include <memory>
#include <mutex>
class FastDDSDebugNode;
using namespace yomk::payload;

class YomkDdsDebugService : public YomkService
{
public:
    YomkDdsDebugService(YomkServer* server);
    virtual ~YomkDdsDebugService();
    virtual int init() override;

private:
    yomk::payload::Response getVersion(YomkMsgPtr pkg);
    yomk::payload::Response createNode(YomkMsgPtr pkg);
    yomk::payload::Response topicPrint(YomkMsgPtr pkg);
    yomk::payload::Response listTopics(YomkMsgPtr pkg);
    yomk::payload::Response topicInfo(YomkMsgPtr pkg);
    yomk::payload::Response topicFind(YomkMsgPtr pkg);
    yomk::payload::Response interfaceShow(YomkMsgPtr pkg);
    yomk::payload::Response interfaceList(YomkMsgPtr pkg);
    yomk::payload::Response topicExample(YomkMsgPtr pkg);
    yomk::payload::Response topicMsg(YomkMsgPtr pkg);
    yomk::payload::Response topicPub(YomkMsgPtr pkg);
    yomk::payload::Response listNodes(YomkMsgPtr pkg);
    yomk::payload::Response nodeInfo(YomkMsgPtr pkg);
    yomk::payload::Response deleteNode(YomkMsgPtr pkg);

private:
    // 单 debug 节点：一个进程至多一个实例，重复 create 须先 delete；服务析构时自动销毁节点
    // 并按节点内部顺序清理全部 DDS 实体（推荐进程退出前经 /delete_node 显式清理）。
    std::unique_ptr<FastDDSDebugNode> node_;
    std::mutex mtx_;  // 串行化 node_ 的增删查改与节点入口调用
};

// topic pub 持续发布停止标志：发布循环（FastDDSDebugNode::topicPub 内部，repeatIntervalMs>0
// 时）只读，SIGINT 处理函数（CLI onSignal）经 debugPubStop 写——atomic 无锁 store 保证
// async-signal-safe（不可经 yomk 请求链停止：请求链路含锁与内存分配，handler 内禁用）。
// 调用方在发起持续发布前应先 debugPubReset 复位，避免上次会话残留置位导致循环秒退。
namespace yomk
{
inline std::atomic<bool> g_debugPubStop{false};
inline void debugPubStop()
{
    g_debugPubStop.store(true);
}
inline void debugPubReset()
{
    g_debugPubStop.store(false);
}
}  // namespace yomk
