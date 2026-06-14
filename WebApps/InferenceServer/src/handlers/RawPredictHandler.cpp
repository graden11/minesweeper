#include "../../include/handlers/RawPredictHandler.h"
#include "../../include/ModelFactory.h"
#include "../../include/InferenceEngine.h"
#include "../../include/RequestBatcher.h"

#include "../../../../HttpServer/include/http/HttpResponse.h"
#include "../../../../HttpServer/include/utils/MetricsCollector.h"

#include <chrono>
#include <future>
#include <thread>
#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>

namespace
{

void sendRawError(const http::HttpRequest &req, http::HttpResponse *resp,
                  http::HttpResponse::HttpStatusCode code,
                  const std::string &message)
{
    json err;
    err["status"] = "error";
    err["message"] = message;
    std::string errBody = err.dump();
    resp->setStatusLine(req.getVersion(), code,
        code == http::HttpResponse::k400BadRequest ? "Bad Request" : "Internal Server Error");
    resp->setContentType("application/json");
    resp->setContentLength(errBody.size());
    resp->setBody(std::move(errBody));
    resp->setCloseConnection(code != http::HttpResponse::k200Ok);
}

} // anonymous namespace

void RawPredictHandler::handle(const http::HttpRequest &req, http::HttpResponse *resp)
{
    try
    {
        std::string modelName = req.getQueryParameters("model_name");
        if (modelName.empty())
            modelName = "resnet50";

        // ── T5: raw body available (no JSON parse needed) ──
        if (auto* pt = resp->getPerfTrace().get())
        {
            pt->t5_json_parse_done = pt->nowUs();
            pt->endpoint = req.path();
        }

        const std::string& body = req.getBody();
        if (body.empty())
        {
            sendRawError(req, resp, http::HttpResponse::k400BadRequest, "empty body");
            return;
        }

        std::string ct = req.getHeader("Content-Type");
        if (ct.find("image/") == std::string::npos)
        {
            sendRawError(req, resp, http::HttpResponse::k400BadRequest,
                         "Content-Type must be an image type (image/jpeg, image/png, etc.)");
            return;
        }

        std::vector<uint8_t> imageBytes(body.begin(), body.end());

        // ── T6: no base64 decode needed ──
        if (auto* pt = resp->getPerfTrace().get())
            pt->t6_base64_decode_done = pt->nowUs();

        // Batching path
        if (batcher_)
        {
            auto future = batcher_->submit(modelName, std::move(imageBytes), 0, 0, 0,
                                           resp->getPerfTrace());

            // ── T7-T8 ──
            if (auto* pt = resp->getPerfTrace().get())
            {
                pt->t7_batcher_submit_done = pt->nowUs();
                pt->t8_future_get_begin = pt->nowUs();
            }

            // ── Phase 4: Async response — don't block IO thread ──
            resp->setDeferred(true);
            auto conn = resp->getTcpConnection();
            auto version = req.getVersion();
            auto complete = resp->takeCompleteCallback();
            auto perfTrace = resp->getPerfTrace();

            std::thread([conn = std::move(conn),
                         version = std::move(version),
                         future = std::move(future),
                         perfTrace = std::move(perfTrace),
                         complete = std::move(complete)]() mutable {
                std::string resultJson;
                try
                {
                    resultJson = future.get();
                }
                catch (const std::exception& e)
                {
                    resultJson = R"({"status":"error","message":")"
                               + std::string(e.what()) + "\"}";
                }

                if (perfTrace)
                    perfTrace->t9_future_get_return = perfTrace->nowUs();

                auto buf = std::make_shared<muduo::net::Buffer>();
                {
                    http::HttpResponse r(false);
                    r.setStatusLine(version, http::HttpResponse::k200Ok, "OK");
                    r.setContentType("application/json");
                    r.setContentLength(resultJson.size());
                    r.setBody(std::move(resultJson));
                    r.setPerfTrace(perfTrace);
                    if (perfTrace)
                        perfTrace->t10_response_set = perfTrace->nowUs();
                    r.appendToBuffer(buf.get());
                }

                conn->getLoop()->runInLoop([conn, buf]() {
                    conn->send(buf.get());
                });

                if (perfTrace)
                    perfTrace->dump(100);

                complete();
            }).detach();

            return;
        }

        // Direct path (no batching)
        auto engine = factory_->getModel(modelName);
        if (!engine)
        {
            sendRawError(req, resp, http::HttpResponse::k400BadRequest,
                         "unknown model: " + modelName);
            return;
        }

        std::string resultJson = engine->predictFromBytes(imageBytes);

        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setContentType("application/json");
        resp->setContentLength(resultJson.size());
        resp->setBody(std::move(resultJson));
        resp->setCloseConnection(false);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "RawPredictHandler error: " << e.what();
        sendRawError(req, resp, http::HttpResponse::k500InternalServerError,
                     std::string("internal error: ") + e.what());
    }
    catch (...)
    {
        LOG_ERROR << "RawPredictHandler unknown error";
        sendRawError(req, resp, http::HttpResponse::k500InternalServerError, "internal error");
    }
}
