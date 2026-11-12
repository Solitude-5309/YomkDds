#ifndef FASTDDSNODE_H
#define FASTDDSNODE_H

// 引公共头与 FastDDSXmlHelper：平铺引号命中 include/，安装树经 yomk/dds/ 前缀提供同一头；
// 缺省实参需完整类型，必须引 YomkDdsService.h
#include "YomkDdsService.h"
#include "FastDDSXmlHelper.h"

#include <cstdint>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

// 由 YomkDdsService 持有（每个 nodeName 一个实例）；析构按 DataReader/Writer → Topic →
// Publisher/Subscriber → Participant 顺序清理。
class FastDDSNode
{
    class SubListener;  // DataReaderListener 实现，对齐契约见 .cpp
    using DataCallback = std::function<void(const void*)>;  // 消息指针仅回调期间有效
    struct PubInfo
    {
        eprosima::fastdds::dds::TypeSupport type;  // shared_ptr 持有 TopicDataType 所有权
        eprosima::fastdds::dds::DataWriter* writer = nullptr;
    };
    struct SubInfo
    {
        eprosima::fastdds::dds::TypeSupport type;
        eprosima::fastdds::dds::DataReader* reader = nullptr;
        eprosima::fastdds::dds::TopicDataType* topicType =
            nullptr;           // 指向 type 所辖同一对象，仅供 delete_data(data)，不单独持有所有权
        void* data = nullptr;  // create_data() 创建的对齐接收缓冲，析构时 delete_data() 释放
        std::unique_ptr<SubListener> listener;  // 持有 data 与用户回调
    };

public:
    FastDDSNode();
    ~FastDDSNode();
    FastDDSNode(const FastDDSNode&) = delete;
    FastDDSNode& operator=(const FastDDSNode&) = delete;

public:
    // 仅可成功一次（惰性创建）。nodeName 非空时下沉为参与者名：经 SPDP 发现传播，创建时
    // 固化不可改，string_255 超长自动截断。XML 提取整体填充 qos（含 name），节点名必须在
    // 提取后最终覆盖，不可被 XML 顶掉；serverSpec 非空强制 CLIENT 直配。
    bool setDomainId(uint32_t domainId, const std::string& nodeName = "",
        const yomkdds_xml::ParticipantQosSpec& spec = yomkdds_xml::ParticipantQosSpec{},
        std::string* err = nullptr);
    // 接管 type 所有权；qos 缺省全哨兵=端点默认底座，非哨兵项逐项覆盖。
    bool registerSubTopic(const std::string& topicName, void* type, DataCallback callback,
        const DDSQosProfile& qos = DDSQosProfile{});
    // 接管 type 所有权；qos 同 registerSubTopic。
    bool registerPubTopic(const std::string& topicName, void* type,
        const DDSQosProfile& qos = DDSQosProfile{});
    // data 仅借用，write 内部完成序列化/拷贝。
    bool publish(const std::string& topicName, const void* data);
    // 仅 plain 类型支持；借出指针经 write/discard 后失效。
    bool loan(const std::string& topicName, void*& sample);
    // loan 后未 publish 须调用，否则样本池泄漏。
    bool discardLoan(const std::string& topicName, void*& sample);

private:
    eprosima::fastdds::dds::Topic* getOrCreateTopic(const std::string& topicName, const std::string& typeName);

private:
    eprosima::fastdds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::fastdds::dds::Publisher* publisher_ = nullptr;        // 发布者，拥有全部 writer
    eprosima::fastdds::dds::Subscriber* subscriber_ = nullptr;      // 订阅者，拥有全部 reader
    std::map<std::string, eprosima::fastdds::dds::Topic*> topics_;  // topicName → Topic，发布/订阅共享
    std::map<std::string, PubInfo> pubTopics_;
    std::map<std::string, SubInfo> subTopics_;
    std::mutex mtx_;  // 串行化本节点全部公开方法
};

#endif  // FASTDDSNODE_H
