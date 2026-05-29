#include "../../include/middleware/MetricsMiddleware.h"
#include "../../include/utils/MetricsCollector.h"
#include "../../include/utils/LogUtil.h"

#include <muduo/base/Timestamp.h>

namespace http {
namespace middleware {

namespace {
thread_local std::string t_path;
thread_local muduo::Timestamp t_start;
}

void MetricsMiddleware::before(HttpRequest &request)
{
    t_path = request.path();
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

    MetricsCollector::instance().record(t_path, latency_us, is_error);

    LOG_ACCESS("{} {} {}us",
               t_path,
               static_cast<int>(response.getStatusCode()),
               latency_us);

    t_start = muduo::Timestamp();  // invalidate
}

} // namespace middleware
} // namespace http
