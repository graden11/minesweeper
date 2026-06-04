#include "../../include/handlers/ConvertHandler.h"
#include "../../include/InferenceServer.h"
#include "../../include/ConversionManager.h"
#include <fstream>
#include <muduo/base/Logging.h>

void ConvertHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    if (req.path() == "/models/convert" && req.method() == http::HttpRequest::kPost)
        handleConvert(req, resp);
    else
        handleStatus(req, resp);
}

void ConvertHandler::handleConvert(const http::HttpRequest& req, http::HttpResponse* resp)
{
    try {
        json reqBody = json::parse(req.getBody());
        inference::BuildOptions opts;
        opts.onnxPath = reqBody.value("onnx_path", "");
        opts.enginePath = reqBody.value("engine_path", "");
        opts.fp16 = reqBody.value("precision", "fp16") == "fp16";
        opts.inputC = reqBody.value("input_channels", 3);
        opts.inputH = reqBody.value("input_height", 224);
        opts.inputW = reqBody.value("input_width", 224);
        opts.inputName = reqBody.value("input_name", "input");
        opts.maxBatchSize = reqBody.value("max_batch_size", 8);

        if (opts.onnxPath.empty() || opts.enginePath.empty()) {
            json err;
            err["status"] = "error";
            err["message"] = "onnx_path and engine_path are required";
            std::string body = err.dump();
            resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
            resp->setContentType("application/json");
            resp->setContentLength(body.size());
            resp->setBody(body);
            resp->setCloseConnection(false);
            return;
        }
        if (!std::ifstream(opts.onnxPath).good()) {
            json err;
            err["status"] = "error";
            err["message"] = "ONNX file not found: " + opts.onnxPath;
            std::string body = err.dump();
            resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
            resp->setContentType("application/json");
            resp->setContentLength(body.size());
            resp->setBody(body);
            resp->setCloseConnection(false);
            return;
        }
        auto* cm = server_->getConversionManager();
        if (!cm) {
            json err;
            err["status"] = "error";
            err["message"] = "Conversion manager not available";
            std::string body = err.dump();
            resp->setStatusLine(req.getVersion(), http::HttpResponse::k500InternalServerError, "Internal Server Error");
            resp->setContentType("application/json");
            resp->setContentLength(body.size());
            resp->setBody(body);
            resp->setCloseConnection(true);
            return;
        }

        std::string jobId = cm->submit(opts);

        json result;
        result["status"] = "ok";
        result["job_id"] = jobId;
        result["message"] = "Conversion queued";
        std::string body = result.dump();
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setContentType("application/json");
        resp->setContentLength(body.size());
        resp->setBody(body);
        resp->setCloseConnection(false);
    } catch (const json::exception& e) {
        json err;
        err["status"] = "error";
        err["message"] = std::string("invalid JSON: ") + e.what();
        std::string body = err.dump();
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setContentType("application/json");
        resp->setContentLength(body.size());
        resp->setBody(body);
        resp->setCloseConnection(false);
    } catch (const std::exception& e) {
        json err;
        err["status"] = "error";
        err["message"] = e.what();
        std::string body = err.dump();
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k500InternalServerError, "Internal Server Error");
        resp->setContentType("application/json");
        resp->setContentLength(body.size());
        resp->setBody(body);
        resp->setCloseConnection(true);
    }
}

void ConvertHandler::handleStatus(const http::HttpRequest& req, http::HttpResponse* resp)
{
    std::string jobId = req.getQueryParameters("id");
    if (jobId.empty()) {
        auto* cm = server_->getConversionManager();
        if (!cm) { resp->setStatusLine(req.getVersion(), http::HttpResponse::k500InternalServerError, "ISE"); return; }
        auto jobs = cm->listJobs();
        json arr = json::array();
        for (auto& j : jobs) {
            json o;
            o["id"] = j.id; o["status"] = (int)j.status; o["progress"] = j.progress;
            o["message"] = j.message; o["error"] = j.error;
            o["engine_path"] = j.enginePath;
            o["engine_size"] = j.engineSizeBytes;
            o["onnx_path"] = j.options.onnxPath;
            arr.push_back(o);
        }
        std::string body = arr.dump();
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setContentType("application/json");
        resp->setContentLength(body.size());
        resp->setBody(body);
        resp->setCloseConnection(false);
        return;
    }
    auto* cm = server_->getConversionManager();
    if (!cm) {
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k500InternalServerError, "ISE");
        return;
    }
    auto job = cm->getStatus(jobId);
    json o;
    o["id"] = job.id;
    o["status"] = (int)job.status;
    o["progress"] = job.progress;
    o["message"] = job.message;
    o["error"] = job.error;
    o["engine_path"] = job.enginePath;
    o["engine_size"] = job.engineSizeBytes;
    o["onnx_path"] = job.options.onnxPath;
    std::string body = o.dump();
    resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
    resp->setContentType("application/json");
    resp->setContentLength(body.size());
    resp->setBody(body);
    resp->setCloseConnection(false);
}
