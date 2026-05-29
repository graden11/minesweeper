#pragma once
#include "../../../../HttpServer/include/router/RouterHandler.h"
#include "../InferenceServer.h"

class EntryHandler : public http::router::RouterHandler 
{
public:
    explicit EntryHandler(InferenceServer* server) : server_(server) {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;

private:
    InferenceServer* server_;
};