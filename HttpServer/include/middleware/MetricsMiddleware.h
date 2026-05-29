#pragma once

#include "../middleware/Middleware.h"

#include <muduo/base/Timestamp.h>
#include <string>

namespace http {
namespace middleware {

class MetricsMiddleware : public Middleware
{
public:
    void before(HttpRequest &request) override;
    void after(HttpResponse &response) override;
};

} // namespace middleware
} // namespace http
