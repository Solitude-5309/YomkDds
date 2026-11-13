# 功能文档

本册回答一个问题：**YomkDds 提供哪些功能，每个功能的能力边界与参数约束是什么**。

- 想知道「怎么一步步用」，请转到[用户使用手册册](../manual/README.md)
- 本册的阅读方式：从「功能全景」找到能力条目 → 进入对应小文档查「能力清单 / 参数与约束 / 错误与边界行为」

## 一、功能全景

YomkDds 是 [YomkServer](https://github.com/Solitude-5309/YomkServer) 框架的 DDS 分布式通信扩展，集成 Fast DDS 提供分布式发布订阅能力。用户经 YomkDds API 操作三个服务域（核心通信为 `yomk::dds()` 链式），另有 `yomkdds` 命令行工具以 CLI 形式覆盖调试与 Bag 大部分能力：

```mermaid
flowchart LR
    U["用户程序 / 终端"]

    subgraph YomkDds["YomkDds API（经 YomkServer 服务注册）"]
        S1["YomkDdsService<br>核心通信"]
        S2["YomkDdsDebugService<br>调试服务"]
        S3["YomkDdsBagService<br>Bag 服务"]
    end

    CLI["yomkdds 命令行工具"]

    S1 --> D["Fast DDS<br>（节点 / DataWriter / DataReader）"]
    S2 --> D
    S3 --> D
    CLI --> S2
    CLI --> S3
    D --> N["DDS 域内其他参与者<br>（YomkDds / ROS2 / 任意 DDS 程序）"]
```

三个服务域与 CLI 的职责划分：

| 服务域 | 职责 | API 入口 | CLI 覆盖 |
|---|---|---|---|
| YomkDdsService | 节点生命周期、主题注册、发布订阅、Loan、XML 加载 | `yomk::dds()` 链式 | 不覆盖（编程能力） |
| YomkDdsDebugService | 类型无关的域内观察（主题/节点/类型/端点）与 JSON 载荷发布 | `YOMKDDS_DEBUG_*` | `yomkdds topic/node/interface` 全部覆盖 |
| YomkDdsBagService | mcap 格式录制、元信息、重建索引、转换、回放 | `YOMKDDS_BAG_*` | `yomkdds bag` 全部覆盖（play 仅 CLI） |

> 除链式 `loan()`（返回值即借出样本指针，判空即失败判定）外，核心通信链式终结均返回 `yomk::payload::Response`，调用后须判 `m_status == yomk::payload::ResStatus::Succeeded`；失败时可读 `m_msg` 获取错误信息。

## 二、功能速查矩阵

| 功能域 | 能力 | 链式/宏 API | yomkdds CLI | 详见 |
|---|---|---|---|---|
| 核心通信 | 版本查询 | `yomk::dds().version()` | — | [core-communication.md](./core-communication.md) |
| 核心通信 | 加载 XML profiles | `yomk::dds().xml(xmlPath).load_cfg()` | — | [xml-profiles.md](./xml-profiles.md) |
| 核心通信 | 创建/删除节点 | `yomk::dds().node(nodeName).create()` / `...node(nodeName).remove()` | — | [core-communication.md](./core-communication.md) |
| 核心通信 | 注册发布/订阅主题 | `yomk::dds().node(...).topic(...).type(...).create_pub() / ...create_sub()` | — | [core-communication.md](./core-communication.md) |
| 核心通信 | 发布消息 | `yomk::dds().node(...).topic(...).data(...).pub()` | — | [core-communication.md](./core-communication.md) |
| 核心通信 | Loan 借出零拷贝发布 | `yomk::dds().node(...).topic(...).loan()` / `...sample(...).return_loan()` | — | [loan-zero-copy.md](./loan-zero-copy.md) |
| QoS | 端点 QoS 描述（8 项 + ROS2 底座） | `DDSQosProfile`（qos 尾参） | `--qos-ros` / `--qos-*` / `--qos-file` | [qos-profiles.md](./qos-profiles.md) |
| 发现 | 发现服务器 UDP/TCP 直配 | `yomk::dds().node(...)` 直配累积器 / `YOMKDDS_DEBUG_NODE/BAG_NODE` 定位串尾参 | 发现服务器三环境变量 | [discovery-server.md](./discovery-server.md) |
| 发现 | XML profiles 三级提取链 | `yomk::dds().xml(xmlPath).load_cfg()` | — | [xml-profiles.md](./xml-profiles.md) |
| 互通 | 与 ROS2 收发同类型消息 | set_name 类型名直改（编程约定） | — | [ros2-interop.md](./ros2-interop.md) |
| 调试 | 主题消息观察 / 频率测量 | `YOMKDDS_DEBUG_TOPIC_PRINT()` | `topic print` / `topic hz` | [debug-service.md](./debug-service.md) |
| 调试 | 列出主题与节点 | `YOMKDDS_DEBUG_TOPIC_LIST(_T)` / `YOMKDDS_DEBUG_NODE_LIST()` | `topic list` / `node list` | [debug-service.md](./debug-service.md) |
| 调试 | 主题/节点详情（端点、GUID、QoS） | `YOMKDDS_DEBUG_TOPIC_INFO(_V)` / `YOMKDDS_DEBUG_NODE_INFO()` | `topic info` / `node info` | [debug-service.md](./debug-service.md) |
| 调试 | 按类型反查主题 | `YOMKDDS_DEBUG_TOPIC_FIND()` | `topic type` / `topic find` | [debug-service.md](./debug-service.md) |
| 调试 | 类型内省（IDL 结构） | `YOMKDDS_DEBUG_INTERFACE_SHOW/LIST()` | `interface show` / `interface list` | [debug-service.md](./debug-service.md) |
| 调试 | 发布示例与 JSON 载荷发布 | `YOMKDDS_DEBUG_TOPIC_EXAMPLE/MSG/PUB()` | `topic pub` | [debug-service.md](./debug-service.md) |
| Bag | 录制到 mcap（分片/缓存/暂停/通配） | `YOMKDDS_BAG_RECORD()` | `bag record` | [bag-service.md](./bag-service.md) |
| Bag | 查看 bag 元信息 | `YOMKDDS_BAG_INFO()` | `bag info` | [bag-service.md](./bag-service.md) |
| Bag | 重建 bag 元信息 | `YOMKDDS_BAG_REINDEX()` | `bag reindex` | [bag-service.md](./bag-service.md) |
| Bag | 转换 bag（过滤/裁剪/归并） | `YOMKDDS_BAG_CONVERT()` | `bag convert` | [bag-service.md](./bag-service.md) |
| Bag | 回放 bag | `YOMKDDS_BAG_PLAY()`（须先自建 bag 节点） | `bag play`（CLI 自动建删 bag 节点） | [bag-service.md](./bag-service.md) |

## 三、文档导航

| 文档 | 一句话职责 |
|---|---|
| [core-communication.md](./core-communication.md) | 节点生命周期、主题注册、发布订阅与回调消费、版本查询 |
| [loan-zero-copy.md](./loan-zero-copy.md) | plain 类型的借出（loan）零拷贝发布机制与池语义 |
| [qos-profiles.md](./qos-profiles.md) | DDSQosProfile 8 项配置、ROS2 预置底座、宏/CLI 两侧 QoS 优先级 |
| [discovery-server.md](./discovery-server.md) | 发现服务器 UDP/TCP 直配、CLIENT 协议、跨域互通 |
| [xml-profiles.md](./xml-profiles.md) | yomk::dds().load_cfg() 语义与 participant 三级 profile 提取链 |
| [ros2-interop.md](./ros2-interop.md) | set_name 类型名直改实现与 ROS2 互通约束 |
| [debug-service.md](./debug-service.md) | 调试服务与 yomkdds 观察类命令的全能力说明 |
| [bag-service.md](./bag-service.md) | Bag 录制/元信息/重建/转换/回放全能力说明 |

> 横切主题：环境变量（域号、发现服务器开关、收敛次数等）统一记录在 [环境变量与公共配置参考](../manual/env-vars-reference.md)，功能文档不重复展开。
