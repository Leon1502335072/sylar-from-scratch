#include "socket.h"
#include "iomanager.h"
#include "fd_manager.h"
#include "log.h"
#include "macro.h"
#include "hook.h"
#include <limits.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 通过传入的地址创建TCP类型的类型的socket 主要是第二个参数：SOCK_STREAM
Socket::ptr Socket::CreateTCP(sylar::Address::ptr address) 
{
    Socket::ptr sock(new Socket(address->getFamily(), TCP, 0));
    return sock;
}

// 通过传入的地址创建TCP类型的类型的socket 主要是第二个参数：SOCK_DGRAM
Socket::ptr Socket::CreateUDP(sylar::Address::ptr address) 
{
    Socket::ptr sock(new Socket(address->getFamily(), UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

// 直接创建IPv4 TCP类型的socket
Socket::ptr Socket::CreateTCPSocket() 
{
    Socket::ptr sock(new Socket(IPv4, TCP, 0));
    return sock;
}

// 直接创建IPv4 UDP类型的socket
Socket::ptr Socket::CreateUDPSocket() 
{
    Socket::ptr sock(new Socket(IPv4, UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

// 直接创建IPv6 TCP类型的socket
Socket::ptr Socket::CreateTCPSocket6() 
{
    Socket::ptr sock(new Socket(IPv6, TCP, 0));
    return sock;
}

// 直接创建IPv6 UDP类型的socket
Socket::ptr Socket::CreateUDPSocket6() 
{
    Socket::ptr sock(new Socket(IPv6, UDP, 0));
    sock->newSock();
    sock->m_isConnected = true;
    return sock;
}

// 直接创建UNIX TCP类型的socket
Socket::ptr Socket::CreateUnixTCPSocket() 
{
    Socket::ptr sock(new Socket(UNIX, TCP, 0));
    return sock;
}

// 直接创建UNIX类型的socket
Socket::ptr Socket::CreateUnixUDPSocket() 
{
    Socket::ptr sock(new Socket(UNIX, UDP, 0));
    return sock;
}

Socket::Socket(int family, int type, int protocol)
    : m_sock(-1)
    , m_family(family)
    , m_type(type)
    , m_protocol(protocol)
    , m_isConnected(false) {
}

Socket::~Socket() 
{
    close();
}

int64_t Socket::getSendTimeout() 
{
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(m_sock);
    if (ctx) 
    {
        return ctx->getTimeout(SO_SNDTIMEO);
    }
    return -1;
}

void Socket::setSendTimeout(int64_t v) 
{
    struct timeval tv 
    {
        // 两个数据成员，秒、毫秒
        int(v / 1000), int(v % 1000 * 1000)
    };
    // setOption里面掉我们hook住的setsockopt函数
    setOption(SOL_SOCKET, SO_SNDTIMEO, tv);
}

int64_t Socket::getRecvTimeout()
{
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(m_sock);
    if (ctx) 
    {
        return ctx->getTimeout(SO_RCVTIMEO);
    }
    return -1;
}

void Socket::setRecvTimeout(int64_t v) 
{
    struct timeval tv 
    {
        int(v / 1000), int(v % 1000 * 1000)
    };
    setOption(SOL_SOCKET, SO_RCVTIMEO, tv);
}

bool Socket::getOption(int level, int option, void *result, socklen_t *len) 
{
    // 自己hook过的函数
    int rt = getsockopt(m_sock, level, option, result, (socklen_t *)len);
    if (rt) 
    {
        SYLAR_LOG_DEBUG(g_logger) << "getOption sock=" << m_sock
                                  << " level=" << level << " option=" << option
                                  << " errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

bool Socket::setOption(int level, int option, const void *result, socklen_t len) 
{
    int rt = setsockopt(m_sock, level, option, result, (socklen_t)len);
    if (rt) 
    {
        SYLAR_LOG_DEBUG(g_logger) << "setOption sock=" << m_sock
                                  << " level=" << level << " option=" << option
                                  << " errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

Socket::ptr Socket::accept() 
{
    // 先为接受的连接创建一个socket（智能指针）
    Socket::ptr sock(new Socket(m_family, m_type, m_protocol));
    // 此时返回的newsock（fd）没有自己的addr，因为第二个参数是nullptr
    int newsock = ::accept(m_sock, nullptr, nullptr);
    if (newsock == -1) 
    {
        SYLAR_LOG_ERROR(g_logger) << "accept(" << m_sock << ") errno="
                                  << errno << " errstr=" << strerror(errno);
        return nullptr;
    }
    
    // 此处对新的socket进行初始化：端口复用，无法送延迟，本地ip，对端ip等
    if (sock->init(newsock)) 
    {
        return sock;
    }
    return nullptr;
}

bool Socket::init(int sock) 
{
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(sock);
    if (ctx && ctx->isSocket() && !ctx->isClose()) 
    {
        m_sock        = sock;
        m_isConnected = true;
        initSock();
        getLocalAddress();
        getRemoteAddress();
        return true;
    }
    return false;
}

bool Socket::bind(const Address::ptr addr) 
{
    // 本地地址是在绑定的时候赋值的，也就是说需要从外部将ip地址传进来
    m_localAddress = addr;
    // 判断m_sock（fd）是否有效，无效再重新创建一个fd
    if (!isValid()) 
    {
        SYLAR_LOG_INFO(g_logger) << "-----here-----";
        newSock();
        if (SYLAR_UNLIKELY(!isValid())) 
        {
            return false;
        }
    }

    // 判断外部传进来的地址协议簇和本socket内部的协议簇是否相同
    if (SYLAR_UNLIKELY(addr->getFamily() != m_family)) 
    {
        SYLAR_LOG_ERROR(g_logger) << "bind sock.family("
                                  << m_family << ") addr.family(" << addr->getFamily()
                                  << ") not equal, addr=" << addr->toString();
        return false;
    }

    // 如果通过父类指针创建的是unix的地址，此转化成功，否则不成功
    UnixAddress::ptr uaddr = std::dynamic_pointer_cast<UnixAddress>(addr);

    // 传入的是unix类型的地址
    if (uaddr) 
    {
        Socket::ptr sock = Socket::CreateUnixTCPSocket();
        if (sock->connect(uaddr)) 
        {
            return false;
        } 
        else 
        {
            sylar::FSUtil::Unlink(uaddr->getPath(), true);
        }
    }

    // 传入的是IPV4/IPv6类型的IP地址
    if (::bind(m_sock, addr->getAddr(), addr->getAddrLen())) 
    {
        SYLAR_LOG_ERROR(g_logger) << "bind error errrno=" << errno
                                  << " errstr=" << strerror(errno);
        return false;
    }
    getLocalAddress();
    return true;
}

bool Socket::reconnect(uint64_t timeout_ms) 
{
    if (!m_remoteAddress) 
    {
        SYLAR_LOG_ERROR(g_logger) << "reconnect m_remoteAddress is null";
        return false;
    }
    m_localAddress.reset();
    return connect(m_remoteAddress, timeout_ms);
}

bool Socket::connect(const Address::ptr addr, uint64_t timeout_ms) 
{
    m_remoteAddress = addr;
    if (!isValid()) 
    {
        newSock();
        if (SYLAR_UNLIKELY(!isValid())) 
        {
            return false;
        }
    }

    if (SYLAR_UNLIKELY(addr->getFamily() != m_family)) 
    {
        SYLAR_LOG_ERROR(g_logger) << "connect sock.family("
                                  << m_family << ") addr.family(" << addr->getFamily()
                                  << ") not equal, addr=" << addr->toString();
        return false;
    }
    // 超时时间无穷大 就走原始的connect函数
    if (timeout_ms == (uint64_t)-1) 
    {
        if (::connect(m_sock, addr->getAddr(), addr->getAddrLen())) 
        {
            SYLAR_LOG_ERROR(g_logger) << "sock=" << m_sock << " connect(" << addr->toString()
                                      << ") error errno=" << errno << " errstr=" << strerror(errno);
            close();
            return false;
        }
    } 
    else // 否则超时时间有效，就走connect_with_timeout函数
    {
        if (::connect_with_timeout(m_sock, addr->getAddr(), addr->getAddrLen(), timeout_ms)) 
        {
            SYLAR_LOG_ERROR(g_logger) << "sock=" << m_sock << " connect(" << addr->toString()
                                      << ") timeout=" << timeout_ms << " error errno="
                                      << errno << " errstr=" << strerror(errno);
            close();
            return false;
        }
    }
    m_isConnected = true;
    getRemoteAddress();
    getLocalAddress();
    return true;
}

bool Socket::listen(int backlog) 
{
    // 判断
    if (!isValid()) 
    {
        SYLAR_LOG_ERROR(g_logger) << "listen error sock=-1";
        return false;
    }
    // 调全局的listen方法
    if (::listen(m_sock, backlog)) 
    {
        SYLAR_LOG_ERROR(g_logger) << "listen error errno=" << errno
                                  << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

bool Socket::close() 
{
    if (!m_isConnected && m_sock == -1) 
    {
        return true;
    }
    m_isConnected = false;
    if (m_sock != -1) 
    {
        ::close(m_sock); // ::表示调用的是全局的close函数 而不是Socket::close
        m_sock = -1;
    }
    return false;
}

int Socket::send(const void *buffer, size_t length, int flags) 
{
    if (isConnected()) 
    {
        // 
        return ::send(m_sock, buffer, length, flags);
    }
    return -1;
}

int Socket::send(const iovec *buffers, size_t length, int flags) 
{
    if (isConnected()) 
    {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov    = (iovec *)buffers;
        msg.msg_iovlen = length;
        // 这点调的是sendmsg方法
        return ::sendmsg(m_sock, &msg, flags);
    }
    return -1;
}

int Socket::sendTo(const void *buffer, size_t length, const Address::ptr to, int flags) 
{
    // 针对udp
    if (isConnected()) 
    {
        return ::sendto(m_sock, buffer, length, flags, to->getAddr(), to->getAddrLen());
    }
    return -1;
}

int Socket::sendTo(const iovec *buffers, size_t length, const Address::ptr to, int flags) 
{
    // 针对udp
    if (isConnected()) 
    {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov     = (iovec *)buffers;
        msg.msg_iovlen  = length;
        msg.msg_name    = to->getAddr();
        msg.msg_namelen = to->getAddrLen();
        return ::sendmsg(m_sock, &msg, flags);
    }
    return -1;
}

int Socket::recv(void *buffer, size_t length, int flags) 
{
    if (isConnected()) 
    {
        return ::recv(m_sock, buffer, length, flags);
    }
    return -1;
}

int Socket::recv(iovec *buffers, size_t length, int flags) 
{
    if (isConnected()) 
    {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov    = (iovec *)buffers;
        msg.msg_iovlen = length;
        return ::recvmsg(m_sock, &msg, flags);
    }
    return -1;
}

int Socket::recvFrom(void *buffer, size_t length, Address::ptr from, int flags) 
{
    // 针对udp
    if (isConnected()) 
    {
        socklen_t len = from->getAddrLen();
        return ::recvfrom(m_sock, buffer, length, flags, from->getAddr(), &len);
    }
    return -1;
}

int Socket::recvFrom(iovec *buffers, size_t length, Address::ptr from, int flags) 
{
    // 针对udp
    if (isConnected()) 
    {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov     = (iovec *)buffers;
        msg.msg_iovlen  = length;
        msg.msg_name    = from->getAddr();
        msg.msg_namelen = from->getAddrLen();
        return ::recvmsg(m_sock, &msg, flags);
    }
    return -1;
}

Address::ptr Socket::getRemoteAddress() 
{
    if (m_remoteAddress) 
    {
        return m_remoteAddress;
    }

    Address::ptr result;
    switch (m_family) 
    {
        case AF_INET:
            result.reset(new IPv4Address());
            break;
        case AF_INET6:
            result.reset(new IPv6Address());
            break;
        case AF_UNIX:
            result.reset(new UnixAddress());
            break;
        default:
            result.reset(new UnknownAddress(m_family));
            break;
    }
    // 这点如果是unix的话 会得到最大地址长度，真实长度在下面再处理设置
    socklen_t addrlen = result->getAddrLen();
    if (getpeername(m_sock, result->getAddr(), &addrlen)) 
    {
        SYLAR_LOG_ERROR(g_logger) << "getpeername error sock=" << m_sock
                                  << " errno=" << errno << " errstr=" << strerror(errno);
        return Address::ptr(new UnknownAddress(m_family));
    }
    if (m_family == AF_UNIX) 
    {
        UnixAddress::ptr addr = std::dynamic_pointer_cast<UnixAddress>(result);
        addr->setAddrLen(addrlen);
    }
    m_remoteAddress = result;
    return m_remoteAddress;
}

Address::ptr Socket::getLocalAddress() 
{
    if (m_localAddress) 
    {
        return m_localAddress;
    }

    Address::ptr result;
    switch (m_family) 
    {
        case AF_INET:
            result.reset(new IPv4Address());
            break;
        case AF_INET6:
            result.reset(new IPv6Address());
            break;
        case AF_UNIX:
            result.reset(new UnixAddress());
            break;
        default:
            result.reset(new UnknownAddress(m_family));
            break;
    }
    socklen_t addrlen = result->getAddrLen();
    // getsockname函数用于获取与某个套接字关联的本地协议地址
    // 将m_sock内的本地地址拿出，放到第二个参数中
    if (getsockname(m_sock, result->getAddr(), &addrlen)) 
    {
        SYLAR_LOG_ERROR(g_logger) << "getsockname error sock=" << m_sock
                                  << " errno=" << errno << " errstr=" << strerror(errno);
        return Address::ptr(new UnknownAddress(m_family));
    }

    // unix类型的地址长度是变长的，所以要重新设置一下，而且设置的函数在子类中 所以还要转一下指针
    if (m_family == AF_UNIX) 
    {
        UnixAddress::ptr addr = std::dynamic_pointer_cast<UnixAddress>(result);
        addr->setAddrLen(addrlen);
    }
    m_localAddress = result;
    return m_localAddress;
}

bool Socket::isValid() const 
{
    return m_sock != -1;
}

int Socket::getError() 
{
    int error     = 0;
    socklen_t len = sizeof(error);
    if (!getOption(SOL_SOCKET, SO_ERROR, &error, &len)) 
    {
        error = errno;
    }
    return error;
}

std::ostream &Socket::dump(std::ostream &os) const 
{
    os << "[Socket sock=" << m_sock
       << " is_connected=" << m_isConnected
       << " family=" << m_family
       << " type=" << m_type
       << " protocol=" << m_protocol;
    if (m_localAddress) 
    {
        os << " local_address=" << m_localAddress->toString();
    }
    if (m_remoteAddress) 
    {
        os << " remote_address=" << m_remoteAddress->toString();
    }
    os << "]";
    return os;
}

std::string Socket::toString() const 
{
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

bool Socket::cancelRead() 
{
    return IOManager::GetThis()->cancelEvent(m_sock, sylar::IOManager::READ);
}

bool Socket::cancelWrite() 
{
    return IOManager::GetThis()->cancelEvent(m_sock, sylar::IOManager::WRITE);
}

bool Socket::cancelAccept() 
{
    return IOManager::GetThis()->cancelEvent(m_sock, sylar::IOManager::READ);
}

bool Socket::cancelAll() 
{
    return IOManager::GetThis()->cancelAll(m_sock);
}

void Socket::initSock() 
{
    int val = 1;
    // 给socket设置端口立即复用
    setOption(SOL_SOCKET, SO_REUSEADDR, val);

    // 如果是字节流式socket，再设置无延迟发送
    if (m_type == SOCK_STREAM) 
    {
        // TCP_NODELAY是一个套接字选项，用于控制TCP套接字的延迟行为。
        // 当TCP_NODELAY选项被启用时，即设置为true，就会禁用Nagle算
        // 法，从而实现TCP套接字的无延迟传输。这意味着每次发送数据时
        // 都会立即发送，不会等待缓冲区的填充或等待确认
        setOption(IPPROTO_TCP, TCP_NODELAY, val);
    }
}

void Socket::newSock() 
{
    // 这个socket就是最原始的那个，通过这个函数返回一个fd
    m_sock = socket(m_family, m_type, m_protocol);
    if (SYLAR_LIKELY(m_sock != -1)) 
    {
        // 设置端口复用，无延迟发送
        initSock();
    } 
    else 
    {
        SYLAR_LOG_ERROR(g_logger) << "socket(" << m_family
                                  << ", " << m_type << ", " << m_protocol << ") errno="
                                  << errno << " errstr=" << strerror(errno);
    }
}

std::ostream &operator<<(std::ostream &os, const Socket &sock) 
{
    return sock.dump(os);
}

} // namespace sylar
