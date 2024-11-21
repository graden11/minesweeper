#include "HttpRequest.h"

void HttpRequest::setReceiveTime(muduo::Timestamp t)
{ receiveTime_ = t; }

bool HttpRequest::setMethod(const char* start, const char* end)
{
    assert(method_ == kInvalid);
    std::string m(start, end); // [start, end)
    if (m == "GET")
    {
        method_ = kGet;
    }
    else if (m == "POST")
    {
        method_ = kPost;
    }
    else if (m == "PUT")
    {
        method_ = kPut;
    }
    else if (m == "DELETE")
    {
        method_ = kDelete;
    }
    else 
    {
        method_ = kInvalid;
    }

    return method_ != kInvalid;
}

void HttpRequest::setPath(const char* start, const char* end)
{
    path_.assign(start, end);
}

void HttpRequest::setArgument(const char* start, const char* end)
{
    arguments_.assign(start, end);
}

void HttpRequest::addHeader(const char* start, const char* colon, const char* end)
{
    std::string key(start, colon);
    ++colon;
    while (colon < end && isspace(*colon))
    {
        ++colon;
    }
    std::string value(colon, end);
    while (!value.empty() && isspace(value[value.size() - 1])) // 消除尾部空格
    {
        value.resize(value.size() - 1);
    }
    headers_[key] = value;
}

std::string HttpRequest::getHeader(const std::string& field) const
{
    std::string result;
    auto it = headers_.find(field);
    if (it != headers_.end())
    {
        result = it->second;
    }
    return result;
}

void HttpRequest::swap(HttpRequest& that)
{
    std::swap(method_, that.method_);
    std::swap(path_, that.path_);
    std::swap(arguments_, that.arguments_);
    std::swap(version_, that.version_);
    std::swap(headers_, that.headers_);
    std::swap(receiveTime_, that.receiveTime_);
}