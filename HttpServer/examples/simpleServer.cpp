// #include "http/HttpServer.h"
// #include <muduo/net/EventLoop.h>

// int main() {
//     muduo::net::EventLoop loop;
//     muduo::net::InetAddress listenAddr(443); // HTTPS 默认端口

//     // 创建 HTTPS 服务器
//     HttpServer server(443, "https_server", true); // 启用 SSL

//     // 配置 SSL
//     ssl::SslConfig sslConfig;
//     sslConfig.setCertificateFile("/etc/letsencrypt/live/your-domain.com/fullchain.pem");
//     sslConfig.setPrivateKeyFile("/etc/letsencrypt/live/your-domain.com/privkey.pem");
//     sslConfig.setProtocolVersion(ssl::SSLVersion::TLS_1_2);
    
//     // 可选：配置证书链
//     sslConfig.setCertificateChainFile("/path/to/chain.crt");
    
//     // 可选：配置加密套件
//     sslConfig.setCipherList("HIGH:!aNULL:!MD5");
    
//     // 可选：配置会话缓存
//     sslConfig.setSessionTimeout(300);
//     sslConfig.setSessionCacheSize(20480);

//     // 设置 SSL 配置
//     server.setSslConfig(sslConfig);

//     // 启动服务器
//     server.start();
    
//     // 运行事件循环
//     loop.loop();
    
//     return 0;
// }