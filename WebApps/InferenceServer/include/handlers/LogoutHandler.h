#pragma once
#include "../../../../HttpServer/include/router/RouterHandler.h"
#include "../InferenceServer.h"
#include "../../../HttpServer/include/utils/JsonUtil.h"

class LogoutHandler : public http::router::RouterHandler 
{
public:
    explicit LogoutHandler(InferenceServer* server) : server_(server) {}
    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;
private:
    InferenceServer* server_;
};