// 内存所有权协议（贯穿全部 handler，无法从签名推导）：
//   type（DDSTopic/DDSSubRequest.type）caller new、服务端全路径接管，无论成败均由本服务 delete；
//   data（DDSPublish.data）借用（非所有权），publish 同步写入后归 caller 管理；
//   sample（DDSLoan/LoanSample.sample）从 writer 池借出，经 write（发布）或 discard（归还）交回中间件。
#include "YomkDdsService.h"
#include "YomkDdsMsg.h"

#include "FastDDSNode.h"
// 显式引入 yomkdds_xml::ParticipantQosSpec（FastDDSNode.h 传递亦可得，对齐 IWYU）
#include "FastDDSXmlHelper.h"

#include <fstream>
#include <sstream>

namespace
{
constexpr uint32_t kMaxDomainId = 232;
// 异常安全守卫：loan 响应构造抛异常时栈展开归还池，成功后由 caller 解除
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

YomkDdsService::YomkDdsService(YomkServer* server) : YomkService(server)
{
    name("/YomkDdsService");
}

YomkDdsService::~YomkDdsService() = default;

int YomkDdsService::init()
{
    installFunc("/version", &YomkDdsService::getVersion);
    installFunc("/load_xml", &YomkDdsService::loadXml);
    installFunc("/create_node", &YomkDdsService::createNode);
    installFunc("/delete_node", &YomkDdsService::deleteNode);
    installFunc("/register_pub_topic", &YomkDdsService::registerPubTopic);
    installFunc("/register_sub_topic", &YomkDdsService::registerSubTopic);
    installFunc("/publish", &YomkDdsService::publish);
    installFunc("/loan", &YomkDdsService::loan);
    installFunc("/discard_loan", &YomkDdsService::discardLoan);
    return 0;
}

yomk::payload::Response YomkDdsService::getVersion(YomkMsgPtr pkg)
{
    std::string version = "YomkDds v" YOMKDDS_VERSION_STRING;
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::String::shared(version));
}

yomk::payload::Response YomkDdsService::loadXml(YomkMsgPtr pkg)
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
        ::yomk::log().tag("YomkDdsService::loadXml").error("xml path is empty");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "xml path is empty");
    }
    // 缓存 raw 串：节点层 QoS 提取链吃 raw 串而非文件路径
    std::ifstream file(p->msg.xmlPath);
    if (!file)
    {
        ::yomk::log().tag("YomkDdsService::loadXml").error("xml file [", p->msg.xmlPath, "] not readable");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "xml file [" + p->msg.xmlPath + "] not readable");
    }
    std::ostringstream buf;
    buf << file.rdbuf();
    const std::string xml = buf.str();
    if (xml.empty())
    {
        ::yomk::log().tag("YomkDdsService::loadXml").error("xml file [", p->msg.xmlPath, "] is empty");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "xml file [" + p->msg.xmlPath + "] is empty");
    }
    // 此处仅证明文件可解析，QoS 提取由节点层经 xmlCache() 完成；rc 为 ReturnCode_t（int32_t），
    // 整值回显便于对照官方错误码
    auto* factory = eprosima::fastdds::dds::DomainParticipantFactory::get_instance();
    const auto rc = factory->load_XML_profiles_file(p->msg.xmlPath);
    if (rc != eprosima::fastdds::dds::RETCODE_OK)
    {
        ::yomk::log().tag("YomkDdsService::loadXml").error("load_XML_profiles_file [", p->msg.xmlPath, "] failed, rc=", rc);
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed,
            "load_XML_profiles_file [" + p->msg.xmlPath + "] failed, rc=" + std::to_string(rc));
    }
    // 缓存更新须在 mtx_ 内（防并发 load 交错写）；读取侧（Debug/Bag 服务）仅拷贝
    std::lock_guard<std::mutex> lock(mtx_);
    xmlCache_ = xml;
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsService::createNode(YomkMsgPtr pkg)
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
        ::yomk::log().tag("YomkDdsService::createNode").error("node name is empty");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node name is empty");
    }
    if (p->msg.domainId > kMaxDomainId)
    {
        ::yomk::log().tag("YomkDdsService::createNode").error("domainId [", p->msg.domainId, "] out of valid range [0,232]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "domainId [" + std::to_string(p->msg.domainId) + "] out of valid range [0,232]");
    }

    // create_participant（经 setDomainId）必须在 mtx_ 内串行，不可锁外构造：FastDDS 3.6.1 的
    // factory 单例 lazy-init 与 SystemInfo::get_username()（非线程安全 getpwuid，写 libc 静态缓冲）
    // 并发下 data race；tsan 实证锁外有 race、锁内 0 race。并发安全优先于启动期路径的微优化
    std::lock_guard<std::mutex> lock(mtx_);
    if (nodes_.find(p->msg.nodeName) != nodes_.end())
    {
        ::yomk::log().tag("YomkDdsService::createNode").error("node [", p->msg.nodeName, "] already exists");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] already exists");
    }

    auto node = std::make_unique<FastDDSNode>();
    // xmlCache_ 空串=未加载（提取链跳过回退现状）；直配串空串=不直配现状；
    // 非法 serverSpec 经 err 回显具体非法项
    yomkdds_xml::ParticipantQosSpec spec;
    spec.xml = xmlCache_;
    spec.udpServerSpec = p->msg.udpDiscoverServerLocator;
    spec.tcpServerSpec = p->msg.tcpDiscoverServerLocator;
    std::string nodeErr;
    if (!node->setDomainId(p->msg.domainId, p->msg.nodeName, spec, &nodeErr))
    {
        ::yomk::log().tag("YomkDdsService::createNode").error("create node [", p->msg.nodeName, "] failed: ", nodeErr.empty() ? "setDomainId error" : nodeErr);
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed,
            "create node [" + p->msg.nodeName + "] failed: " +
                (nodeErr.empty() ? "setDomainId error" : nodeErr));
    }
    nodes_[p->msg.nodeName] = std::move(node);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsService::deleteNode(YomkMsgPtr pkg)
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
        ::yomk::log().tag("YomkDdsService::deleteNode").error("node [", p->d, "] not exists");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->d + "] not exists");
    }
    nodes_.erase(it);
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsService::registerPubTopic(YomkMsgPtr pkg)
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
        ::yomk::log().tag("YomkDdsService::registerPubTopic").error("node [", p->msg.nodeName, "] not exists");
        // type 所有权无条件接管：未委托到节点层，须在此释放避免泄漏
        delete static_cast<eprosima::fastdds::dds::TopicDataType*>(p->msg.type);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] not exists");
    }
    if (!it->second->registerPubTopic(p->msg.topicName, p->msg.type, p->msg.qos))
    {
        ::yomk::log().tag("YomkDdsService::registerPubTopic").error("registerPubTopic [", p->msg.topicName, "] failed on node [", p->msg.nodeName, "]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "registerPubTopic [" + p->msg.topicName + "] failed on node [" + p->msg.nodeName + "]");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsService::registerSubTopic(YomkMsgPtr pkg)
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
        ::yomk::log().tag("YomkDdsService::registerSubTopic").error("node [", p->msg.nodeName, "] not exists");
        // type 所有权无条件接管：未委托到节点层，须在此释放避免泄漏
        delete static_cast<eprosima::fastdds::dds::TopicDataType*>(p->msg.type);
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] not exists");
    }
    if (!it->second->registerSubTopic(p->msg.topicName, p->msg.type, p->msg.callback, p->msg.qos))
    {
        ::yomk::log().tag("YomkDdsService::registerSubTopic").error("registerSubTopic [", p->msg.topicName, "] failed on node [", p->msg.nodeName, "]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "registerSubTopic [" + p->msg.topicName + "] failed on node [" + p->msg.nodeName + "]");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsService::publish(YomkMsgPtr pkg)
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
        ::yomk::log().tag("YomkDdsService::publish").error("node [", p->msg.nodeName, "] not exists");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] not exists");
    }
    if (!it->second->publish(p->msg.topicName, p->msg.data))
    {
        ::yomk::log().tag("YomkDdsService::publish").error("publish [", p->msg.topicName, "] failed on node [", p->msg.nodeName, "]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "publish [" + p->msg.topicName + "] failed on node [" + p->msg.nodeName + "]");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

yomk::payload::Response YomkDdsService::loan(YomkMsgPtr pkg)
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
        ::yomk::log().tag("YomkDdsService::loan").error("node [", p->msg.nodeName, "] not exists");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] not exists");
    }
    void* sample = nullptr;
    if (!it->second->loan(p->msg.topicName, sample) || sample == nullptr)
    {
        ::yomk::log().tag("YomkDdsService::loan").error("loan [", p->msg.topicName, "] failed on node [", p->msg.nodeName, "] (非 plain 类型或池耗尽，回退普通发布)");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "loan [" + p->msg.topicName + "] failed on node [" + p->msg.nodeName + "]");
    }
    // 构造抛异常（如 bad_alloc）时守卫栈展开归还池，避免样本滞留致池耗尽
    LoanGuard loanGuard{it->second.get(), &p->msg.topicName, sample};
    yomk::payload::Response resp(yomk::payload::ResStatus::Succeeded, "ok", ::yomk::msgs::LoanSample::shared(LoanSample{sample}));
    loanGuard.sample = nullptr;  // 响应已持有 sample，解除守卫
    return resp;
}

yomk::payload::Response YomkDdsService::discardLoan(YomkMsgPtr pkg)
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
        ::yomk::log().tag("YomkDdsService::discardLoan").error("node [", p->msg.nodeName, "] not exists");
        return yomk::payload::Response(yomk::payload::ResStatus::Failed, "node [" + p->msg.nodeName + "] not exists");
    }
    if (!it->second->discardLoan(p->msg.topicName, p->msg.sample))
    {
        ::yomk::log().tag("YomkDdsService::discardLoan").error("discardLoan [", p->msg.topicName, "] failed on node [", p->msg.nodeName, "]");
        return yomk::payload::Response(
            yomk::payload::ResStatus::Failed, "discardLoan [" + p->msg.topicName + "] failed on node [" + p->msg.nodeName + "]");
    }
    return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "ok");
}

std::string YomkDdsService::xmlCache_;  // 已成功加载的 XML profiles raw 串（空=未加载）

const std::string& YomkDdsService::xmlCache()
{
    return xmlCache_;
}
