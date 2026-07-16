#include "HTTPSendServer.h"

#include "../../Common/AppTempDirectory.h"
#include "../../Common/FileMessageCodec.h"
#include "../../Common/ProtocolFrameCodec.h"

#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

class PausableFileDevice : public QIODevice
{
public:
    explicit PausableFileDevice(const QString& path, QObject* parent = nullptr) : QIODevice(parent), m_file(path) {}
    bool openFile()
    {
        if (!m_file.open(QIODevice::ReadOnly))
            return false;
        QIODevice::open(QIODevice::ReadOnly);
        return true;
    }
    QString errorString() const
    {
        return m_file.errorString();
    }
    qint64 size() const override
    {
        return m_file.size();
    }
    qint64 pos() const override
    {
        return m_file.pos();
    }
    bool seek(qint64 p) override
    {
        return m_file.seek(p) && QIODevice::seek(p);
    }
    bool atEnd() const override
    {
        return m_file.atEnd();
    }
    void setPaused(bool value)
    {
        m_paused = value;
        if (!value)
            emit readyRead();
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        return m_paused ? 0 : m_file.read(data, maxSize);
    }
    qint64 writeData(const char*, qint64) override
    {
        return -1;
    }

private:
    QFile m_file;
    bool m_paused = false;
};

HTTPSendServer::HTTPSendServer(QObject* parent) : QObject(parent)
{
    p_networkManager = new QNetworkAccessManager(this);
    connect(p_networkManager, &QNetworkAccessManager::finished, this, &HTTPSendServer::slotReplyFinished);
}

HTTPSendServer::~HTTPSendServer() = default;

void HTTPSendServer::sendHttpData(QString strIp, QString strPort, QString strData, QString& strErrorMessage)
{
    QString urlStr = QString("http://%1:%2/").arg(strIp).arg(strPort);
    QUrl url(urlStr);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");

    // Content-Length处理HTTP边界，统一帧中的CRC16负责内容校验。
    QByteArray sendData = ProtocolFrameCodec::encode(strData.toUtf8());
    QNetworkReply* reply = p_networkManager->post(request, sendData);
    prepareReply(reply);

    // 连接错误信号
    connect(reply,
            &QNetworkReply::errorOccurred,
            this,
            [this, reply](QNetworkReply::NetworkError)
            {
                // 错误在 slotReplyFinished 中统一处理
            });

    strErrorMessage = QString("HTTP请求已发送到 %1").arg(urlStr);
}

void HTTPSendServer::sendHttpFile(QString strIp, QString strPort, QString filePath, QString& strErrorMessage)
{
    QUrl url(QString("http://%1:%2/").arg(strIp).arg(strPort));
    PausableFileDevice* file = new PausableFileDevice(filePath);
    if (!file->openFile())
    {
        strErrorMessage = file->errorString();
        delete file;
        return;
    }
    QFileInfo info(filePath);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QMimeDatabase().mimeTypeForFile(info).name());
    request.setRawHeader("X-File-Transfer", "stream-v1");
    request.setRawHeader("X-File-Name", QUrl::toPercentEncoding(info.fileName()));
    request.setRawHeader("X-File-Size", QByteArray::number(file->size()));
    QNetworkReply* reply = p_networkManager->post(request, file);
    file->setParent(reply);
    m_activeUpload = file;
    connect(reply,
            &QObject::destroyed,
            this,
            [this, file]()
            {
                if (m_activeUpload == file)
                    m_activeUpload = nullptr;
            });
    prepareReply(reply);
    connect(reply,
            &QNetworkReply::uploadProgress,
            this,
            [this, name = info.fileName()](qint64 sent, qint64 total)
            {
                if (sent >= 0 && total > 0)
                    emit sigFileProgress(name, quint64(sent), quint64(total));
            });
    strErrorMessage = QStringLiteral("HTTP文件上传请求已发送：%1").arg(filePath);
}

void HTTPSendServer::pauseFileTransfer()
{
    if (m_activeUpload)
        m_activeUpload->setPaused(true);
}
void HTTPSendServer::resumeFileTransfer()
{
    if (m_activeUpload)
        m_activeUpload->setPaused(false);
}

void HTTPSendServer::prepareReply(QNetworkReply* reply)
{
    auto ensure = [this, reply]()
    {
        if (m_downloads.contains(reply))
            return;
        QByteArray encoded = reply->rawHeader("X-File-Name");
        if (encoded.isEmpty())
            return;
        DownloadState s;
        s.fileName = QUrl::fromPercentEncoding(encoded);
        s.mimeType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        s.total = quint64(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong());
        s.path = AppTempDirectory::path() + "/http_reply_" + s.fileName;
        s.file = QSharedPointer<QFile>::create(s.path);
        if (s.file->open(QIODevice::WriteOnly))
            m_downloads.insert(reply, s);
    };
    connect(reply, &QNetworkReply::metaDataChanged, this, ensure);
    connect(reply,
            &QIODevice::readyRead,
            this,
            [this, reply, ensure]()
            {
                ensure();
                if (!m_downloads.contains(reply))
                    return;
                QByteArray data = reply->readAll();
                DownloadState& s = m_downloads[reply];
                qint64 n = s.file->write(data);
                if (n > 0)
                {
                    s.received += quint64(n);
                    emit sigFileProgress(s.fileName, s.received, s.total);
                }
            });
}

void HTTPSendServer::slotReplyFinished(QNetworkReply* reply)
{
    if (m_downloads.contains(reply))
    {
        DownloadState s = m_downloads.take(reply);
        QByteArray tail = reply->readAll();
        if (!tail.isEmpty())
        {
            qint64 n = s.file->write(tail);
            if (n > 0)
                s.received += quint64(n);
        }
        s.file->close();
        emit sigFilePathReceived(
            reply->url().host(), reply->url().port(80), s.fileName, s.mimeType, s.path, s.received);
        reply->deleteLater();
        return;
    }
    if (reply->error() == QNetworkReply::NoError)
    {
        QByteArray responseData = reply->readAll();
        QByteArray frame = responseData, payload;
        QString recvData;
        if (ProtocolFrameCodec::decodeOne(frame, payload))
        {
            FileMessageCodec::FileMessage file;
            QString fileError;
            if (FileMessageCodec::decode(payload, file, fileError))
            {
                emit sigFileReceived(
                    reply->url().host(), reply->url().port(80), file.fileName, file.mimeType, file.data);
                reply->deleteLater();
                return;
            }
            recvData = QString::fromUtf8(payload);
        }
        else
            recvData = QStringLiteral("[HTTP响应帧校验失败]");
        QString strIp = reply->url().host();
        quint16 port = reply->url().port(80);
        emit sigReplyReceived(strIp, port, recvData);
    }
    else
    {
        QString strIp = reply->url().host();
        quint16 port = reply->url().port(80);
        QString errorMsg = QString("HTTP请求失败：%1").arg(reply->errorString());
        emit sigReplyReceived(strIp, port, errorMsg);
    }

    reply->deleteLater();
}
