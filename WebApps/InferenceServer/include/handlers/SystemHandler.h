#pragma once

#include "../../../../HttpServer/include/router/RouterHandler.h"

class InferenceServer;

class SystemHandler : public http::router::RouterHandler
{
public:
    explicit SystemHandler(InferenceServer* server) : server_(server) {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;

private:
    void handleGetHardware(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleApplyConfig(const http::HttpRequest& req, http::HttpResponse* resp);
    void handleRestart(const http::HttpRequest& req, http::HttpResponse* resp);

    InferenceServer* server_;
};
