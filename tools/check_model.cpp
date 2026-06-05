#include <onnxruntime_cxx_api.h>
#include <iostream>
int main() {
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "check");
    Ort::SessionOptions opts;
    Ort::Session session(env, "/tmp/yolov8l.onnx", opts);
    for (size_t i = 0; i < session.GetInputCount(); i++) {
        auto name = session.GetInputNameAllocated(i, Ort::AllocatorWithDefaultOptions());
        auto info = session.GetInputTypeInfo(i);
        auto shape = info.GetTensorTypeAndShapeInfo().GetShape();
        std::cout << "IN " << name.get() << ": [";
        for (size_t j=0;j<shape.size();j++){if(j)std::cout<<",";std::cout<<shape[j];}
        std::cout << "]" << std::endl;
    }
    for (size_t i=0;i<session.GetOutputCount();i++){
        auto name=session.GetOutputNameAllocated(i,Ort::AllocatorWithDefaultOptions());
        auto shape=session.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
        std::cout<<"OUT "<<name.get()<<": [";
        for(size_t j=0;j<shape.size();j++){if(j)std::cout<<",";std::cout<<shape[j];}
        std::cout<<"]"<<std::endl;
    }
}
