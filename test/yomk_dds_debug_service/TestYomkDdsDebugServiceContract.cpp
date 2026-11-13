/**
 * @file TestYomkDdsDebugServiceContract.cpp
 * @brief YomkDdsDebugService 服务层契约测试（DDS-free，白盒经服务器公开路由直接分发）
 *
 * 范围：仅验证调试服务 14 端点在不触发任何 DDS 运行时（不创建 participant）前提下的
 *       输入校验与错误码契约；真实 DDS 生命周期（创建/删除/登记）归
 *       TestYomkDdsDebugServiceLifecycle，节点层守卫与端到端流量归 TestFastDDSDebugNode。
 * 覆盖：
 *   T1 funcInfos 内省 14 端点齐全 + 未知端点 eNo；
 *   T2 /version 正常路径（eOk + 版本串契约 + 忽略 pkg）；
 *   T3 /create_node、/topic_print、/list_topics、/topic_info、/topic_find、/interface_show、
 *      /interface_list、/topic_example、/topic_pub、/list_nodes、/node_info 解包双守卫
 *      （nullptr / 异类包 / 改名伪造）；
 *   T4 /delete_node 特殊契约：单节点模型无参载荷，handler (void)pkg 不走解包守卫，未建节点时
 *      无论何种载荷均 eNo "debug node not created"（与 YomkDdsService delete_node 的 String
 *      包解包守卫形态不同，属设计差异点）；
 *   T5 /create_node domainId 越界前置拦截（233 / UINT32_MAX → eNo，不触 DDS）；
 *   T6 /topic_print 未建节点早退（node_ 空检查先于 output 空检查）；
 *   T7 /topic_info 未建节点早退（node_ 空检查先于独立收敛触达）。
 *   T8 /list_nodes 未建节点早退（node_ 空检查先于独立收敛触达）。
 *   T9 /node_info 未建节点早退（node_ 空检查先于独立收敛触达）。
 *   T10 /topic_find 未建节点早退（node_ 空检查先于独立收敛触达）。
 *   T11 /interface_show 未建节点早退（node_ 空检查先于独立收敛触达）。
 *   T13 /topic_pub 未建节点早退（node_ 空检查先于独立收敛触达）。
 *   T14 /topic_msg 未建节点早退（node_ 空检查先于独立收敛触达）。
 * DDS-free 保证：T5 的越界校验在锁外返回；T4/T6/T7/T8/T9/T10/T11/T12/T13/T14 的 node_ 空检查在触达节点层前返回；
 *   /version 不碰 DDS；本测试绝不传合法 DDSDebugNode{0..232}（那会创建真实 participant）。
 *
 * 风格：纯 main() + check 函数 + 失败计数（零第三方依赖），返回非 0 表示存在失败用例。
 */

#include "TestCheck.h"
#include "TestServiceAccess.h"
#include "YomkDdsDebugService.h" // 服务/DDSDebugNode/DDSDebugTopic/String/YOMK_* 宏
#include "YomkDdsMsg.h"

#include <cstdint>
#include <string>

namespace
{
    // T2：/version 契约（不解包，忽略 pkg）
    void testVersion(YomkDdsDebugService *svc)
    {
        auto resp = testInvoke(svc, "/version");
        check(resp.status == yomk::payload::ResStatus::Succeeded, "/version 返回 eOk");
        check(resp.msg == "ok", "/version msg == ok");

        auto ver = ::yomk::msgs::unpack<::yomk::msgs::String>(resp.data);;
        check(ver != nullptr, "/version data 可解包为 String");
        if (ver != nullptr)
        {
            check(ver->d.rfind("YomkDds v", 0) == 0, "/version 版本串前缀为 YomkDds v");
        }

        auto respNull = testInvoke(svc, "/version", nullptr);
        check(respNull.status == yomk::payload::ResStatus::Succeeded, "/version 传 nullptr 仍 eOk（忽略 pkg）");
    }

    // T3：/create_node、/topic_print、/list_topics、/topic_info、/list_nodes、/node_info 的解包双守卫
    // （nullptr / 异类包 / 改名伪造；DDSDebugInfo 加 verbose、DDSDebugList 加 types 字段后
    // 守卫包名不变，无需改断言）
    void testUnpackGuards(YomkDdsDebugService *svc)
    {
        auto checkGuards = [&](const char *ep, const char *expectName, YomkMsgPtr wrongPkg)
        {
            // (a) nullptr → 第一守卫 !pkg
            auto ra = testInvoke(svc, ep, nullptr);
            check(ra.status == yomk::payload::ResStatus::Failed &&
                      ra.msg.find("pkg is null or pkg is not") != std::string::npos,
                  std::string(ep) + " : nullptr 包命中第一守卫 eNo");

            // (b) 异类包（保留自身名）→ 第一守卫 name()!=expectName
            auto rb = testInvoke(svc, ep, wrongPkg);
            check(rb.status == yomk::payload::ResStatus::Failed &&
                      rb.msg.find("pkg is null or pkg is not") != std::string::npos,
                  std::string(ep) + " : 异类包命中第一守卫 eNo");

            // (c) 改名伪造：异类包改名为期望名 → 第一守卫通过、dynamic_pointer_cast 失败命中第二守卫
            wrongPkg->name(expectName);
            auto rc = testInvoke(svc, ep, wrongPkg);
            check(rc.status == yomk::payload::ResStatus::Failed &&
                      rc.msg.find("dynamic_pointer_cast failed") != std::string::npos,
                  std::string(ep) + " : 改名伪造命中第二守卫(dynamic_pointer_cast 失败) eNo");
        };

        checkGuards("/create_node", "DDSDebugNode", ::yomk::msgs::String::shared("wrong"));
        checkGuards("/topic_print", "DDSDebugTopic", ::yomk::msgs::DDSDebugNode::shared(DDSDebugNode{0}));
        checkGuards("/list_topics", "DDSDebugList", ::yomk::msgs::String::shared("wrong"));
        checkGuards("/topic_info", "DDSDebugInfo", ::yomk::msgs::String::shared("wrong"));
        checkGuards("/topic_find", "DDSDebugFind", ::yomk::msgs::String::shared("wrong"));
        checkGuards("/list_nodes", "DDSNodeList", ::yomk::msgs::String::shared("wrong"));
        checkGuards("/node_info", "DDSNodeInfo", ::yomk::msgs::String::shared("wrong"));
    }

    // T4：/delete_node 特殊契约——单节点模型无参载荷，(void)pkg 不走解包守卫
    void testNoPayloadEndpoints(YomkDdsDebugService *svc)
    {
        // 未建节点：无论载荷形态，均在 node_ 空检查处早退（不触 DDS）
        auto checkNoPayload = [&](const char *ep)
        {
            auto rNull = testInvoke(svc, ep, nullptr);
            check(rNull.status == yomk::payload::ResStatus::Failed &&
                      rNull.msg.find("debug node not created") != std::string::npos,
                  std::string(ep) + " : nullptr 载荷 → eNo debug node not created（不走解包守卫）");

            auto rWrong = testInvoke(svc, ep, ::yomk::msgs::String::shared("wrong"));
            check(rWrong.status == yomk::payload::ResStatus::Failed &&
                      rWrong.msg.find("debug node not created") != std::string::npos,
                  std::string(ep) + " : 异类载荷 → eNo debug node not created（载荷被忽略）");

            auto rNoArg = testInvoke(svc, ep);
            check(rNoArg.status == yomk::payload::ResStatus::Failed &&
                      rNoArg.msg.find("debug node not created") != std::string::npos,
                  std::string(ep) + " : 无载荷 → eNo debug node not created（对齐无参宏形态）");
        };
        checkNoPayload("/delete_node");
    }

    // T5：/create_node domainId 越界前置拦截（锁外校验，不触 DDS）
    void testCreateDomainBoundary(YomkDdsDebugService *svc)
    {
        auto r233 = testInvoke(svc, "/create_node", ::yomk::msgs::DDSDebugNode::shared(DDSDebugNode{233}));
        check(r233.status == yomk::payload::ResStatus::Failed &&
                  r233.msg.find("out of valid range") != std::string::npos,
              "domainId=233 → eNo out of valid range（前置拦截，不建 participant）");

        auto rmax = testInvoke(svc, "/create_node", ::yomk::msgs::DDSDebugNode::shared(DDSDebugNode{UINT32_MAX}));
        check(rmax.status == yomk::payload::ResStatus::Failed &&
                  rmax.msg.find("out of valid range") != std::string::npos,
              "domainId=UINT32_MAX → eNo out of valid range（前置拦截，不再回绕深入 setDomainId）");
    }

    // T6：/topic_print 未建节点早退（node_ 空检查先于 output 空检查）
    void testTopicPrintNoNode(YomkDdsDebugService *svc)
    {
        auto rEmptyCb = testInvoke(svc, "/topic_print", ::yomk::msgs::DDSDebugTopic::shared(DDSDebugTopic{"t_debug", nullptr}));
        check(rEmptyCb.status == yomk::payload::ResStatus::Failed &&
                  rEmptyCb.msg.find("debug node not created") != std::string::npos,
              "/topic_print 合法包+空回调+未建节点 → eNo debug node not created（node_ 检查在前）");

        auto rValid = testInvoke(svc,
            "/topic_print", ::yomk::msgs::DDSDebugTopic::shared(DDSDebugTopic{"t_debug", [](const std::string &) {}}));
        check(rValid.status == yomk::payload::ResStatus::Failed &&
                  rValid.msg.find("debug node not created") != std::string::npos,
              "/topic_print 合法包+非空回调+未建节点 → eNo debug node not created");
    }

    // T7：/topic_info 未建节点早退（node_ 空检查先于独立收敛触达）
    void testTopicInfoNoNode(YomkDdsDebugService *svc)
    {
        // stableRounds=1 为单次快照快速路径；未建节点时 node_ 空检查先行返回，不触 DDS；
        // 3 字段聚合初始化在 DDSDebugInfo 加 verbose（默认 false）后仍兼容（编译级验证）
        auto rValid = testInvoke(svc, "/topic_info", ::yomk::msgs::DDSDebugInfo::shared(DDSDebugInfo{"t_info", 1, 50}));
        check(rValid.status == yomk::payload::ResStatus::Failed &&
                  rValid.msg.find("debug node not created") != std::string::npos,
              "/topic_info 合法包+未建节点 → eNo debug node not created（node_ 检查先于触达节点层）");
    }

    // T8：/list_nodes 未建节点早退（node_ 空检查先于独立收敛触达）
    void testListNodeNoNode(YomkDdsDebugService *svc)
    {
        // stableRounds=1 为单次快照快速路径；未建节点时 node_ 空检查先行返回，不触 DDS
        auto rValid = testInvoke(svc, "/list_nodes", ::yomk::msgs::DDSNodeList::shared(DDSNodeList{1, 50}));
        check(rValid.status == yomk::payload::ResStatus::Failed &&
                  rValid.msg.find("debug node not created") != std::string::npos,
              "/list_nodes 合法包+未建节点 → eNo debug node not created（node_ 检查先于触达节点层）");
    }

    // T9：/node_info 未建节点早退（node_ 空检查先于独立收敛触达）
    void testNodeInfoNoNode(YomkDdsDebugService *svc)
    {
        // stableRounds=1 为单次快照快速路径；未建节点时 node_ 空检查先行返回，不触 DDS
        auto rValid =
            testInvoke(svc, "/node_info", ::yomk::msgs::DDSNodeInfo::shared(DDSNodeInfo{"t_no_node", 1, 50}));
        check(rValid.status == yomk::payload::ResStatus::Failed &&
                  rValid.msg.find("debug node not created") != std::string::npos,
              "/node_info 合法包+未建节点 → eNo debug node not created（node_ 检查先于触达节点层）");
    }

    // T10：/topic_find 未建节点早退（node_ 空检查先于独立收敛触达）
    void testTopicFindNoNode(YomkDdsDebugService *svc)
    {
        // stableRounds=1 为单次快照快速路径；未建节点时 node_ 空检查先行返回，不触 DDS
        auto rValid = testInvoke(svc, "/topic_find", ::yomk::msgs::DDSDebugFind::shared(DDSDebugFind{"t_no_node", 1, 50}));
        check(rValid.status == yomk::payload::ResStatus::Failed &&
                  rValid.msg.find("debug node not created") != std::string::npos,
              "/topic_find 合法包+未建节点 → eNo debug node not created（node_ 检查先于触达节点层）");
    }

    // T11：/interface_show 未建节点早退（node_ 空检查先于独立收敛触达）
    void testInterfaceShowNoNode(YomkDdsDebugService *svc)
    {
        // stableRounds=1 为单次快照快速路径；未建节点时 node_ 空检查先行返回，不触 DDS
        auto rValid = testInvoke(svc,
            "/interface_show", ::yomk::msgs::DDSDebugInterfaceShow::shared(DDSDebugInterfaceShow{"t_no_node", 1, 50}));
        check(rValid.status == yomk::payload::ResStatus::Failed &&
                  rValid.msg.find("debug node not created") != std::string::npos,
              "/interface_show 合法包+未建节点 → eNo debug node not created（node_ 检查先于触达节点层）");
    }

    // T11：/interface_list 未建节点早退（node_ 空检查先于独立收敛触达）
    void testInterfaceListNoNode(YomkDdsDebugService *svc)
    {
        // stableRounds=1 为单次快照快速路径；未建节点时 node_ 空检查先行返回，不触 DDS
        auto rValid = testInvoke(svc,
            "/interface_list", ::yomk::msgs::DDSDebugInterfaceList::shared(DDSDebugInterfaceList{1, 50}));
        check(rValid.status == yomk::payload::ResStatus::Failed &&
                  rValid.msg.find("debug node not created") != std::string::npos,
              "/interface_list 合法包+未建节点 → eNo debug node not created（node_ 检查先于触达节点层）");
    }

    // T12：/topic_example 未建节点早退（node_ 空检查先于独立收敛触达）
    void testTopicExampleNoNode(YomkDdsDebugService *svc)
    {
        // stableRounds=1 为单次快照快速路径；未建节点时 node_ 空检查先行返回，不触 DDS
        auto rValid = testInvoke(svc,
            "/topic_example", ::yomk::msgs::DDSDebugTopicExample::shared(DDSDebugTopicExample{"t_no_node", 1, 50}));
        check(rValid.status == yomk::payload::ResStatus::Failed &&
                  rValid.msg.find("debug node not created") != std::string::npos,
              "/topic_example 合法包+未建节点 → eNo debug node not created（node_ 检查先于触达节点层）");
    }

    // T13：/topic_pub 未建节点早退（node_ 空检查先于独立收敛触达）
    void testTopicPubNoNode(YomkDdsDebugService *svc)
    {
        // stableRounds=1 为单次快照快速路径；未建节点时 node_ 空检查先行返回，不触 DDS
        auto rValid = testInvoke(svc,
            "/topic_pub", ::yomk::msgs::DDSDebugTopicPub::shared(DDSDebugTopicPub{"t_no_node", "{}", 1, 50}));
        check(rValid.status == yomk::payload::ResStatus::Failed &&
                  rValid.msg.find("debug node not created") != std::string::npos,
              "/topic_pub 合法包+未建节点 → eNo debug node not created（node_ 检查先于触达节点层）");
    }

    // T14：/topic_msg 未建节点早退（node_ 空检查先于独立收敛触达）
    void testTopicMsgNoNode(YomkDdsDebugService *svc)
    {
        // stableRounds=1 为单次快照快速路径；未建节点时 node_ 空检查先行返回，不触 DDS
        auto rValid = testInvoke(svc,
            "/topic_msg", ::yomk::msgs::DDSDebugTopicMsg::shared(DDSDebugTopicMsg{"t_no_node", 1, 50}));
        check(rValid.status == yomk::payload::ResStatus::Failed &&
                  rValid.msg.find("debug node not created") != std::string::npos,
              "/topic_msg 合法包+未建节点 → eNo debug node not created（node_ 检查先于触达节点层）");
    }
} // namespace

int main()
{
    ::yomk::server().init();

    // 注册被测服务：所有权移交框架（shared_ptr 持有），init() 内部调用后 weak_from_this() 方有效
    auto *svc = new YomkDdsDebugService(::yomk::server().instance().get());
    check(::yomk::server().srv_ptr(svc).add() == 0, "YomkDdsDebugService 注册成功（所有权移交框架，init() 已内部调用）");

    // T1：内省——14 端点齐全
    auto infos = testFuncInfos(svc);
    check(infos.size() == 14 && infos.count("/version") && infos.count("/create_node") &&
              infos.count("/topic_print") && infos.count("/list_topics") && infos.count("/topic_info") &&
              infos.count("/topic_find") && infos.count("/interface_show") && infos.count("/interface_list") &&
              infos.count("/topic_example") && infos.count("/topic_msg") && infos.count("/topic_pub") &&
              infos.count("/list_nodes") && infos.count("/node_info") && infos.count("/delete_node"),
          "funcInfos 内省 14 端点齐全");

    testVersion(svc);
    testUnpackGuards(svc);
    testNoPayloadEndpoints(svc);
    testCreateDomainBoundary(svc);
    testTopicPrintNoNode(svc);
    testTopicInfoNoNode(svc);
    testListNodeNoNode(svc);
    testNodeInfoNoNode(svc);
    testTopicFindNoNode(svc);
    testInterfaceShowNoNode(svc);
    testInterfaceListNoNode(svc);
    testTopicExampleNoNode(svc);
    testTopicPubNoNode(svc);
    testTopicMsgNoNode(svc);

    check(testInvoke(svc, "/no_such_endpoint").status == yomk::payload::ResStatus::Failed, "未知端点返回 eNo");

    return testReport("TestYomkDdsDebugServiceContract");
}
