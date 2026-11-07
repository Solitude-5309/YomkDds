// YomkRpcService 实现：将 /YomkRpcService/* 端点的请求分发到对应 handler，再委托给 FastDDSNode 执行 DDS 操作。
//
// 内存所有权协议（贯穿全部 handler）：
//   - type（DDSTopic/DDSSubRequest.type）：caller new，服务端全路径接管——无论成功或失败均由本服务 delete，caller 不再释放；
//   - data（DDSPublish.data）：借用（非所有权），publish 同步写入后 caller 自行管理其生命周期；
//   - sample（DDSLoan/DDSLoanResult.sample）：loan 从 writer 池借出，所有权经 write（发布）或 discard（归还）交回中间件。
//
// 线程安全：nodes_ 的增删查改统一以 mtx_ 串行化（见各 handler 的 lock_guard）；create_node 的并发约束详见其内部注释。
#include "YomkRpcService.h"

#include "FastDDSNode.h"
// 引 yomkrpc_xml::ParticipantQosSpec（createNode 组 spec；FastDDSNode.h 传递亦可得，
// 显式引入对齐 IWYU）
#include "FastDDSXmlHelper.h"

#include <fstream>
#include <sstream>

namespace
{
constexpr uint32_t kMaxDomainId = 232;  // DDS 域号上限
// loan 响应构造期间的异常安全守卫：构造抛异常时栈展开归还池；成功后由 caller 解除（见 loan()）。
struct LoanGuard
{
    FastDDSNode* node = nullptr;
    const std::string* topicName = nullptr;
    void* sample = nullptr;
    ~LoanGuard()
    {
        if (node != nullptr && sample != nullptr && topicName != nullptr)
        {
            node->discardLoan(*topicName, sample);
        }
    }
};
}  // namespace

YomkRpcService::YomkRpcService(YomkServer* server) : YomkService(server)
{
    name("/YomkRpcService");
}

YomkRpcService::~YomkRpcService() = default;

int YomkRpcService::init()
{
    installFunc("/version", &YomkRpcService::getVersion);
    installFunc("/load_xml", &YomkRpcService::loadXml);
    installFunc("/create_node", &YomkRpcService::createNode);
    installFunc("/delete_node", &YomkRpcService::deleteNode);
    installFunc("/register_pub_topic", &YomkRpcService::registerPubTopic);
    installFunc("/register_sub_topic", &YomkRpcService::registerSubTopic);
    installFunc("/publish", &YomkRpcService::publish);
    installFunc("/loan", &YomkRpcService::loan);
    installFunc("/discard_loan", &YomkRpcService::discardLoan);
    return 0;
}

yomk::payload::Response YomkRpcService::getVersion(YomkMsgPtr pkg)
{
    std::string version = "YomkRpc v" YOMKRPC_VERSION_STRING;
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::String::shared(version));
}

yomk::payload::Response YomkRpcService::loadXml(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSXmlLoad")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSXmlLoad. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSXmlLoad>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSXmlLoad] is dynamic_pointer_cast failed. ");
    };

    if (p->msg.xmlPath.empty())
    {
        ::yomk::log().tag("YomkRpcService::loadXml").error("xml path is empty");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "xml path is empty");
    }
    // 读 raw 串缓存：get_*_qos_from_xml 系列吃 raw 串非文件路径，节点层三级提取链经 xmlCache() 取用
    std::ifstream file(p->msg.xmlPath);
    if (!file)
    {
        ::yomk::log().tag("YomkRpcService::loadXml").error("xml file [", p->msg.xmlPath, "] not readable");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "xml file [" + p->msg.xmlPath + "] not readable");
    }
    std::ostringstream buf;
    buf << file.rdbuf();
    const std::string xml = buf.str();
    if (xml.empty())
    {
        ::yomk::log().tag("YomkRpcService::loadXml").error("xml file [", p->msg.xmlPath, "] is empty");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "xml file [" + p->msg.xmlPath + "] is empty");
    }
    // Fast DDS 解析校验前置：文件格式非法在 load 时报（rc 为 int32_t 别名 ReturnCode_t，整值
    // 回显便于对照官方错误码；成功仅证明文件可解析，缓存后由节点层按名/默认档提取）
    auto* factory = eprosima::fastdds::dds::DomainParticipantFactory::get_instance();
    const auto rc = factory->load_XML_profiles_file(p->msg.xmlPath);
    if (rc != eprosima::fastdds::dds::RETCODE_OK)
    {
        ::yomk::log().tag("YomkRpcService::loadXml").error("load_XML_profiles_file [", p->msg.xmlPath, "] failed, rc=", rc);
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed,
            "load_XML_profiles_file [" + p->msg.xmlPath + "] failed, rc=" + std::to_string(rc));
    }
    // 缓存更新在服务 mtx_ 内（防同服务并发 load 交错写）；读取侧（Debug/Bag 服务经 xmlCache()）仅拷贝
    std::lock_guard<std::mutex> lock(mtx_);
    xmlCache_ = xml;
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkRpcService::createNode(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSNode")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSNode. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSNode>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSNode] is dynamic_pointer_cast failed. ");
    };

    if (p->msg.nodeName.empty())
    {
        ::yomk::log().tag("YomkRpcService::createNode").error("node name is empty");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node name is empty");
    }
    if (p->msg.domainId > kMaxDomainId)
    {
        ::yomk::log().tag("YomkRpcService::createNode").error("domainId [", p->msg.domainId, "] out of valid range [0,232]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "domainId [" + std::to_string(p->msg.domainId) + "] out of valid range [0,232]");
    }

    // create_participant（经 setDomainId）必须在全局 mtx_ 内串行，不可“锁外构造”优化：
    // FastDDS 3.6.1 的 DomainParticipantFactory 单例 lazy-init(get_shared_instance/load_profiles) 与
    // SystemInfo::get_username()(→ 非线程安全 getpwuid，写 libc 静态缓冲) 在并发 create_participant 下
    // data race。tsan 实证：移出锁后 A1 3 race / stress 2 race（create-vs-create），锁内串行则 0 race
    // （git stash 基线对照 6×0）。并发安全优先于该罕见路径（create_node 多在启动期）的微优化。
    std::lock_guard<std::mutex> lock(mtx_);
    if (nodes_.find(p->msg.nodeName) != nodes_.end())
    {
        ::yomk::log().tag("YomkRpcService::createNode").error("node [", p->msg.nodeName, "] already exists");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] already exists");
    }

    auto node = std::make_unique<FastDDSNode>();
    // XML 配置通道 + 发现服务器直配（闭环 4）：spec 组装——xmlCache_ 空串=未加载，提取链
    // 跳过回退现状；双直配串空串=不直配现状。非法 serverSpec 经 err 回显具体非法项
    yomkrpc_xml::ParticipantQosSpec spec;
    spec.xml = xmlCache_;
    spec.udpServerSpec = p->msg.udpDiscoverServerLocator;
    spec.tcpServerSpec = p->msg.tcpDiscoverServerLocator;
    std::string nodeErr;
    if (!node->setDomainId(p->msg.domainId, p->msg.nodeName, spec, &nodeErr))
    {
        ::yomk::log().tag("YomkRpcService::createNode").error("create node [", p->msg.nodeName, "] failed: ", nodeErr.empty() ? "setDomainId error" : nodeErr);
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed,
            "create node [" + p->msg.nodeName + "] failed: " +
                (nodeErr.empty() ? "setDomainId error" : nodeErr));
    }
    nodes_[p->msg.nodeName] = std::move(node);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkRpcService::deleteNode(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "String")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not String. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::String>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[String] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = nodes_.find(p->d);
    if (it == nodes_.end())
    {
        ::yomk::log().tag("YomkRpcService::deleteNode").error("node [", p->d, "] not exists");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->d + "] not exists");
    }
    nodes_.erase(it);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkRpcService::registerPubTopic(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSTopic")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSTopic. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSTopic>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSTopic] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = nodes_.find(p->msg.nodeName);
    if (it == nodes_.end())
    {
        ::yomk::log().tag("YomkRpcService::registerPubTopic").error("node [", p->msg.nodeName, "] not exists");
        // type 所有权无条件接管：节点不存在未委托到节点层，须在此释放，避免泄漏。
        delete static_cast<eprosima::fastdds::dds::TopicDataType*>(p->msg.type);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] not exists");
    }
    if (!it->second->registerPubTopic(p->msg.topicName, p->msg.type, p->msg.qos))
    {
        ::yomk::log().tag("YomkRpcService::registerPubTopic").error("registerPubTopic [", p->msg.topicName, "] failed on node [", p->msg.nodeName, "]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "registerPubTopic [" + p->msg.topicName + "] failed on node [" + p->msg.nodeName + "]");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkRpcService::registerSubTopic(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSSubRequest")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSSubRequest. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSSubRequest>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSSubRequest] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = nodes_.find(p->msg.nodeName);
    if (it == nodes_.end())
    {
        ::yomk::log().tag("YomkRpcService::registerSubTopic").error("node [", p->msg.nodeName, "] not exists");
        // type 所有权无条件接管：节点不存在未委托到节点层，须在此释放，避免泄漏。
        delete static_cast<eprosima::fastdds::dds::TopicDataType*>(p->msg.type);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] not exists");
    }
    if (!it->second->registerSubTopic(p->msg.topicName, p->msg.type, p->msg.callback, p->msg.qos))
    {
        ::yomk::log().tag("YomkRpcService::registerSubTopic").error("registerSubTopic [", p->msg.topicName, "] failed on node [", p->msg.nodeName, "]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "registerSubTopic [" + p->msg.topicName + "] failed on node [" + p->msg.nodeName + "]");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkRpcService::publish(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSPublish")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSPublish. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSPublish>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSPublish] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = nodes_.find(p->msg.nodeName);
    if (it == nodes_.end())
    {
        ::yomk::log().tag("YomkRpcService::publish").error("node [", p->msg.nodeName, "] not exists");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] not exists");
    }
    if (!it->second->publish(p->msg.topicName, p->msg.data))
    {
        ::yomk::log().tag("YomkRpcService::publish").error("publish [", p->msg.topicName, "] failed on node [", p->msg.nodeName, "]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "publish [" + p->msg.topicName + "] failed on node [" + p->msg.nodeName + "]");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkRpcService::loan(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSLoan")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSLoan. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSLoan>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSLoan] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = nodes_.find(p->msg.nodeName);
    if (it == nodes_.end())
    {
        ::yomk::log().tag("YomkRpcService::loan").error("node [", p->msg.nodeName, "] not exists");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] not exists");
    }
    void* sample = nullptr;
    if (!it->second->loan(p->msg.topicName, sample) || sample == nullptr)
    {
        ::yomk::log().tag("YomkRpcService::loan").error("loan [", p->msg.topicName, "] failed on node [", p->msg.nodeName, "] (非 plain 类型或池耗尽，回退普通发布)");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "loan [" + p->msg.topicName + "] failed on node [" + p->msg.nodeName + "]");
    }
    // 异常安全：响应包构造若抛 bad_alloc，守卫在栈展开时归还池，避免借出样本滞留致池耗尽。
    LoanGuard loanGuard{it->second.get(), &p->msg.topicName, sample};
    yomk::payload::Response resp(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::DDSLoanResult::shared(DDSLoanResult{sample}));
    loanGuard.sample = nullptr;  // 响应已持有 sample，解除守卫
    return resp;
}

yomk::payload::Response YomkRpcService::discardLoan(YomkMsgPtr pkg)
{
        if (!pkg || pkg->name() != "DDSLoan")
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg is null or pkg is not DDSLoan. ");
    }
    auto p = std::dynamic_pointer_cast<::yomk::msgs::DDSLoan>(pkg);
    if (!p)
    {
        return ::yomk::payload::Response(
            ::yomk::payload::ResStatus::Failed, " pkg[DDSLoan] is dynamic_pointer_cast failed. ");
    };

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = nodes_.find(p->msg.nodeName);
    if (it == nodes_.end())
    {
        ::yomk::log().tag("YomkRpcService::discardLoan").error("node [", p->msg.nodeName, "] not exists");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] not exists");
    }
    if (!it->second->discardLoan(p->msg.topicName, p->msg.sample))
    {
        ::yomk::log().tag("YomkRpcService::discardLoan").error("discardLoan [", p->msg.topicName, "] failed on node [", p->msg.nodeName, "]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "discardLoan [" + p->msg.topicName + "] failed on node [" + p->msg.nodeName + "]");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

std::string YomkRpcService::xmlCache_;  // 已成功加载的 XML profiles raw 串（空=未加载）

const std::string& YomkRpcService::xmlCache()
{
    return xmlCache_;
}
