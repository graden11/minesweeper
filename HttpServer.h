#pragma once 

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <memory>
#include <unordered_map>

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/base/Logging.h>

#include "FileOperater.h"
#include "HttpContext.h"
#include "HttpRequest.h"
#include "HttpResponse.h"

class HttpRequest;
class HttpResponse;

class HttpServer : muduo::noncopyable
{
public:
    using HttpCallback = std::function<void (const HttpRequest&, HttpResponse*)>;

    HttpServer(const muduo::net::InetAddress& listenAddr,
               const std::string& name,
               muduo::net::TcpServer::Option option = muduo::net::TcpServer::kNoReusePort);
    
    muduo::net::EventLoop* getLoop() const 
    { 
        return server_.getLoop(); 
    }

    void setHttpCallback(const HttpCallback& cb)
    {
        httpCallback_ = cb;
    }

    void setThreadNum(int numThreads)
    {
        server_.setThreadNum(numThreads);
    }

    void start();
private:
    void onConnection(const muduo::net::TcpConnectionPtr& conn);
    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,
                   muduo::Timestamp receiveTime);
    void onRequest(const muduo::net::TcpConnectionPtr&, const HttpRequest&);
    void onHttpProcess(const HttpRequest&, HttpResponse* resp);

    void packagingResp(const HttpRequest& req, HttpResponse* resp,const std::string& reqResource);
    
private:
    muduo::net::TcpServer                        server_;
    muduo::net::EventLoop                        mainLoop_;
    HttpCallback                                 httpCallback_;   
    std::unordered_map<std::string, std::string> users_;
}; 


