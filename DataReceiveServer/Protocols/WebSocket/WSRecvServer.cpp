#include "WSRecvServer.h"

#include "../../Common/AppTempDirectory.h"
#include "../../Common/FileMessageCodec.h"
#include "../../Common/ProtocolFrameCodec.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QUuid>

WSRecvServer::WSRecvServer(QObject* parent) : QObject(parent)
{
    p_wsServer = new QWebSocketServer(QStringLiteral("DataReceiveServer"), QWebSocketServer::NonSecureMode, this);
    m_lastSenderSocket = nullptr;
    m_isListening = false;
    connect(p_wsServer, &QWebSocketServer::newConnection, this, &WSRecvServer::slotNewConnection);
}

WSRecvServer::~WSRecvServer()
{
    stopListen();
}

bool WSRecvServer::startListen(quint16 port, QString& strErrorMessage)
{
    if (m_isListening)
    {
        strErrorMessage = "WebSocket已经在监听中";
        return false;
    }

    bool ok = p_wsServer->listen(QHostAddress::Any, port);

    if (ok)
    {
        m_isListening = true;
        strErrorMessage = "WebSocket监听成功，端口：" + QString::number(port);
        return true;
    }
    else
    {
        strErrorMessage = "WebSocket监听失败：" + p_wsServer->errorString();
        return false;
    }
}

void WSRecvServer::stopListen()
{
    if (m_isListening)
    {
        for (QWebSocket* socket : m_clientList)
        {
            disconnect(socket, &QWebSocket::textMessageReceived, this, &WSRecvServer::slotTextMessageReceived);
            disconnect(socket, &QWebSocket::disconnected, this, &WSRecvServer::slotDisconnected);
            socket->close();
            socket->deleteLater();
        }
        m_clientList.clear();
        m_lastSenderSocket = nullptr;

        p_wsServer->close();
        m_isListening = false;
    }
}

void WSRecvServer::slotNewConnection()
{
    while (p_wsServer->hasPendingConnections())
    {
        QWebSocket* clientSocket = p_wsServer->nextPendingConnection();
        m_clientList.append(clientSocket);
        connect(clientSocket, &QWebSocket::textMessageReceived, this, &WSRecvServer::slotTextMessageReceived);
        connect(clientSocket, &QWebSocket::binaryMessageReceived, this, &WSRecvServer::slotBinaryMessageReceived);
        connect(clientSocket, &QWebSocket::disconnected, this, &WSRecvServer::slotDisconnected);
    }
}

void WSRecvServer::slotBinaryMessageReceived(const QByteArray& message)
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket)
        return;
    m_lastSenderSocket = socket;
    QByteArray frame = message, payload;
    if (!ProtocolFrameCodec::decodeOne(frame, payload))
        return;
    FileMessageCodec::StreamMessage stream;
    QString error;
    if (FileMessageCodec::decodeStream(payload, stream, error))
    {
        if (stream.type == FileMessageCodec::StreamType::Start)
        {
            IncomingFile s;
            s.fileName = stream.fileName;
            s.mimeType = stream.mimeType;
            s.total = stream.totalSize;
            s.path = AppTempDirectory::path() + "/" + stream.fileId + "_" + QFileInfo(stream.fileName).fileName();
            s.file = QSharedPointer<QFile>::create(s.path);
            if (s.file->open(QIODevice::WriteOnly))
                m_incomingFiles[socket].insert(stream.fileId, s);
        }
        else if (stream.type == FileMessageCodec::StreamType::Chunk && m_incomingFiles[socket].contains(stream.fileId))
        {
            IncomingFile& s = m_incomingFiles[socket][stream.fileId];
            s.file->seek(qint64(stream.offset));
            qint64 n = s.file->write(stream.chunk);
            s.received += quint64(qMax<qint64>(0, n));
            emit sigFileProgress(s.fileName, s.received, s.total);
        }
        else if (stream.type == FileMessageCodec::StreamType::End && m_incomingFiles[socket].contains(stream.fileId))
        {
            IncomingFile s = m_incomingFiles[socket].take(stream.fileId);
            s.file->close();
            emit sigFilePathReceived(
                socket->peerAddress().toString(), socket->peerPort(), s.fileName, s.mimeType, s.path, s.received);
        }
    }
    else
    {
        FileMessageCodec::FileMessage file;
        if (FileMessageCodec::decode(payload, file, error))
            emit sigFileReceived(
                socket->peerAddress().toString(), socket->peerPort(), file.fileName, file.mimeType, file.data);
    }
}

void WSRecvServer::sendFileChunks(QWebSocket* socket, const QString& filePath, QString& error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        error = file.errorString();
        return;
    }
    QFileInfo info(file);
    quint64 total = quint64(file.size()), offset = 0;
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString mime = QMimeDatabase().mimeTypeForFile(info).name();
    socket->sendBinaryMessage(
        ProtocolFrameCodec::encode(FileMessageCodec::encodeStreamStart(id, info.fileName(), mime, total)));
    while (!file.atEnd())
    {
        QByteArray chunk = file.read(1024 * 1024);
        socket->sendBinaryMessage(ProtocolFrameCodec::encode(FileMessageCodec::encodeStreamChunk(id, offset, chunk)));
        offset += quint64(chunk.size());
        emit sigFileProgress(info.fileName(), offset, total);
        QCoreApplication::processEvents();
    }
    socket->sendBinaryMessage(ProtocolFrameCodec::encode(FileMessageCodec::encodeStreamEnd(id)));
    error.clear();
}

void WSRecvServer::slotTextMessageReceived(const QString& message)
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket)
        return;

    m_lastSenderSocket = socket;

    QString strIp = socket->peerAddress().toString();
    quint16 port = socket->peerPort();

    QByteArray frame = QByteArray::fromBase64(message.toLatin1()), payload;
    if (ProtocolFrameCodec::decodeOne(frame, payload))
        emit sigDataReceived(strIp, port, QString::fromUtf8(payload));
}

void WSRecvServer::slotDisconnected()
{
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket)
        return;

    m_clientList.removeOne(socket);
    m_incomingFiles.remove(socket);
    if (m_lastSenderSocket == socket)
        m_lastSenderSocket = nullptr;
    socket->deleteLater();
}

void WSRecvServer::sendReply(QString strData, QString& strErrorMessage)
{
    if (!m_lastSenderSocket || m_lastSenderSocket->state() != QAbstractSocket::ConnectedState)
    {
        strErrorMessage = "没有可回复的WebSocket目标（客户端已断开连接）";
        return;
    }

    // 与发送端完全相同：文本消息承载Base64后的统一帧，避免回复内容被截断/误解析。
    qint64 result = m_lastSenderSocket->sendTextMessage(
        QString::fromLatin1(ProtocolFrameCodec::encode(strData.toUtf8()).toBase64()));

    if (result > 0)
    {
        strErrorMessage = QString("WebSocket回复成功！字节数：%1").arg(result);
    }
    else
    {
        strErrorMessage = QString("WebSocket回复失败：%1").arg(m_lastSenderSocket->errorString());
    }
}

void WSRecvServer::sendFileReply(QString filePath, QString& strErrorMessage)
{
    if (!m_lastSenderSocket || m_lastSenderSocket->state() != QAbstractSocket::ConnectedState)
    {
        strErrorMessage = QStringLiteral("没有可回复的WebSocket连接");
        return;
    }
    sendFileChunks(m_lastSenderSocket, filePath, strErrorMessage);
    if (strErrorMessage.isEmpty())
        strErrorMessage = QStringLiteral("WebSocket文件回复成功");
}
