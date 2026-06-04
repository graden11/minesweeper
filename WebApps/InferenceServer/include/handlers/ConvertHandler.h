#pragma once

#include "../../../../HttpServer/include/router/RouterHandler.h"
#include "../../../../HttpServer/include/utils/JsonUtil.h"

class InferenceServer;

class ConvertHandler : public http::router::RouterHandler
{
public:
    explicit ConvertHandler(InferenceServer* server) : server_(server) {}
    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;

private:
    InferenceServer* server_;

    void handleConvert(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleStatus(const http::HttpRequest& req, http::HttpResponse* resp);
};
