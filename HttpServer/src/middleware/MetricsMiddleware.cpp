#include "../../include/middleware/MetricsMiddleware.h"
#include "../../include/utils/MetricsCollector.h"
#include "../../include/utils/LogUtil.h"

#include <muduo/base/Timestamp.h>

namespace http {
namespace middleware {

namespace {
thread_local std::string t_path;
thread_local std::string t_method;
thread_local muduo::Timestamp t_start;
}

void MetricsMiddleware::before(HttpRequest &request)
{
    t_path = request.path();
    t_method = request.methodString();
    t_start = request.receiveTime();
    (void)request;
}

void MetricsMiddleware::after(HttpResponse &response)
{
    if (!t_start.valid())
        return;

    int64_t latency_us = muduo::Timestamp::now().microSecondsSinceEpoch() -
                         t_start.microSecondsSinceEpoch();
    bool is_error = (static_cast<int>(response.getStatusCode()) >= 400);

    MetricsCollector::instance().record(t_path, t_method, latency_us, is_error);

    // Structured JSON access log — fmt::format via spdlog avoids nlohmann::json
    // object construction + dump() per request on the hot path.
    LOG_ACCESS(
        R"({{"request_id":"{}","method":"{}","path":"{}","status":{},"latency_ms":{:.1f},"client_ip":"{}"}})",
        response.getRequestId(), t_method, t_path,
        static_cast<int>(response.getStatusCode()),
        latency_us / 1000.0, response.getClientIp());

    t_start = muduo::Timestamp();  // invalidate
}

} // namespace middleware
} // namespace http
