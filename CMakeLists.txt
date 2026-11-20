cmake_minimum_required(VERSION 3.14)
# 版本号策略（与 YomkServerPy 同构，编译期注入的单一来源链）：
# project(VERSION) → YOMK_DDS_VERSION → configure_file 生成 yomk/dds/_version.py
# 的 __version__ → yomk.dds.__version__（YomkDdsHandler /get_version 端点暴露）。
# 本行即版本唯一来源，发布时手动 bump。
project(YomkDds VERSION 0.2.0 LANGUAGES NONE)

set(YOMK_DDS_VERSION "${PROJECT_VERSION}")

# ---------- 1. 查找 Python ----------
# 纯 Python 扩展（无 Cython 编译产物），仅 Interpreter 组件：取解释器版本号拼 site-packages 相对路径
find_package(Python REQUIRED COMPONENTS Interpreter)

string(REPLACE "." ";" PYTHON_VERSION_LIST ${Python_VERSION})
list(GET PYTHON_VERSION_LIST 0 PYTHON_MAJOR)
list(GET PYTHON_VERSION_LIST 1 PYTHON_MINOR)
set(PYTHON_SITE_PACKAGES "lib/python${PYTHON_MAJOR}.${PYTHON_MINOR}/site-packages")

# ---------- 2. 命名空间守卫（configure 期） ----------
# 扩展按 PEP 420 并入框架 yomk 命名空间，源树不得带三层命名空间段的 __init__.py
# （会遮蔽框架其它子包）
foreach(_guard src/yomk/__init__.py src/yomk/msgs/__init__.py src/yomk/url/__init__.py)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_guard}")
        message(FATAL_ERROR "扩展源树含 ${_guard}，违反命名空间包纪律，禁止安装")
    endif()
endforeach()

# ---------- 3. 版本注入：configure_file 生成 yomk/dds/_version.py ----------
set(YOMK_MODULE_VERSION "${YOMK_DDS_VERSION}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/yomk/dds/_version.py.in"
    "${CMAKE_CURRENT_BINARY_DIR}/yomk/dds/_version.py"
    @ONLY
)

# ---------- 4. 安装前置校验（必须先于一切 install 规则声明） ----------
# install 规则按声明顺序执行：校验放最前，框架缺失/版本过低时立即中止，
# 任何扩展文件都不落盘（避免"文件已装、校验才报错"的带病安装）。
# 校验只读前缀中已存在的框架文件，不依赖扩展自身安装产物。
# 纯文件/文本检查，不调用 python——import 冒烟依赖 user-site 的 pip 依赖
# （readerwriterlock 等），sudo 安装时以 root 身份不可见，故放 build_ubuntu.sh
# 以调用者身份执行（与框架"特权步骤只落盘、校验回归普通用户"设计一致）
install(CODE "
    if(NOT EXISTS \"\${CMAKE_INSTALL_PREFIX}/${PYTHON_SITE_PACKAGES}/yomk/server/__init__.py\")
        message(FATAL_ERROR \"安装前缀 '\${CMAKE_INSTALL_PREFIX}' 下未找到框架 yomk 命名空间包，扩展须与 YomkServerPy 同前缀安装\")
    endif()
    # 框架版本下限校验（cmake/YomkServerPyVersionCheck.cmake，对齐 C++ YomkServerVersionCheck.cmake 职责；
    # install(CODE) 独立脚本作用域拿不到 configure 期函数，故在安装脚本内 include 模块再调用）
    include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/YomkServerPyVersionCheck.cmake\")
    yomk_serverpy_version_check(\"\${CMAKE_INSTALL_PREFIX}/${PYTHON_SITE_PACKAGES}/yomk/server/_version.py\")
")

# ---------- 5. 安装规则 ----------
# 纯 Python 源树按扩展名过滤安装（.py/.pyi/py.typed，排除 __pycache__）；
# 仅落扩展自身叶子目录（yomk/dds、yomk/msgs/dds、yomk/url/dds），不动框架文件
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/yomk/"
    DESTINATION "${PYTHON_SITE_PACKAGES}/yomk"
    FILES_MATCHING
        PATTERN "*.py"
        PATTERN "*.pyi"
        PATTERN "py.typed"
    PATTERN "__pycache__" EXCLUDE
)

# 生成的 _version.py 单独安装（与框架"生成版本单独安装"规则同构）
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/yomk/dds/_version.py"
    DESTINATION "${PYTHON_SITE_PACKAGES}/yomk/dds"
)

# ---------- 6. 第三方依赖随库安装（对齐 C++ YomkDds main 分支） ----------
# C++ 库把 thirdparty 预编译安装树随库装入前缀；本扩展面向 Python-only 用户
# （不装 C++ 库，C++ 安装树不存在），故同样随库安装三件套，保证 import fastdds 可用：
# - Fast-DDS-3.6.1/install      → bin/include/lib/share/tools（libfastdds/libfastcdr 等
#                                 动态库落 lib/，由 build_ubuntu.sh 注册 ldconfig 解析）
# - Fast-DDS-python-2.6.1/install → lib/pythonX.Y/site-packages/fastdds（SWIG 绑定模块，
#                                 _fastdds_python.so 无 RPATH，依赖上述 lib/ 动态库）
# - Fast-DDS-Gen-4.3.0          → scripts/fastddsgen 等（IDL 代码生成，运行需 java 17）
# mcap/nlohmann_json 为纯头文件依赖（仅 C++ 编译期使用），与 C++ main 一致不安装
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/Fast-DDS-3.6.1/install/"
    DESTINATION .)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/Fast-DDS-python-2.6.1/install/"
    DESTINATION .)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/Fast-DDS-Gen-4.3.0/"
    DESTINATION .)

# ---------- 7. IDL 构建脚本随库安装 ----------
# yomkdds_build_idl.sh 装入 <前缀>/bin，供用户自定义 IDL 类型工程（生成/编译/安装）；
# 脚本内生成工具与依赖均按安装树路径解析（<前缀>/scripts/fastddsgen 等），
# 不依赖源码目录，故可随库发布
install(PROGRAMS scripts/yomkdds_build_idl.sh DESTINATION bin)

