# YomkDds —— YomkServerPy 扩展（dds 域，yomk 命名空间并入形态）

本仓库为 YomkServerPy 的第三方扩展：**独立仓库只写
`yomk/msgs/dds/`（消息类型）、`yomk/dds/`（运行时服务）与
`yomk/url/dds/`（URL 常量池）三个目录**，
安装到框架 site-packages 后按 PEP 420 命名空间与框架子包平级合并。
命名对齐 C++ YomkDds 套件：域 `dds`、公开入口 `dds()`、URL 常量归 `yomk.url.dds`
且不带 dds 前缀。

## 目录结构

```text
YomkDdsPython/                  # 扩展仓库根
├── src/yomk/                   # 扩展源树（与框架同构：只写自己的叶子目录）
│   ├── msgs/dds/               # 消息子包（并入框架 yomk.msgs 命名空间段）
│   │   ├── __init__.py             #   导出 DdsRequest / DdsResponse
│   │   ├── DdsMsg.py               #   消息类型定义
│   │   └── py.typed
│   ├── dds/                    # 运行时域子包（并入框架 yomk 命名空间段，与框架各域同构：builder + 类型桩）
│   │   ├── __init__.py             #   try/except 导入 _version + 导出 dds/DdsBuilder/DdsService
│   │   ├── __init__.pyi            #   类型桩
│   │   ├── _version.py.in          #   版本模板（CMake configure_file 生成 _version.py）
│   │   ├── builder.py              #   dds 域 builder（可选件：只封装本模块自有接口）
│   │   ├── builder.pyi             #   类型桩
│   │   ├── DdsService.py           #   运行时服务（YomkService 子类，/echo 与 /version 端点）
│   │   └── py.typed
│   └── url/dds/                # URL 常量池子包（并入框架 yomk.url 常量池，与 yomk/url/ctx 同构）
│       ├── __init__.py             #   ECHO / GET_VERSION 端点常量（注明功能作用与统一请求形态）
│       ├── __init__.pyi            #   端点常量类型桩
│       └── py.typed
├── cmake/                      # CMake 模块
│   └── YomkServerPyVersionCheck.cmake  # 框架版本下限校验（对齐 C++ YomkServerVersionCheck.cmake 职责）
├── test/run_tests.sh           # 全量测试运行器（与 YomkServerPy run_tests.sh 同构）
├── test/test_dds.py
├── test/fastdds/               # fastdds 绑定基础测试
│   └── test_fastdds.py             #   发布者/订阅者互通（msg 类型）+ 安装树导入来源校验
├── thirdparty/                 # 第三方预编译安装树（与 C++ YomkDds main 同款，随库安装）
│   ├── Fast-DDS-3.6.1/install      #   Fast-DDS C++ 库（libfastdds/libfastcdr 等动态库）
│   ├── Fast-DDS-python-2.6.1/install #   fastdds Python 绑定模块（SWIG，落 site-packages/fastdds）
│   └── Fast-DDS-Gen-4.3.0          #   IDL 代码生成器（运行需 java 17）
├── idl/                        # IDL 源文件目录（只存 .idl，生成源码不入库，build_ubuntu.sh 经 build_idl.sh 直编）
│   └── std_idl.idl                 #   消息类型定义（module yomk/dds/std_idl，全部 M* 类型）
├── CMakeLists.txt              # 安装规则 + 版本注入（单一来源 project(VERSION)）+ 命名空间守卫 + 框架就位校验
├── build_ubuntu.sh             # 一键安装脚本（参考 YomkServerPy，交互输入安装前缀）
├── scripts/
│   └── build_idl.sh           # IDL 类型一键 生成/编译/安装（loan 支持，装入 yomk/dds/ 域）
└── README.md
```

## 命名空间纪律（必须遵守）

框架的 `yomk/`、`yomk/msgs/`、`yomk/url/` 三层是 **PEP 420 命名空间包**（无 `__init__.py`，
`import yomk` 不加载任何子模块、`yomk.__file__ is None`）。本扩展依赖
该机制与框架合并，因此：

- 扩展仓库**不得创建** `yomk/__init__.py`、`yomk/msgs/__init__.py` 或 `yomk/url/__init__.py`
  （CMakeLists.txt configure 期做守卫检查，命中即 FATAL_ERROR）；
- 只新增自己的叶子目录（本扩展为 `yomk/msgs/dds/`、`yomk/dds/` 与 `yomk/url/dds/`）；
- 扩展域版本与框架同构：CMake 构建期注入 `_version.py`，
  `__init__.py` 以 `try: from ._version import __version__ except ImportError` 兜底。

## 扩展运行时服务纪律

框架只拉起 5 个内置服务，**不做隐式发现**。扩展服务由使用方显式注册：

```python
from yomk.server import server
from yomk.dds import DdsService

server().async_thread(1).init()
server().srv(DdsService).create()   # 显式注册后 /DdsService/* 才可达
```

## 安装与验证

安装须与 YomkServerPy 同前缀（扩展按 PEP 420 并入框架 site-packages/yomk/）：

```bash
./build_ubuntu.sh      # 一键安装（默认取 YOMK_PREFIX_PATH；依赖 cmake/python3/g++/swig/python3-dev）
./test/run_tests.sh    # 全量测试（缺省消费 $YOMK_PREFIX_PATH 安装树，--prefix 可覆盖）
```

或手动 CMake：

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/opt/yomk
sudo cmake --install build   # 安装期校验框架就位（import 冒烟以普通用户身份运行 test/test_dds.py 即可）
```

安装后可用：

```python
from yomk.msgs.dds import DdsRequest, DdsResponse   # 扩展消息
from yomk.dds import DdsService, dds                # 扩展运行时 + dds 域 builder
from yomk.url.dds import ECHO                       # 扩展 URL 常量（并入框架 yomk.url 常量池）

server().url(ECHO).msg(DdsRequest("hi")).request()
dds().version()   # dds 版本查询终结，返回 Response
```

dds 域 builder 与框架各域同构，遵循三条 API 形态纪律：

1. **builder 是模块自己的 API**——只封装本模块自有接口（模板仅版本查询终结
   `dds().version()`），不重复框架通用请求链路；新增业务终结方法时在
   `yomk/dds/builder.py` 按同风格追加。
2. **builder 可省略**（可选件）：不提供时须在运行时域 `__init__` 或 URL 常量池
   注释中写明统一请求调用形态并说明各 URL 常量的功能作用。
3. **推荐优先使用统一请求接口**（框架封装好的统一请求形态，屏蔽 URL 之下的路由细节）：

```python
server().url(ECHO).msg(DdsRequest("hi")).request()
```

## 第三方依赖（随库安装）

Python-only 用户不装 C++ YomkDds 库，其 Fast-DDS 运行时依赖由本扩展自行随库安装
（对齐 C++ main 分支：`thirdparty/` 预编译安装树经 CMake `install(DIRECTORY ...)` 落入
同一前缀）：`fastdds` 绑定模块落 `lib/pythonX.Y/site-packages/fastdds`，
`libfastdds`/`libfastcdr` 等动态库落 `lib/`，`build_ubuntu.sh` 幂等注册 ldconfig
（`/etc/ld.so.conf.d/yomk.conf`）解析绑定模块的动态库依赖（无 RPATH）。
安装后任意终端可直接 `import fastdds`。

## IDL 通信数据包安装（build_idl.sh）

将自定义 IDL 一键生成/编译/安装为可 import 的消息类型工程（支持 loan）。
依赖：cmake/python3/g++/swig/python3-dev/java 17（fastddsgen 运行时）；
生成工具**仅使用安装树内的 fastddsgen**（`$YOMK_PREFIX_PATH/scripts/fastddsgen`），
不使用源码 `thirdparty/` 中的：

```bash
bash scripts/build_idl.sh idl/std_idl.idl                        # 交互输入安装路径
bash scripts/build_idl.sh idl/std_idl.idl -p /opt/yomk           # 推荐：-p 指定（可 tab 补全）
# 交互输入时若前缀父目录不存在会判定为疑似拼写错误并拒绝；调试输入链路:
# INSTALL_IDL_DEBUG=1 bash scripts/build_idl.sh ...
```

流程与产物：

1. **生成**：当前目录 `./build_idl_<YYYYMMDD_HHMMSS>/` 下生成类型工程（被 .gitignore 的 `build*/` 规则忽略）
   （`fastddsgen -python -d . -de final <idl>`，`-de final` 即 loan 支持）；
   工程可重复编译，留作源码归档或删除均可
2. **编译**：生成目录内 cmake 配置/编译；自定义前缀无 Fast-DDS 安装树时，
   依赖解析自动回退框架前缀（`$YOMK_PREFIX_PATH`）
3. **安装 + 校正**：C++ 类型库落 `<前缀>/lib/`（ldconfig 刷新，免密 sudo 可用
   则自动执行）；Python 模块自动校正 Debian `posix_local` scheme 的
   `local/.../dist-packages` 偏移，目的地按前缀分派：

| 安装前缀 | Python 模块落点 | 导入形态 |
| --- | --- | --- |
| 默认（`$YOMK_PREFIX_PATH`，与框架同前缀） | `<前缀>/lib/pythonX.Y/site-packages/yomk/dds/` | `yomk.dds.<IDL文件名>`（与主库同域） |
| 自定义前缀 | `<前缀>/lib/pythonX.Y/site-packages/yomk/dds/`（同上） | 同上 |

安装脚本会自动消除 module 声明带来的多余层级：IDL 的 module 声明（如
`module yomk { module dds {`）与扩展域 `yomk/dds` 同名的父目录、以及剩余
module 层与文件名工程目录同名（如 `std_idl/std_idl`）的同名层，都会在安装
时被剥离/合并，最终一律单层落在 `yomk/dds/<IDL文件名>/`。

脚本收尾会打印本次实际的落点与导入形态（无需自行推导）。

**自定义前缀注意**：该前缀若仅含 msg 模块（未用 `build_ubuntu.sh` 完整部署扩展域），
与框架同时挂载时，框架侧的 `yomk.dds` 常规包（PEP 420）会遮蔽本前缀的
`yomk/dds` 命名空间段，无法以 `yomk.dds.<模块>` 导入——需向该前缀完整部署
扩展域，并将其 site-packages 挂在框架之前；脚本验证会检测此情况并降级为
"直挂 `yomk/dds` 校验模块文件"同时给出警告。

   自定义前缀不在系统 PYTHONPATH 内，使用前需自行挂载：

```bash
export PYTHONPATH=<自定义前缀>/lib/pythonX.Y/site-packages:$PYTHONPATH
```

   说明：默认前缀下模块并入 `yomk/dds/`；自定义前缀下若也子挂 `yomk/dds/`，
   会被框架侧的 `yomk.dds` 常规包（PEP 420）遮蔽而不可导入，故保持生成布局。

脚本收尾打印实际落点与导入形态，并做导入冒烟（通过才 exit 0）。
安装后即可用生成的类型收发数据（示例，以实测过的 `module TestMsg { struct Demo ... }` 为例）：

```python
from yomk.dds.TestMsg.demo import Demo   # 默认前缀安装后的导入形态

data = Demo()
data.payload("hello")    # 字段写入走 setter
data.payload()           # 字段读取走 getter，返回 'hello'
```

## 版本号管理

版本机制与 YomkServerPy 完全同构，编译期注入的单一来源链：
`CMakeLists.txt` 的 `project(YomkDds VERSION x.y.z)` → `YOMK_DDS_VERSION` →
`configure_file` 生成 `yomk/dds/_version.py` → `yomk.dds.__version__`
（`DdsService.version()` 端点暴露）。改版本 = 改 [CMakeLists.txt:8](CMakeLists.txt#L8)
的 `project(VERSION)` 一行，重跑 `./build_ubuntu.sh` 即生效；
源树直用（未经安装注入）时 `__version__` 回退 `"unknown"`（与框架各域一致）。

## 功能端点

| 端点 | URL 常量 | 请求包 | 回包 |
| --- | --- | --- | --- |
| 回显 | `ECHO` (`/DdsService/echo`) | `DdsRequest` | `Response.data` 位承载 `DdsResponse` |
| 版本查询 | `GET_VERSION` (`/DdsService/version`) | 无（msg 可省略） | `Response.data` 为 `"dds v<版本>"` 字符串 |
