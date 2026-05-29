#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct ServerConfig {
    int port = 80;
    std::string name = "HttpServer";
    int threads = 4;
    std::string log_level = "WARN";
};

struct MysqlConfig {
    std::string host = "tcp://mysql:3306";
    std::string user = "root";
    std::string password = "root";
    std::string database = "Gomoku";
    int pool_size = 10;
};

struct ModelEntryConfig {
    std::string type;   // "onnx" or "tensorrt"
    std::string path;
};

struct LoggingConfig {
    std::string level = "INFO";
    std::string file  = "server.log";
};

struct AppConfig {
    ServerConfig server;
    MysqlConfig mysql;
    LoggingConfig logging;
    std::string labels_path;
    std::unordered_map<std::string, ModelEntryConfig> models;
};

inline AppConfig loadConfig(const std::string &filePath)
{
    AppConfig cfg;

    std::ifstream f(filePath);
    if (!f.good())
    {
        std::cerr << "Config file not found: " << filePath
                  << ", using defaults" << std::endl;
        return cfg;
    }

    json j;
    try {
        f >> j;
    } catch (const std::exception &e) {
        std::cerr << "Failed to parse config: " << e.what() << std::endl;
        return cfg;
    }

    // Server
    if (j.contains("server"))
    {
        auto &s = j["server"];
        if (s.contains("port"))     cfg.server.port = s["port"].get<int>();
        if (s.contains("name"))     cfg.server.name = s["name"].get<std::string>();
        if (s.contains("threads"))  cfg.server.threads = s["threads"].get<int>();
        if (s.contains("log_level")) cfg.server.log_level = s["log_level"].get<std::string>();
    }

    // Logging
    if (j.contains("logging"))
    {
        auto &l = j["logging"];
        if (l.contains("level")) cfg.logging.level = l["level"].get<std::string>();
        if (l.contains("file"))  cfg.logging.file  = l["file"].get<std::string>();
    }

    // MySQL
    if (j.contains("mysql"))
    {
        auto &m = j["mysql"];
        if (m.contains("host"))      cfg.mysql.host      = m["host"].get<std::string>();
        if (m.contains("user"))      cfg.mysql.user      = m["user"].get<std::string>();
        if (m.contains("password"))  cfg.mysql.password  = m["password"].get<std::string>();
        if (m.contains("database"))  cfg.mysql.database  = m["database"].get<std::string>();
        if (m.contains("pool_size")) cfg.mysql.pool_size = m["pool_size"].get<int>();
    }

    // Models
    if (j.contains("models"))
    {
        auto &m = j["models"];
        if (m.contains("labels_path"))
            cfg.labels_path = m["labels_path"].get<std::string>();

        if (m.contains("engines"))
        {
            for (auto &[name, entry] : m["engines"].items())
            {
                ModelEntryConfig mec;
                mec.type = entry.value("type", "onnx");
                mec.path = entry.value("path", "");
                cfg.models[name] = mec;
            }
        }
    }

    return cfg;
}
