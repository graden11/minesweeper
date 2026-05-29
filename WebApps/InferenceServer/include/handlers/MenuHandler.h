#pragma once

#include "../../../HttpServer/include/router/RouterHandler.h"
#include "../InferenceServer.h"


class MenuHandler : public http::router::RouterHandler
{
public:
    explicit MenuHandler(InferenceServer* server) : server_(server) {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;
private:
    InferenceServer* server_;
};