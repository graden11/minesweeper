#include "../../include/ssl/SslConnection.h"
#include <muduo/base/Logging.h>
#include <openssl/err.h>

namespace ssl
{

// 自定义 BIO 方法
static BIO_METHOD* createCustomBioMethod() 
{
    BIO_METHOD* method = BIO_meth_new(BIO_TYPE_MEM, "custom");
    BIO_meth_set_write(method, SslConnection::bioWrite);
    BIO_meth_set_read(method, SslConnection::bioRead);
    BIO_meth_set_ctrl(method, SslConnection::bioCtrl);
    return method;
}

SslConnection::SslConnection(const TcpConnectionPtr& conn, SslContext* ctx)
    : ssl_(nullptr)
    , ctx_(ctx)
    , conn_(conn)
    , state_(SSLState::HANDSHAKE)
    , readBio_(nullptr)
    , writeBio_(nullptr)
{
    // 创建 SSL 对象
    ssl_ = SSL_new(ctx_->getNativeHandle());
    if (!ssl_) {
        LOG_ERROR << "Failed to create SSL object";
        return;
    }

    // 创建自定义 BIO
    static BIO_METHOD* method = createCustomBioMethod();
    readBio_ = BIO_new(method);
    writeBio_ = BIO_new(method);
    if (!readBio_ || !writeBio_) {
        LOG_ERROR << "Failed to create BIO";
        return;
    }

    // 设置 BIO 的自定义数据为 this
    BIO_set_data(readBio_, this);
    BIO_set_data(writeBio_, this);

    // 设置 SSL 使用自定义 BIO
    SSL_set_bio(ssl_, readBio_, writeBio_);
}

SslConnection::~SslConnection() 
{
    if (ssl_) 
    {
        SSL_free(ssl_);  // 这会同时释放 BIO
    }
}

void SslConnection::startHandshake() 
{
    SSL_set_accept_state(ssl_);
    handleHandshake();
}

void SslConnection::send(const void* data, size_t len) 
{
    if (state_ != SSLState::ESTABLISHED) 
    {
        LOG_WARN << "Cannot send data before handshake completion";
        return;
    }

    int written = SSL_write(ssl_, data, len);
    if (written <= 0) {
        handleError(getLastError(written));
    }
}

void SslConnection::onRead(const TcpConnectionPtr& conn, BufferPtr buf, 
                         muduo::Timestamp time) 
{
    // 将收到的数据添加到读缓冲区
    readBuffer_.append(buf->peek(), buf->readableBytes());
    buf->retrieveAll();

    if (state_ == SSLState::HANDSHAKE) 
    {
        handleHandshake();
    }
    else if (state_ == SSLState::ESTABLISHED) 
    {
        // 解密数据
        char decryptBuf[4096];
        int nread;
        while ((nread = SSL_read(ssl_, decryptBuf, sizeof(decryptBuf))) > 0) 
        {
            onDecrypted(decryptBuf, nread);
        }

        SSLError error = getLastError(nread);
        if (error != SSLError::WANT_READ) 
        {
            handleError(error);
        }
    }
}

void SslConnection::handleHandshake() 
{
    int ret = SSL_do_handshake(ssl_);
    if (ret == 1) 
    {
        state_ = SSLState::ESTABLISHED;
        LOG_INFO << "SSL handshake completed";
    } 
    else
    {
        SSLError error = getLastError(ret);
        if (error != SSLError::WANT_READ) 
        {
            handleError(error);
        }
    }
}

void SslConnection::onEncrypted(const char* data, size_t len) 
{
    writeBuffer_.append(data, len);
    conn_->send(&writeBuffer_);
}

void SslConnection::onDecrypted(const char* data, size_t len) 
{
    decryptedBuffer_.append(data, len);
}

SSLError SslConnection::getLastError(int ret) 
{
    int err = SSL_get_error(ssl_, ret);
    switch (err) 
    {
        case SSL_ERROR_NONE:
            return SSLError::NONE;
        case SSL_ERROR_WANT_READ:
            return SSLError::WANT_READ;
        case SSL_ERROR_WANT_WRITE:
            return SSLError::WANT_WRITE;
        case SSL_ERROR_SYSCALL:
            return SSLError::SYSCALL;
        case SSL_ERROR_SSL:
            return SSLError::SSL;
        default:
            return SSLError::UNKNOWN;
    }
}

void SslConnection::handleError(SSLError error) 
{
    switch (error) 
    {
        case SSLError::WANT_READ:
        case SSLError::WANT_WRITE:
            // 需要等待更多数据或写入缓冲区可用
            break;
        case SSLError::SSL:
        case SSLError::SYSCALL:
        case SSLError::UNKNOWN:
            LOG_ERROR << "SSL error occurred: " << ERR_error_string(ERR_get_error(), nullptr);
            state_ = SSLState::ERROR;
            conn_->shutdown();
            break;
        default:
            break;
    }
}

int SslConnection::bioWrite(BIO* bio, const char* data, int len) 
{
    SslConnection* conn = static_cast<SslConnection*>(BIO_get_data(bio));
    if (!conn) return -1;

    conn->conn_->send(data, len);
    return len;
}

int SslConnection::bioRead(BIO* bio, char* data, int len) 
{
    SslConnection* conn = static_cast<SslConnection*>(BIO_get_data(bio));
    if (!conn) return -1;

    size_t readable = conn->readBuffer_.readableBytes();
    if (readable == 0) 
    {
        return -1;  // 无数据可读
    }

    size_t toRead = std::min(static_cast<size_t>(len), readable);
    memcpy(data, conn->readBuffer_.peek(), toRead);
    conn->readBuffer_.retrieve(toRead);
    return toRead;
}

long SslConnection::bioCtrl(BIO* bio, int cmd, long num, void* ptr) 
{
    switch (cmd) 
    {
        case BIO_CTRL_FLUSH:
            return 1;
        default:
            return 0;
    }
}

} // namespace ssl 