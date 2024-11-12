#include "address.h"
#include "log.h"
#include <sstream>
#include <netdb.h>
#include <ifaddrs.h>
#include <stddef.h>

#include "endian.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

// 此次封装的IPv4，IPv6的地址结构体存的都是大端序，也就是说在一开始创建的时候内部的逻辑就把他转化成大端序了
// 如果用于网络通信可直接使用，如果要输出的话需要再转回来，此逻辑在insert()内实现

// 获取可变位全为1的值 也就是主机位全为1 192.168.200.130/24 这里的bits=24，然后得到uint_32类型的255 
template <class T>
static T CreateMask(uint32_t bits) 
{
    // sizeof返回的是字节数 1右移多少位
    return (1 << (sizeof(T) * 8 - bits)) - 1;
}

template <class T>
static uint32_t CountBytes(T value) 
{
    uint32_t result = 0;
    for (; value; ++result) 
    {
        value &= value - 1;
    }
    return result;
}

Address::ptr Address::LookupAny(const std::string &host, int family, int type, int protocol)                                
{
    std::vector<Address::ptr> result;
    if (Lookup(result, host, family, type, protocol)) 
    {
        return result[0];
    }
    return nullptr;
}

IPAddress::ptr Address::LookupAnyIPAddress(const std::string &host, int family, int type, int protocol) 
{
                                           
    std::vector<Address::ptr> result;
    if (Lookup(result, host, family, type, protocol)) 
    {
        //for(auto& i : result) {
        //    std::cout << i->toString() << std::endl;
        //}
        for (auto &i : result) 
        {
            IPAddress::ptr v = std::dynamic_pointer_cast<IPAddress>(i);
            if (v) 
            {
                return v;
            }
        }
    }
    return nullptr;
}

bool Address::Lookup(std::vector<Address::ptr> &result, const std::string &host,
                     int family, int type, int protocol)
{
    addrinfo hints, *results, *next;
    hints.ai_flags     = 0;
    hints.ai_family    = family;
    hints.ai_socktype  = type;
    hints.ai_protocol  = protocol;
    hints.ai_addrlen   = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr      = NULL;
    hints.ai_next      = NULL;

    std::string node;
    const char *service = NULL;

    //检查 ipv6address serivce
    if (!host.empty() && host[0] == '[') 
    {
        // void* memchr(const void* buf,int ch,size_t count)
        // 从buf所指内存区的前count个字节查找字符ch，当第一次遇到字符ch时停止查找。如果成功，返回指向字符ch的指针；否则返回null
        // [fe80::52c8:3387:5111:45c5]
        const char *endipv6 = (const char *)memchr(host.c_str() + 1, ']', host.size() - 1);
        if (endipv6) 
        {
            //TODO check out of range
            if (*(endipv6 + 1) == ':') 
            {
                // 
                service = endipv6 + 2;
            }
            node = host.substr(1, endipv6 - host.c_str() - 1);
        }
    }

    //检查 node serivce 拿到端口号放到node中
    if (node.empty()) 
    {
        service = (const char *)memchr(host.c_str(), ':', host.size());
        if (service) 
        {
            if (!memchr(service + 1, ':', host.c_str() + host.size() - service - 1)) 
            {
                node = host.substr(0, service - host.c_str());
                ++service;
            }
        }
    }

    if (node.empty()) 
    {
        node = host;
    }
    int error = getaddrinfo(node.c_str(), service, &hints, &results);
    if (error) 
    {
        SYLAR_LOG_DEBUG(g_logger) << "Address::Lookup getaddress(" << host << ", "
                                  << family << ", " << type << ") err=" << error << " errstr="
                                  << gai_strerror(error);
        return false;
    }

    next = results;
    while (next) 
    {
        result.push_back(Create(next->ai_addr, (socklen_t)next->ai_addrlen));
        /// 一个ip/端口可以对应多种接字类型，比如SOCK_STREAM, SOCK_DGRAM, SOCK_RAW，所以这里会返回重复的结果
        SYLAR_LOG_DEBUG(g_logger) << "family:" << next->ai_family << ", sock type:" << next->ai_socktype;
        next = next->ai_next;
    }

    freeaddrinfo(results);
    return !result.empty();
}

// ‌getifaddrs函数用于获取系统中所有网络接口的信息，包括接口名称、地址、子网掩码等。‌ 
// 该函数通过创建一个链表来存储所有网络接口的信息，每个节点是一个struct ifaddrs结构体，
// 包含了接口的详细信息。成功时，函数返回0；失败时返回-1，并设置errno以指示错误类型‌

bool Address::GetInterfaceAddresses(std::multimap<std::string, std::pair<Address::ptr, uint32_t>> &result, int family) 
{
    struct ifaddrs *next, *results;
    if (getifaddrs(&results) != 0) 
    {
        SYLAR_LOG_DEBUG(g_logger) << "Address::GetInterfaceAddresses getifaddrs "
                                     " err="
                                  << errno << " errstr=" << strerror(errno);
        return false;
    }

    try 
    {
        for (next = results; next; next = next->ifa_next) 
        {
            Address::ptr addr;
            uint32_t prefix_len = ~0u;
            if (family != AF_UNSPEC && family != next->ifa_addr->sa_family) 
            {
                continue;
            }
            switch (next->ifa_addr->sa_family) 
            {
                case AF_INET: 
                {
                    addr             = Create(next->ifa_addr, sizeof(sockaddr_in));
                    uint32_t netmask = ((sockaddr_in *)next->ifa_netmask)->sin_addr.s_addr;
                    prefix_len       = CountBytes(netmask);
                } break;
                case AF_INET6: 
                {
                    addr              = Create(next->ifa_addr, sizeof(sockaddr_in6));
                    in6_addr &netmask = ((sockaddr_in6 *)next->ifa_netmask)->sin6_addr;
                    prefix_len        = 0;
                    for (int i = 0; i < 16; ++i) 
                    {
                        prefix_len += CountBytes(netmask.s6_addr[i]);
                    }
                } break;
                default:
                    break;
            }

            if (addr) 
            {
                result.insert(std::make_pair(next->ifa_name,
                                             std::make_pair(addr, prefix_len)));
            }
        }
    } 
    catch (...) 
    {
        SYLAR_LOG_ERROR(g_logger) << "Address::GetInterfaceAddresses exception";
        freeifaddrs(results);
        return false;
    }
    freeifaddrs(results);
    return !result.empty();
}

bool Address::GetInterfaceAddresses(std::vector<std::pair<Address::ptr, uint32_t>> &result, const std::string &iface, int family) 
{
    if (iface.empty() || iface == "*") 
    {
        if (family == AF_INET || family == AF_UNSPEC) 
        {
            result.push_back(std::make_pair(Address::ptr(new IPv4Address()), 0u));
        }
        if (family == AF_INET6 || family == AF_UNSPEC) 
        {
            result.push_back(std::make_pair(Address::ptr(new IPv6Address()), 0u));
        }
        return true;
    }

    std::multimap<std::string, std::pair<Address::ptr, uint32_t>> results;

    // 获取本地所有的网络接口（也就是网卡）
    if (!GetInterfaceAddresses(results, family)) 
    {
        return false;
    }

    // 筛选符合条件的网络接口
    auto its = results.equal_range(iface);
    for (; its.first != its.second; ++its.first) 
    {
        result.push_back(its.first->second);
    }
    return !result.empty();
}

int Address::getFamily() const 
{
    return getAddr()->sa_family;
}

std::string Address::toString() const
{
    std::stringstream ss;
    insert(ss);
    return ss.str();
}

Address::ptr Address::Create(const sockaddr *addr, socklen_t addrlen) 
{
    if (addr == nullptr) 
    {
        return nullptr;
    }

    Address::ptr result;
    switch (addr->sa_family) 
    {
        case AF_INET:
            result.reset(new IPv4Address(*(const sockaddr_in *)addr));
            break;
        case AF_INET6:
            result.reset(new IPv6Address(*(const sockaddr_in6 *)addr));
            break;
        default:
            result.reset(new UnknownAddress(*addr));
            break;
    }
    return result;
}

// 重载小于号 <
bool Address::operator < (const Address &rhs) const 
{
    socklen_t minlen = std::min(getAddrLen(), rhs.getAddrLen());
    int result       = memcmp(getAddr(), rhs.getAddr(), minlen);
    if (result < 0) 
    {
        return true;
    } 
    else if (result > 0) 
    {
        return false;
    } 
    else if (getAddrLen() < rhs.getAddrLen()) 
    {
        return true;
    }
    return false;
}
// 重载小于号 ==
bool Address::operator == (const Address &rhs) const 
{
    return getAddrLen() == rhs.getAddrLen() && memcmp(getAddr(), rhs.getAddr(), getAddrLen()) == 0;
}

// 重载小于号 !=
bool Address::operator != (const Address &rhs) const 
{
    return !(*this == rhs);
}


/*
包含服务提供商地址信息的结构。
struct addrinfo 
{
    int ai_flags;              标志，可选：AI_PASSIVE, AI_CANONNAME 
    int ai_family;             协议簇
    int ai_socktype;           socket类型：字节流，数据报  
    int ai_protocol;           协议（IPv4 or IPv6） 
    socklen_t ai_addrlen;      ai_addr地址的长度 
    char *ai_canonname;        该主机对应的标准名称 
    struct sockaddr *ai_addr;  该结构体对应的一个网络地址 
    struct addrinfo *ai_next;  指向下一个addrinfo结构体的指针 
}; 
*/

IPAddress::ptr IPAddress::Create(const char *address, uint16_t port) 
{
    addrinfo hints, *results;
    memset(&hints, 0, sizeof(addrinfo));

    hints.ai_flags  = AI_NUMERICHOST;  // AI_NUMERICHOST该宏表示address参数只能是数字化的地址字符串，不能是域名
    hints.ai_family = AF_UNSPEC;       // AF_UNSPEC该宏表示未确定的协议簇

    // getaddrinfo解析网址，返回IP地址,addrinfo的结构（列表）指针 返回的地址都是网络字节序（大端）
    int error = getaddrinfo(address, NULL, &hints, &results);
    if (error) 
    {
        SYLAR_LOG_DEBUG(g_logger) << "IPAddress::Create(" << address
                                  << ", " << port << ") error=" << error
                                  << " errno=" << errno << " errstr=" << strerror(errno);
        return nullptr;
    }

    try 
    {
        // dynamic_pointer_cast => 父类型的智能指针 Address::ptr 转化成子类型的智能指针 IPAddress::ptr
        IPAddress::ptr result = std::dynamic_pointer_cast<IPAddress>(
            Address::Create(results->ai_addr, (socklen_t)results->ai_addrlen));
        if (result) 
        {
            result->setPort(port);
        }
        // 释放空间
        freeaddrinfo(results);
        return result;
    } 
    catch (...) 
    {
        freeaddrinfo(results);
        return nullptr;
    }
}

// byteswapOnLittleEndian函数的作用是将一个32位数从主机字节顺序转换成网络字节顺序 相当于htonl()
// 主机一般是小端，网络一般是大端
IPv4Address::ptr IPv4Address::Create(const char *address, uint16_t port) 
{
    IPv4Address::ptr rt(new IPv4Address);
    rt->m_addr.sin_port = byteswapOnLittleEndian(port);
    // inet_pton可将字符串类型的ip地址以网络字节序（也就是大端）存储在第三个参数中，所以这点不用再调byteswapOnLittleEndian
    int result = inet_pton(AF_INET, address, &rt->m_addr.sin_addr);
    if (result <= 0) 
    {
        SYLAR_LOG_DEBUG(g_logger) << "IPv4Address::Create(" << address << ", "
                                  << port << ") rt=" << result << " errno=" << errno
                                  << " errstr=" << strerror(errno);
        return nullptr;
    }
    return rt;
}

// 构造一
IPv4Address::IPv4Address(const sockaddr_in &address) 
{
    m_addr = address;
}

// 构造二 
IPv4Address::IPv4Address(uint32_t address, uint16_t port) 
{
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin_family      = AF_INET; 
    m_addr.sin_port        = byteswapOnLittleEndian(port); 
    m_addr.sin_addr.s_addr = byteswapOnLittleEndian(address);
}

sockaddr *IPv4Address::getAddr() 
{
    return (sockaddr *)&m_addr;
}

const sockaddr *IPv4Address::getAddr() const 
{
    return (sockaddr *)&m_addr;
}

socklen_t IPv4Address::getAddrLen() const 
{
    return sizeof(m_addr);
}

std::ostream &IPv4Address::insert(std::ostream &os) const 
{
    // 创建出来的IPv4已经是大端了 需要转回来
    uint32_t addr = byteswapOnLittleEndian(m_addr.sin_addr.s_addr);
    os << ((addr >> 24) & 0xff) << "."
       << ((addr >> 16) & 0xff) << "."
       << ((addr >> 8) & 0xff) << "."
       << (addr & 0xff);
    os << ":" << byteswapOnLittleEndian(m_addr.sin_port);
    return os;
}

IPAddress::ptr IPv4Address::broadcastAddress(uint32_t prefix_len) 
{
    if (prefix_len > 32) 
    {
        return nullptr;
    }

    sockaddr_in baddr(m_addr);
    baddr.sin_addr.s_addr |= byteswapOnLittleEndian(
        CreateMask<uint32_t>(prefix_len));
    return IPv4Address::ptr(new IPv4Address(baddr));
}

IPAddress::ptr IPv4Address::networkAddress(uint32_t prefix_len) 
{
    if (prefix_len > 32) 
    {
        return nullptr;
    }

    sockaddr_in baddr(m_addr);
    baddr.sin_addr.s_addr &= byteswapOnLittleEndian(
        ~CreateMask<uint32_t>(prefix_len));
    return IPv4Address::ptr(new IPv4Address(baddr));
}

IPAddress::ptr IPv4Address::subnetMask(uint32_t prefix_len) 
{
    sockaddr_in subnet;
    memset(&subnet, 0, sizeof(subnet));
    subnet.sin_family      = AF_INET;
    // 得到全为1的主机位 先转大端在取反 在输出的时候
    subnet.sin_addr.s_addr = ~byteswapOnLittleEndian(CreateMask<uint32_t>(prefix_len));
    return IPv4Address::ptr(new IPv4Address(subnet));
}

uint32_t IPv4Address::getPort() const 
{
    return byteswapOnLittleEndian(m_addr.sin_port);
}

void IPv4Address::setPort(uint16_t v) 
{
    m_addr.sin_port = byteswapOnLittleEndian(v);
}

IPv6Address::ptr IPv6Address::Create(const char *address, uint16_t port)
{
    IPv6Address::ptr rt(new IPv6Address);
    rt->m_addr.sin6_port = byteswapOnLittleEndian(port);
    // inet_pton直接转大端并存储在&rt->m_addr.sin6_addr中
    int result = inet_pton(AF_INET6, address, &rt->m_addr.sin6_addr);
    if (result <= 0) 
    {
        SYLAR_LOG_DEBUG(g_logger) << "IPv6Address::Create(" << address << ", "
                                  << port << ") rt=" << result << " errno=" << errno
                                  << " errstr=" << strerror(errno);
        return nullptr;
    }
    return rt;
}

// 注意：IPv6在构造之初只有端口转成了大端在m_addr里存着，地址转大端是在IPv6的create函数中用inet_pton做的

// 构造一
IPv6Address::IPv6Address() 
{
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin6_family = AF_INET6;
}

// 构造二
IPv6Address::IPv6Address(const sockaddr_in6 &address) 
{
    m_addr = address;
}

//构造三
IPv6Address::IPv6Address(const uint8_t address[16], uint16_t port) 
{
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin6_family = AF_INET6;
    m_addr.sin6_port   = byteswapOnLittleEndian(port);
    memcpy(&m_addr.sin6_addr.s6_addr, address, 16);
}

sockaddr *IPv6Address::getAddr() 
{
    return (sockaddr *)&m_addr;
}

const sockaddr *IPv6Address::getAddr() const 
{
    return (sockaddr *)&m_addr;
}

socklen_t IPv6Address::getAddrLen() const 
{
    return sizeof(m_addr);
}

//fe80:0000:0000:0000:52c8:3387:5111:45c5  ->  fe80::52c8:3387:5111:45c5(压缩后)

std::ostream &IPv6Address::insert(std::ostream &os) const 
{
    os << "[";
    uint16_t *addr  = (uint16_t *)m_addr.sin6_addr.s6_addr;
    bool used_zeros = false;
    for (size_t i = 0; i < 8; ++i) 
    {
        if (addr[i] == 0 && !used_zeros) 
        {
            continue;
        }
        if (i && addr[i - 1] == 0 && !used_zeros) 
        {
            os << ":";
            used_zeros = true;
        }
        if (i) 
        {
            os << ":";
        }
        // 转16进制 hex 16进制 dec 10进制
        os << std::hex << (int)byteswapOnLittleEndian(addr[i]) << std::dec;
    }

    if (!used_zeros && addr[7] == 0) 
    {
        os << "::";
    }

    os << "]:" << byteswapOnLittleEndian(m_addr.sin6_port);
    return os;
}

// 注意 sizeof(uint8_t)=1 所以上面的createMask返回的是2^8 - 1 = 255 (11111111)

// 低prefix_len位全为1就是组播 假如prefix_len=64 那么低64位全为1就行了
IPAddress::ptr IPv6Address::broadcastAddress(uint32_t prefix_len) 
{
    sockaddr_in6 baddr(m_addr);
    baddr.sin6_addr.s6_addr[prefix_len / 8] |=
        CreateMask<uint8_t>(prefix_len % 8);
    for (int i = prefix_len / 8 + 1; i < 16; ++i) 
    {
        baddr.sin6_addr.s6_addr[i] = 0xff;
    }
    return IPv6Address::ptr(new IPv6Address(baddr));
}

IPAddress::ptr IPv6Address::networkAddress(uint32_t prefix_len) 
{
    sockaddr_in6 baddr(m_addr);
    baddr.sin6_addr.s6_addr[prefix_len / 8] &=
        CreateMask<uint8_t>(prefix_len % 8);
    for (int i = prefix_len / 8 + 1; i < 16; ++i) 
    {
        baddr.sin6_addr.s6_addr[i] = 0x00;
    }
    return IPv6Address::ptr(new IPv6Address(baddr));
}


IPAddress::ptr IPv6Address::subnetMask(uint32_t prefix_len) 
{
    sockaddr_in6 subnet;
    memset(&subnet, 0, sizeof(subnet));
    subnet.sin6_family = AF_INET6;
    subnet.sin6_addr.s6_addr[prefix_len / 8] =
        ~CreateMask<uint8_t>(prefix_len % 8);

    for (uint32_t i = 0; i < prefix_len / 8; ++i) 
    {
        subnet.sin6_addr.s6_addr[i] = 0xff;
    }
    return IPv6Address::ptr(new IPv6Address(subnet));
}

uint32_t IPv6Address::getPort() const 
{
    return byteswapOnLittleEndian(m_addr.sin6_port);
}

void IPv6Address::setPort(uint16_t v) 
{
    m_addr.sin6_port = byteswapOnLittleEndian(v);
}

// 现将0转成sockaddr_un *类型的指针，再取成员，再sizeof 再-1 最终大小为127
static const size_t MAX_PATH_LEN = sizeof(((sockaddr_un *)0)->sun_path) - 1;

UnixAddress::UnixAddress() 
{
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sun_family = AF_UNIX;
    // 宏函数 offsetof(sockaddr_un, sun_path)得到sockaddr_un结构体中 sun_path的偏移量 + sun_path的最大存储长度
    // offsetof(sockaddr_un, sun_path) = 2
    m_length = offsetof(sockaddr_un, sun_path) + MAX_PATH_LEN;
}

UnixAddress::UnixAddress(const std::string &path) 
{
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sun_family = AF_UNIX;
    m_length          = path.size() + 1;

    // '\0'字符串的结尾标志
    if (!path.empty() && path[0] == '\0') 
    {
        --m_length;
    }

    if (m_length > sizeof(m_addr.sun_path)) 
    {
        throw std::logic_error("path too long");
    }
    memcpy(m_addr.sun_path, path.c_str(), m_length);
    m_length += offsetof(sockaddr_un, sun_path);
}

void UnixAddress::setAddrLen(uint32_t v) 
{
    m_length = v;
}

sockaddr *UnixAddress::getAddr() 
{
    return (sockaddr *)&m_addr;
}

const sockaddr *UnixAddress::getAddr() const 
{
    return (sockaddr *)&m_addr;
}

socklen_t UnixAddress::getAddrLen() const 
{
    return m_length;
}

std::string UnixAddress::getPath() const 
{
    std::stringstream ss;
    if (m_length > offsetof(sockaddr_un, sun_path) && m_addr.sun_path[0] == '\0') 
    {
        ss << "\\0" << std::string(m_addr.sun_path + 1, m_length - offsetof(sockaddr_un, sun_path) - 1);
    } 
    else 
    {
        ss << m_addr.sun_path;
    }
    return ss.str();
}

std::ostream &UnixAddress::insert(std::ostream &os) const 
{
    if (m_length > offsetof(sockaddr_un, sun_path) && m_addr.sun_path[0] == '\0') 
    {
        return os << "\\0" << std::string(m_addr.sun_path + 1, m_length - offsetof(sockaddr_un, sun_path) - 1);
    }
    return os << m_addr.sun_path;
}

UnknownAddress::UnknownAddress(int family) 
{
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sa_family = family;
}

UnknownAddress::UnknownAddress(const sockaddr &addr) 
{
    m_addr = addr;
}

sockaddr *UnknownAddress::getAddr() 
{
    return (sockaddr *)&m_addr;
}

const sockaddr *UnknownAddress::getAddr() const 
{
    return &m_addr;
}

socklen_t UnknownAddress::getAddrLen() const 
{
    return sizeof(m_addr);
}

std::ostream &UnknownAddress::insert(std::ostream &os) const 
{
    os << "[UnknownAddress family=" << m_addr.sa_family << "]";
    return os;
}

std::ostream &operator << (std::ostream &os, const Address &addr) 
{
    return addr.insert(os);
}

} // namespace sylar
