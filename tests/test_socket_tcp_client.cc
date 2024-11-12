/**
 * @file test_socket_tcp_client.cc
 * @brief 测试Socket类，tcp客户端
 * @version 0.1
 * @date 2021-09-18
 */
#include<sylar/sylar.h>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void test_tcp_client() 
{
    int ret;
    // 1、创建
    auto socket = sylar::Socket::CreateTCPSocket();
    SYLAR_ASSERT(socket);

    auto addr = sylar::Address::LookupAnyIPAddress("0.0.0.0:12345", AF_INET);
    SYLAR_ASSERT(addr);
    
    // 2、连接
    ret = socket->connect(addr);
    SYLAR_ASSERT(ret);
    SYLAR_LOG_INFO(g_logger) << "connect success, local address: " << socket->getLocalAddress()->toString();
    SYLAR_LOG_INFO(g_logger) << "connect success, peer address: " << socket->getRemoteAddress()->toString();

    std::string buffer;
    buffer.resize(1024);
    socket->recv(&buffer[0], buffer.size());
    SYLAR_LOG_INFO(g_logger) << "recv: " << buffer;
    socket->close();

    return;
}

void test_connect_baidu()
{
    SYLAR_LOG_INFO(g_logger) << "begin";
    
    auto addr = sylar::Address::LookupAnyIPAddress("www.baidu.com");
    if(addr)
    {
        SYLAR_LOG_INFO(g_logger) << "get addr: " << addr->toString();
    }
    else
    {
        SYLAR_LOG_INFO(g_logger) << "get addr fialed";
    }
    addr->setPort(80);
    SYLAR_LOG_INFO(g_logger) << "addr: " << addr->toString();


    auto socket = sylar::Socket::CreateTCPSocket();
    SYLAR_ASSERT(socket);
    
    //连接
    if(!socket->connect(addr))
    {
        SYLAR_LOG_INFO(g_logger) << "connect address " << addr->toString() << " failed";
    }
    else
    {
        SYLAR_LOG_INFO(g_logger) << "connect address " << addr->toString() << " successed";
    }

    const char buff[] = "GET / HTTP/1.0\r\n\r\n";
    int ret           = socket->send(buff, sizeof(buff));
    if (ret <= 0) {
        SYLAR_LOG_INFO(g_logger) << "send  failed";
    }

    std::string recvbuff;
    recvbuff.resize(4096);
    // 这点recvbuff是string类的一个对象，他并不是像 char str[]="1234"，数组名就是字符串的首地址
    // 而如果直接&recvbuff，那么是对这个对象取地址，而不是这个对象内部的字符数组，又string重载了[]
    // 所以可以直接通过recvbuff[0]拿到首元素的地址，也就是申请的字符空间的地址
    ret = socket->recv(&recvbuff[0], recvbuff.size());
    
    if(ret <= 0)
    {
        SYLAR_LOG_INFO(g_logger) << "recv failed ";
    }

    recvbuff.resize(ret);
    std::cout << recvbuff << std::endl;


    SYLAR_LOG_INFO(g_logger) << "local address2: " << socket->getLocalAddress()->toString();
    SYLAR_LOG_INFO(g_logger) << "remote address: " << socket->getRemoteAddress()->toString();
    socket->close();

    SYLAR_LOG_INFO(g_logger) << "end ";
    return;
}

int main(int argc, char *argv[]) 
{
    sylar::EnvMgr::GetInstance()->init(argc, argv);
    sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    sylar::IOManager iom;
    //iom.schedule(&test_tcp_client);
    iom.schedule(test_connect_baidu);

    return 0;
}