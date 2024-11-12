/**
 * @file uri.cc
 * @brief URI封装实现，基于nodejs/http-parser
 * @version 0.1
 * @date 2021-11-14
 */

#include "uri.h"
#include "http/http_parser.h"
#include <sstream>

namespace sylar {

Uri::ptr Uri::Create(const std::string &urlstr) 
{
    // 创建一个uri，端口默认值为0
    Uri::ptr uri(new Uri);
    struct http_parser_url parser;

    http_parser_url_init(&parser);

    if (http_parser_parse_url(urlstr.c_str(), urlstr.length(), 0, &parser) != 0) 
    {
        return nullptr;
    }

    // 最后一位是scheme的校验位
    if (parser.field_set & (1 << UF_SCHEMA)) 
    {
        // off是scheme在字符串中的偏移量，len是长度
        uri->setScheme(
            std::string(urlstr.c_str() + parser.field_data[UF_SCHEMA].off,
                        parser.field_data[UF_SCHEMA].len));
    }

    // 倒数第七位是userinfo的校验位
    if (parser.field_set & (1 << UF_USERINFO)) 
    {
        uri->setUserinfo(
            std::string(urlstr.c_str() + parser.field_data[UF_USERINFO].off,
                        parser.field_data[UF_USERINFO].len));
    }
    
    // 倒数第二位是host的校验位
    if (parser.field_set & (1 << UF_HOST)) 
    {
        uri->setHost(
            std::string(urlstr.c_str() + parser.field_data[UF_HOST].off,
                        parser.field_data[UF_HOST].len));
    }

    // 倒数第三位是port的校验位
    if (parser.field_set & (1 << UF_PORT)) 
    {
        uri->setPort(std::stoi(
            std::string(urlstr.c_str() + parser.field_data[UF_PORT].off,
                        parser.field_data[UF_PORT].len)));
    } 
    else 
    {
        //默认端口号解析只支持http/ws/https
        if (uri->getScheme() == "http" || uri->getScheme() == "ws") 
        {
            uri->setPort(80);
        } 
        else if (uri->getScheme() == "https") 
        {
            uri->setPort(443);
        }
    }

    // 倒数第四位是path的校验位
    if (parser.field_set & (1 << UF_PATH)) 
    {
        uri->setPath(
            std::string(urlstr.c_str() + parser.field_data[UF_PATH].off,
                        parser.field_data[UF_PATH].len));
    }

    // 倒数第五位是query的校验位
    if (parser.field_set & (1 << UF_QUERY)) 
    {
        uri->setQuery(
            std::string(urlstr.c_str() + parser.field_data[UF_QUERY].off,
                        parser.field_data[UF_QUERY].len));
    }

    // 倒数第六位是fragment的校验位
    if (parser.field_set & (1 << UF_FRAGMENT)) 
    {
        uri->setFragment(
            std::string(urlstr.c_str() + parser.field_data[UF_FRAGMENT].off,
                        parser.field_data[UF_FRAGMENT].len));
    }

    return uri;
}

Uri::Uri()
    : m_port(0) {}

int32_t Uri::getPort() const 
{
    if (m_port) 
    {
        return m_port;
    }
    if (m_scheme == "http" || m_scheme == "ws") 
    {
        return 80;
    } 
    else if (m_scheme == "https" || m_scheme == "wss") 
    {
        return 443;
    }
    return m_port;
}

const std::string &Uri::getPath() const 
{
    static std::string s_default_path = "/";
    return m_path.empty() ? s_default_path : m_path;
}

bool Uri::isDefaultPort() const 
{
    if (m_scheme == "http" || m_scheme == "ws") 
    {
        return m_port == 80;
    } 
    else if (m_scheme == "https") 
    {
        return m_port == 443;
    } 
    else 
    {
        return false;
    }
}

std::string Uri::toString() const 
{
    std::stringstream ss;

    ss << m_scheme
       << "://"
       << m_userinfo
       << (m_userinfo.empty() ? "" : "@")
       << m_host << (isDefaultPort() ? "" : ":" + std::to_string(m_port))
       << getPath()
       << (m_query.empty() ? "" : "?")
       << m_query
       << (m_fragment.empty() ? "" : "#")
       << m_fragment;

    return ss.str();
}

Address::ptr Uri::createAddress() const 
{
    // LookupAnyIPAddress有默认的协议簇AF_INET IPv4的，即不指定协议簇的话默认创建IPv4类型的地址
    // 通过m_host = www.baidu.com 拿到百度的IP地址并创建一个Address的对象
    auto addr = Address::LookupAnyIPAddress(m_host);
    if(addr) 
    {
        addr->setPort(getPort());
    }
    return addr;
}

} // namespace sylar