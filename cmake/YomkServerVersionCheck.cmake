# ============================================================
# YomkServer 版本检测模块
#
# 用途：YomkDds 依赖 YomkServer 3.6.18 及以上版本（分域头文件布局 include/yomk/<域>/），
#   版本过低时在 configure 阶段立即报错终止，阻止编译。
# 用法：find_package(YomkServer REQUIRED) 之后调用一次
#   yomk_server_version_check()
# ============================================================

function(yomk_server_version_check)
    # YomkServer_VERSION 由 YomkServerConfig.cmake 导出（find_package 时设置）
    if(YomkServer_VERSION VERSION_LESS "3.6.18")
        message(FATAL_ERROR
            "YomkDds 依赖 YomkServer 3.6.18 及以上版本，当前检测到版本: '${YomkServer_VERSION}'，"
            "请安装 YomkServer 3.6.18 或更高版本后重新配置")
    endif()
endfunction()
