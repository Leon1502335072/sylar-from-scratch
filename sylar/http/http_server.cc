#include "http_server.h"
#include "../log.h"
//#include "servlets/config_servlet.h"
//#include "servlets/status_servlet.h"

namespace sylar {
namespace http {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

HttpServer::HttpServer(bool keepalive
                        ,sylar::IOManager* worker
                        ,sylar::IOManager* io_worker
                        ,sylar::IOManager* accept_worker)
                        :TcpServer(io_worker, accept_worker)
                        ,m_isKeepalive(keepalive) 
{
    m_dispatch.reset(new ServletDispatch);
    m_type = "http";
    //m_dispatch->addServlet("/_/status", Servlet::ptr(new StatusServlet));
    //m_dispatch->addServlet("/_/config", Servlet::ptr(new ConfigServlet));
}

void HttpServer::setName(const std::string& v) 
{
    TcpServer::setName(v);
    m_dispatch->setDefault(std::make_shared<NotFoundServlet>(v));
}

// 服务端在accept之后会添加一个协程任务就是handleclient
void HttpServer::handleClient(Socket::ptr client) 
{
    SYLAR_LOG_DEBUG(g_logger) << "handleClient: " << *client;
    // 创建一个会话session
    HttpSession::ptr session(new HttpSession(client));
    do {
        // 1、接收并解析请求
        auto req = session->recvRequest();
        if(!req) 
        {
            SYLAR_LOG_DEBUG(g_logger) << "recv http request fail, errno="
                << errno << " errstr=" << strerror(errno)
                << " cliet:" << *client << " keep_alive=" << m_isKeepalive;
            break;
        }

        // 2、返回响应
        HttpResponse::ptr rsp(new HttpResponse(req->getVersion(), req->isClose() || !m_isKeepalive));
        rsp->setHeader("Server", getName());
        m_dispatch->handle(req, rsp, session);
        // 3、发送到socket缓冲区
        session->sendResponse(rsp);

        if(!m_isKeepalive || req->isClose()) 
        {
            // SYLAR_LOG_INFO(g_logger) << "here break";
            // SYLAR_LOG_INFO(g_logger) << "m_isKeepalive: " << m_isKeepalive
            //                          << " req->isClose(): " << req->isClose();
            break;
        }

    } while(true);

    session->close();
}

}
}
