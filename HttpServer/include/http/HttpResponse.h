#pragma once

#include <muduo/net/TcpServer.h>

#include <chrono>
#include <memory>
#include <string>
#include <sstream>
#include <spdlog/spdlog.h>

namespace http
{

// Per-request timing trace.  Stored on HttpResponse and passed into the
// batcher so every checkpoint can record elapsed us since T0.
struct PerfTrace
{
    using Clock = std::chrono::steady_clock;
    using Us    = std::chrono::microseconds;

    Clock::time_point origin;

    // ── Server-side checkpoints T0-T14 (us since origin) ──
    int64_t t0_on_message      = 0;
    int64_t t1_parse_done      = 0;
    int64_t t2_on_request      = 0;
    int64_t t3_middleware_before = 0;
    int64_t t4_handler_enter   = 0;
    int64_t t5_json_parse_done  = 0;
    int64_t t6_base64_decode_done = 0;
    int64_t t7_batcher_submit_done = 0;
    int64_t t8_future_get_begin   = 0;
    int64_t t9_future_get_return  = 0;
    int64_t t10_response_set      = 0;
    int64_t t11_middleware_after   = 0;
    int64_t t12_append_begin       = 0;
    int64_t t13_append_done        = 0;
    int64_t t14_send_return        = 0;

    // ── Batcher-side checkpoints B0-B6 (us since origin) ──
    int64_t b0_enqueue         = 0;
    int64_t b1_pick_first      = 0;
    int64_t b2_batch_collected  = 0;
    int64_t b3_group_dispatch   = 0;
    int64_t b4_predict_begin    = 0;
    int64_t b5_predict_done     = 0;
    int64_t b6_promise_set      = 0;

    std::string endpoint;
    std::string model;

    PerfTrace() { origin = Clock::now(); }

    int64_t nowUs() const {
        return std::chrono::duration_cast<Us>(Clock::now() - origin).count();
    }

    // log sampled trace (1/N) to spdlog
    void dump(int sampleMod = 100) const {
        static std::atomic<size_t> counter{0};
        if (counter.fetch_add(1) % static_cast<size_t>(sampleMod) != 0)
            return;
        std::ostringstream oss;
        oss << "perf_trace endpoint=" << endpoint << " model=" << model << " ";
        oss << "T0=" << t0_on_message
            << " T1=" << t1_parse_done
            << " T2=" << t2_on_request
            << " T3=" << t3_middleware_before
            << " T4=" << t4_handler_enter
            << " T5=" << t5_json_parse_done
            << " T6=" << t6_base64_decode_done
            << " T7=" << t7_batcher_submit_done
            << " T8=" << t8_future_get_begin
            << " T9=" << t9_future_get_return
            << " T10=" << t10_response_set
            << " T11=" << t11_middleware_after
            << " T12=" << t12_append_begin
            << " T13=" << t13_append_done
            << " T14=" << t14_send_return
            << " B0=" << b0_enqueue
            << " B1=" << b1_pick_first
            << " B2=" << b2_batch_collected
            << " B3=" << b3_group_dispatch
            << " B4=" << b4_predict_begin
            << " B5=" << b5_predict_done
            << " B6=" << b6_promise_set;
        spdlog::info("{}", oss.str());
    }
};

class HttpResponse
{
public:
    enum HttpStatusCode
    {
        kUnknown,
        k200Ok = 200,
        k204NoContent = 204,
        k301MovedPermanently = 301,
        k400BadRequest = 400,
        k401Unauthorized = 401,
        k403Forbidden = 403,
        k404NotFound = 404,
        k409Conflict = 409,
        k500InternalServerError = 500,
        k503ServiceUnavailable = 503,
    };

    HttpResponse(bool close = true)
        : statusCode_(kUnknown)
        , closeConnection_(close)
    {}

    void setVersion(std::string version)
    { httpVersion_ = version; }
    void setStatusCode(HttpStatusCode code)
    { statusCode_ = code; }

    HttpStatusCode getStatusCode() const
    { return statusCode_; }

    void setStatusMessage(const std::string message)
    { statusMessage_ = message; }

    void setCloseConnection(bool on)
    { closeConnection_ = on; }

    bool closeConnection() const
    { return closeConnection_; }
    
    void setContentType(const std::string& contentType)
    { addHeader("Content-Type", contentType); }

    void setContentLength(uint64_t length)
    { addHeader("Content-Length", std::to_string(length)); }

    void addHeader(const std::string& key, const std::string& value)
    { headers_[key] = value; }
    
    void setBody(const std::string& body)
    {
        body_ = body;
    }

    void setBody(std::string&& body)
    {
        body_ = std::move(body);
    }

    void setRequestId(const std::string& id) { requestId_ = id; }
    std::string getRequestId() const { return requestId_; }

    void setClientIp(const std::string& ip) { clientIp_ = ip; }
    std::string getClientIp() const { return clientIp_; }

    void setPerfTrace(std::shared_ptr<PerfTrace> pt) { perfTrace_ = std::move(pt); }
    std::shared_ptr<PerfTrace> getPerfTrace() const { return perfTrace_; }

    void setStatusLine(const std::string& version,
                         HttpStatusCode statusCode,
                         const std::string& statusMessage);

    void setErrorHeader(){}

    void appendToBuffer(muduo::net::Buffer* outputBuf) const;
private:
    std::string                        httpVersion_; 
    HttpStatusCode                     statusCode_;
    std::string                        statusMessage_;
    bool                               closeConnection_;
    std::map<std::string, std::string> headers_;
    std::string                        body_;
    bool                               isFile_;
    std::string                        requestId_;
    std::string                        clientIp_;
    std::shared_ptr<PerfTrace>         perfTrace_;
};

} // namespace http