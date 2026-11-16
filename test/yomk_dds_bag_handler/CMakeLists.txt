# =============================================================================
# YomkDdsBagHandler：bag 服务两层测试
# 契约测试（DDS-free，白盒 invoke 验证 6 端点输入校验/错误码）+
# 生命周期测试（真实 DDS：create → record → Ctrl+C 模拟停止 → delete 全链 + 单节点
# 重复创建拒绝 + 录制定格后重复 record 拒绝）。
# =============================================================================
add_executable(TestYomkDdsBagHandlerContract TestYomkDdsBagHandlerContract.cpp)
target_link_libraries(TestYomkDdsBagHandlerContract PRIVATE yomkdds_under_test)

add_test(NAME TestYomkDdsBagHandlerContract COMMAND ${YOMKDDS_TEST_LAUNCHER} $<TARGET_FILE:TestYomkDdsBagHandlerContract>)
set_tests_properties(TestYomkDdsBagHandlerContract PROPERTIES
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan.supp:print_suppressions=0;TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tsan.supp:print_suppressions=0"
)

add_executable(TestYomkDdsBagHandlerLifecycle TestYomkDdsBagHandlerLifecycle.cpp)
target_link_libraries(TestYomkDdsBagHandlerLifecycle PRIVATE yomkdds_under_test YomkDdsStdMsgs)

add_test(NAME TestYomkDdsBagHandlerLifecycle COMMAND ${YOMKDDS_TEST_LAUNCHER} $<TARGET_FILE:TestYomkDdsBagHandlerLifecycle>)
set_tests_properties(TestYomkDdsBagHandlerLifecycle PROPERTIES
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/lsan.supp:print_suppressions=0;TSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tsan.supp:print_suppressions=0"
)
