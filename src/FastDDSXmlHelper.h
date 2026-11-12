#pragma once
// participant 级 QoS 合成辅助，三个节点类的 setDomainId 合成链共用；纯头文件 inline 无对应
// 源文件，仅内部使用不出现在公共头（公共头零 fastdds 依赖）。

#include <fastdds/dds/core/detail/DDSReturnCode.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>

#include <fastdds/rtps/attributes/RTPSParticipantAttributes.hpp>
#include <fastdds/rtps/common/Locator.hpp>
#include <fastdds/rtps/transport/TCPv4TransportDescriptor.hpp>  // TCP 直配项的 participant 侧传输注入
#include <fastdds/utils/IPLocator.hpp>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "YomkDdsService.h"  // DDSServerInfo（平铺引号命中 include/；本头仅库内与测试树编译，src 头不安装）

namespace yomkdds_xml
{

// 服务层从 xmlCache() 与发现服务器直配聚合串组装后传入 setDomainId；字段空=跳过该项配置。
struct ParticipantQosSpec
{
    std::string xml;            // 已成功 load 的 XML profiles raw 串；空=未加载，跳过提取
    std::string udpServerSpec;  // UDP 发现服务器直配聚合串 "ip[:port];ip[:port]"；空=不直配
    std::string tcpServerSpec;  // TCP 发现服务器直配聚合串（格式同上，port 为 TCP 监听口）；空=不直配
};

constexpr uint32_t kDdsPortMax = 65535;

// 分号分隔 "ip[:port]" 项，对齐 ROS_DISCOVERY_SERVER 官方格式，port 缺省 11811；
// 空项/空 ip/非纯数字/越界 port 均非法，err 回填具体项文本并返回 false。
inline bool parseServerLocator(const std::string& spec,
        std::vector<DDSServerInfo>& out, std::string& err)
{
    out.clear();
    err.clear();
    size_t begin = 0;
    while (true)
    {
        const size_t sep = spec.find(';', begin);
        const std::string item =
            spec.substr(begin, sep == std::string::npos ? std::string::npos : sep - begin);
        if (item.empty())
        {
            err = "empty server item in serverLocator [" + spec + "]";
            return false;
        }
        DDSServerInfo server;
        const size_t colon = item.find(':');
        server.address = colon == std::string::npos ? item : item.substr(0, colon);
        if (server.address.empty())
        {
            err = "empty address in serverLocator item [" + item + "]";
            return false;
        }
        if (colon != std::string::npos)
        {
            const std::string portText = item.substr(colon + 1);
            if (portText.empty() || portText.find_first_not_of("0123456789") != std::string::npos)
            {
                err = "invalid port in serverLocator item [" + item + "]";
                return false;
            }
            const unsigned long parsed = std::strtoul(portText.c_str(), nullptr, 10);
            if (parsed < 1 || parsed > kDdsPortMax)
            {
                err = "port out of range [1,65535] in serverLocator item [" + item + "]";
                return false;
            }
            server.port = static_cast<uint32_t>(parsed);
        }
        out.push_back(std::move(server));
        if (sep == std::string::npos)
        {
            break;
        }
        begin = sep + 1;
    }
    return true;
}

// 合并语义：直配 > XML 提取打底 > 默认；3.6.1 client 连 server 仅需 ip:port + CLIENT。
// TCP 项须 participant 具备 TCPv4 传输（builtin 默认集不含），故补注入 TCPv4TransportDescriptor
// ——与 builtin alongside 共存不影响 UDP/SHM，XML 已注入时跳过防重复；spec 非法返回 false 不改 qos。
inline bool applyDiscoveryServers(eprosima::fastdds::dds::DomainParticipantQos& qos,
        const std::string& udpServerSpec, const std::string& tcpServerSpec, std::string& err)
{
    std::vector<DDSServerInfo> udpServers;
    std::vector<DDSServerInfo> tcpServers;
    // 空 spec = 该通道不直配（跳过解析，非错误）；parseServerLocator 本身视空 spec 为非法
    if ((!udpServerSpec.empty() && !parseServerLocator(udpServerSpec, udpServers, err)) ||
        (!tcpServerSpec.empty() && !parseServerLocator(tcpServerSpec, tcpServers, err)))
    {
        return false;
    }
    if (udpServers.empty() && tcpServers.empty())
    {
        return true;
    }
    using eprosima::fastdds::rtps::DiscoveryProtocol;
    using eprosima::fastdds::rtps::IPLocator;
    using eprosima::fastdds::rtps::Locator;
    using eprosima::fastdds::rtps::TCPv4TransportDescriptor;
    qos.wire_protocol().builtin.discovery_config.discoveryProtocol = DiscoveryProtocol::CLIENT;
    auto& servers = qos.wire_protocol().builtin.discovery_config.m_DiscoveryServers;
    servers.clear();
    for (const auto& server : udpServers)
    {
        Locator locator;  // 默认 kind=LOCATOR_KIND_UDPv4
        IPLocator::setIPv4(locator, server.address);
        locator.port = server.port;
        servers.push_back(locator);
    }
    for (const auto& server : tcpServers)
    {
        Locator locator;
        locator.kind = LOCATOR_KIND_TCPv4;  // kind 是 Locator 公有成员，IPLocator 无 setKind
        IPLocator::setIPv4(locator, server.address);
        locator.port = server.port;
        servers.push_back(locator);
    }
    if (!tcpServers.empty())
    {
        bool hasTcpTransport = false;
        for (const auto& transport : qos.transport().user_transports)
        {
            // 描述符基类无 kind() 元数据，用 dynamic_pointer_cast 识别 TCPv4 描述符（含派生）
            if (std::dynamic_pointer_cast<TCPv4TransportDescriptor>(transport))
            {
                hasTcpTransport = true;
                break;
            }
        }
        if (!hasTcpTransport)
        {
            qos.transport().user_transports.push_back(std::make_shared<TCPv4TransportDescriptor>());
        }
    }
    return true;
}

// Precondition: qos 已置 PARTICIPANT_QOS_DEFAULT。三级链：按节点名 profile（profile 名 =
// YomkDds 节点名，逐字全等）→ 默认档兜底 → 全落空保持 DEFAULT 底座（非错误）；XML 提取
// 整体填充 qos 含 name，节点名必须由调用点在提取后最终覆盖；空名跳过按名级（Fast DDS 固定拒绝空 profile 名）。
inline void participantQosFromXml(eprosima::fastdds::dds::DomainParticipantQos& qos,
        const std::string& nodeName, const std::string& xml)
{
    if (xml.empty())
    {
        return;
    }
    auto* factory = eprosima::fastdds::dds::DomainParticipantFactory::get_instance();
    if ((!nodeName.empty() &&
            factory->get_participant_qos_from_xml(xml, qos, nodeName) !=
                    eprosima::fastdds::dds::RETCODE_OK) &&
        factory->get_default_participant_qos_from_xml(xml, qos) !=
            eprosima::fastdds::dds::RETCODE_OK)
    {
        qos = eprosima::fastdds::dds::PARTICIPANT_QOS_DEFAULT;
    }
}

}  // namespace yomkdds_xml
