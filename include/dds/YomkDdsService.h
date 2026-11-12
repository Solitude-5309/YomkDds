#pragma once
// Maintainer notes: core communication service; owns one FastDDSNode per node
// name in nodes_. The XML profiles raw cache is process-wide: the debug and
// bag services read it through xmlCache() when creating their participants.
// Ownership protocol: register_pub/sub_topic takes over the TypeSupport
// instance unconditionally (failure paths included); publish borrows the
// sample; loan hands out a writer-pool pointer reclaimed on write/discard.
#include <yomk/server/YomkServerApi.h>
#include <yomk/log/YomkLoggerApi.h>

#include "YomkDdsPayload.h"  // flat quoted include: install tree, test tree and main tree all resolve same-dir

#include <map>
#include <memory>
#include <mutex>
#include <string>
class FastDDSNode;
using namespace yomk::payload;

/** @brief Core communication service: node lifecycle, pub/sub endpoints and zero-copy loan */
// @zh: 核心通信服务：节点生命周期、发布/订阅端点与零拷贝借出
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
    std::mutex mtx_;  // serializes all node entry calls (create/delete/register/publish)

    // XML profiles raw string cached after a successful load: get_*_qos_from_xml
    // consumes the raw string, not a path; empty = not loaded. Written under
    // mtx_ inside loadXml/createNode; readers only copy.
    static std::string xmlCache_;

public:
    /** @brief Access the cached XML profiles raw string (thread-safe; empty = not loaded) */
    // @zh: 已加载 XML profiles raw 串访问器（线程安全；空串=未加载）
    static const std::string& xmlCache();
};
