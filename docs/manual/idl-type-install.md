# 使用手册：安装 IDL 通信数据包（自定义消息类型）

> **本文档覆盖范围**：用 `scripts/install_idl.sh` 一键把自定义 IDL 消息类型生成、编译并安装进 YOMK 安装树——产出支持 loan 的 C++ 类型库与 SWIG Python 模块，安装完成即可在 Python 侧 `import yomk.dds.<模块>` 使用。IDL 语法本身、已内置的 YomkDdsStdMsgs 类型用法见 [pub-sub-tutorial.md](./pub-sub-tutorial.md)。

## 目标

完成本文档后，你将：

1. 会编写一个最小的 IDL 消息类型文件（以仓库自带 `scripts/rpc.idl` 为例）；
2. 会用一条命令完成「生成 → 编译 → 安装 → 导入验证」全流程；
3. 知道安装产物的落点（C++ 类型库、Python 模块）与两种导入形态（默认前缀 / 自定义前缀）。

## 前提与准备

| 依赖 | 要求 | 说明 |
|---|---|---|
| YomkDds 扩展 | 已安装 | 通过 `build_ubuntu.sh` 安装（见 [setup-and-build.md](./setup-and-build.md)），随库安装的 Fast-DDS-Gen 位于 `<安装前缀>/scripts/fastddsgen`；脚本只使用安装树内的生成工具，不使用源码 thirdparty 中的 |
| `YOMK_PREFIX_PATH` | 已指向安装前缀 | 未设置时默认按 `/opt/yomk` 处理 |
| cmake / python3 / g++ | 已安装 | msg 类型工程配置与编译 |
| swig | 4.0.x | Ubuntu noble 上可执行名可能为 `swig4.1`，二者有其一即可；`sudo apt install swig` |
| python3-dev | 已安装 | SWIG 绑定编译需要 `Python.h` |
| java | 17 | fastddsgen 运行时需要；`sudo apt install openjdk-17-jre` |

依赖缺失时脚本会在第一步逐项报错并给出安装命令，不会产生半成品。

## 分步操作

### 第 1 步：编写 IDL 文件

IDL 即 Fast-DDS-Gen 支持的结构体语法。仓库自带一个最小示例 `scripts/rpc.idl`：

```idl
struct RPCString
{
    string s;
};
```

自己的类型按同样方式写在一个 `.idl` 文件里即可；一个文件可含多个 struct。

### 第 2 步：运行一键安装脚本

```bash
bash scripts/install_idl.sh scripts/rpc.idl
```

脚本会依次执行（全程自动，仅在安装路径处交互一次）：

1. **依赖检查**：逐项确认 cmake、python3、g++、swig、python3-dev、java；
2. **生成**：在当前目录新建 `./msg_<时间戳>/`，经安装树内 fastddsgen 生成支持 loan 的类型工程（`fastddsgen -python -d . -de final <idl>`），产物含 C++ 类型与 SWIG Python 绑定及 CMakeLists.txt；
3. **编译**：在 `./msg_<时间戳>/` 内 cmake 配置编译，`CMAKE_PREFIX_PATH` 指向安装前缀（自定义前缀无 Fast-DDS 安装树时回退框架前缀解析依赖）；
4. **安装**：默认安装到 `$YOMK_PREFIX_PATH`（交互输入可改）；C++ 类型库落 `<前缀>/lib`；
5. **目的地校正**：Debian/Ubuntu 系统 Python 的 posix_local scheme 会把模块算到 `<前缀>/local/.../dist-packages`（sys.path 外不可导入），脚本自动搬移到正确目的地并清理 `local/` 空壳，只搬本次新增条目、不动既有包；
6. **刷新动态库缓存**：注册 `<前缀>/lib` 进 `/etc/ld.so.conf.d/yomk.conf` 并执行 `ldconfig`；
7. **导入验证**：逐模块实际 `import`，全部通过才返回 0。

### 第 3 步：确认导入形态

| 安装前缀 | Python 导入形态 | 说明 |
|---|---|---|
| 默认前缀（与框架同前缀，如 `/opt/yomk`） | `import yomk.dds.<模块>` | 模块并入扩展域 `yomk/dds/`，与主库消息类型同域 |
| 自定义前缀 | `import <模块>` | 模块落该前缀 site-packages 顶层；该前缀不在系统 PYTHONPATH 内，使用时需挂入 `PYTHONPATH` |

> 为什么自定义前缀不并入 `yomk/dds/`：框架侧的 `yomk.dds` 是带 `__init__.py` 的常规包，会把自定义前缀下的 `yomk/dds` 命名空间段遮蔽，子挂进去不可导入，故保持生成布局放顶层。

## 预期输出

成功时脚本以汇总面板收尾（exit 0）：

```text
===========================================
 IDL 类型工程安装完成!
-------------------------------------------
 生成/编译目录: <当前目录>/msg_<时间戳>
 安装路径:      /opt/yomk
 Python 模块:   /opt/yomk/lib/python3.10/site-packages/yomk/dds/{<模块>}
 导入形态:      yomk.dds.<模块>
 C++ 类型库:    /opt/yomk/lib/
===========================================
```

之后即可在 Python 中导入使用生成的类型（SWIG 绑定按 getter/setter 方法访问字段）；C++ 侧类型库经 `find_package`/链接 `<前缀>/lib` 下的 `lib<工程名>.so` 使用。

## 常见错误与排障

| 现象 | 原因 | 处理 |
|---|---|---|
| `未找到生成工具 <前缀>/scripts/fastddsgen` | 扩展未安装或前缀不对 | 先跑 `build_ubuntu.sh`（见 [setup-and-build.md](./setup-and-build.md)），确认 `YOMK_PREFIX_PATH` |
| `缺少依赖: swig` / `python3-dev` / `java` | 编译依赖不全 | 按报错中的 apt 命令安装后重跑 |
| `msg 类型工程 cmake 配置失败` | Fast-DDS/fastcdr 依赖未解析 | 确认安装前缀内有 Fast-DDS 安装树（脚本会回退框架前缀解析；两者都没有时先完成扩展安装） |
| `警告: 未检测到新增 Python 模块条目` | 目的地校正没找到本次产物 | 按提示人工核对报错行中的目录 |
| `提示: 无免密 sudo，请手动执行注册动态库缓存` | ldconfig 未自动执行 | 手动 `sudo ldconfig` |
| 导入失败（exit 1） | 验证环境缺 fastdds 绑定或动态库 | 按脚本报错行核对；自定义前缀场景脚本会自动挂框架前缀的 site-packages 与 lib 重试 |
| 使用自定义前缀时找不到模块 | 前缀不在 PYTHONPATH | 将其 site-packages 挂入 `PYTHONPATH`（脚本收尾面板有提示） |

## 延伸阅读

- [setup-and-build.md](./setup-and-build.md) — 扩展本体与 fastddsgen 的安装来源
- [loan-tutorial.md](./loan-tutorial.md) — 本脚本生成的类型支持 loan；plain 类型才可借出，string 等非 plain 类型走普通发布
- [pub-sub-tutorial.md](./pub-sub-tutorial.md) — 装好的类型如何用于发布订阅
