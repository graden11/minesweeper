#include "HttpServer.h"

#include <any>

// 默认http回应函数
void defaultHttpCallback(const HttpRequest&, HttpResponse* resp)
{
    resp->setStatusCode(HttpResponse::k404NotFound);
    resp->setStatusMessage("Not Found");
    resp->setCloseConnection(true);
}

HttpServer::HttpServer(const muduo::net::InetAddress& listenAddr,
                       const std::string& name,
                       muduo::net::TcpServer::Option option)
    : server_(&mainLoop_, listenAddr, name, option)
    , httpCallback_(std::bind(&HttpServer::onHttpProcess, this, std::placeholders::_1, std::placeholders::_2))
{
    // 设置回调函数
    server_.setConnectionCallback(
        std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback( 
        std::bind(&HttpServer::onMessage, this, 
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3));
} 

// 服务器运行函数
void HttpServer::start()
{
    LOG_WARN << "HttpServer[" << server_.name() << "] starts listening on" << server_.ipPort();
    server_.start();
    mainLoop_.loop();
}

void HttpServer::onConnection(const muduo::net::TcpConnectionPtr& conn)
{   
    // 给连接设置一个HttpContext对象用于解析请求报文，提取封装请求信息
    if (conn->connected())
    {   
        conn->setContext(HttpContext()); 
    }
}

void HttpServer::onMessage(const muduo::net::TcpConnectionPtr& conn,
                           muduo::net::Buffer* buf,
                           muduo::Timestamp receiveTime)
{
   // HttpContext对象用于解析出buf中的请求报文，并把报文的关键信息封装到HttpRequest对象中
    HttpContext* context = boost::any_cast<HttpContext>(conn->getMutableContext());
    if (!context->parseRequest(buf, receiveTime)) // 解析一个http请求
    {   
        // 如果解析http报文过程中出错
        conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
        conn->shutdown();
    }
    
    // 如果buf缓冲区中解析出一个完整的数据包才封装响应报文
    if (context->gotAll())
    {
        onRequest(conn, context->request());
        context->reset(); 
    }                 
}

void HttpServer::onRequest(const muduo::net::TcpConnectionPtr& conn, const HttpRequest& req)
{
    const std::string& connection = req.getHeader("Connection");
    bool close = ((connection == "close") ||
     (req.getVersion() == "HTTP/1.0" && connection != "Keep-Alive"));
    HttpResponse response(close);
    
    // 根据请求报文信息来封装响应报文对象
    httpCallback_(req, &response);

    muduo::net::Buffer buf;
    response.appendToBuffer(&buf);
    conn->send(&buf);
    
    // 如果是短连接的话，返回响应报文后就断开连接
    if (response.closeConnection())
    {
        conn->shutdown();
    }
}

void HttpServer::onHttpProcess(const HttpRequest& req, HttpResponse* resp)
{   
    std::string reqResource;   
    if (req.method() == HttpRequest::kGet)
    {
        // 判断请求哪个html网页
        if(req.path() == "/login.html")
        {
            reqResource.append("resource/login.html");
        }
        else if (req.path() == "/" || req.path() == "/register.html")
        {
            reqResource.append("resource/register.html");
        }
        else
        {
            reqResource.append("resource");
            reqResource.append(req.path().c_str());
        } 
    }
    else if (req.method() == HttpRequest::kPost)
    {
        if (!req.getBody().empty())
        {
            // 请求体，默认是账号密码, 格式：username=xxx&password=xxx
            std::string username = req.getBody()
            .substr(req.getBody().find("=") + 1, req.getBody().find("&"));
            
            std::string password = req.getBody()
            .substr(req.getBody().find("&") + 1)
            .substr(req.getBody().find("=") + 1); 
            
            // 判断是登录还是注册
            if (req.path() == "/login.cgi")
            {
                // 判断是否存有账号密码
                if(users_.find(username) != users_.end())
                {
                    if (users_[username] == password) // 用户名密码正确
                    {
                        reqResource.append("resource/index.html");
                        
                    }
                    else 
                    {
                        reqResource.append("resource/loginError.html");
                    }
                }
                else
                {
                    reqResource.append("resource/loginError.html");
                }
            }
            else if (req.path() == "/register.cgi")
            {
                // 注册，添加账号密码，判断是否已经存在
                if (users_.find(username) != users_.end())
                {
                    reqResource.append("resource/registerError.html");
                }
                else
                {
                    users_.insert(std::make_pair(username, password));
                    reqResource.append("resource/login.html");
                }
            }
        }
    }
    
    packagingResp(req, resp, reqResource);
}

void HttpServer::packagingResp(const HttpRequest& req, HttpResponse* resp,
                   const std::string& reqResource)
{
    FileOperater fileOperater(reqResource);
    if (!fileOperater.isValid()) 
    {
        LOG_WARN << reqResource << " no such file";
        fileOperater.resetDefaultFile();
    }
    
    std::vector<char> buffer(fileOperater.size());
    fileOperater.readFile(buffer); // 读出文件数据
    
    // 封装响应报文
    resp->setVersion(req.getVersion());
    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setStatusMessage("OK");
    resp->setContentLength(fileOperater.size());
    resp->setContentType("text/html");
    resp->setBody(std::string(buffer.data(), buffer.size()));
}