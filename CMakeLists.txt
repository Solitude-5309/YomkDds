cmake_minimum_required(VERSION 3.14)
# 版本号策略（与 YomkServerPy 同构，编译期注入的单一来源链）：
# project(VERSION) → YOMK_DDS_VERSION → configure_file 生成 yomk/dds/_version.py
# 的 __version__ → yomk.dds.__version__（DdsService.version 端点暴露）。
# 本行即版本唯一来源，发布时手动 bump。
project(YomkDds VERSION 0.0.2 LANGUAGES NONE)

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

# ---------- 4. 安装规则 ----------
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

# ---------- 5. 安装期校验 ----------
# 框架就位校验：扩展必须装入 YomkServerPy 所在安装前缀（PEP 420 命名空间合并的前提）。
# 纯文件存在性检查，不调用 python——import 冒烟依赖 user-site 的 pip 依赖
# （readerwriterlock 等），sudo 安装时以 root 身份不可见，故放 build_ubuntu.sh
# 以调用者身份执行（与框架"特权步骤只落盘、校验回归普通用户"设计一致）
install(CODE "
    if(NOT EXISTS \"\${CMAKE_INSTALL_PREFIX}/${PYTHON_SITE_PACKAGES}/yomk/server/__init__.py\")
        message(FATAL_ERROR \"安装前缀 '\${CMAKE_INSTALL_PREFIX}' 下未找到框架 yomk 命名空间包，扩展须与 YomkServerPy 同前缀安装\")
    endif()
")
