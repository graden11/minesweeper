#pragma once

#include "../../../../HttpServer/include/router/RouterHandler.h"
#include "../../../../HttpServer/include/utils/JsonUtil.h"

class ModelFactory;

class PredictHandler : public http::router::RouterHandler
{
public:
    explicit PredictHandler(ModelFactory* factory) : factory_(factory) {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;

private:
    ModelFactory* factory_;
};
