// 输出契约：消息内容零打印进服务层——/print_topic 携带用户回调经 setOutputSink 原样转交节点，
// 仅服务自身状态错误走 YOMK_ERROR_TAG。
// createNode 的 setDomainId 即 create_participant，必须在 mtx_ 内串行（同 YomkDdsHandler::createNode 注释）。
#include "YomkDdsDebugHandler.h"
#include "YomkDdsMsg.h"

#include "FastDDSDebugNode.h"
// 显式引入 yomkdds_xml::ParticipantQosSpec（FastDDSDebugNode.h 传递亦可得，对齐 IWYU）
#include "FastDDSXmlHelper.h"

#include <set>
#include <utility>
#include <vector>

namespace
{
constexpr uint32_t kMaxDomainId = 232;
}  // namespace

YomkDdsDebugHandler::YomkDdsDebugHandler(YomkServer* server) : YomkService(server)
{
    name("/YomkDdsDebugHandler");
}

YomkDdsDebugHandler::~YomkDdsDebugHandler() = default;

int YomkDdsDebugHandler::init()
{
    installFunc("/get_version", &YomkDdsDebugHandler::getVersion);
    installFunc("/create_node", &YomkDdsDebugHandler::createNode);
    installFunc("/print_topic", &YomkDdsDebugHandler::printTopic);
    installFunc("/list_topics", &YomkDdsDebugHandler::listTopics);
    installFunc("/get_topic_info", &YomkDdsDebugHandler::getTopicInfo);
    installFunc("/find_topics", &YomkDdsDebugHandler::findTopics);
    installFunc("/show_interface", &YomkDdsDebugHandler::showInterface);
    installFunc("/list_interfaces", &YomkDdsDebugHandler::listInterfaces);
    installFunc("/get_topic_example", &YomkDdsDebugHandler::getTopicExample);
    installFunc("/get_topic_msg", &YomkDdsDebugHandler::getTopicMsg);
    installFunc("/start_pub", &YomkDdsDebugHandler::startPub);
    installFunc("/stop_pub", &YomkDdsDebugHandler::stopPub);
    installFunc("/list_nodes", &YomkDdsDebugHandler::listNodes);
    installFunc("/get_node_info", &YomkDdsDebugHandler::getNodeInfo);
    installFunc("/delete_node", &YomkDdsDebugHandler::deleteNode);
    return 0;
}

yomk::payload::Response YomkDdsDebugHandler::getVersion(YomkMsgPtr pkg)
{
    std::string version = "YomkDds v" YOMKDDS_VERSION_STRING;
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::String::shared(version));
}

yomk::payload::Response YomkDdsDebugHandler::createNode(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::DebugCreateNodeRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DebugCreateNodeRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::DebugCreateNodeRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[DebugCreateNodeRequest] is dynamic_pointer_cast failed. ");
    };

    if (p->msg.domainId > kMaxDomainId)
    {
        yomk::log().tag("YomkDdsDebugHandler::createNode").error("domainId [", p->msg.domainId, "] out of valid range [0,232]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "domainId [" + std::to_string(p->msg.domainId) + "] out of valid range [0,232]");
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ != nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::createNode").error("debug node already exists, delete it first");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node already exists, delete it first");
    }

    auto node = std::make_unique<FastDDSDebugNode>();
    // xmlCache 经 YomkDdsHandler::xmlCache() 单例取用（load_xml 成功后非空）；
    // 直配串空串=不直配现状
    auto spec = yomkdds_xml::ParticipantQosSpec{};
    spec.xml = YomkDdsHandler::xmlCache();
    spec.udpServerSpec = p->msg.udpDiscoverServerLocator;
    spec.tcpServerSpec = p->msg.tcpDiscoverServerLocator;
    std::string nodeErr;
    if (!node->setDomainId(p->msg.domainId, spec, &nodeErr))
    {
        yomk::log().tag("YomkDdsDebugHandler::createNode").error("create debug node failed: ", nodeErr.empty() ? "setDomainId error" : nodeErr);
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed,
            "create debug node failed: " + (nodeErr.empty() ? "setDomainId error" : nodeErr));
    }
    node_ = std::move(node);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsDebugHandler::printTopic(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::DebugPrintTopicRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DebugPrintTopicRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::DebugPrintTopicRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[DebugPrintTopicRequest] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::printTopic").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    if (p->msg.output == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::printTopic").error("output callback is empty");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "output callback is empty");
    }
    // 先 setOutputSink 后 subscribeTopic：sink 须在工作线程建立订阅前就位（FastDDSDebugNode 约定）。
    node_->setOutputSink(p->msg.output);
    // 拒绝原因由节点层归类透传；保留 failed 字样前缀以维持调用方对失败语义的既有判定
    std::string error;
    if (!node_->subscribeTopic(p->msg.topicName, &error, p->msg.qos))
    {
        if (error.empty())
        {
            error = "subscribeTopic [" + p->msg.topicName + "] failed";  // 兜底，避免空 msg
        }
        else
        {
            error = "subscribeTopic [" + p->msg.topicName + "] failed: " + error;
        }
        yomk::log().tag("YomkDdsDebugHandler::printTopic").error(error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsDebugHandler::listTopics(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::DebugListTopicsRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DebugListTopicsRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::DebugListTopicsRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[DebugListTopicsRequest] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::listTopics").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 持锁阻塞收敛等待（最长约 stableRounds*intervalMs）：锁保护 node_ 生命周期不被并发
    // deleteNode 破坏，等待期间本服务其他端点被串行化（单用户 CLI 场景可接受）；
    // node_ 非空即已入域，listTopicInfos 必返回 true；0 值收敛参数由节点层钳制默认
    std::vector<yomk::payload::dds::TopicInfo> infos;
    node_->listTopicInfos(infos, p->msg.stableRounds, p->msg.intervalMs);
    // 只回结构化数据：类型名恒在载荷内，行文本（含是否带类型名）由调用方决定
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
            yomk::msgs::dds::TopicInfoList::shared(yomk::payload::dds::TopicInfoList{std::move(infos)}));
}

yomk::payload::Response YomkDdsDebugHandler::findTopics(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::DebugFindTopicsRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DebugFindTopicsRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::DebugFindTopicsRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[DebugFindTopicsRequest] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::findTopics").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 无匹配按查询失败处理（find 族语义），便于暴露类型名拼写错误
    std::vector<std::pair<std::string, std::string>> pairs;
    node_->listTopics(pairs, p->msg.stableRounds, p->msg.intervalMs);
    std::vector<std::string> lines;
    for (const auto& entry : pairs)
    {
        if (entry.second == p->msg.typeName)
        {
            lines.emplace_back(entry.first);
        }
    }
    if (lines.empty())
    {
        yomk::log().tag("YomkDdsDebugHandler::findTopics").error("type [", p->msg.typeName, "] not found");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "type [" + p->msg.typeName + "] not found");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::StringArray::shared(lines));
}

yomk::payload::Response YomkDdsDebugHandler::showInterface(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::DebugShowInterfaceRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DebugShowInterfaceRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::DebugShowInterfaceRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[DebugShowInterfaceRequest] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::showInterface").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 失败原因由节点层归类透传：类型名未出现 / TypeObject 不可得 / 顶层非 struct / 反射采集失败；
    // 未发现该类型按查询失败处理（find 族语义），便于暴露类型名拼写错误
    yomk::payload::dds::InterfaceInfo info;
    std::string error;
    if (!node_->interfaceShow(p->msg.typeName, info, p->msg.stableRounds, p->msg.intervalMs, &error))
    {
        if (error.empty())
        {
            error = "type [" + p->msg.typeName + "] not found";  // 兜底，保证既有文本语义不回退
        }
        yomk::log().tag("YomkDdsDebugHandler::showInterface").error(error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
            yomk::msgs::dds::InterfaceInfo::shared(std::move(info)));
}

yomk::payload::Response YomkDdsDebugHandler::listInterfaces(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::DebugListInterfacesRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DebugListInterfacesRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::DebugListInterfacesRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[DebugListInterfacesRequest] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::listInterfaces").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 域内无任何类型按查询失败处理（find 族语义），便于暴露域号或收敛参数配置错误
    std::vector<std::pair<std::string, std::string>> pairs;
    node_->listTopics(pairs, p->msg.stableRounds, p->msg.intervalMs);
    std::set<std::string> typeNames;
    for (const auto& entry : pairs)
    {
        if (!entry.second.empty())
        {
            typeNames.insert(entry.second);
        }
    }
    if (typeNames.empty())
    {
        yomk::log().tag("YomkDdsDebugHandler::listInterfaces").error("no interface types discovered");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "no interface types discovered");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
                        yomk::msgs::StringArray::shared(std::vector<std::string>(typeNames.begin(), typeNames.end())));
}

yomk::payload::Response YomkDdsDebugHandler::getTopicExample(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::DebugGetTopicExampleRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DebugGetTopicExampleRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::DebugGetTopicExampleRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[DebugGetTopicExampleRequest] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::getTopicExample").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 失败原因由节点层按步归类透传：步骤 1 topic not found / 步骤 2 类型侧 / 步骤 3 示例生成侧；
    // 未发现主题按查询失败处理（info 族语义）。响应只含结构化包，段头/IDL 语法/发布命令由调用方渲染
    yomk::payload::dds::TopicExampleInfo info;
    std::string error;
    if (!node_->topicExample(p->msg.topicName, info, p->msg.stableRounds, p->msg.intervalMs, &error))
    {
        if (error.empty())
        {
            error = "topic [" + p->msg.topicName + "] not found";  // 兜底，保证既有文本语义不回退
        }
        yomk::log().tag("YomkDdsDebugHandler::getTopicExample").error(error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
            yomk::msgs::dds::TopicExampleInfo::shared(std::move(info)));
}

yomk::payload::Response YomkDdsDebugHandler::getTopicMsg(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::DebugGetTopicMsgRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DebugGetTopicMsgRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::DebugGetTopicMsgRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[DebugGetTopicMsgRequest] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::getTopicMsg").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 失败原因由节点层按步归类透传：步骤 1 topic not found / 步骤 2 JSON 模板生成侧；
    // 是否落盘由调用方处理，响应不含展示文本
    yomk::payload::dds::TopicMessageInfo info;
    std::string error;
    if (!node_->topicMsg(p->msg.topicName, info, p->msg.stableRounds, p->msg.intervalMs, &error))
    {
        if (error.empty())
        {
            error = "topic [" + p->msg.topicName + "] not found";  // 兜底，保证既有文本语义不回退
        }
        yomk::log().tag("YomkDdsDebugHandler::getTopicMsg").error(error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
            yomk::msgs::dds::TopicMessageInfo::shared(std::move(info)));
}

yomk::payload::Response YomkDdsDebugHandler::startPub(YomkMsgPtr pkg)
{
    if (!pkg || pkg->name() != yomk::msgs::dds::DebugTopicPubRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DebugTopicPubRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::DebugTopicPubRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[DebugTopicPubRequest] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::startPub").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 持锁长驻阻塞；repeatIntervalMs>0 为持续发布模式，节点层周期发送直到 g_debugPubStop
    // （CLI Ctrl+C 或 /stop_pub）置位或发满 maxTimes 条；计数（含首轮）随结构化响应带回，
    // 单次发布同样非空
    std::string error;
    yomk::payload::dds::TopicPublishInfo info;
    if (!node_->topicPub(p->msg.topicName, p->msg.json, info, error, p->msg.stableRounds,
                p->msg.intervalMs, p->msg.repeatIntervalMs, p->msg.requiredSubscribers,
                p->msg.maxTimes, p->msg.qos))
    {
        yomk::log().tag("YomkDdsDebugHandler::startPub").error("pub failed: ", error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "delivered",
            yomk::msgs::dds::TopicPublishInfo::shared(std::move(info)));
}

yomk::payload::Response YomkDdsDebugHandler::stopPub(YomkMsgPtr pkg)
{
    // pkg 无关：本端点是协议级停止的桥——无锁直置进程内控制面标志。
    // 绝不可取 mtx_：此刻可能正有 startPub 持锁长驻阻塞（repeat 循环），取锁即互锁死；
    // 发布循环轮询到置位即收尾返回，被阻塞的 startPub 调用带回 total/failed 计数。
    // 期望建匹配等待期（waiting for subscribers）置位同样中断并报错返回。
    (void)pkg;
    yomk::ctrl::dds::g_debugPubStop.stop();
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "stop requested");
}

yomk::payload::Response YomkDdsDebugHandler::getTopicInfo(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::DebugGetTopicInfoRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DebugGetTopicInfoRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::DebugGetTopicInfoRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[DebugGetTopicInfoRequest] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::getTopicInfo").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // node_ 非空即已入域；持锁阻塞收敛（锁保护 node_ 生命周期，同 listTopics）；0 值参数由
    // 节点层钳制；端点详情恒采集——展示与否由调用方决定（服务层不出文本）
    yomk::payload::dds::TopicInfo info;
    if (!node_->topicInfo(p->msg.topicName, info, p->msg.stableRounds, p->msg.intervalMs))
    {
        yomk::log().tag("YomkDdsDebugHandler::getTopicInfo").error("topic [", p->msg.topicName, "] not found");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "topic [" + p->msg.topicName + "] not found");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
            yomk::msgs::dds::TopicInfo::shared(std::move(info)));
}

yomk::payload::Response YomkDdsDebugHandler::listNodes(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::DebugListNodesRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DebugListNodesRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::DebugListNodesRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[DebugListNodesRequest] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::listNodes").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // node_ 非空即已入域，listNodes 必返回 true；无 "not found" 分支——空列表合法（域内暂无
    // 命名参与者）；0 值收敛参数由节点层钳制默认
    std::vector<std::string> names;
    node_->nodeList(names, p->msg.stableRounds, p->msg.intervalMs);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", yomk::msgs::StringArray::shared(names));
}

yomk::payload::Response YomkDdsDebugHandler::getNodeInfo(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != yomk::msgs::dds::DebugGetNodeInfoRequest::kName)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DebugGetNodeInfoRequest. ");
    }
    auto p = std::dynamic_pointer_cast<yomk::msgs::dds::DebugGetNodeInfoRequest>(pkg);
    if (!p)
    {
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, " pkg[DebugGetNodeInfoRequest] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::getNodeInfo").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // node_ 非空即已入域；持锁阻塞收敛（锁保护 node_ 生命周期，同 listNodes）；0 值参数由节点层钳制
    yomk::payload::dds::NodeInfo info;
    if (!node_->nodeInfo(p->msg.nodeName, info, p->msg.stableRounds, p->msg.intervalMs))
    {
        yomk::log().tag("YomkDdsDebugHandler::getNodeInfo").error("node [", p->msg.nodeName, "] not found");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] not found");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
            yomk::msgs::dds::NodeInfo::shared(std::move(info)));
}

yomk::payload::Response YomkDdsDebugHandler::deleteNode(YomkMsgPtr pkg)
{
    (void)pkg;  // 单 debug 节点模型无参数载荷

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        yomk::log().tag("YomkDdsDebugHandler::deleteNode").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    node_.reset();  // 析构按节点层内部顺序清理（停工作线程 → reader → topic → subscriber → participant）
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}
