#include "HTTPRecvServer.h"

#include "../../Common/AppTempDirectory.h"
#include "../../Common/FileMessageCodec.h"
#include "../../Common/ProtocolFrameCodec.h"

#include <QFileInfo>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QUrl>

HTTPRecvServer::HTTPRecvServer(QObject* parent) : QObject(parent)
{
    p_tcpServer = new QTcpServer(this);
    m_lastSenderSocket = nullptr;
    m_isListening = false;
    connect(p_tcpServer, &QTcpServer::newConnection, this, &HTTPRecvServer::slotNewConnection);
}

HTTPRecvServer::~HTTPRecvServer()
{
    stopListen();
}

bool HTTPRecvServer::startListen(quint16 port, QString& strErrorMessage)
{
    if (m_isListening)
    {
        strErrorMessage = "HTTP已经在监听中";
        return false;
    }

    bool ok = p_tcpServer->listen(QHostAddress::Any, port);

    if (ok)
    {
        m_isListening = true;
        strErrorMessage = "HTTP监听成功，端口：" + QString::number(port);
        return true;
    }
    else
    {
        strErrorMessage = "HTTP监听失败：" + p_tcpServer->errorString();
        return false;
    }
}

void HTTPRecvServer::stopListen()
{
    if (m_isListening)
    {
        for (QTcpSocket* socket : m_clientList)
        {
            disconnect(socket, &QTcpSocket::readyRead, this, &HTTPRecvServer::slotReadData);
            if (socket->state() != QAbstractSocket::UnconnectedState)
            {
                socket->disconnectFromHost();
                socket->waitForDisconnected(1000);
            }
            socket->deleteLater();
        }
        m_clientList.clear();
        m_lastSenderSocket = nullptr;

        p_tcpServer->close();
        m_isListening = false;
    }
}

void HTTPRecvServer::slotNewConnection()
{
    while (p_tcpServer->hasPendingConnections())
    {
        QTcpSocket* clientSocket = p_tcpServer->nextPendingConnection();
        m_clientList.append(clientSocket);
        m_requestBuffers.insert(clientSocket, {});
        connect(clientSocket, &QTcpSocket::readyRead, this, &HTTPRecvServer::slotReadData);
        connect(clientSocket,
                &QTcpSocket::disconnected,
                this,
                [this, clientSocket]()
                {
                    m_clientList.removeOne(clientSocket);
                    if (m_lastSenderSocket == clientSocket)
                        m_lastSenderSocket = nullptr;
                    m_requestBuffers.remove(clientSocket);
                    m_uploadStates.remove(clientSocket);
                    clientSocket->deleteLater();
                });
    }
}

void HTTPRecvServer::slotReadData()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;

    QByteArray& requestData = m_requestBuffers[socket];
    requestData += socket->readAll();
    m_lastSenderSocket = socket;
    QString strIp = socket->peerAddress().toString();
    quint16 port = socket->peerPort();

    if (!m_uploadStates.contains(socket))
    {
        int headerEnd = requestData.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        QByteArray headers = requestData.left(headerEnd);
        qint64 contentLength = 0;
        QString fileName;
        QByteArray mime;
        for (QByteArray line : headers.split('\n'))
        {
            line = line.trimmed();
            QByteArray lower = line.toLower();
            int colon = line.indexOf(':');
            QByteArray value = colon >= 0 ? line.mid(colon + 1).trimmed() : QByteArray();
            if (lower.startsWith("content-length:"))
                contentLength = value.toLongLong();
            else if (lower.startsWith("x-file-name:"))
                fileName = QUrl::fromPercentEncoding(value);
            else if (lower.startsWith("content-type:"))
                mime = value;
        }
        if (!fileName.isEmpty())
        {
            UploadState state;
            state.fileName = fileName;
            state.mimeType = QString::fromUtf8(mime);
            state.total = quint64(contentLength);
            state.path = AppTempDirectory::path() + "/http_" + QString::number(quintptr(socket)) + "_" +
                         QFileInfo(state.fileName).fileName();
            state.file = QSharedPointer<QFile>::create(state.path);
            if (!state.file->open(QIODevice::WriteOnly))
                return;
            m_uploadStates.insert(socket, state);
            requestData.remove(0, headerEnd + 4);
        }
        else
        {
            if (requestData.size() < headerEnd + 4 + contentLength)
                return;
            QByteArray body = requestData.mid(headerEnd + 4, int(contentLength)), payload;
            if (ProtocolFrameCodec::decodeOne(body, payload))
            {
                FileMessageCodec::FileMessage file;
                QString error;
                if (FileMessageCodec::decode(payload, file, error))
                    emit sigFileReceived(strIp, port, file.fileName, file.mimeType, file.data);
                else
                    emit sigDataReceived(strIp, port, QString::fromUtf8(payload));
            }
            requestData.remove(0, headerEnd + 4 + int(contentLength));
            return;
        }
    }
    UploadState& state = m_uploadStates[socket];
    quint64 remaining = state.total - state.received;
    int take = int(qMin<quint64>(remaining, quint64(requestData.size())));
    if (take > 0)
    {
        qint64 n = state.file->write(requestData.constData(), take);
        if (n > 0)
        {
            state.received += quint64(n);
            requestData.remove(0, int(n));
            emit sigFileProgress(state.fileName, state.received, state.total);
        }
    }
    if (state.received == state.total)
    {
        UploadState done = m_uploadStates.take(socket);
        done.file->flush();
        done.file->close();
        emit sigFilePathReceived(strIp, port, done.fileName, done.mimeType, done.path, done.received);
    }
}

void HTTPRecvServer::sendReply(QString strData, QString& strErrorMessage)
{
    if (!m_lastSenderSocket || m_lastSenderSocket->state() != QAbstractSocket::ConnectedState)
    {
        strErrorMessage = "没有可回复的HTTP目标（连接已断开）";
        return;
    }

    QByteArray responseData = makeHttpResponse(strData);
    qint64 result = m_lastSenderSocket->write(responseData);

    if (result > 0)
    {
        if (m_lastSenderSocket->waitForBytesWritten(3000))
        {
            strErrorMessage = QString("HTTP回复成功！字节数：%1").arg(result);
            m_lastSenderSocket->disconnectFromHost();
        }
        else
        {
            strErrorMessage = "HTTP回复超时！";
        }
    }
    else
    {
        strErrorMessage = QString("HTTP回复失败：%1").arg(m_lastSenderSocket->errorString());
    }
}

void HTTPRecvServer::sendFileReply(QString filePath, QString& strErrorMessage)
{
    if (!m_lastSenderSocket || m_lastSenderSocket->state() != QAbstractSocket::ConnectedState)
    {
        strErrorMessage = QStringLiteral("没有可回复的HTTP连接");
        return;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        strErrorMessage = file.errorString();
        return;
    }
    QFileInfo info(file);
    quint64 total = quint64(file.size()), sent = 0;
    QByteArray header = "HTTP/1.1 200 OK\r\nContent-Type: " + QMimeDatabase().mimeTypeForFile(info).name().toUtf8() +
                        "\r\nContent-Length: " + QByteArray::number(total) +
                        "\r\nX-File-Name: " + QUrl::toPercentEncoding(info.fileName()) +
                        "\r\nConnection: close\r\n\r\n";
    m_lastSenderSocket->write(header);
    while (!file.atEnd())
    {
        QByteArray chunk = file.read(1024 * 1024);
        if (m_lastSenderSocket->write(chunk) != chunk.size())
        {
            strErrorMessage = QStringLiteral("HTTP文件回复写入失败");
            return;
        }
        sent += quint64(chunk.size());
        emit sigFileProgress(info.fileName(), sent, total);
        while (m_lastSenderSocket->bytesToWrite() > 16 * 1024 * 1024)
            if (!m_lastSenderSocket->waitForBytesWritten(30000))
            {
                strErrorMessage = QStringLiteral("HTTP文件回复超时");
                return;
            }
    }
    while (m_lastSenderSocket->bytesToWrite() > 0)
        m_lastSenderSocket->waitForBytesWritten(30000);
    strErrorMessage = QStringLiteral("HTTP文件回复完成");
    m_lastSenderSocket->disconnectFromHost();
}

QString HTTPRecvServer::parseHttpBody(const QByteArray& requestData) const
{
    int bodyIndex = requestData.indexOf("\r\n\r\n");
    if (bodyIndex < 0)
        return QString::fromUtf8(requestData);

    QByteArray bodyData = requestData.mid(bodyIndex + 4);
    QByteArray payload;
    if (ProtocolFrameCodec::decodeOne(bodyData, payload))
        return QString::fromUtf8(payload);
    return QStringLiteral("[HTTP帧校验失败]");
}

QByteArray HTTPRecvServer::makeHttpResponse(const QString& strData) const
{
    QByteArray bodyData = ProtocolFrameCodec::encode(strData.toUtf8());
    QByteArray responseData;
    responseData.append("HTTP/1.1 200 OK\r\n");
    responseData.append("Content-Type: text/plain; charset=utf-8\r\n");
    responseData.append("Content-Length: " + QByteArray::number(bodyData.size()) + "\r\n");
    responseData.append("Connection: close\r\n");
    responseData.append("\r\n");
    responseData.append(bodyData);
    return responseData;
}
