#include "tcp_server.h"
#include "config.h"
#include "log.h"
#include "fiber.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

static sylar::ConfigVar<uint64_t>::ptr g_tcp_server_read_timeout =
    sylar::Config::Lookup("tcp_server.read_timeout", (uint64_t)(60 * 1000 * 2),
            "tcp server read timeout");

TcpServer::TcpServer(sylar::IOManager* io_worker,
                    sylar::IOManager* accept_worker)
                    :m_ioWorker(io_worker)
                    ,m_acceptWorker(accept_worker)
                    ,m_recvTimeout(g_tcp_server_read_timeout->getValue())
                    ,m_name("sylar/1.0.0")
                    ,m_type("tcp")
                    ,m_isStop(true) 
{
    // accept_worker主要负责listen socket的调度工作
    // io_worker主要负责服务器accept之后客户端socket的调度工做
    // 经测试 io_worker = accept_worker
    // if(io_worker==accept_worker)
    // {
    //     SYLAR_LOG_INFO(g_logger) << "io_worker == accept_worker";
    // }
}

TcpServer::~TcpServer() 
{
    // 关闭所有的监听的socket
    for(auto& i : m_socks) 
    {
        i->close();
    }
    m_socks.clear();
}

bool TcpServer::bind(sylar::Address::ptr addr) 
{
    std::vector<Address::ptr> addrs;
    std::vector<Address::ptr> fails;
    addrs.push_back(addr);
    return bind(addrs, fails);
}

bool TcpServer::bind(const std::vector<Address::ptr>& addrs
                        ,std::vector<Address::ptr>& fails ) 
{
    for(auto& addr : addrs) 
    {
        // 根据地址类型创建相应类型的socket
        Socket::ptr sock = Socket::CreateTCP(addr);
        // socket绑定地址
        if(!sock->bind(addr)) 
        {
            SYLAR_LOG_ERROR(g_logger) << "bind fail errno="
                << errno << " errstr=" << strerror(errno)
                << " addr=[" << addr->toString() << "]";
            // 绑定失败加入到fails集合
            fails.push_back(addr);
            continue;
        }

        // 随后开启监听
        if(!sock->listen()) 
        {
            SYLAR_LOG_ERROR(g_logger) << "listen fail errno="
                << errno << " errstr=" << strerror(errno)
                << " addr=[" << addr->toString() << "]";
            // 监听失败的也加入到fails
            fails.push_back(addr);
            continue;
        }
        // 完成bind并且成功监听的socket集合
        m_socks.push_back(sock);
    }

    // fails 非空
    if(!fails.empty()) 
    {
        m_socks.clear();
        return false;
    }

    for(auto& i : m_socks) 
    {
        SYLAR_LOG_INFO(g_logger) << "type=" << m_type
            << " name=" << m_name
            << " server bind success: " << *i;
    }
    return true;
}

void TcpServer::startAccept(Socket::ptr sock) 
{
    while(!m_isStop) 
    {
        // 接受连接 并创建客户端socket
        Socket::ptr client = sock->accept();
        if(client) 
        {
            client->setRecvTimeout(m_recvTimeout);
            m_ioWorker->schedule(std::bind(&TcpServer::handleClient,
                        shared_from_this(), client));
        } 
        else 
        {
            SYLAR_LOG_ERROR(g_logger) << "accept errno=" << errno
                << " errstr=" << strerror(errno);
        }
    }
}

bool TcpServer::start() 
{
    if(!m_isStop) 
    {
        return true;
    }
    m_isStop = false;

    for(auto& sock : m_socks) 
    {
        m_acceptWorker->schedule(std::bind(&TcpServer::startAccept,
                    shared_from_this(), sock));
    }
    return true;
}

void TcpServer::stop() 
{
    m_isStop = true;
    auto self = shared_from_this();
    m_acceptWorker->schedule([this, self]() 
    {
        for(auto& sock : m_socks) 
        {
            sock->cancelAll();
            sock->close();
        }
        m_socks.clear();
    });
}

void TcpServer::handleClient(Socket::ptr client)
 {
    SYLAR_LOG_INFO(g_logger) << "handleClient: " << *client;
}

std::string TcpServer::toString(const std::string& prefix) 
{
    std::stringstream ss;
    ss << prefix << "[type=" << m_type
       << " name=" << m_name
       << " io_worker=" << (m_ioWorker ? m_ioWorker->getName() : "")
       << " accept_worker=" << (m_acceptWorker ? m_acceptWorker->getName() : "")
       << " recv_timeout=" << m_recvTimeout << "]" << std::endl;
    
    std::string pfx = prefix.empty() ? "    " : prefix;

    for(auto& i : m_socks) 
    {
        ss << pfx << pfx << *i << std::endl;
    }
    return ss.str();
}

}
