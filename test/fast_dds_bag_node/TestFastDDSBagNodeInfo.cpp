/**
 * @file TestFastDDSBagNodeInfo.cpp
 * @brief FastDDSBagNode 节点层 bag info 测试（直测静态函数，DDS-free）
 *
 * 范围：bagInfo 对手写 metadata.json 临时 bag 目录的结构化回填断言（metadata 字段逐项对账、
 *       逐主题原序、目录递归字节数）与三类失败路径契约。节点层只回数据，行标签/单位换算/
 *       人类可读时间归调用方渲染，故本测试不做任何文本形态断言。
 *       经 record 真实落盘的产物断言归 TestFastDDSBagNodeRecord；服务层回执链路归
 *       TestYomkDdsBagHandlerContract。
 * 覆盖：
 *   I1 单主题全字段（files/storageId/startTimeNs/durationNs/messageCount/
 *      serializationFormat=cdr/bagSizeBytes=目录总字节/topics 单条四字段）；
 *   I2 多主题（topics 保持 metadata 原序，count 逐项对账）；
 *   I3 多分片（files 保持 relative_file_paths 原序）；
 *   I4 bagSizeBytes 精确边界（1023/1024/2048 字节原值透传，单位换算归调用方）；
 *   I5 起止/时长全 0（epoch 0 bag 的纳秒原值；人类可读形态归调用方）；
 *   I6 路径不存在 → false "bag path [..] does not exist" 且 outInfo 清空；
 *   I7 目录存在无 metadata.json → false "could not find metadata.json in bag directory [..]"；
 *   I8 JSON 损坏 → false "parse metadata.json failed: ..." 且 outInfo 清空；
 *   I9 空 topics → topics 为空（调用方据此渲染空标签行）；
 *   I10 topic_metadata 带 qos 键 → qos 原样回填；无 qos 键 → qos 空串（旧 bag 兼容）。
 *
 * 用例目录避开 bag_ 前缀（防泄漏快照误报惯例），结束统一清理。
 *
 * 风格：纯 main() + check 函数 + 失败计数（零第三方依赖），返回非 0 表示存在失败用例。
 */

#include "TestCheck.h"
#include "FastDDSBagNode.h" // 被测静态函数 bagInfo（DDS-free 直测）

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    // 造临时 bag 目录：手写 metadata.json（字段与节点写侧产物同构）+ 定长假数据文件
    // （字节数参与 bagSizeBytes 目录递归累加）；dataFileBytes=0 不造数据文件
    bool makeBagDir(const std::string &dir, const std::string &metadataJson,
                    std::uintmax_t dataFileBytes)
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        if (!std::filesystem::create_directory(dir, ec))
        {
            return false;
        }
        std::ofstream meta(dir + "/metadata.json", std::ios::binary);
        if (!meta)
        {
            return false;
        }
        meta << metadataJson;
        meta.close();
        if (!meta)
        {
            return false;
        }
        if (dataFileBytes == 0)
        {
            return true;
        }
        std::ofstream data(dir + "/bag_0.mcap", std::ios::binary);
        if (!data)
        {
            return false;
        }
        const std::string chunk(1024, 'x');
        for (std::uintmax_t left = dataFileBytes; left > 0;)
        {
            const std::uintmax_t n = left < chunk.size() ? left : chunk.size();
            data.write(chunk.data(), static_cast<std::streamsize>(n));
            if (!data)
            {
                return false;
            }
            left -= n;
        }
        data.close();
        return static_cast<bool>(data);
    }

    void cleanupDir(const std::string &dir)
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
} // namespace

int main()
{
    // 单主题 metadata（含写侧 _format 伴生键，字段同构 dump(4) 产物；伴生键读侧不消费）
    const std::string metaSingle = R"({
    "version": 1,
    "storage_identifier": "mcap",
    "relative_file_paths": [
        "bag_0.mcap"
    ],
    "starting_time": {
        "nanoseconds_since_epoch": 1570799349123456789,
        "nanoseconds_since_epoch_format": "2019-10-11_06-09-09_123-456-789"
    },
    "duration": {
        "nanoseconds": 12000000000,
        "nanoseconds_format": "00-00-12_000-000-000"
    },
    "message_count": 13,
    "topics_with_message_count": [
        {
            "topic_metadata": {
                "name": "/rt/a",
                "type": "yomk::dds_idl::std_msgs::MString"
            },
            "message_count": 5
        }
    ]
}
)";

    // ---- I1：单主题全字段 ----
    {
        const std::string dir = "info_i1";
        check(makeBagDir(dir, metaSingle, 0), "I1 前置：临时 bag 目录创建成功");
        FastDDSBagNode::BagInfo info;
        std::string error;
        check(FastDDSBagNode::bagInfo(dir, info, &error), "I1 bagInfo → true");
        check(error.empty(), "I1 成功路径 error 为空");
        check(info.files.size() == 1 && info.files[0] == "bag_0.mcap", "I1 files 原序透传");
        // bagSizeBytes = 目录递归总字节（仅 metadata.json 一个文件，含其自身）
        const auto metaBytes = static_cast<uint64_t>(metaSingle.size());
        check(info.bagSizeBytes == metaBytes, "I1 bagSizeBytes = 目录总字节（metadata.json 计内）");
        check(info.storageId == "mcap", "I1 storageId = mcap");
        check(info.startTimeNs == 1570799349123456789ULL, "I1 startTimeNs 纳秒原值透传");
        check(info.durationNs == 12000000000ULL, "I1 durationNs 纳秒原值透传");
        check(info.messageCount == 13, "I1 messageCount = 13");
        check(info.serializationFormat == "cdr", "I1 serializationFormat = cdr（单格式链路）");
        check(info.topics.size() == 1, "I1 topics 单项");
        if (info.topics.size() == 1)
        {
            const auto &topic = info.topics.front();
            check(topic.topicName == "/rt/a", "I1 topics[0].topicName");
            check(topic.typeName == "yomk::dds_idl::std_msgs::MString", "I1 topics[0].typeName 原样透传");
            check(topic.qos.empty(), "I1 无 qos 键 → qos 空串（旧 bag 兼容）");
            check(topic.messageCount == 5, "I1 topics[0].messageCount = 5");
        }
        cleanupDir(dir);
    }

    // ---- I2：多主题（topics 保持 metadata 原序） ----
    {
        const std::string meta = R"({
    "version": 1,
    "storage_identifier": "mcap",
    "relative_file_paths": ["bag_0.mcap"],
    "starting_time": {"nanoseconds_since_epoch": 1570799349123456789},
    "duration": {"nanoseconds": 12000000000},
    "message_count": 13,
    "topics_with_message_count": [
        {"topic_metadata": {"name": "/rt/a", "type": "yomk::dds_idl::std_msgs::MString"}, "message_count": 5},
        {"topic_metadata": {"name": "/rt/b", "type": "yomk::dds_idl::std_msgs::MString"}, "message_count": 8}
    ]
}
)";
        const std::string dir = "info_i2";
        check(makeBagDir(dir, meta, 0), "I2 前置：两主题 bag 目录创建成功");
        FastDDSBagNode::BagInfo info;
        check(FastDDSBagNode::bagInfo(dir, info, nullptr), "I2 bagInfo → true");
        check(info.topics.size() == 2, "I2 topics 两项（原序，不排序）");
        if (info.topics.size() == 2)
        {
            check(info.topics[0].topicName == "/rt/a" && info.topics[0].messageCount == 5,
                  "I2 topics[0] = /rt/a / 5");
            check(info.topics[1].topicName == "/rt/b" && info.topics[1].messageCount == 8,
                  "I2 topics[1] = /rt/b / 8");
        }
        cleanupDir(dir);
    }

    // ---- I3：多分片（files 保持 relative_file_paths 原序） ----
    {
        const std::string meta = R"({
    "version": 1,
    "storage_identifier": "mcap",
    "relative_file_paths": ["bag_0.mcap", "bag_1.mcap", "bag_2.mcap"],
    "starting_time": {"nanoseconds_since_epoch": 1570799349123456789},
    "duration": {"nanoseconds": 12000000000},
    "message_count": 13,
    "topics_with_message_count": [
        {"topic_metadata": {"name": "/rt/a", "type": "yomk::dds_idl::std_msgs::MString"}, "message_count": 13}
    ]
}
)";
        const std::string dir = "info_i3";
        check(makeBagDir(dir, meta, 0), "I3 前置：三分片 bag 目录创建成功");
        FastDDSBagNode::BagInfo info;
        check(FastDDSBagNode::bagInfo(dir, info, nullptr), "I3 bagInfo → true");
        check(info.files.size() == 3 && info.files[0] == "bag_0.mcap" &&
                  info.files[1] == "bag_1.mcap" && info.files[2] == "bag_2.mcap",
              "I3 files 三分片原序透传");
        cleanupDir(dir);
    }

    // ---- I4：bagSizeBytes 精确边界（原值透传，单位换算归调用方） ----
    {
        // metadata 固定一份，数据文件补差值使目录总和精确命中边界
        const std::string meta = R"({"version":1,"storage_identifier":"mcap",
"relative_file_paths":["bag_0.mcap"],"starting_time":{"nanoseconds_since_epoch":0},
"duration":{"nanoseconds":0},"message_count":0,"topics_with_message_count":[]})";
        const auto metaBytes = static_cast<std::uintmax_t>(meta.size());
        check(metaBytes < 1023, "I4 前置：metadata.json 小于 1023 字节（差值数据可凑边界）");
        const std::string dir = "info_i4";

        for (const std::uintmax_t total : {1023, 1024, 2048})
        {
            check(makeBagDir(dir, meta, total - metaBytes),
                  "I4 前置：目录总 " + std::to_string(total) + " 字节创建成功");
            FastDDSBagNode::BagInfo info;
            check(FastDDSBagNode::bagInfo(dir, info, nullptr),
                  "I4 总 " + std::to_string(total) + " 字节 → true");
            check(info.bagSizeBytes == total,
                  "I4 bagSizeBytes 原值 = " + std::to_string(total) + " 字节");
            cleanupDir(dir);
        }
    }

    // ---- I5：起止/时长全 0（纳秒原值；人类可读形态归调用方） ----
    {
        const std::string meta = R"({
    "version": 1,
    "storage_identifier": "mcap",
    "relative_file_paths": [],
    "starting_time": {"nanoseconds_since_epoch": 0},
    "duration": {"nanoseconds": 0},
    "message_count": 0,
    "topics_with_message_count": []
}
)";
        const std::string dir = "info_i5";
        check(makeBagDir(dir, meta, 0), "I5 前置：epoch 0 bag 目录创建成功");
        FastDDSBagNode::BagInfo info;
        check(FastDDSBagNode::bagInfo(dir, info, nullptr), "I5 bagInfo → true");
        check(info.startTimeNs == 0 && info.durationNs == 0 && info.messageCount == 0,
              "I5 startTimeNs/durationNs/messageCount 全 0");
        check(info.files.empty(), "I5 relative_file_paths 空数组 → files 为空");
        cleanupDir(dir);
    }

    // ---- I6/I7/I8：失败路径（false + error 文案契约 + outInfo 清空） ----
    {
        FastDDSBagNode::BagInfo info;
        std::string error;

        // 先用合法输入填充一次，验证失败路径把 outInfo 归零而非残留上次内容
        const std::string okDir = "info_i6_ok";
        check(makeBagDir(okDir, metaSingle, 0), "I6 前置：合法 bag 目录创建成功");
        check(FastDDSBagNode::bagInfo(okDir, info, nullptr) && info.messageCount == 13,
              "I6 前置：合法目录回填成功（供清空断言对照）");
        cleanupDir(okDir);

        check(!FastDDSBagNode::bagInfo("info_no_such_dir", info, &error),
              "I6 路径不存在 → false");
        check(error == "bag path [info_no_such_dir] does not exist", "I6 error 精确文案");
        check(info.files.empty() && info.topics.empty() && info.messageCount == 0,
              "I6 失败后 outInfo 已清空");

        const std::string dir = "info_i7";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        check(std::filesystem::create_directory(dir, ec), "I7 前置：无 metadata 的空目录创建成功");
        check(!FastDDSBagNode::bagInfo(dir, info, &error), "I7 无 metadata.json → false");
        check(error == "could not find metadata.json in bag directory [" + dir + "]",
              "I7 error 精确文案");
        cleanupDir(dir);

        const std::string dir8 = "info_i8";
        check(makeBagDir(dir8, "{ not valid json", 0), "I8 前置：损坏 metadata 目录创建成功");
        check(!FastDDSBagNode::bagInfo(dir8, info, &error), "I8 JSON 损坏 → false");
        check(error.rfind("parse metadata.json failed: ", 0) == 0,
              "I8 error 前缀 parse metadata.json failed:");
        check(info.files.empty() && info.topics.empty(), "I8 失败后 outInfo 清空");
        cleanupDir(dir8);
    }

    // ---- I9：空 topics → topics 为空（调用方据此渲染空标签行） ----
    {
        const std::string meta = R"({
    "version": 1,
    "storage_identifier": "mcap",
    "relative_file_paths": ["bag_0.mcap"],
    "starting_time": {"nanoseconds_since_epoch": 0},
    "duration": {"nanoseconds": 0},
    "message_count": 0,
    "topics_with_message_count": []
}
)";
        const std::string dir = "info_i9";
        check(makeBagDir(dir, meta, 0), "I9 前置：空主题清单 bag 目录创建成功");
        FastDDSBagNode::BagInfo info;
        check(FastDDSBagNode::bagInfo(dir, info, nullptr), "I9 bagInfo → true");
        check(info.topics.empty(), "I9 空 topics → topics 为空");
        cleanupDir(dir);
    }

    // ---- I10：topic_metadata 带 qos 键 → qos 原样回填 ----
    {
        const std::string meta = R"({
    "version": 1,
    "storage_identifier": "mcap",
    "relative_file_paths": ["bag_0.mcap"],
    "starting_time": {"nanoseconds_since_epoch": 0},
    "duration": {"nanoseconds": 0},
    "message_count": 5,
    "topics_with_message_count": [
        {"topic_metadata": {"name": "/rt/a", "type": "yomk::dds_idl::std_msgs::MString",
                            "qos": "RELIABLE,TRANSIENT_LOCAL"}, "message_count": 5}
    ]
}
)";
        const std::string dir = "info_i10";
        check(makeBagDir(dir, meta, 0), "I10 前置：带 qos 的 bag 目录创建成功");
        FastDDSBagNode::BagInfo info;
        check(FastDDSBagNode::bagInfo(dir, info, nullptr), "I10 bagInfo → true");
        check(info.topics.size() == 1 && info.topics[0].qos == "RELIABLE,TRANSIENT_LOCAL",
              "I10 topics[0].qos 原样回填（是否展示由调用方决定）");
        cleanupDir(dir);
    }

    return testReport("TestFastDDSBagNodeInfo");
}
