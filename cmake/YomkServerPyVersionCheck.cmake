# ============================================================
# YomkServerPy 版本检测模块
#
# 用途：YomkDdsPython（本分支）依赖的是 Python 框架 YomkServerPy 2.3.13 及以上版本
#   （yomk/ PEP 420 命名空间新包布局 + server/_facade 链式请求面），
#   版本过低时在安装阶段立即报错终止，阻止带病安装（问题不延迟到运行时）。
#   对齐 C++ YomkDds cmake/YomkServerVersionCheck.cmake 的职责形态：
#   版本下限单一来源为本文件，校验逻辑收敛为函数。
# 用法：根 CMakeLists 安装期经 install(CODE) include 本模块后调用一次
#   yomk_serverpy_version_check(<框架 yomk/server/_version.py 绝对路径>)
# ============================================================

function(yomk_serverpy_version_check _ver_file)
    # 版本单一来源：框架 yomk/server/_version.py（YomkServerPy 构建期 configure_file 注入）
    if(NOT EXISTS "${_ver_file}")
        message(FATAL_ERROR
            "未找到框架版本文件 ${_ver_file}，YomkServerPy 过旧或缺失（最低要求 2.3.13），"
            "请安装 YomkServerPy 2.3.13 或更高版本后重新配置安装")
    endif()
    file(STRINGS "${_ver_file}" _srvpy_ver_line REGEX "__version__")
    string(REGEX MATCH "[0-9]+[0-9.]*" _srvpy_ver "${_srvpy_ver_line}")
    if(NOT _srvpy_ver OR _srvpy_ver VERSION_LESS "2.3.13")
        message(FATAL_ERROR
            "YomkDdsPython 依赖 YomkServerPy 2.3.13 及以上版本，当前检测到版本: '${_srvpy_ver}'，"
            "请安装 YomkServerPy 2.3.13 或更高版本后重新配置安装")
    endif()
    message(STATUS "[YomkServerPy Check] 框架版本 ${_srvpy_ver} >= 2.3.13，校验通过")
endfunction()
