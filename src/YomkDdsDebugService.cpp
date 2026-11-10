// YomkDdsDebugService 实现：将 /YomkDdsDebugService/* 端点的请求分发到对应 handler，委托给内部的
// FastDDSDebugNode（类型无关调试节点：发现 → 动态类型 → 自动订阅 → JSON 结构化输出）。
//
// 输出契约：消息内容零打印进服务层——/topic_print 携带用户回调，经 setOutputSink 原样转交节点，
// JSON 文本逐条投递给回调；仅服务自身状态错误走 YOMK_ERROR_TAG。
//
// 线程安全：node_ 的增删查改与节点入口调用统一以 mtx_ 串行化（createNode 内的 setDomainId 即
// create_participant，沿用 YomkDdsService::createNode 的既有结论：必须在锁内串行，见其注释）。
#include "YomkDdsDebugService.h"

#include "FastDDSDebugNode.h"
// 引 yomkdds_xml::ParticipantQosSpec（createNode 组 spec；FastDDSDebugNode.h 传递亦可得）
#include "FastDDSXmlHelper.h"

#include <set>
#include <utility>
#include <vector>

namespace
{
constexpr uint32_t kMaxDomainId = 232;  // DDS 域号上限
}  // namespace

YomkDdsDebugService::YomkDdsDebugService(YomkServer* server) : YomkService(server)
{
    name("/YomkDdsDebugService");
}

YomkDdsDebugService::~YomkDdsDebugService() = default;

int YomkDdsDebugService::init()
{
    installFunc("/version", &YomkDdsDebugService::getVersion);
    installFunc("/create_node", &YomkDdsDebugService::createNode);
    installFunc("/topic_print", &YomkDdsDebugService::topicPrint);
    installFunc("/list_topics", &YomkDdsDebugService::listTopics);
    installFunc("/topic_info", &YomkDdsDebugService::topicInfo);
    installFunc("/topic_find", &YomkDdsDebugService::topicFind);
    installFunc("/interface_show", &YomkDdsDebugService::interfaceShow);
    installFunc("/interface_list", &YomkDdsDebugService::interfaceList);
    installFunc("/topic_example", &YomkDdsDebugService::topicExample);
    installFunc("/topic_msg", &YomkDdsDebugService::topicMsg);
    installFunc("/topic_pub", &YomkDdsDebugService::topicPub);
    installFunc("/list_nodes", &YomkDdsDebugService::listNodes);
    installFunc("/node_info", &YomkDdsDebugService::nodeInfo);
    installFunc("/delete_node", &YomkDdsDebugService::deleteNode);
    return 0;
}

yomk::payload::Response YomkDdsDebugService::getVersion(YomkMsgPtr pkg)
{
    std::string version = "YomkDds v" YOMKDDS_VERSION_STRING;
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::String::shared(version));
}

yomk::payload::Response YomkDdsDebugService::createNode(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSDebugNode")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSDebugNode. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSDebugNode>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSDebugNode] is dynamic_pointer_cast failed. ");
    };

    if (p->msg.domainId > kMaxDomainId)
    {
        ::yomk::log().tag("YomkDdsDebugService::createNode").error("domainId [", p->msg.domainId, "] out of valid range [0,232]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "domainId [" + std::to_string(p->msg.domainId) + "] out of valid range [0,232]");
    }

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ != nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::createNode").error("debug node already exists, delete it first");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node already exists, delete it first");
    }

    auto node = std::make_unique<FastDDSDebugNode>();
    // XML 配置通道 + 发现服务器直配（闭环 4）：xmlCache 经 YomkDdsService::xmlCache()
    // 单例取用（load_xml 端点成功后非空）；双直配串空串=不直配现状
    auto spec = yomkdds_xml::ParticipantQosSpec{};
    spec.xml = YomkDdsService::xmlCache();
    spec.udpServerSpec = p->msg.udpDiscoverServerLocator;
    spec.tcpServerSpec = p->msg.tcpDiscoverServerLocator;
    std::string nodeErr;
    if (!node->setDomainId(p->msg.domainId, spec, &nodeErr))
    {
        ::yomk::log().tag("YomkDdsDebugService::createNode").error("create debug node failed: ", nodeErr.empty() ? "setDomainId error" : nodeErr);
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed,
            "create debug node failed: " + (nodeErr.empty() ? "setDomainId error" : nodeErr));
    }
    node_ = std::move(node);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsDebugService::topicPrint(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSDebugTopic")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSDebugTopic. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSDebugTopic>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSDebugTopic] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::topicPrint").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    if (p->msg.output == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::topicPrint").error("output callback is empty");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "output callback is empty");
    }
    // 先 setOutputSink 后 subscribeTopic：sink 须在工作线程建立订阅前就位（FastDDSDebugNode 约定）。
    node_->setOutputSink(p->msg.output);
    // 拒绝原因由节点层归类透传（未入域 / subscriber 未建 / 主题名空 / 重复登记），
    // 不再统一报笼统 failed；保留 failed 字样以维持调用方对失败语义的既有判定
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
        ::yomk::log().tag("YomkDdsDebugService::topicPrint").error(error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsDebugService::listTopics(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSDebugList")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSDebugList. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSDebugList>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSDebugList] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::listTopics").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // node_ 非空即已入域，listTopics 必返回 true；持锁收敛等待（最长约 stableRounds*intervalMs）：
    // 锁保护 node_ 生命周期不被并发 deleteNode 破坏（同 createNode 锁内 setDomainId 的既有约定），
    // 等待期间本服务其他端点请求被串行化（单用户 CLI 场景无实际影响）；0 值参数由节点层钳制默认
    std::vector<std::pair<std::string, std::string>> pairs;
    node_->listTopics(pairs, p->msg.stableRounds, p->msg.intervalMs);
    // 非 types 模式仅主题名；types 模式每行 "主题名 [类型名]"（单空格 + 方括号，对齐
    // ros2 topic list -t 形态；类型名原样输出，pair 已按主题名排序）
    std::vector<std::string> lines;
    lines.reserve(pairs.size());
    for (const auto& entry : pairs)
    {
        lines.emplace_back(p->msg.types ? entry.first + " [" + entry.second + "]" : entry.first);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::StringArray::shared(lines));
}

yomk::payload::Response YomkDdsDebugService::topicFind(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSDebugFind")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSDebugFind. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSDebugFind>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSDebugFind] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::topicFind").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 持锁取发现缓存全集快照（listTopics 已按主题名排序），按类型名精确匹配过滤；
    // 无匹配按查询失败处理（info 族语义），便于调用方发现类型名拼写错误
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
        ::yomk::log().tag("YomkDdsDebugService::topicFind").error("type [", p->msg.typeName, "] not found");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "type [" + p->msg.typeName + "] not found");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::StringArray::shared(lines));
}

yomk::payload::Response YomkDdsDebugService::interfaceShow(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSDebugInterfaceShow")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSDebugInterfaceShow. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSDebugInterfaceShow>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSDebugInterfaceShow] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::interfaceShow").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 持锁取该类型的 IDL 行集（节点层收敛轮询：TypeInformation→TypeObject→DynamicType 内省）；
    // 未发现该类型按查询失败处理（find 族语义），便于调用方发现类型名拼写错误。
    // 失败原因由节点层归类透传：类型名未出现 / TypeObject 不可得 / 顶层非 struct / IDL 拼装失败
    std::vector<std::string> lines;
    std::string error;
    if (!node_->interfaceShow(p->msg.typeName, lines, p->msg.stableRounds, p->msg.intervalMs, &error))
    {
        if (error.empty())
        {
            error = "type [" + p->msg.typeName + "] not found";  // 兜底，保证既有文本语义不回退
        }
        ::yomk::log().tag("YomkDdsDebugService::interfaceShow").error(error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::StringArray::shared(lines));
}

yomk::payload::Response YomkDdsDebugService::interfaceList(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSDebugInterfaceList")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSDebugInterfaceList. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSDebugInterfaceList>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSDebugInterfaceList] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::interfaceList").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 持锁取发现缓存全集快照（listTopics 已按主题名排序），收集非空类型名去重排序
    // （std::set 保证字典序，同类型多主题仅出一行）；域内无任何类型按查询失败处理
    // （find 族语义），便于调用方发现域号或收敛参数配置错误
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
        ::yomk::log().tag("YomkDdsDebugService::interfaceList").error("no interface types discovered");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "no interface types discovered");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
                        ::yomk::msgs::StringArray::shared(std::vector<std::string>(typeNames.begin(), typeNames.end())));
}

yomk::payload::Response YomkDdsDebugService::topicExample(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSDebugTopicExample")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSDebugTopicExample. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSDebugTopicExample>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSDebugTopicExample] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::topicExample").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 持锁取该主题的发布示例三段行集（节点层依次收敛：类型名→IDL 行集→JSON 示例；
    // 未发现主题按查询失败处理，info 族语义）。失败原因由节点层按步归类透传：
    // 步骤 1 topic not found / 步骤 2 类型侧原因 / 步骤 3 示例生成侧原因
    std::vector<std::string> lines;
    std::string error;
    if (!node_->topicExample(p->msg.topicName, lines, p->msg.stableRounds, p->msg.intervalMs, &error))
    {
        if (error.empty())
        {
            error = "topic [" + p->msg.topicName + "] not found";  // 兜底，保证既有文本语义不回退
        }
        ::yomk::log().tag("YomkDdsDebugService::topicExample").error(error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::StringArray::shared(lines));
}

yomk::payload::Response YomkDdsDebugService::topicMsg(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSDebugTopicMsg")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSDebugTopicMsg. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSDebugTopicMsg>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSDebugTopicMsg] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::topicMsg").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 持锁取该主题的消息描述两要素（节点层依次收敛：类型名→JSON 模板；未发现主题按
    // 查询失败处理，info 族语义）；StringArray 恰 2 行：d[0]=类型名、d[1]=紧凑 msg JSON
    // （机器可读契约，非展示行集）。失败原因由节点层按步归类透传：步骤 1 topic not found /
    // 步骤 2 JSON 模板生成侧原因
    std::string typeName;
    std::string json;
    std::string error;
    if (!node_->topicMsgJson(p->msg.topicName, typeName, json,
                p->msg.stableRounds, p->msg.intervalMs, &error))
    {
        if (error.empty())
        {
            error = "topic [" + p->msg.topicName + "] not found";  // 兜底，保证既有文本语义不回退
        }
        ::yomk::log().tag("YomkDdsDebugService::topicMsg").error(error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok",
            ::yomk::msgs::StringArray::shared(std::vector<std::string>{typeName, json}));
}

yomk::payload::Response YomkDdsDebugService::topicPub(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSDebugTopicPub")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSDebugTopicPub. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSDebugTopicPub>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSDebugTopicPub] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::topicPub").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // 持锁执行发布（节点层内部：发现收敛→类型重建→匹配收敛（requiredSubscribers>0 时先
    // 阻塞等待期望建匹配数达标，可被停止标志中断）→write 一次→ack 自适应确认；失败原因
    // 透传调用方：not found / invalid json / not all subscribers acknowledged /
    // waiting for subscribers interrupted 等）；repeatIntervalMs>0 为持续发布模式：首轮
    // 校验后节点层周期重复发送直到 g_debugPubStop 置位（CLI Ctrl+C）或发满 maxTimes 条
    // （>0 时，发满达标在销毁发布链前保底排空 200ms），统计 total/failed
    // （含首轮）经 StringArray 随响应带回
    std::string error;
    uint32_t total = 0;
    uint32_t failed = 0;
    if (!node_->topicPub(p->msg.topicName, p->msg.json, error, p->msg.stableRounds,
                p->msg.intervalMs, p->msg.repeatIntervalMs, p->msg.requiredSubscribers,
                p->msg.maxTimes, &total, &failed, p->msg.qos))
    {
        ::yomk::log().tag("YomkDdsDebugService::topicPub").error("pub failed: ", error);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, error);
    }
    if (p->msg.repeatIntervalMs > 0)
    {
        return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "delivered",
                ::yomk::msgs::StringArray::shared(std::vector<std::string>{
                        "total=" + std::to_string(total), "failed=" + std::to_string(failed)}));
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "delivered");
}

yomk::payload::Response YomkDdsDebugService::topicInfo(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSDebugInfo")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSDebugInfo. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSDebugInfo>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSDebugInfo] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::topicInfo").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // node_ 非空即已入域；持锁对单个目标主题做独立收敛查询（最长约 stableRounds*intervalMs）：
    // 锁保护 node_ 生命周期不被并发 deleteNode 破坏（同 listTopics 既有约定）；0 值参数由节点层钳制；
    // verbose 模式同一次查询顺带取端点详情（避免二次收敛轮询）
    std::string typeName;
    size_t publisherCount = 0;
    size_t subscriptionCount = 0;
    std::vector<FastDDSDebugNode::EndpointDetail> pubDetails;
    std::vector<FastDDSDebugNode::EndpointDetail> subDetails;
    if (!node_->topicInfo(p->msg.topicName, typeName, publisherCount, subscriptionCount,
                p->msg.stableRounds, p->msg.intervalMs,
                p->msg.verbose ? &pubDetails : nullptr,
                p->msg.verbose ? &subDetails : nullptr))
    {
        ::yomk::log().tag("YomkDdsDebugService::topicInfo").error("topic [", p->msg.topicName, "] not found");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "topic [" + p->msg.topicName + "] not found");
    }
    // 非 verbose 三行：类型名原样输出（不做任何风格转换）+ 两端点计数；verbose 追加逐端点
    // 详情段（空行规则：Type 后一段、每端点块前一段；count 为 0 的端点类型无清单段）
    std::vector<std::string> lines;
    if (!p->msg.verbose)
    {
        lines.reserve(3);
        lines.emplace_back("Type: " + typeName);
        lines.emplace_back("Publisher count: " + std::to_string(publisherCount));
        lines.emplace_back("Subscription count: " + std::to_string(subscriptionCount));
        return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::StringArray::shared(lines));
    }
    lines.emplace_back("Type: " + typeName);
    lines.emplace_back("");
    lines.emplace_back("Publisher count: " + std::to_string(publisherCount));
    for (const auto& detail : pubDetails)
    {
        lines.emplace_back("");
        lines.emplace_back("Node name: " + detail.nodeName);
        lines.emplace_back("Endpoint type: PUBLISHER");
        lines.emplace_back("GUID: " + detail.guid);
        lines.emplace_back("QoS profile:");
        lines.insert(lines.end(), detail.qosLines.begin(), detail.qosLines.end());
    }
    lines.emplace_back("");
    lines.emplace_back("Subscription count: " + std::to_string(subscriptionCount));
    for (const auto& detail : subDetails)
    {
        lines.emplace_back("");
        lines.emplace_back("Node name: " + detail.nodeName);
        lines.emplace_back("Endpoint type: SUBSCRIPTION");
        lines.emplace_back("GUID: " + detail.guid);
        lines.emplace_back("QoS profile:");
        lines.insert(lines.end(), detail.qosLines.begin(), detail.qosLines.end());
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::StringArray::shared(lines));
}

yomk::payload::Response YomkDdsDebugService::listNodes(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSNodeList")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSNodeList. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSNodeList>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSNodeList] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::listNodes").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // node_ 非空即已入域，listNodes 必返回 true；持锁收敛等待（最长约 stableRounds*intervalMs）：
    // 锁保护 node_ 生命周期不被并发 deleteNode 破坏（同 listTopics 既有约定）；0 值参数由节点层
    // 钳制为默认。无 "not found" 分支——空列表合法（域内暂无命名参与者），语义同 list_topics 空域
    std::vector<std::string> names;
    node_->nodeList(names, p->msg.stableRounds, p->msg.intervalMs);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::StringArray::shared(names));
}

yomk::payload::Response YomkDdsDebugService::nodeInfo(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSNodeInfo")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSNodeInfo. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSNodeInfo>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSNodeInfo] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::nodeInfo").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    // node_ 非空即已入域；持锁对单个目标节点名做独立收敛查询（最长约 stableRounds*intervalMs）：
    // 锁保护 node_ 生命周期不被并发 deleteNode 破坏（同 listNodes 既有约定）；0 值参数由节点层钳制
    std::vector<std::pair<std::string, std::string>> publishers;
    std::vector<std::pair<std::string, std::string>> subscribers;
    if (!node_->nodeInfo(p->msg.nodeName, publishers, subscribers, p->msg.stableRounds, p->msg.intervalMs))
    {
        ::yomk::log().tag("YomkDdsDebugService::nodeInfo").error("node [", p->msg.nodeName, "] not found");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] not found");
    }
    // 输出行（对齐 ros2 node info 形态）：节点名行 + Subscribers 段 + Publishers 段，每条主题
    // 一行 "    <topic>: <type>"（类型名原样输出，无任何风格转换）；空段仅打段头
    std::vector<std::string> lines;
    lines.reserve(3 + publishers.size() + subscribers.size());
    lines.emplace_back(p->msg.nodeName);
    lines.emplace_back("  Subscribers:");
    for (const auto& entry : subscribers)
    {
        lines.emplace_back("    " + entry.first + ": " + entry.second);
    }
    lines.emplace_back("  Publishers:");
    for (const auto& entry : publishers)
    {
        lines.emplace_back("    " + entry.first + ": " + entry.second);
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::StringArray::shared(lines));
}

yomk::payload::Response YomkDdsDebugService::deleteNode(YomkMsgPtr pkg)
{
    (void)pkg;  // 单 debug 节点模型无参数载荷

    std::lock_guard<std::mutex> lock(mtx_);
    if (node_ == nullptr)
    {
        ::yomk::log().tag("YomkDdsDebugService::deleteNode").error("debug node not created");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "debug node not created");
    }
    node_.reset();  // 节点析构即按其内部顺序（停工作线程 → reader → topic → subscriber → participant）清理
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}
