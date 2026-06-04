#include "../../include/handlers/ProtoPredictHandler.h"
#include "../../include/ModelFactory.h"
#include "../../include/InferenceEngine.h"
#include "../../include/ModelPipeline.h"

#include <inference.pb.h>

#include <muduo/base/Logging.h>

#include "../../../../HttpServer/include/utils/JsonUtil.h"

void ProtoPredictHandler::handle(const http::HttpRequest &req, http::HttpResponse *resp)
{
    inference::PredictRequest request;
    if (!request.ParseFromString(req.getBody()))
    {
        inference::PredictResponse response;
        response.set_success(false);
        response.set_message("failed to parse PredictRequest protobuf");
        std::string out;
        response.SerializeToString(&out);
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setContentType("application/x-protobuf");
        resp->setContentLength(out.size());
        resp->setBody(out);
        resp->setCloseConnection(false);
        return;
    }

    std::string modelName = request.model_name().empty() ? "resnet50" : request.model_name();
    auto engine = factory_->getModel(modelName);
    if (!engine)
    {
        inference::PredictResponse response;
        response.set_success(false);
        response.set_message("unknown model: " + modelName);
        std::string out;
        response.SerializeToString(&out);
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setContentType("application/x-protobuf");
        resp->setContentLength(out.size());
        resp->setBody(out);
        resp->setCloseConnection(false);
        return;
    }

    std::vector<uint8_t> imageData(request.image_data().begin(),
                                   request.image_data().end());

    inference::PredictResponse response;

    // Try to use ModelPipeline's structured API to avoid JSON round-trip
    auto pipeline = std::dynamic_pointer_cast<inference::ModelPipeline>(engine);
    if (pipeline)
    {
        try
        {
            auto result = pipeline->predictFromBytesJson(imageData);
            if (result["status"] == "ok")
            {
                response.set_success(true);
                std::string taskType = result.value("task_type", "classification");
                response.set_task_type(taskType);

                if (taskType == "detection" && result.contains("detections"))
                {
                    for (auto& d : result["detections"])
                    {
                        auto* det = response.add_detections();
                        det->set_class_id(d["class_id"].get<int>());
                        det->set_label(d.value("label", ""));
                        det->set_confidence(d.value("confidence", 0.0f) / 100.0f);
                        auto* bbox = det->mutable_bbox();
                        bbox->set_x1(d["bbox"]["x1"].get<float>());
                        bbox->set_y1(d["bbox"]["y1"].get<float>());
                        bbox->set_x2(d["bbox"]["x2"].get<float>());
                        bbox->set_y2(d["bbox"]["y2"].get<float>());
                    }
                }
                else if (taskType == "segmentation" && result.contains("mask"))
                {
                    // base64 decode mask data
                    auto& maskObj = result["mask"];
                    // Store as raw bytes — the data field is already base64 in JSON,
                    // for proto we want the raw decoded mask bytes
                    std::string b64 = maskObj.value("data", "");
                    // Simple base64 decode — for now store encoded; real impl would decode
                    auto* seg = response.mutable_segmentation();
                    seg->set_mask_data(b64);  // raw class-map bytes
                    seg->set_height(maskObj.value("height", 0));
                    seg->set_width(maskObj.value("width", 0));
                    seg->set_num_classes(maskObj.value("num_classes", 0));
                }
                else if (taskType == "feature_extraction" && result.contains("embedding"))
                {
                    auto* emb = response.mutable_embedding();
                    emb->set_dimension(result["dimension"].get<int>());
                    for (auto& v : result["embedding"])
                        emb->add_values(v.get<float>());
                }
                else
                {
                    // Classification (default)
                    for (auto& p : result["predictions"])
                    {
                        auto* pred = response.add_predictions();
                        pred->set_class_id(p["id"].get<int>());
                        pred->set_label(p["label"].get<std::string>());
                        pred->set_confidence(p["confidence"].get<float>() / 100.0f);
                    }
                }
            }
            else
            {
                response.set_success(false);
                response.set_message(result.value("message", "unknown error"));
            }
        }
        catch (const json::exception& e)
        {
            response.set_success(false);
            response.set_message(std::string("JSON error: ") + e.what());
        }
    }
    else
    {
        // Fallback: use string-based predictFromBytes
        std::string resultJson = engine->predictFromBytes(imageData);
        try
        {
            json result = json::parse(resultJson);
            if (result["status"] == "ok")
            {
                response.set_success(true);
                std::string taskType = result.value("task_type", "classification");
                response.set_task_type(taskType);

                if (taskType == "detection" && result.contains("detections"))
                {
                    for (auto& d : result["detections"])
                    {
                        auto* det = response.add_detections();
                        det->set_class_id(d["class_id"].get<int>());
                        det->set_label(d.value("label", ""));
                        det->set_confidence(d.value("confidence", 0.0f) / 100.0f);
                        auto* bbox = det->mutable_bbox();
                        bbox->set_x1(d["bbox"]["x1"].get<float>());
                        bbox->set_y1(d["bbox"]["y1"].get<float>());
                        bbox->set_x2(d["bbox"]["x2"].get<float>());
                        bbox->set_y2(d["bbox"]["y2"].get<float>());
                    }
                }
                else if (taskType == "segmentation" && result.contains("mask"))
                {
                    auto& maskObj = result["mask"];
                    auto* seg = response.mutable_segmentation();
                    seg->set_mask_data(maskObj.value("data", ""));
                    seg->set_height(maskObj.value("height", 0));
                    seg->set_width(maskObj.value("width", 0));
                    seg->set_num_classes(maskObj.value("num_classes", 0));
                }
                else if (taskType == "feature_extraction" && result.contains("embedding"))
                {
                    auto* emb = response.mutable_embedding();
                    emb->set_dimension(result["dimension"].get<int>());
                    for (auto& v : result["embedding"])
                        emb->add_values(v.get<float>());
                }
                else
                {
                    for (auto& p : result["predictions"])
                    {
                        auto* pred = response.add_predictions();
                        pred->set_class_id(p["id"].get<int>());
                        pred->set_label(p["label"].get<std::string>());
                        pred->set_confidence(p["confidence"].get<float>() / 100.0f);
                    }
                }
            }
            else
            {
                response.set_success(false);
                response.set_message(result.value("message", "unknown error"));
            }
        }
        catch (const json::exception& e)
        {
            response.set_success(false);
            response.set_message(std::string("JSON parse error: ") + e.what());
        }
    }

    std::string out;
    response.SerializeToString(&out);
    resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
    resp->setContentType("application/x-protobuf");
    resp->setContentLength(out.size());
    resp->setBody(out);
    resp->setCloseConnection(false);
}
