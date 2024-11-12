#include "sylar/sylar.h"
#include "sylar/http/http.h"
#include "sylar/log.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void run()
{
    // 1、创建服务器要绑定的ip地址
    sylar::Address::ptr addr = sylar::Address::LookupAnyIPAddress("0.0.0.0:8020");
    if(!addr)
    {
        SYLAR_LOG_INFO(g_logger) << " get address fail";
        return;
    }
    else
    {
        SYLAR_LOG_INFO(g_logger) << "addr: " << *addr;
    }

    // 2、创建一个服务器对像
    sylar::http::HttpServer::ptr httpserver(new sylar::http::HttpServer);
    
    // 3、服务器绑定地址
    while(!httpserver->bind(addr))
    {
        SYLAR_LOG_INFO(g_logger) << "bind address " << *addr << " fail";
        sleep(2);
    }
    
    // 4、服务器开始工作
    httpserver->start();
    SYLAR_LOG_INFO(g_logger) << " httpinfo: " << httpserver->toString();
}

int main(int argc, char** argv)
{
    sylar::IOManager iom(1);
    iom.schedule(run);
    return 0;
}