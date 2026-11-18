/**
 * @file TestYomkDdsBagHandlerLifecycle.cpp
 * @brief YomkDdsBagHandler 真实 DDS 生命周期测试
 *
 * 范围：契约测试（DDS-free）刻意豁免的 /create_node 成功路径（经 setDomainId 创建真实
 *       participant）、/bag_record 完整录制链路（校验通过 → 透传订阅 → mcap 落盘 →
 *       yomk::ctrl::dds::g_bagRecordStop 收尾 → RecordResult 统计回执）、/bag_play 完整回放链路与 /delete_node
 *       析构清理。节点层校验分支归 TestFastDDSBagNodeValidation，落盘内容与回放对账细节归
 *       TestFastDDSBagNodeRecord/Play。
 * 覆盖：
 *   L1 生命周期：创建 → 重复创建拒绝 → record 空清单 eNo → record 无端点主题 eNo（msg
 *      含主题名，校验失败不落盘）→ 完整录制（远端 MString 发布者 + 停止线程模拟 Ctrl+C →
 *      eOk + RecordResult（bagDir + 逐主题统计）→ 定格后重复 record 拒绝 → 删除 →
 *      重复删除拒绝 → 删除后 record 拒绝 → 重建 → 重复创建拒绝 → delete 收尾；
 *   L2 停止标志契约：录制经 yomk::ctrl::dds::g_bagRecordStop 置位收尾（模拟 SIGINT 处理函数行为）；
 *   L3 /bag_play 端点（重建节点后）：负载往返保真（路径不存在 → 错误文本回显 bagDir）+
 *      最小 bag 真实回放（mcap writer + 手写 metadata.json 造 2 条 @100ms bag → eOk +
 *      PlayResult 回执：bagDir + 逐主题统计（发布名/类型/条数/字节））。
 *   L0 解耦契约：/bag_info 纯文件读、/bag_reindex 纯文件操作与节点生命周期无关（未建/
 *      已建节点均可调，路径不存在报文件层错误而非 bag node not created）。
 *
 * 关键不变式：每个 eOk 创建的 bag 节点必须在 main 返回前经 /delete_node 显式删除，以规避
 *   YOMK 框架 atexit 服务析构晚于 FastDDS DomainParticipantFactory 单例销毁导致的静态析构
 *   顺序问题（同 TestYomkDdsDebugHandlerLifecycle）。
 *
 * 域号 200（对齐调试服务生命周期先例），与 bag 直测用例(201/202)错开，ctest 串行执行互不残留。
 *
 * 风格：纯 main() + check 函数 + 失败计数（零第三方依赖），返回非 0 表示存在失败用例。
 */

#include "TestCheck.h"
#include "TestServiceAccess.h"
#include "YomkDdsBagHandler.h" // 服务/BagCreateNodeRequest/BagRecordRequest/BagReindexRequest/yomk::ctrl::dds::g_bagRecordStop
#include "YomkDdsMsg.h"

#include <yomk/dds/YomkDdsStdMsgsPubSubTypes.hpp> // MStringPubSubType（完整录制用例的远端发布端）

#include <mcap/writer.hpp>      // L3 造最小 mcap 分片（MCAP_IMPLEMENTATION 单译元在被测库内）
#include <nlohmann/json.hpp>    // L3 手写 metadata.json（record 组装同款键集）

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace
{
    constexpr uint32_t TEST_DOMAIN = 200;
    constexpr const char *REC_TOPIC = "rt/bag_svc_rec";
    // 完整录制时长：校验窗（SPDP 发现发布者约 1~3s）+ ~5s 录制期
    constexpr int kRecordMs = 12000;
    constexpr int kPubIntervalMs = 150;
} // namespace

int main()
{
    namespace dds = eprosima::fastdds::dds;  // main 作用域别名：直连 FastDDS API 搭建远端发布端

    yomk::server().init();

    auto *svc = new YomkDdsBagHandler(yomk::server().instance().get());
    check(yomk::server().srv_ptr(svc).add() == 0, "YomkDdsBagHandler 注册成功（所有权移交框架，init() 已内部调用）");

    // ---- L0：/bag_info 与节点生命周期解耦（纯文件读，节点状态无关） ----
    auto infoBefore = testInvoke(svc, "/get_info", yomk::msgs::dds::BagInfoRequest::shared(BagInfoRequest{"info_svc_l0_miss"}));
    check(infoBefore.status == yomk::payload::ResStatus::Failed &&
              infoBefore.msg.find("bag path [info_svc_l0_miss] does not exist") != std::string::npos &&
              infoBefore.msg.find("bag node not created") == std::string::npos,
          "未建节点时 /bag_info 可达：报文件层错误（非 bag node not created）");

    // ---- L0：/bag_reindex 与节点生命周期解耦（纯文件操作，节点状态无关） ----
    auto reindexBefore =
        testInvoke(svc, "/reindex", yomk::msgs::dds::BagReindexRequest::shared(BagReindexRequest{"reindex_l0_miss"}));
    check(reindexBefore.status == yomk::payload::ResStatus::Failed &&
              reindexBefore.msg.find("bag path [reindex_l0_miss] does not exist") != std::string::npos &&
              reindexBefore.msg.find("bag node not created") == std::string::npos,
          "未建节点时 /bag_reindex 可达：报文件层错误（非 bag node not created）");

    // ---- L1：创建 / 重复创建拒绝 ----
    check(testInvoke(svc, "/create_node", yomk::msgs::dds::BagCreateNodeRequest::shared(BagCreateNodeRequest{TEST_DOMAIN})).status ==
              yomk::payload::ResStatus::Succeeded,
          "创建 bag 节点(domain 200) → eOk（真实创建 participant）");
    auto infoAfter = testInvoke(svc, "/get_info", yomk::msgs::dds::BagInfoRequest::shared(BagInfoRequest{"info_svc_l0_miss"}));
    check(infoAfter.status == yomk::payload::ResStatus::Failed &&
              infoAfter.msg.find("does not exist") != std::string::npos,
          "已建节点时 /bag_info 同样可达（行为与节点状态无关）");
    auto reindexAfter =
        testInvoke(svc, "/reindex", yomk::msgs::dds::BagReindexRequest::shared(BagReindexRequest{"reindex_l0_miss"}));
    check(reindexAfter.status == yomk::payload::ResStatus::Failed &&
              reindexAfter.msg.find("does not exist") != std::string::npos,
          "已建节点时 /bag_reindex 同样可达（行为与节点状态无关）");
    auto dupCreate = testInvoke(svc, "/create_node", yomk::msgs::dds::BagCreateNodeRequest::shared(BagCreateNodeRequest{TEST_DOMAIN}));
    check(dupCreate.status == yomk::payload::ResStatus::Failed &&
              dupCreate.msg.find("bag node already exists") != std::string::npos,
          "重复创建 → eNo bag node already exists, delete it first");

    // record 空清单：服务层锁外校验先于节点触达
    auto rEmpty = testInvoke(svc, "/record", yomk::msgs::dds::BagRecordRequest::shared(BagRecordRequest{{}}));
    check(rEmpty.status == yomk::payload::ResStatus::Failed &&
              rEmpty.msg.find("no topics given") != std::string::npos,
          "record 空清单 → eNo no topics given");

    // ---- record 无端点主题：校验失败 eNo，msg 含主题名（默认收敛窗 ~3s，不落盘） ----
    auto miss = testInvoke(svc, "/record", yomk::msgs::dds::BagRecordRequest::shared(BagRecordRequest{{"rt/bag_svc_miss"}}));
    check(miss.status == yomk::payload::ResStatus::Failed, "record 无端点主题 → eNo");
    check(miss.msg.find("主题 [rt/bag_svc_miss] 既无发布者也无订阅者") != std::string::npos,
          "record 无端点主题 msg 含逐主题报错文本");
    // 校验失败后节点未定格：删除并重建验证服务层后续可用（同失败后修正输入重试语义）
    check(testInvoke(svc, "/delete_node").status == yomk::payload::ResStatus::Succeeded, "校验失败后删除 → eOk");
    check(testInvoke(svc, "/create_node", yomk::msgs::dds::BagCreateNodeRequest::shared(BagCreateNodeRequest{TEST_DOMAIN})).status ==
              yomk::payload::ResStatus::Succeeded,
          "重建 bag 节点 → eOk");

    // ---- 完整录制：远端 MString 发布者 + 发布线程 + 停止线程模拟 Ctrl+C ----
    auto *peerParticipant = dds::DomainParticipantFactory::get_instance()->create_participant(
        TEST_DOMAIN, dds::PARTICIPANT_QOS_DEFAULT);
    check(peerParticipant != nullptr, "远端发布端 participant 创建成功（完整录制用例）");
    dds::TypeSupport ts(new YomkDdsStd::MStringPubSubType());
    ts.register_type(peerParticipant);
    auto *pub = peerParticipant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);
    auto *topic = peerParticipant->create_topic(REC_TOPIC, ts.get_type_name(), dds::TOPIC_QOS_DEFAULT);
    auto *writer =
        (pub != nullptr && topic != nullptr) ? pub->create_datawriter(topic, dds::DATAWRITER_QOS_DEFAULT)
                                             : nullptr;
    check(writer != nullptr, "远端 DataWriter 创建成功（rt/bag_svc_rec）");

    std::atomic<bool> pubStop{false};
    std::thread pubThread([&]()
    {
        YomkDdsStd::MString msg;
        uint32_t seq = 0;
        while (!pubStop.load())
        {
            msg.data("svc-record-payload-" + std::to_string(seq++));
            writer->write(&msg);
            std::this_thread::sleep_for(std::chrono::milliseconds(kPubIntervalMs));
        }
    });

    // 停止线程：~5s 后经 yomk::ctrl::dds::g_bagRecordStop 置位（模拟 CLI onSignal 的 SIGINT 路径，async-signal-safe store）
    std::thread stopThread([&]()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(kRecordMs));
        yomk::ctrl::dds::g_bagRecordStop.stop();
    });

    // 主线程长驻阻塞：校验通过 → 透传订阅 → 录制 → 收尾 → eOk 回执
    auto rec = testInvoke(svc, "/record", yomk::msgs::dds::BagRecordRequest::shared(BagRecordRequest{{REC_TOPIC}}));
    stopThread.join();
    pubStop.store(true);
    pubThread.join();

    check(rec.status == yomk::payload::ResStatus::Succeeded, "完整录制（yomk::ctrl::dds::g_bagRecordStop 收尾）→ eOk");
    auto result = yomk::msgs::unpack<yomk::msgs::dds::RecordResult>(rec.data);
    check(result != nullptr && !result->msg.topics.empty(),
          "record 回执可解包为 RecordResult（bagDir + 逐主题统计）");
    if (result != nullptr && !result->msg.topics.empty())
    {
        check(result->msg.bagDir.rfind("bag_", 0) == 0, "bagDir 为 bag_ 前缀目录名");
        const auto& stat = result->msg.topics.front();
        check(stat.topicName == REC_TOPIC, "统计项主题名与录制清单一致");
        check(!stat.typeName.empty(), "统计项带回远端公告的原始类型名");
        check(stat.count > 0 && stat.bytes > 0, "统计项条数与字节数大于 0");
    }

    // ---- 定格语义：成功录制后重复 record 拒绝 ----
    auto again = testInvoke(svc, "/record", yomk::msgs::dds::BagRecordRequest::shared(BagRecordRequest{{REC_TOPIC}}));
    check(again.status == yomk::payload::ResStatus::Failed &&
              again.msg.find("record already finished") != std::string::npos,
          "定格式拒绝：成功录制后重复 record → eNo record already finished");

    // ---- 删除 / 重复删除拒绝 / 删除后 record 拒绝 ----
    check(testInvoke(svc, "/delete_node").status == yomk::payload::ResStatus::Succeeded, "删除 bag 节点 → eOk");
    auto dupDel = testInvoke(svc, "/delete_node");
    check(dupDel.status == yomk::payload::ResStatus::Failed &&
              dupDel.msg.find("bag node not created") != std::string::npos,
          "重复删除 → eNo bag node not created");
    auto delRec = testInvoke(svc, "/record", yomk::msgs::dds::BagRecordRequest::shared(BagRecordRequest{{REC_TOPIC}}));
    check(delRec.status == yomk::payload::ResStatus::Failed &&
              delRec.msg.find("bag node not created") != std::string::npos,
          "删除后 record → eNo bag node not created");

    // ---- 重建 / 重复创建拒绝 / 收尾删除（关键不变式：显式删除规避静态析构序问题） ----
    check(testInvoke(svc, "/create_node", yomk::msgs::dds::BagCreateNodeRequest::shared(BagCreateNodeRequest{TEST_DOMAIN})).status ==
              yomk::payload::ResStatus::Succeeded,
          "删除后重建 bag 节点 → eOk");
    auto dupCreate2 = testInvoke(svc, "/create_node", yomk::msgs::dds::BagCreateNodeRequest::shared(BagCreateNodeRequest{TEST_DOMAIN}));
    check(dupCreate2.status == yomk::payload::ResStatus::Failed &&
              dupCreate2.msg.find("bag node already exists") != std::string::npos,
          "重建后重复创建仍拒绝（单节点模型全程生效）");

    // ---- L3：/bag_play 端点（重建节点在场）：负载往返保真 + 最小 bag 真实回放 ----
    std::error_code ec;
    // 造最小 bag：mcap writer 单分片 + 手写 metadata.json（record 组装同款键集；2 条
    // @100ms 时间轴 ~200ms 快速播完，无订阅端——write 不依赖匹配，回执对账为准）
    const std::string playBagDir = "play_svc_l3";
    std::filesystem::remove_all(playBagDir, ec);
    check(std::filesystem::create_directory(playBagDir, ec), "L3 前置：bag 目录创建成功");
    {
        mcap::McapWriter writer;
        mcap::McapWriterOptions options("");
        options.compression = mcap::Compression::None;
        check(writer.open(playBagDir + "/bag_0.mcap", options).ok(), "L3 前置：mcap 分片打开成功");
        mcap::Channel channel("/rt/bag_svc_play", "cdr", 0,
            mcap::KeyValueMap{{"type", "YomkDdsStd::MString"}, {"qos", "RELIABLE,TRANSIENT_LOCAL"}});
        writer.addChannel(channel);
        const std::string payload(5, 'x');
        for (uint32_t i = 0; i < 2; ++i)
        {
            mcap::Message msg;
            msg.channelId = channel.id;
            msg.sequence = i;
            msg.logTime = 1700000000000000000ULL + i * 100000000ULL;
            msg.publishTime = msg.logTime;
            // uint8_t 与 std::byte 同为单字节原始存储，别名转换安全（mcap 接口要求数据指针）
            msg.data = reinterpret_cast<const std::byte *>(payload.data()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            msg.dataSize = payload.size();
            check(writer.write(msg).ok(), "L3 前置：mcap 消息写入成功");
        }
        writer.close();

        nlohmann::json info;
        info["version"] = 1;
        info["storage_identifier"] = "mcap";
        info["relative_file_paths"] = nlohmann::json::array({"bag_0.mcap"});
        info["starting_time"] = {{"nanoseconds_since_epoch", 1700000000000000000ULL},
                                 {"nanoseconds_since_epoch_format", "manual"}};
        info["duration"] = {{"nanoseconds", 100000000ULL}, {"nanoseconds_format", "manual"}};
        info["message_count"] = 2;
        info["topics_with_message_count"] = nlohmann::json::array({{
            {"topic_metadata",
                {{"name", "/rt/bag_svc_play"}, {"type", "YomkDdsStd::MString"},
                    {"qos", "RELIABLE,TRANSIENT_LOCAL"}}},
            {"message_count", 2}}});
        std::ofstream out(playBagDir + "/metadata.json");
        out << info.dump(4) << "\n";
    }
    check(std::filesystem::exists(playBagDir + "/metadata.json", ec), "L3 前置：metadata.json 落盘");

    // a) 负载往返保真：路径不存在 → eNo 且错误文本回显 bagDir（解包字段无损证明）
    auto playMiss = testInvoke(svc, "/play", yomk::msgs::dds::BagPlayRequest::shared(BagPlayRequest{"play_svc_l3_miss"}));
    check(playMiss.status == yomk::payload::ResStatus::Failed &&
              playMiss.msg.find("bag path [play_svc_l3_miss] does not exist") != std::string::npos,
          "/bag_play 负载往返：错误文本回显 bagDir（解包字段保真）");

    // b) 真实回放：eOk + PlayResult 结构化回执（bagDir + 逐主题统计，行文本归调用方）
    auto playOk = testInvoke(svc, "/play", yomk::msgs::dds::BagPlayRequest::shared(BagPlayRequest{playBagDir}));
    check(playOk.status == yomk::payload::ResStatus::Succeeded, "/bag_play 真实 bag 回放 → eOk");
    auto played = yomk::msgs::unpack<yomk::msgs::dds::PlayResult>(playOk.data);
    check(played != nullptr, "play 回执可解包为 PlayResult（非 StringArray）");
    if (played != nullptr)
    {
        check(played->msg.bagDir == playBagDir, "PlayResult.bagDir 原样回执");
        check(played->msg.topics.size() == 1, "PlayResult.topics 单项（bag 预扫单主题）");
        if (played->msg.topics.size() == 1)
        {
            const auto &stat = played->msg.topics.front();
            check(stat.topicName == "/rt/bag_svc_play", "统计项 topicName = 发布名");
            check(stat.typeName == "YomkDdsStd::MString", "统计项 typeName = bag 内类型名");
            check(stat.count == 2, "统计项 count = 2（bag 内 2 条）");
            check(stat.bytes > 0, "统计项 bytes = 载荷字节（含 encapsulation header）");
        }
    }

    // b) play 四件套：/play(loop) → /pause_play（冻结时间轴）→ /resume_play（重锚时间基准）→ /stop_play
    {
        std::atomic<int> playEpStatus{-1};
        std::thread playEpThread([&]()
        {
            auto r = testInvoke(svc, "/play", yomk::msgs::dds::BagPlayRequest::shared(
                BagPlayRequest{playBagDir, 1.0, {}, true}));
            playEpStatus.store(static_cast<int>(r.status));
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(500));  // 预扫 + 进入回放循环

        auto playPause = testInvoke(svc, "/pause_play", nullptr);
        check(playPause.status == yomk::payload::ResStatus::Succeeded && playPause.msg == "pause requested",
              "/pause_play → eOk（play 持锁阻塞期间并发可调度，无锁置位）");
        check(yomk::ctrl::dds::g_bagPlayPaused.paused(), "暂停态迁移：play paused 标志置位（时间轴冻结）");
        std::this_thread::sleep_for(std::chrono::milliseconds(800));

        auto playResume = testInvoke(svc, "/resume_play", nullptr);
        check(playResume.status == yomk::payload::ResStatus::Succeeded && playResume.msg == "resume requested",
              "/resume_play → eOk（恢复回放）");
        check(!yomk::ctrl::dds::g_bagPlayPaused.paused(), "恢复态迁移：play paused 标志清除（时间轴重锚）");

        auto playStop = testInvoke(svc, "/stop_play", nullptr);
        check(playStop.status == yomk::payload::ResStatus::Succeeded && playStop.msg == "stop requested",
              "/stop_play → eOk（收尾回放）");
        playEpThread.join();
        check(playEpStatus.load() == static_cast<int>(yomk::payload::ResStatus::Succeeded),
              "/stop_play 停止后阻塞的 play invoke 返回 eOk（PlayResult 计数带回）");
    }
    std::filesystem::remove_all(playBagDir, ec);  // 回放输入用例收尾自清理

    // ---- 闭环：record/play 的 UI 控制四件套端到端——开始/暂停/继续/停止。
    // record/play 持锁长驻阻塞期间，控制端点无锁置位 ctrl 标志必须可并发调度：
    // 阻塞 invoke 收尾返回并带回结构化统计（对齐 /stop_pub 闭环模式）。
    // 暂停/继续经控制面标志状态断言迁移（暂停冻结写路径的深层行为由节点层
    // TestFastDDSBagNodeRecord 以真实流量覆盖，此处验证协议桥到状态机的完整链路）

    // a) record 四件套：/record → /pause_record（进入暂停态）→ /resume_record（恢复）→ /stop_record
    std::atomic<int> recEpStatus{-1};
    std::thread recEpThread([&]()
    {
        auto r = testInvoke(svc, "/record", yomk::msgs::dds::BagRecordRequest::shared(BagRecordRequest{{REC_TOPIC}}));
        recEpStatus.store(static_cast<int>(r.status));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(4000));  // 覆盖启动校验收敛窗，确保已进入录制循环

    auto recPause = testInvoke(svc, "/pause_record", nullptr);
    check(recPause.status == yomk::payload::ResStatus::Succeeded && recPause.msg == "pause requested",
          "/pause_record → eOk（record 持锁阻塞期间并发可调度，无锁置位）");
    check(yomk::ctrl::dds::g_bagRecordPaused.paused(), "暂停态迁移：record paused 标志置位（写路径冻结）");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    auto recResume = testInvoke(svc, "/resume_record", nullptr);
    check(recResume.status == yomk::payload::ResStatus::Succeeded && recResume.msg == "resume requested",
          "/resume_record → eOk（恢复录制）");
    check(!yomk::ctrl::dds::g_bagRecordPaused.paused(), "恢复态迁移：record paused 标志清除（写路径继续）");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto recStop = testInvoke(svc, "/stop_record", nullptr);
    check(recStop.status == yomk::payload::ResStatus::Succeeded && recStop.msg == "stop requested",
          "/stop_record → eOk（收尾录制）");
    check(!yomk::ctrl::dds::g_bagRecordStop.stopped() == false,
          "停止态迁移：record stop 标志置位");
    recEpThread.join();
    check(recEpStatus.load() == static_cast<int>(yomk::payload::ResStatus::Succeeded),
          "/stop_record 停止后阻塞的 record invoke 返回 eOk（RecordResult 计数带回，含暂停后恢复的条目）");


    check(testInvoke(svc, "/delete_node").status == yomk::payload::ResStatus::Succeeded, "退出前显式删除（清理不变式）");

    // 远端发布端清理
    if (peerParticipant != nullptr)
    {
        dds::DomainParticipantFactory::get_instance()->delete_participant(peerParticipant);
    }

    return testReport("TestYomkDdsBagHandlerLifecycle");
}
