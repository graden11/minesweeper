#include "../../include/handlers/MetricsHandler.h"
#include "../../../HttpServer/include/utils/MetricsCollector.h"

void MetricsHandler::handle(const http::HttpRequest &req, http::HttpResponse *resp)
{
    auto json = MetricsCollector::instance().toJson();
    std::string body = json.dump(2);

    resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
    resp->setContentType("application/json");
    resp->setContentLength(body.size());
    resp->setBody(body);
    resp->setCloseConnection(false);
}
