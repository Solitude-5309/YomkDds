/**
 * @file TestYomkDdsDebugHandlerLifecycle.cpp
 * @brief YomkDdsDebugHandler 真实 DDS 生命周期测试
 *
 * 范围：契约测试（DDS-free）刻意豁免的 /create_node 成功路径（经 setDomainId 创建真实
 *       participant）、/topic_print 登记契约与 /delete_node 析构清理，及 domainId 边界行为。
 *       节点层守卫（setDomainId 重复/未入域登记）与端到端流量（发现→动态类型→订阅→JSON 输出）
 *       已由 TestFastDDSDebugNode 覆盖，本测试不重复；topic_info/node_info 命中用例仅直连
 *       FastDDS 建最小远端发布端触发 EDP 发现（不发布数据），覆盖服务层结构化载荷组装路径。
 * 覆盖：
 *   A-1 生命周期：创建 → list_topics 收敛空列表 → list_nodes 单次快照空列表 → topic_info 未发现
 *       主题 eNo → 重复创建拒绝 → topicPrint 空回调拒绝 → 正常登记 → 同主题重复登记拒绝 →
 *       异主题登记 → 删除 → 重复删除拒绝 → 删除后 list_topics/list_nodes/topic_info/node_info
 *       拒绝 → 删除后重建 → node list 命中（命名 peer 经 SPDP 传播）→ topic_info 结构化包断言
 *       （topicName/typeName/收发计数/逐端点 nodeName+GUID+结构化 QoS 字段）→
 *       list_topics 结构化 TopicInfoArray 断言（topicName/typeName/计数）→
 *       topic_find 按类型名反查命中唯一行与未匹配类型 eNo → interface_show 按类型名输出
 *       结构化 InterfaceInfo（root 成员表 data/string + 空依赖）与未发现类型 eNo →
 *       interface_list 类型名去重清单命中唯一行与空域未发现 eNo → topic_example 按主题名
 *       输出结构化 TopicExampleInfo（topicName/typeName/interfaceInfo 反射树/json 默认值
 *       模板）与未发现主题 eNo →
 *       topic_pub 发布一次消息（eOk delivered + TopicPublishInfo total=1/failed=0，ack 确认
 *       送达）与非法 JSON eNo →
 *       topic_pub 带 qos 尾参（BEST_EFFORT+VOLATILE 持续模式，闭环 2）→ 直连 observer
 *       发现读回双差异行（服务层下传链路端到端）→
 *       topic_msg 结构化 TopicMessageInfo 命中（topicName/typeName/json 字段断言）与
 *       未发现主题 eNo →
 *       node_info 结构化 NodeInfo 命中（nodeName + publishers/subscribers 的 NodeEndpoint
 *       清单，对齐 ros2 node info 语义）+ 未发现节点名 eNo → 退出前清理；
 *   A-2 domainId 边界：有效域 0 与上界 232（eOk；真实创建 participant 后即删）。
 *
 * 关键不变式：每个 eOk 创建的调试节点必须在 main 返回前经 /delete_node 显式删除，以规避
 *   YOMK 框架 atexit 服务析构晚于 FastDDS DomainParticipantFactory 单例销毁导致的静态析构
 *   顺序问题（参见 TestYomkDdsNodeLifecycle / ExampleYomkDdsPub）。
 *
 * 风格：纯 main() + check 函数 + 失败计数（零第三方依赖），返回非 0 表示存在失败用例。
 */

#include "TestCheck.h"
#include "TestServiceAccess.h"
#include "YomkDdsDebugHandler.h" // 服务/DebugCreateNodeRequest/DebugTopicPrintRequest/DebugTopicInfoRequest/YOMK_* 宏
#include "YomkDdsMsg.h"
#include "FastDDSDebugNode.h"    // qos 下传用例的直连 observer（发现 QoS 行读回）

// 引 MStringPubSubType（仅 topic_info/node_info 命中用例的最小远端发布端）
#include <yomk/dds/YomkDdsStdMsgsPubSubTypes.hpp>

#include <fastdds/dds/domain/DomainParticipant.hpp>        // 直连 FastDDS API 搭建远端发布端/订阅端（命中用例）
#include <fastdds/dds/domain/DomainParticipantFactory.hpp> // participant 创建/删除
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

#include <cstdint>
#include <atomic>
#include <iostream>
#include <string>
#include <thread>

namespace
{
    // 有效域（FastDDS 约 0-232），避开默认域 0 的潜在网络干扰
    constexpr uint32_t TEST_DOMAIN = 200;

    // 构造 /create_node 请求包
    YomkMsgPtr mkNode(uint32_t domainId)
    {
        return yomk::msgs::dds::DebugCreateNodeRequest::shared(DebugCreateNodeRequest{domainId});
    }

    // 构造 /topic_print 请求包（no-op 输出回调：登记路径不产生真实流量）
    YomkMsgPtr mkPrint(const std::string &topicName)
    {
        return yomk::msgs::dds::DebugTopicPrintRequest::shared(DebugTopicPrintRequest{topicName, [](const std::string &) {}});
    }

    // 构造 /list_topics 请求包（stableRounds=1 单次快照路径，免收敛等待）
    YomkMsgPtr mkList()
    {
        return yomk::msgs::dds::DebugListTopicsRequest::shared(DebugListTopicsRequest{1, 100});
    }

    // 构造 /topic_info 请求包（未发现用例传 rounds=1 走单次快照快速路径；命中用例传收敛参数）
    YomkMsgPtr mkInfo(const std::string &topicName, uint32_t stableRounds, uint32_t intervalMs)
    {
        return yomk::msgs::dds::DebugTopicInfoRequest::shared(DebugTopicInfoRequest{topicName, stableRounds, intervalMs});
    }

    // 构造 /list_nodes 请求包（stableRounds=1 单次快照路径，免收敛等待）
    YomkMsgPtr mkNodeList()
    {
        return yomk::msgs::dds::DebugListNodesRequest::shared(DebugListNodesRequest{1, 50});
    }

    // 构造 /node_info 请求包（未发现用例传 rounds=1 走单次快照快速路径；命中用例传收敛参数）
    YomkMsgPtr mkNodeInfo(const std::string &nodeName, uint32_t stableRounds, uint32_t intervalMs)
    {
        return yomk::msgs::dds::DebugNodeInfoRequest::shared(DebugNodeInfoRequest{nodeName, stableRounds, intervalMs});
    }
} // namespace

int main()
{
    yomk::server().init();

    auto *svc = new YomkDdsDebugHandler(yomk::server().instance().get());
    check(yomk::server().srv_ptr(svc).add() == 0, "YomkDdsDebugHandler 注册成功（所有权移交框架，init() 已内部调用）");

    // ---- A-1：生命周期（创建/重复创建/登记契约/删除/重建） ----
    check(testInvoke(svc, "/create_node", mkNode(TEST_DOMAIN)).status == yomk::payload::ResStatus::Succeeded,
          "创建调试节点(domain 200) → eOk（真实创建 participant）");

    // list_topics：ctest 串行执行，此刻域 200 无任何远端 writer；stableRounds=1 单次快照
    // 即得空列表（收敛参数链路直通节点层）
    auto listed = testInvoke(svc, "/list_topics", mkList());
    check(listed.status == yomk::payload::ResStatus::Succeeded && listed.data != nullptr,
          "list_topics（入域无 writer，单次快照）→ eOk（空列表路径）");
    auto arr = yomk::msgs::unpack<yomk::msgs::dds::TopicInfoArray>(listed.data);
    check(arr != nullptr && arr->d.d.empty(), "list_topics 返回包可解包为空 TopicInfoArray");

    // interface_list：同域无任何类型，空集按 find 族语义 eNo（收敛参数链路同直通节点层）
    auto ifaceListMiss = testInvoke(svc, "/interface_list", yomk::msgs::dds::DebugInterfaceListRequest::shared(DebugInterfaceListRequest{1, 50}));
    check(ifaceListMiss.status == yomk::payload::ResStatus::Failed &&
          ifaceListMiss.msg.find("no interface types discovered") != std::string::npos,
          "interface_list 空域 → eNo no interface types discovered（find 族语义）");

    // topic_example：同域无此主题，eNo（收敛参数链路同直通节点层）
    auto topicExampleMiss = testInvoke(svc, "/topic_example", yomk::msgs::dds::DebugTopicExampleRequest::shared(DebugTopicExampleRequest{"t_example_miss", 1, 50}));
    check(topicExampleMiss.status == yomk::payload::ResStatus::Failed &&
          topicExampleMiss.msg.find("topic [t_example_miss] not found") != std::string::npos,
          "topic_example 空域未发现主题 → eNo topic [...] not found（info 族语义）");
    // 错误精确化回归：空域下仍属第一层"主题未发现"，msg 不得被误归类为第二层原因
    // （TypeObject 不可得 / 类型重建失败），验证服务层兜底与节点层按步归类均正确
    check(topicExampleMiss.msg.find("not available") == std::string::npos,
          "topic_example 空域 msg 仍为第一层文本（未误归类为 TypeObject 不可得）");

    // topic_pub：同域无此主题，eNo（发布前的发现收敛失败即早退，不建任何发布实体）
    auto topicPubMiss = testInvoke(svc, "/topic_pub", yomk::msgs::dds::DebugTopicPubRequest::shared(DebugTopicPubRequest{"t_pub_miss", "{}", 1, 50}));
    check(topicPubMiss.status == yomk::payload::ResStatus::Failed &&
          topicPubMiss.msg.find("topic [t_pub_miss] not found") != std::string::npos,
          "topic_pub 空域未发现主题 → eNo topic [...] not found（发布前早退）");

    // topic_msg：同域无此主题，eNo（收敛参数链路同直通节点层）
    auto topicMsgMiss = testInvoke(svc, "/topic_msg", yomk::msgs::dds::DebugTopicMsgRequest::shared(DebugTopicMsgRequest{"t_msg_miss", 1, 50}));
    check(topicMsgMiss.status == yomk::payload::ResStatus::Failed &&
          topicMsgMiss.msg.find("topic [t_msg_miss] not found") != std::string::npos,
          "topic_msg 空域未发现主题 → eNo topic [...] not found（info 族语义）");
    check(topicMsgMiss.msg.find("not available") == std::string::npos,
          "topic_msg 空域 msg 仍为第一层文本（未误归类为 TypeObject 不可得）");

    // list_nodes：ctest 串行执行，此刻域 200 无其他参与者；调试节点自身不在自身发现缓存中，
    // stableRounds=1 单次快照即得空列表（空列表合法：域内暂无命名参与者，语义同 list_topics 空域）
    auto listedNodes = testInvoke(svc, "/list_nodes", mkNodeList());
    check(listedNodes.status == yomk::payload::ResStatus::Succeeded && listedNodes.data != nullptr,
          "list_nodes（入域无命名参与者，单次快照）→ eOk（空列表路径）");
    auto nodeArr = yomk::msgs::unpack<yomk::msgs::StringArray>(listedNodes.data);;
    check(nodeArr != nullptr && nodeArr->d.empty(), "list_nodes 返回包可解包为空 StringArray");

    // topic_info 未发现主题：node_ 已建、域内无该主题，单次快照快速路径 → eNo
    auto infoAbsent = testInvoke(svc, "/topic_info", mkInfo("t_info_absent", 1, 50));
    check(infoAbsent.status == yomk::payload::ResStatus::Failed &&
              infoAbsent.msg.find("not found") != std::string::npos,
          "topicInfo 未发现主题（单次快照）→ eNo topic not found");

    auto dup = testInvoke(svc, "/create_node", mkNode(TEST_DOMAIN));
    check(dup.status == yomk::payload::ResStatus::Failed && dup.msg.find("already exists") != std::string::npos,
          "重复创建 → eNo already exists（单节点模型，须先 delete）");

    auto emptyCb = testInvoke(svc, "/topic_print", yomk::msgs::dds::DebugTopicPrintRequest::shared(DebugTopicPrintRequest{"t_debug_a", nullptr}));
    check(emptyCb.status == yomk::payload::ResStatus::Failed &&
              emptyCb.msg.find("output callback is empty") != std::string::npos,
          "topicPrint 空回调 → eNo output callback is empty");

    check(testInvoke(svc, "/topic_print", mkPrint("t_debug_a")).status == yomk::payload::ResStatus::Succeeded,
          "topicPrint(t_debug_a) → eOk（登记待发现）");

    auto dupTopic = testInvoke(svc, "/topic_print", mkPrint("t_debug_a"));
    check(dupTopic.status == yomk::payload::ResStatus::Failed && dupTopic.msg.find("failed") != std::string::npos,
          "同主题重复登记 → eNo（subscribeTopic 去重拒绝）");
    // 拒绝原因由节点层归类透传：保留 failed 前缀的同时携具体原因，不再只有笼统 failed
    check(dupTopic.msg.find("subscribeTopic [t_debug_a] failed: ") != std::string::npos &&
              dupTopic.msg.find("already registered") != std::string::npos,
          "重复登记 msg 为 failed: + 具体原因（already registered）");
    std::cout << "[OBSERVE] dup topic_print msg |" << dupTopic.msg << "|" << std::endl;

    check(testInvoke(svc, "/topic_print", mkPrint("t_debug_b")).status == yomk::payload::ResStatus::Succeeded,
          "topicPrint(t_debug_b) → eOk（异主题可并行登记）");

    check(testInvoke(svc, "/delete_node").status == yomk::payload::ResStatus::Succeeded,
          "删除调试节点 → eOk（析构清理 participant 与登记）");

    auto del2 = testInvoke(svc, "/delete_node");
    check(del2.status == yomk::payload::ResStatus::Failed && del2.msg.find("not created") != std::string::npos,
          "重复删除 → eNo debug node not created");

    auto listAfterDel = testInvoke(svc, "/list_topics", mkList());
    check(listAfterDel.status == yomk::payload::ResStatus::Failed &&
              listAfterDel.msg.find("not created") != std::string::npos,
          "删除后 list_topics → eNo debug node not created");

    auto infoAfterDel = testInvoke(svc, "/topic_info", mkInfo("t_info_absent", 1, 50));
    check(infoAfterDel.status == yomk::payload::ResStatus::Failed &&
              infoAfterDel.msg.find("not created") != std::string::npos,
          "删除后 topic_info → eNo debug node not created");

    auto nodesAfterDel = testInvoke(svc, "/list_nodes", mkNodeList());
    check(nodesAfterDel.status == yomk::payload::ResStatus::Failed &&
              nodesAfterDel.msg.find("not created") != std::string::npos,
          "删除后 list_nodes → eNo debug node not created");

    auto nodeInfoAfterDel = testInvoke(svc, "/node_info", mkNodeInfo("t_no_node", 1, 50));
    check(nodeInfoAfterDel.status == yomk::payload::ResStatus::Failed &&
              nodeInfoAfterDel.msg.find("not created") != std::string::npos,
          "删除后 node_info → eNo debug node not created");

    check(testInvoke(svc, "/create_node", mkNode(TEST_DOMAIN)).status == yomk::payload::ResStatus::Succeeded,
          "删除后重建调试节点 → eOk（delete/create 闭环）");

    // ---- node list 命中用例：命名远端 participant（仅入域触发 SPDP 发现，不建任何端点）----
    // 复用重建后的域 200 调试节点；置于 topic_info 命中用例之前——本块先执行保证
    // seenParticipants_ 纯净（topic_info 块的 hitParticipant 默认名 "RTPSParticipant" 若先被
    // 发现，REMOVED 不追踪会残留进本块断言）。SPDP 参与者发现独立于 EDP：peer 无需任何端点。
    {
        namespace dds = eprosima::fastdds::dds;  // 块内别名：直连 FastDDS API 造命名参与者
        dds::DomainParticipantQos peerQos = dds::PARTICIPANT_QOS_DEFAULT;
        peerQos.name(std::string("test-svc-peer"));
        auto *peerParticipant = dds::DomainParticipantFactory::get_instance()->create_participant(
            TEST_DOMAIN, peerQos);
        check(peerParticipant != nullptr, "命名 peer participant 创建成功（node list 命中用例）");
        // "/" 名 peer：ROS2 参与者默认占位名（rmw_fastrtps 把参与者名统一置为根 enclave "/"，
        // rcl_init 兜底，节点名走另一通道），显式 qos.name("/") 复现，验证 list_nodes 的
        // "/" 过滤语义（ROS2 参与者不占行）
        dds::DomainParticipantQos slashQos = dds::PARTICIPANT_QOS_DEFAULT;
        slashQos.name(std::string("/"));
        auto *slashParticipant = dds::DomainParticipantFactory::get_instance()->create_participant(
            TEST_DOMAIN, slashQos);
        check(slashParticipant != nullptr, "slash peer participant 创建成功（list_nodes 过滤用例）");
        if (peerParticipant != nullptr)
        {
            // peer 后于调试节点入域——调试节点经活动发现（PDP 组播）收到 DISCOVERED_PARTICIPANT，
            // 收敛窗口覆盖发现延迟；late joiner 反向时序已由 TestFastDDSDebugNode 覆盖
            auto hitNodes = testInvoke(svc, "/list_nodes", yomk::msgs::dds::DebugListNodesRequest::shared(DebugListNodesRequest{5, 100}));
            check(hitNodes.status == yomk::payload::ResStatus::Succeeded && hitNodes.data != nullptr,
                  "list_nodes(5,100ms) → eOk（参与者发现收敛命中）");
            auto nodesArr = yomk::msgs::unpack<yomk::msgs::StringArray>(hitNodes.data);;
            check(nodesArr != nullptr, "list_nodes 返回包可解包为 StringArray");
            bool hasPeer = false;
            bool noBadLine = true;  // 每行非空、非 "/" 且不含 GUID 串特征 '|'（无效名参与者跳过的证据）
            bool noSelf = true;
            if (nodesArr != nullptr)
            {
                for (const auto &name : nodesArr->d)
                {
                    if (name == "test-svc-peer")
                    {
                        hasPeer = true;
                    }
                    if (name.empty() || name == "/" || name.find('|') != std::string::npos)
                    {
                        noBadLine = false;
                    }
                    if (name.rfind("yomkdds_debug_", 0) == 0)
                    {
                        noSelf = false;
                    }
                }
                std::cout << "[OBSERVE] list_nodes " << nodesArr->d.size() << " nodes" << std::endl;
            }
            check(hasPeer, "list_nodes 含 test-svc-peer（SPDP participant_name 端到端传播）");
            check(noBadLine, "list_nodes 无空名/\"/\"名/GUID 串行（无效名参与者跳过）");
            check(noSelf, "list_nodes 不含调试节点自身（自身不在发现回调中）");

            dds::DomainParticipantFactory::get_instance()->delete_participant(peerParticipant);
        }
        if (slashParticipant != nullptr)
        {
            dds::DomainParticipantFactory::get_instance()->delete_participant(slashParticipant);
        }
    }

    // ---- topic_info 命中用例：远端最小发布端（仅建端点触发 EDP 发现，不发布数据）----
    // 复用重建后的域 200 调试节点；时序模式同 TestFastDDSDebugNode 多端点用例：
    // list 预热（约 400ms）+ info 独立收敛窗口（500ms 起）覆盖本地 EDP 发现延迟
    {
        namespace dds = eprosima::fastdds::dds;  // 块内别名：直连 FastDDS API 搭建远端发布端
        constexpr const char *HIT_TOPIC = "t_info_hit";
        auto *hitParticipant = dds::DomainParticipantFactory::get_instance()->create_participant(
            TEST_DOMAIN, dds::PARTICIPANT_QOS_DEFAULT);
        check(hitParticipant != nullptr, "远端发布端 participant 创建成功（topic_info 命中用例）");
        if (hitParticipant != nullptr)
        {
            // TypeSupport 须活过 topic/writer 使用期（声明顺序同 TestFastDDSDebugNode 计数用例）
            dds::TypeSupport ts(new YomkDdsStd::MStringPubSubType());
            ts.register_type(hitParticipant);
            auto *pub = hitParticipant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);
            auto *topic = hitParticipant->create_topic(
                HIT_TOPIC, ts.get_type_name(), dds::TOPIC_QOS_DEFAULT);
            auto *writer = (pub != nullptr && topic != nullptr)
                ? pub->create_datawriter(topic, dds::DATAWRITER_QOS_DEFAULT)
                : nullptr;
            check(writer != nullptr, "远端 DataWriter 创建成功（t_info_hit，仅建端点不发布数据）");

            if (writer != nullptr)
            {
                // list_topics 预热仅为本用例时序前置（late joiner 经 EDP 重放发现信息）；
                // topic_info 内部独立收敛查询，不依赖 list_topics
                auto warm = testInvoke(svc, "/list_topics", yomk::msgs::dds::DebugListTopicsRequest::shared(DebugListTopicsRequest{5, 100}));
                check(warm.status == yomk::payload::ResStatus::Succeeded, "list_topics(5,100ms) 预热 → eOk（发现时序前置）");

                auto hit = testInvoke(svc, "/topic_info", mkInfo(HIT_TOPIC, 5, 100));
                check(hit.status == yomk::payload::ResStatus::Succeeded && hit.data != nullptr,
                      "topicInfo(t_info_hit,5,100ms) → eOk（独立收敛命中）");
                auto info = yomk::msgs::unpack<yomk::msgs::dds::TopicInfo>(hit.data);
                check(info != nullptr, "topicInfo 返回包可解包为结构化 TopicInfo");
                if (info != nullptr)
                {
                    const auto &ti = info->msg;
                    check(ti.topicName == HIT_TOPIC, "topicInfo topicName 回填被查询主题名");
                    check(ti.typeName == "YomkDdsStd::MString",
                          "topicInfo typeName 为原始 DDS 类型名（无转换）");
                    check(ti.publisherCount == 1 && ti.publishers.size() == 1,
                          "topicInfo 发布端计数与清单一致（1）");
                    check(ti.subscriptionCount == 0 && ti.subscriptions.empty(),
                          "topicInfo 订阅端计数与清单一致（0，无清单段）");
                    if (ti.publishers.size() == 1)
                    {
                        // 端点详情恒在载荷内（是否展示由调用方渲染），逐字段断言 writer 快照
                        const auto &ep = ti.publishers.front();
                        check(ep.guid.find('|') != std::string::npos,
                              "端点 GUID 为 FastDDS 原生 prefix|entity 形态");
                        check(!ep.qos.historyAdvertised,
                              "writer 发现数据不携带 history（标记未公告而非零值）");
                        check(ep.qos.reliability == "RELIABLE",
                              "端点 reliability 为结构化枚举名（RELIABLE）");
                        check(ep.qos.lifespanNs == yomk::payload::dds::kInfiniteDurationNs,
                              "无限期时长折算为 kInfiniteDurationNs 哨兵");
                        check(ep.qos.partition.empty(), "partition 未公告 → 空清单");
                        std::cout << "[OBSERVE] topicInfo endpoint guid=" << ep.guid
                                  << " reliability=" << ep.qos.reliability
                                  << " durability=" << ep.qos.durability << std::endl;
                    }
                }

                // list 结构化载荷：域 200 纯净仅 HIT_TOPIC 一个主题，类型名恒在载荷内
                // （是否渲染为 "主题名 [类型名]" 由调用方决定，服务层不出行文本）
                auto listHit = testInvoke(svc, "/list_topics", yomk::msgs::dds::DebugListTopicsRequest::shared(DebugListTopicsRequest{5, 100}));
                check(listHit.status == yomk::payload::ResStatus::Succeeded, "list_topics(5,100ms) → eOk");
                auto listInfos = yomk::msgs::unpack<yomk::msgs::dds::TopicInfoArray>(listHit.data);
                std::vector<yomk::payload::dds::TopicInfo> listRows;
                if (listInfos != nullptr)
                {
                    listRows = listInfos->d.d;
                }
                check(listRows.size() == 1 &&
                          listRows[0].topicName == HIT_TOPIC &&
                          listRows[0].typeName == "YomkDdsStd::MString" &&
                          listRows[0].publisherCount == 1 &&
                          listRows[0].subscriptions.empty(),
                      "list_topics 唯一项结构化回填 topicName/typeName/计数");

                // topic find：按类型名精确匹配反查（域 200 纯净唯一类型）→ 唯一行 HIT_TOPIC；
                // 未匹配类型报错退出（info 族语义，可发现类型名拼写错误）
                auto findHit = testInvoke(svc,
                        "/topic_find", yomk::msgs::dds::DebugTopicFindRequest::shared(DebugTopicFindRequest{"YomkDdsStd::MString", 5, 100}));
                check(findHit.status == yomk::payload::ResStatus::Succeeded,
                      "topic_find(YomkDdsStd::MString,5,100ms) → eOk（按类型反查命中）");
                auto findArr = yomk::msgs::unpack<yomk::msgs::StringArray>(findHit.data);;
                check(findArr != nullptr && findArr->d.size() == 1 && findArr->d[0] == HIT_TOPIC,
                      "topic_find 命中唯一行为主题名（类型精确匹配，按主题名排序）");
                auto findMiss = testInvoke(svc,
                        "/topic_find", yomk::msgs::dds::DebugTopicFindRequest::shared(DebugTopicFindRequest{"Not::Exist", 1, 50}));
                check(findMiss.status == yomk::payload::ResStatus::Failed &&
                          findMiss.msg.find("type [Not::Exist] not found") != std::string::npos,
                      "topic_find 未匹配类型 → eNo type [Not::Exist] not found（info 族语义）");

                // interface show：按类型名输出反射成员表 + struct 依赖闭包（域 200 纯净唯一类型，
                // TypeObject 经 TypeLookup 已就绪）→ root 成员恰一个 data/string 且无依赖；
                // 未发现类型报错（find 族语义）
                auto ifaceHit = testInvoke(svc, "/interface_show", yomk::msgs::dds::DebugInterfaceShowRequest::shared(DebugInterfaceShowRequest{"YomkDdsStd::MString", 5, 100}));
                check(ifaceHit.status == yomk::payload::ResStatus::Succeeded,
                      "interface_show(YomkDdsStd::MString,5,100ms) → eOk（按类型名命中）");
                auto ifacePkg = yomk::msgs::unpack<yomk::msgs::dds::InterfaceInfo>(ifaceHit.data);
                check(ifacePkg != nullptr && ifacePkg->msg.root.typeName == "YomkDdsStd::MString",
                      "interface_show 载荷为 InterfaceInfo 且 root.typeName 为查询类型名");
                check(ifacePkg != nullptr && ifacePkg->msg.root.members.size() == 1 &&
                          ifacePkg->msg.root.members[0].name == "data" &&
                          ifacePkg->msg.root.members[0].typeName == "string",
                      "interface_show root 成员表恰一项 data/string（声明序 + IDL 类型表达式）");
                check(ifacePkg != nullptr && ifacePkg->msg.dependencies.empty(),
                      "interface_show MString 无 struct 依赖 → dependencies 为空");
                if (ifacePkg != nullptr)
                {
                    std::cout << "[OBSERVE] iface root=" << ifacePkg->msg.root.typeName
                              << " members=" << ifacePkg->msg.root.members.size()
                              << " deps=" << ifacePkg->msg.dependencies.size() << std::endl;
                }
                auto ifaceMiss = testInvoke(svc, "/interface_show", yomk::msgs::dds::DebugInterfaceShowRequest::shared(DebugInterfaceShowRequest{"Not::Exist", 1, 50}));
                check(ifaceMiss.status == yomk::payload::ResStatus::Failed &&
                          ifaceMiss.msg.find("type [Not::Exist] not found") != std::string::npos,
                      "interface_show 未发现类型 → eNo type [Not::Exist] not found（find 族语义）");
                // 错误精确化回归：类型名未入发现缓存属第一层，msg 不得被误归类为
                // TypeObject 不可得（writer 已在场且 TypeObject 已就绪）
                check(ifaceMiss.msg.find("not available") == std::string::npos,
                      "interface_show 未发现类型 msg 仍为第一层文本");

                // interface_list：hit 域内唯一类型名（去重后单行，interface show 配套导航）
                auto ifaceListHit = testInvoke(svc, "/interface_list", yomk::msgs::dds::DebugInterfaceListRequest::shared(DebugInterfaceListRequest{1, 50}));
                check(ifaceListHit.status == yomk::payload::ResStatus::Succeeded && ifaceListHit.data != nullptr,
                      "interface_list 命中 → eOk（发现缓存收集类型名）");
                auto ifaceListArr = yomk::msgs::unpack<yomk::msgs::StringArray>(ifaceListHit.data);;
                check(ifaceListArr != nullptr && ifaceListArr->d.size() == 1 &&
                          ifaceListArr->d[0] == "YomkDdsStd::MString",
                      "interface_list 去重清单唯一行 == YomkDdsStd::MString");
                for (const auto &l : ifaceListArr->d)
                {
                    std::cout << "[OBSERVE] interface list: " << l << std::endl;
                }

                // topic_example：结构化载荷（topicName/typeName/interfaceInfo/json）——
                // 展示文本（Type/IDL/example 命令）由 CLI 渲染，服务层不再出 StringArray
                auto exampleHit = testInvoke(svc, "/topic_example", yomk::msgs::dds::DebugTopicExampleRequest::shared(DebugTopicExampleRequest{HIT_TOPIC, 5, 100}));
                check(exampleHit.status == yomk::payload::ResStatus::Succeeded && exampleHit.data != nullptr,
                      "topic_example(hello_world,5,100ms) → eOk（发布示例命中）");
                auto examplePkg = yomk::msgs::unpack<yomk::msgs::dds::TopicExampleInfo>(exampleHit.data);
                check(examplePkg != nullptr, "topic_example 返回包可解包为结构化 TopicExampleInfo");
                if (examplePkg != nullptr)
                {
                    const auto &ex = examplePkg->msg;
                    check(ex.topicName == HIT_TOPIC, "topic_example topicName 回填被查询主题名");
                    check(ex.typeName == "YomkDdsStd::MString",
                          "topic_example typeName 为原始 DDS 类型名（无转换）");
                    check(ex.interfaceInfo.root.typeName == ex.typeName,
                          "topic_example interfaceInfo.root.typeName 与平铺 typeName 同值");
                    check(ex.interfaceInfo.root.members.size() == 1 &&
                              ex.interfaceInfo.root.members[0].name == "data" &&
                              ex.interfaceInfo.root.members[0].typeName == "string",
                          "topic_example root 成员表恰一项 data/string（声明序 + IDL 类型表达式）");
                    check(ex.interfaceInfo.dependencies.empty(),
                          "topic_example MString 无 struct 依赖 → dependencies 为空");
                    check(!ex.json.empty() && ex.json.front() == '{' && ex.json.back() == '}' &&
                              ex.json.find("\"data\"") != std::string::npos,
                          "topic_example json 为紧凑默认值模板（{...} 且含 data 字段）");
                    std::cout << "[OBSERVE] topic example type=" << ex.typeName
                              << " json=|" << ex.json << "|" << std::endl;
                }

                // topic_pub：writer 在场（默认 RELIABLE）→ 发现收敛+匹配收敛+write 一次+ack 确认
                auto pubHit = testInvoke(svc, "/topic_pub", yomk::msgs::dds::DebugTopicPubRequest::shared(DebugTopicPubRequest{HIT_TOPIC, R"({"data":"t_pub_once"})", 5, 100}));
                check(pubHit.status == yomk::payload::ResStatus::Succeeded &&
                          pubHit.msg.find("delivered") != std::string::npos,
                      "topic_pub(hello_world, 合法 JSON) → eOk delivered（ack 确认送达）");
                auto pubPkg = yomk::msgs::unpack<yomk::msgs::dds::TopicPublishInfo>(pubHit.data);
                check(pubPkg != nullptr && pubPkg->msg.topicName == HIT_TOPIC,
                      "topic_pub 载荷为 TopicPublishInfo 且 topicName 回填目标主题");
                check(pubPkg != nullptr && pubPkg->msg.total == 1 && pubPkg->msg.failed == 0,
                      "topic_pub 单次模式统计 total=1/failed=0（写成功一次无失败）");

                // 非法 JSON：解析失败不建 writer，不发布
                auto pubBad = testInvoke(svc, "/topic_pub", yomk::msgs::dds::DebugTopicPubRequest::shared(DebugTopicPubRequest{HIT_TOPIC, "{bad", 1, 50}));
                check(pubBad.status == yomk::payload::ResStatus::Failed &&
                          pubBad.msg.find("invalid json") != std::string::npos,
                      "topic_pub 非法 JSON → eNo invalid json for type [...]（不触 DDS 发布）");

                // topic_pub 带 qos 尾参（闭环 2 服务层下传链路端到端）：显式
                // BEST_EFFORT+VOLATILE（与 writer 默认底座双差异）持续模式给观察窗——
                // 下传链路（聚合尾参→handler 解包→node_->topicPub）完整走到 DDS，QoS
                // 落端点经直连 observer（独立 FastDDSDebugNode）发现读回断言（HIT_TOPIC
                // 上 hitParticipant 远端 writer 在场，命中需 ≥2 端点且全端点扫描，svc
                // writer 的 BE+VOL 与 peer 默认底座双差异无歧义）；debugPubStop 停止
                // 持续发布后 invoke 返回
                {
                    DDSQosProfile pubQos;
                    pubQos.reliability = DDSReliability::BEST_EFFORT;
                    pubQos.durability = DDSDurability::VOLATILE;
                    std::atomic<int> pubQosStatus{-1};
                    std::thread pubQosThread([&]() {
                        auto r = testInvoke(svc, "/topic_pub", yomk::msgs::dds::DebugTopicPubRequest::shared(DebugTopicPubRequest{HIT_TOPIC,
                            R"({"data":"t_pub_qos"})", 5, 100, 100, 0, 0, pubQos}));
                        pubQosStatus.store(static_cast<int>(r.status));
                    });
                    FastDDSDebugNode observer;
                    check(observer.setDomainId(TEST_DOMAIN),
                          "qos 下传用例直连 observer setDomainId → true");
                    yomk::payload::dds::TopicInfo obInfo;
                    bool obSeen = false;
                    for (int i = 0; i < 40 && !obSeen; ++i)
                    {
                        obInfo = yomk::payload::dds::TopicInfo{};
                        // 命中需 ≥2 端点：HIT_TOPIC 上 hitParticipant 远端 writer（默认
                        // 底座）在场，svc topicPub writer 尚未被观察节点发现时不能提前命中
                        obSeen = observer.topicInfo(HIT_TOPIC, obInfo, 2, 100) &&
                                obInfo.publishers.size() >= 2;
                        if (!obSeen)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                    }
                    yomk::debugPubStop();
                    pubQosThread.join();
                    check(obSeen, "qos 下传用例 observer 读回 svc 发布 writer 端点详情");
                    bool obRel = false;
                    bool obDur = false;
                    // 全端点扫描：peer（RE+TL 默认底座）与 svc writer（BE+VOL）双差异无歧义
                    for (const auto &ep : obInfo.publishers)
                    {
                        obRel = obRel || ep.qos.reliability == "BEST_EFFORT";
                        obDur = obDur || ep.qos.durability == "VOLATILE";
                        std::cout << "[OBSERVE] svc qos writer reliability=" << ep.qos.reliability
                                  << " durability=" << ep.qos.durability << std::endl;
                    }
                    check(obRel && obDur,
                          "topic_pub 带 qos 尾参 → writer 发现读回 BEST_EFFORT+VOLATILE（服务层下传链路端到端）");
                    check(pubQosStatus.load() == static_cast<int>(yomk::payload::ResStatus::Succeeded),
                          "qos 下传持续模式停止后 invoke 返回 eOk");
                }

                // topic_msg：结构化载荷（topicName/typeName/json，展示文本由 CLI 渲染）
                auto msgHit = testInvoke(svc, "/topic_msg", yomk::msgs::dds::DebugTopicMsgRequest::shared(DebugTopicMsgRequest{HIT_TOPIC, 5, 100}));
                check(msgHit.status == yomk::payload::ResStatus::Succeeded && msgHit.data != nullptr,
                      "topic_msg(hello_world,5,100ms) → eOk（消息描述命中）");
                auto msgPkg = yomk::msgs::unpack<yomk::msgs::dds::TopicMessageInfo>(msgHit.data);
                check(msgPkg != nullptr, "topic_msg 返回包可解包为结构化 TopicMessageInfo");
                if (msgPkg != nullptr)
                {
                    const auto &tm = msgPkg->msg;
                    check(tm.topicName == HIT_TOPIC, "topic_msg topicName 回填被查询主题名");
                    check(tm.typeName == "YomkDdsStd::MString",
                          "topic_msg typeName 为原始 DDS 类型名（无转换）");
                    check(!tm.json.empty() && tm.json.front() == '{' && tm.json.back() == '}' &&
                              tm.json.find("\"data\"") != std::string::npos,
                          "topic_msg json 为紧凑 msg 模板（{...} 且含 data 字段）");
                    std::cout << "[OBSERVE] topic msg type=" << tm.typeName
                              << " json=|" << tm.json << "|" << std::endl;
                }
            }

            // 清理：writer → topic → publisher → participant（同 TestFastDDSDebugNode 计数用例顺序）
            if (writer != nullptr && pub != nullptr)
            {
                pub->delete_datawriter(writer);
            }
            if (topic != nullptr)
            {
                hitParticipant->delete_topic(topic);
            }
            if (pub != nullptr)
            {
                hitParticipant->delete_publisher(pub);
            }
            dds::DomainParticipantFactory::get_instance()->delete_participant(hitParticipant);
        }
    }

    // ---- node_info 命中用例：远端命名 participant（pub+sub 双端点触发 EDP 发现，不发布数据）----
    // 复用重建后的域 200 调试节点；时序模式同 topic_info 命中用例：list 预热 + node_info 独立
    // 收敛窗口（500ms 起）覆盖本地 PDP/EDP 发现延迟；断言对齐 ros2 node info 输出形态
    {
        namespace dds = eprosima::fastdds::dds;  // 块内别名：直连 FastDDS API 搭建远端双端点
        constexpr const char *PEER_NAME = "test-svc-nodeinfo";
        constexpr const char *HIT_PUB_TOPIC = "t_nodeinfo_hit_pub";
        constexpr const char *HIT_SUB_TOPIC = "t_nodeinfo_hit_sub";
        dds::DomainParticipantQos peerQos = dds::PARTICIPANT_QOS_DEFAULT;
        peerQos.name(std::string(PEER_NAME));
        auto *nodeinfoParticipant = dds::DomainParticipantFactory::get_instance()->create_participant(
            TEST_DOMAIN, peerQos);
        check(nodeinfoParticipant != nullptr, "命名 peer participant 创建成功（node_info 命中用例）");
        if (nodeinfoParticipant != nullptr)
        {
            // TypeSupport 须活过 topic/writer/reader 使用期（声明顺序同 topic_info 命中用例）
            dds::TypeSupport ts(new YomkDdsStd::MStringPubSubType());
            ts.register_type(nodeinfoParticipant);
            auto *pub = nodeinfoParticipant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);
            auto *pubTopic = nodeinfoParticipant->create_topic(
                HIT_PUB_TOPIC, ts.get_type_name(), dds::TOPIC_QOS_DEFAULT);
            auto *writer = (pub != nullptr && pubTopic != nullptr)
                ? pub->create_datawriter(pubTopic, dds::DATAWRITER_QOS_DEFAULT)
                : nullptr;
            auto *sub = nodeinfoParticipant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);
            auto *subTopic = nodeinfoParticipant->create_topic(
                HIT_SUB_TOPIC, ts.get_type_name(), dds::TOPIC_QOS_DEFAULT);
            auto *reader = (sub != nullptr && subTopic != nullptr)
                ? sub->create_datareader(subTopic, dds::DATAREADER_QOS_DEFAULT)
                : nullptr;
            check(writer != nullptr && reader != nullptr,
                  "远端 DataWriter/DataReader 创建成功（t_nodeinfo_hit_pub/sub，仅建端点不发布数据）");

            if (writer != nullptr && reader != nullptr)
            {
                // list_topics 预热仅为本用例时序前置（late joiner 经 EDP 重放发现信息）；
                // node_info 内部独立收敛查询，不依赖 list_topics
                auto warm = testInvoke(svc, "/list_topics", yomk::msgs::dds::DebugListTopicsRequest::shared(DebugListTopicsRequest{5, 100}));
                check(warm.status == yomk::payload::ResStatus::Succeeded, "list_topics(5,100ms) 预热 → eOk（发现时序前置）");

                auto hitNodeInfo = testInvoke(svc, "/node_info", mkNodeInfo(PEER_NAME, 5, 100));
                check(hitNodeInfo.status == yomk::payload::ResStatus::Succeeded && hitNodeInfo.data != nullptr,
                      "nodeInfo(test-svc-nodeinfo,5,100ms) → eOk（独立收敛命中）");
                auto nodePkg = yomk::msgs::unpack<yomk::msgs::dds::NodeInfo>(hitNodeInfo.data);
                check(nodePkg != nullptr, "nodeInfo 返回包可解包为结构化 NodeInfo");
                if (nodePkg != nullptr)
                {
                    const auto &ni = nodePkg->msg;
                    check(ni.nodeName == PEER_NAME, "nodeInfo nodeName 为 participant_name 原样");
                    check(ni.subscribers.size() == 1 &&
                              ni.subscribers[0].topicName == HIT_SUB_TOPIC &&
                              ni.subscribers[0].typeName == "YomkDdsStd::MString",
                          "nodeInfo subscribers 命中 (t_nodeinfo_hit_sub, YomkDdsStd::MString)");
                    check(ni.publishers.size() == 1 &&
                              ni.publishers[0].topicName == HIT_PUB_TOPIC &&
                              ni.publishers[0].typeName == "YomkDdsStd::MString",
                          "nodeInfo publishers 命中 (t_nodeinfo_hit_pub, YomkDdsStd::MString)");
                    std::cout << "[OBSERVE] node_info node=" << ni.nodeName
                              << " pubs=" << ni.publishers.size()
                              << " subs=" << ni.subscribers.size() << std::endl;
                }

                // 未发现节点名：单次快照快速路径 → eNo（归属判定不命中即早退）
                auto absentNode = testInvoke(svc, "/node_info", mkNodeInfo("t_no_such_node", 1, 50));
                check(absentNode.status == yomk::payload::ResStatus::Failed &&
                          absentNode.msg.find("not found") != std::string::npos,
                      "nodeInfo 未发现节点名（单次快照）→ eNo node not found");
            }

            // 清理：reader → writer → topic → publisher/subscriber → participant
            if (reader != nullptr && sub != nullptr)
            {
                sub->delete_datareader(reader);
            }
            if (writer != nullptr && pub != nullptr)
            {
                pub->delete_datawriter(writer);
            }
            if (subTopic != nullptr)
            {
                nodeinfoParticipant->delete_topic(subTopic);
            }
            if (pubTopic != nullptr)
            {
                nodeinfoParticipant->delete_topic(pubTopic);
            }
            if (sub != nullptr)
            {
                nodeinfoParticipant->delete_subscriber(sub);
            }
            if (pub != nullptr)
            {
                nodeinfoParticipant->delete_publisher(pub);
            }
            dds::DomainParticipantFactory::get_instance()->delete_participant(nodeinfoParticipant);
        }
    }

    // ---- A-2：domainId 边界（有效域下界/上界，创建后即删） ----
    check(testInvoke(svc, "/delete_node").status == yomk::payload::ResStatus::Succeeded, "删除重建节点 → eOk（A-2 前清理）");
    check(testInvoke(svc, "/create_node", mkNode(0)).status == yomk::payload::ResStatus::Succeeded,
          "domainId=0 → eOk（有效域下界）");
    check(testInvoke(svc, "/delete_node").status == yomk::payload::ResStatus::Succeeded, "删除 → eOk");
    check(testInvoke(svc, "/create_node", mkNode(232)).status == yomk::payload::ResStatus::Succeeded,
          "domainId=232 → eOk（有效域上界，FastDDS 端口算术上限）");
    check(testInvoke(svc, "/delete_node").status == yomk::payload::ResStatus::Succeeded, "删除 → eOk（关键不变式：退出前清理）");

    // ---- A-3：闭环 4 serverLocator 尾参直调（DebugCreateNodeRequest{domainId, serverLocator} 聚合初始化
    // 经服务层 createNode 组 spec 直达节点层；server 不在场 CLIENT 仍创建成功，非法串回显不建节点）----
    check(testInvoke(svc, "/create_node",
              yomk::msgs::dds::DebugCreateNodeRequest::shared(DebugCreateNodeRequest{TEST_DOMAIN, "127.0.0.1:11811"})).status ==
              yomk::payload::ResStatus::Succeeded,
          "create_node 尾参 serverLocator 直配 → eOk（CLIENT+127.0.0.1:11811，server 不在场）");
    check(testInvoke(svc, "/delete_node").status == yomk::payload::ResStatus::Succeeded, "删除尾参直配节点 → eOk");

    auto badLoc = testInvoke(svc, "/create_node",
                      yomk::msgs::dds::DebugCreateNodeRequest::shared(DebugCreateNodeRequest{TEST_DOMAIN, "10.0.0.1:99999"}));
    check(badLoc.status == yomk::payload::ResStatus::Failed &&
              badLoc.msg.find("port out of range") != std::string::npos,
          "create_node 非法尾参 serverLocator(port 越界) → eNo + 回显（不建节点）");

    return testReport("TestYomkDdsDebugHandlerLifecycle");
}
