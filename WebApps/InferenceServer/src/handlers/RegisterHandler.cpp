#include "../include/handlers/RegisterHandler.h"
#include "../../../../third_party/bcrypt.h"

void RegisterHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    try
    {
        // 解析body(json格式)
        json parsed = json::parse(req.getBody());
        std::string username = parsed["username"];
        std::string password = parsed["password"];

        // 判断用户是否已经存在，如果存在则注册失败
        int userId = insertUser(username, password);
        if (userId != -1)
        {
            // 插入成功
            json successResp;
            successResp["status"] = "success";
            successResp["message"] = "Register successful";
            successResp["userId"] = userId;
            std::string successBody = successResp.dump(4);

            resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
            resp->setCloseConnection(false);
            resp->setContentType("application/json");
            resp->setContentLength(successBody.size());
            resp->setBody(successBody);
        }
        else
        {
            // 插入失败
            json failureResp;
            failureResp["status"] = "error";
            failureResp["message"] = "username already exists";
            std::string failureBody = failureResp.dump(4);

            resp->setStatusLine(req.getVersion(), http::HttpResponse::k409Conflict, "Conflict");
            resp->setCloseConnection(false);
            resp->setContentType("application/json");
            resp->setContentLength(failureBody.size());
            resp->setBody(failureBody);
        }
    }
    catch (const json::exception& e)
    {
        json err;
        err["status"] = "error";
        err["message"] = std::string("invalid JSON: ") + e.what();
        std::string body = err.dump();
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setContentType("application/json");
        resp->setContentLength(body.size());
        resp->setBody(body);
        resp->setCloseConnection(false);
    }
    catch (const std::exception& e)
    {
        json err;
        err["status"] = "error";
        err["message"] = std::string("internal error: ") + e.what();
        std::string body = err.dump();
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k500InternalServerError, "Internal Server Error");
        resp->setContentType("application/json");
        resp->setContentLength(body.size());
        resp->setBody(body);
        resp->setCloseConnection(true);
    }
}

int RegisterHandler::insertUser(const std::string &username, const std::string &password)
{
    if (!isUserExist(username))
    {
        std::string hash = bcrypt::generateHash(password, 12);
        mysqlUtil_.executeUpdate(
            "INSERT INTO users (username, password) VALUES (?, ?)", username, hash);

        sql::ResultSet* res = mysqlUtil_.executeQuery(
            "SELECT id FROM users WHERE username = ?", username);
        if (res->next())
        {
            return res->getInt("id");
        }
    }
    return -1;
}

bool RegisterHandler::isUserExist(const std::string &username)
{
    sql::ResultSet* res = mysqlUtil_.executeQuery(
        "SELECT id FROM users WHERE username = ?", username);
    if (res->next())
    {
        return true;
    }
    return false;
}
