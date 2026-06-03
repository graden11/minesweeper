#pragma once
#include "Session.h"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace http
{
namespace session
{

class SessionStorage
{
public:
    virtual ~SessionStorage() = default;
    virtual void save(std::shared_ptr<Session> session) = 0;
    virtual std::shared_ptr<Session> load(const std::string& sessionId) = 0;
    virtual void remove(const std::string& sessionId) = 0;
    // 获取所有活跃（未过期）的 session id，用于在线用户清理
    virtual std::vector<std::string> getActiveIds() = 0;
};

// 基于内存的会话存储实现
class MemorySessionStorage : public SessionStorage
{
public:
    void save(std::shared_ptr<Session> session) override;
    std::shared_ptr<Session> load(const std::string& sessionId) override;
    void remove(const std::string& sessionId) override;
    std::vector<std::string> getActiveIds() override;
private:
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
    mutable std::mutex mutex_;
};

} // namespace session
} // namespace http