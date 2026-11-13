#pragma once
// 零第三方依赖的极简测试断言头（与 YomkServer/Test 套件风格一致）：
// 纯 main() + check 函数 + 失败计数，进程返回非 0 表示存在失败用例。
// 每个测试可执行程序为单一 TU，inline 变量保证跨 TU 也无重复定义。
#include <iostream>
#include <string>

inline int g_checkTotal = 0;
inline int g_checkFailed = 0;

// 断言并打印单条结果：返回 cond 本身，便于调用处串联跳过后续依赖步骤。
// 注：函数形式拿不到调用点行号，失败定位依赖各用例自身的 msg 描述。
inline bool check(bool cond, const std::string& msg)
{
    ++g_checkTotal;
    if (cond)
    {
        std::cout << "[ OK ] " << msg << std::endl;
    }
    else
    {
        std::cout << "[FAIL] " << msg << std::endl;
        ++g_checkFailed;
    }
    return cond;
}

// 汇总并返回进程退出码：有失败返回 1，全通过返回 0
inline int testReport(const std::string& suiteName)
{
    std::cout << "-------------------------------------------" << std::endl;
    if (g_checkFailed > 0)
    {
        std::cout << suiteName << " FAILED: " << g_checkFailed << "/" << g_checkTotal << " checks failed." << std::endl;
        return 1;
    }
    std::cout << suiteName << " all passed (" << g_checkTotal << " checks)." << std::endl;
    return 0;
}
