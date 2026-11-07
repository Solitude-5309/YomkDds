# ============================================================
# YomkServer 版本系列检测模块
#
# 用途：YomkRpc 依赖 YomkServer 3.x.x 系列，版本不匹配时
#   在 configure 阶段立即报错终止，阻止编译。
# 用法：find_package(YomkServer REQUIRED) 之后调用一次
#   yomk_server_version_check()
# ============================================================

function(yomk_server_version_check)
    # YomkServer_VERSION 由 YomkServerConfig.cmake 导出（find_package 时设置）
    if(NOT YomkServer_VERSION MATCHES "^3\\.")
        message(FATAL_ERROR
            "YomkRpc 依赖 YomkServer 3.x.x 系列，当前检测到版本: '${YomkServer_VERSION}'，"
            "请安装 YomkServer 3.x 后重新配置")
    endif()
endfunction()
