# FastDDS thirdparty 使用说明

thirdparty 下的 Fast-DDS-3.6.1 / Fast-DDS-python-2.6.1 / Fast-DDS-Gen-4.3.0 均为预编译安装树，
由仓库根 CMakeLists.txt 的 install 规则随 build_ubuntu.sh 一并安装到
`$YOMK_PREFIX_PATH`（默认 /opt/yomk，见 /etc/profile.d/yomk.sh）：

```cmake
install(DIRECTORY thirdparty/Fast-DDS-3.6.1/install/         DESTINATION .)
install(DIRECTORY thirdparty/Fast-DDS-python-2.6.1/install/  DESTINATION .)
install(DIRECTORY thirdparty/Fast-DDS-Gen-4.3.0/             DESTINATION .)
```

安装后无需手动 export LD_LIBRARY_PATH：/opt/yomk/lib 已注册到 ldconfig
（/etc/ld.so.conf.d/yomk.conf）。

## linux下 Fast-DDS-3.6.1 C++使用方法

### 编译示例（thirdparty/Fast-DDS-3.6.1/test/hello_world）

1. cd thirdparty/Fast-DDS-3.6.1/test/hello_world
2. mkdir build
3. cd build
4. cmake .. -DCMAKE_PREFIX_PATH=$YOMK_PREFIX_PATH   # CMakeLists 已要求 cmake >= 3.22
5. cmake --build . -j

### 运行示例

新开两个终端分别运行（库已入 ldconfig 缓存，无需额外环境变量）：

1. `./hello_world subscriber`
2. `./hello_world publisher`

预期输出：双方打印 matched，订阅端逐条打印
`Message: 'Hello world' with index: 'N' RECEIVED`。

### 使用Fast-DDS-Gen（需 java 17）

1. cd thirdparty/Fast-DDS-Gen-4.3.0/scripts
2. 创建一个最简单的idl文件rpc.idl，内容如下

    ```idl
    struct RPCString
    {
        string s;
    };
    ```

3. 创建一个gen目录用于存放生成文件（目录必须先存在，fastddsgen 不会自动创建）
4. 生成C++代码：./fastddsgen -d gen rpc.idl 或 生成python代码：./fastddsgen -python -d gen rpc.idl
5. 生成支持loan的msg：./fastddsgen -python -d gen -de final rpc.idl

安装到 /opt/yomk 后也可直接使用 /opt/yomk/scripts/fastddsgen。

### 编译python idl（生成 SWIG Python 绑定模块）

1. 安装swig：sudo apt install swig（需 4.0.x）
2. sudo apt install libpython3-dev
3. cd gen
4. mkdir build
5. cd build
6. cmake .. -DCMAKE_PREFIX_PATH=$YOMK_PREFIX_PATH
7. cmake --build . -j

产物在 build 目录：`<模块名>.py` + `_<模块名>Wrapper.so`（+ C++ 类型支持库
lib<模块名>.so）。将 build 目录加入 PYTHONPATH 即可 import。

## linux下 Fast-DDS-python-2.6.1 Python使用方法

### 安装与导入

随 build_ubuntu.sh 自动装入 $YOMK_PREFIX_PATH：

- Python 包：`/opt/yomk/lib/python3.10/site-packages/fastdds/`
- CMake config：`/opt/yomk/lib/cmake/fastdds_python/`（供生成的绑定模块工程 find_package 使用）

/etc/profile.d/yomkpy.sh 已把 `/opt/yomk/lib/python3.10/site-packages` 挂入
PYTHONPATH，新开终端直接验证：

```bash
python3 -c "import fastdds; print(fastdds.__file__)"
```

### 运行示例（thirdparty/Fast-DDS-python-2.6.1/test/hello_world）

示例为官方 HelloWorldExample（`HelloWorldExample.py` + `README.md`），绑定模块已
预生成在 `generated_code/`（含 `HelloWorld.idl`），无需 java/swig 重新生成。

1. 编译预生成的绑定模块：

    ```bash
    cd thirdparty/Fast-DDS-python-2.6.1/test/hello_world/generated_code
    mkdir build && cd build
    cmake .. -DCMAKE_PREFIX_PATH=$YOMK_PREFIX_PATH
    cmake --build . -j
    ```

2. 运行（两个终端，先把生成模块挂入 PYTHONPATH）：

    ```bash
    REPO=~/workspace/git_store/YomkDds
    export PYTHONPATH=$REPO/thirdparty/Fast-DDS-python-2.6.1/test/hello_world/generated_code/build:$PYTHONPATH
    # 终端1
    python3 HelloWorldExample.py -p subscriber
    # 终端2
    python3 HelloWorldExample.py -p publisher
    ```

   可选参数：`-d <domainID>` 指定 domain，`-m <machineID>` 区分发布机器（见示例
   自带 README.md）。脚本以 `Press any key to stop` 结束，回车退出；非交互环境
   （脚本/CI）可 `echo | python3 HelloWorldExample.py -p subscriber` 用管道喂 stdin。

预期输出：双方打印 matched，订阅端逐条打印 `Received Hello World : N`，发布端
发送 10 条后退出。QoS 默认 BEST_EFFORT + VOLATILE，先起订阅者再起发布者可避免
丢样（详见示例 README.md）。

如需对其他 IDL 生成绑定，见上文"使用Fast-DDS-Gen"与"编译python idl"。

注：示例依赖 /opt/yomk 中的 Fast-DDS-3.6.1 运行库（ldconfig 已可解析，无需
LD_LIBRARY_PATH）；绑定版本为 2.6.1，绑定包为 python3.10 构建，与系统
Python 3.10.12 匹配。
