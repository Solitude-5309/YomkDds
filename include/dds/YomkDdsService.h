#pragma once
// YomkDdsService 对外头：服务类 YomkDdsService 的声明。用户通常直接使用 YomkDdsAPI.h
// 中的宏；请求/响应结构见 YomkDdsPayload.h（yomk::payload），消息包声明见 YomkDdsMsg.h。
#include <yomk/server/YomkServerApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include "YomkDdsPayload.h"  // 平铺引号三环境命中：安装树同目录/测试树平铺/主库平铺

#include <map>
#include <memory>
#include <mutex>
#include <string>
class FastDDSNode;
using namespace yomk::payload;

class YomkDdsService : public YomkService
{
public:
    YomkDdsService(YomkServer* server);
    virtual ~YomkDdsService();
    virtual int init() override;

private:
    yomk::payload::Response getVersion(YomkMsgPtr pkg);
    yomk::payload::Response loadXml(YomkMsgPtr pkg);
    yomk::payload::Response createNode(YomkMsgPtr pkg);
    yomk::payload::Response deleteNode(YomkMsgPtr pkg);
    yomk::payload::Response registerPubTopic(YomkMsgPtr pkg);
    yomk::payload::Response registerSubTopic(YomkMsgPtr pkg);
    yomk::payload::Response publish(YomkMsgPtr pkg);
    yomk::payload::Response loan(YomkMsgPtr pkg);
    yomk::payload::Response discardLoan(YomkMsgPtr pkg);

private:
    std::map<std::string, std::unique_ptr<FastDDSNode>> nodes_;
    std::mutex mtx_;

    // 已成功加载的 XML profiles raw 串（load_XML_profiles_file 成功后缓存）：get_*_qos_from_xml
    // 系列吃 raw 串非文件路径，节点层 participant 三级提取链经 xmlCache() 取用；空串=未加载。
    // 三服务同进程，Debug/Bag 节点创建时同样经此取 XML 串。
    static std::string xmlCache_;

public:
    // XML profiles raw 串访问器（线程安全：load 与 create_node 均在各自服务 mtx_ 内，
    // 串写竞态由调用方 loadXml/createNode 的锁序保证；读取侧仅拷贝）。空串=未加载。
    static const std::string& xmlCache();
};
