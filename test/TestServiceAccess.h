#pragma once
// YomkServer 3.x 将 YomkService::invoke()/funcInfos() 由 public 收紧为 protected
// （服务与框架的协作面），测试不能再经服务实例指针直驱端点。
//
// 改用框架公开的服务器路由接口，语义等价：
//   server()->request(svcName + funcName, pkg)    ≡ svc->invoke(funcName, pkg)
//   server()->serviceFuncInfos(svcName)           ≡ svc->funcInfos()
// 其中服务名以 "/" 开头（如 "/YomkRpcService"），URL 拼接后仍满足
// "/服务名/函数名" 的公开路由格式。
//
// 前置条件：被测服务已通过 srv_ptr(svc).add() 注册（所有测试均在调用前完成注册）。
#include <YomkServer/YomkApi.h>

#include <map>
#include <string>
#include <string_view>

// 白盒直驱服务端点：等价于 3.x 之前的 svc->invoke(funcName, pkg)
inline yomk::payload::Response testInvoke(YomkService* svc, std::string_view funcName, YomkMsgPtr pkg = nullptr)
{
    return ::yomk::server().instance()->request(std::string(svc->name()) + std::string(funcName), pkg);
}

// 白盒内省服务端点：等价于 3.x 之前的 svc->funcInfos()
inline std::map<std::string, yomk::payload::FuncInfo> testFuncInfos(YomkService* svc)
{
    return ::yomk::server().instance()->serviceFuncInfos(svc->name());
}