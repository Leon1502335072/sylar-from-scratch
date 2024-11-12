#include "http_session.h"
#include "http_parser.h"

namespace sylar {
namespace http {

HttpSession::HttpSession(Socket::ptr sock, bool owner)
    : SocketStream(sock, owner) {
}

HttpRequest::ptr HttpSession::recvRequest() 
{
    // 创建一个解析请求的智能指针
    HttpRequestParser::ptr parser(new HttpRequestParser);
    uint64_t buff_size = HttpRequestParser::GetHttpRequestBufferSize();
    // 用智能指针来管理创建出来的内存
    std::shared_ptr<char> buffer(
        new char[buff_size], [](char *ptr) 
        {
            delete[] ptr;
        });

    // 返回一个裸指针
    char *data = buffer.get();
    // 读取一次的字节数中未解析的字节数
    int offset = 0;

    // 请求报文在socket缓冲区是没法直接进行解析的，需要先读到buffer中，再解析
    do 
    {
        int len = read(data + offset, buff_size - offset);
        if (len <= 0) 
        {
            close();
            return nullptr;
        }
        len += offset;
        // 返回已解析的字节数
        size_t nparse = parser->execute(data, len);
        if (parser->hasError()) 
        {
            close();
            return nullptr;
        }
        offset = len - nparse;
        if (offset == (int)buff_size) 
        {
            close();
            return nullptr;
        }
        // 解析完成
        if (parser->isFinished()) 
        {
            break;
        }
    } while (true);

    // 与sylar的HTTP解析库不一样的是，nodejs/http-parser解析结束时body部分已经解析完了，所以这里不再需要单独读取body
    
    // int64_t length = parser->getContentLength();
    // if (length > 0) {
    //     std::string body;
    //     body.resize(length);

    //     int len = 0;
    //     if (length >= offset) {
    //         memcpy(&body[0], data, offset);
    //         len = offset;
    //     } else {
    //         memcpy(&body[0], data, length);
    //         len = length;
    //     }
    //     length -= offset;
    //     if (length > 0) {
    //         if (readFixSize(&body[len], length) <= 0) {
    //             close();
    //             return nullptr;
    //         }
    //     }
    //     parser->getData()->setBody(body);
    // }
    
    parser->getData()->init();
    return parser->getData();
}

int HttpSession::sendResponse(HttpResponse::ptr rsp) 
{
    std::stringstream ss;
    ss << *rsp;
    std::string data = ss.str();
    // 写到socket缓冲区
    return writeFixSize(data.c_str(), data.size());
}

} // namespace http
} // namespace sylar
