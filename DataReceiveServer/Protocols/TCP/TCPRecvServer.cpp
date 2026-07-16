#include "TCPRecvServer.h"

#include "../../Common/AppTempDirectory.h"
#include "../../Common/FileMessageCodec.h"
#include "../../Common/ProtocolFrameCodec.h"

#include <QFileInfo>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QUuid>

TCPRecvServer::TCPRecvServer(QObject* parent) : QObject(parent)
{
    p_tcpServer = new QTcpServer(this);
    m_lastSenderSocket = nullptr;
    m_isListening = false;
    connect(p_tcpServer, &QTcpServer::newConnection, this, &TCPRecvServer::slotNewConnection);
}

TCPRecvServer::~TCPRecvServer()
{
    stopListen();
}

bool TCPRecvServer::startListen(quint16 port, QString& strErrorMessage)
{
    if (m_isListening)
    {
        strErrorMessage = "已经在监听中";
        return false;
    }

    bool ok = p_tcpServer->listen(QHostAddress::Any, port);

    if (ok)
    {
        m_isListening = true;
        strErrorMessage = "TCP监听成功，端口：" + QString::number(port);
        return true;
    }
    else
    {
        strErrorMessage = "TCP监听失败：" + p_tcpServer->errorString();
        return false;
    }
}

void TCPRecvServer::stopListen()
{
    if (m_isListening)
    {
        for (QTcpSocket* socket : m_clientList)
        {
            disconnect(socket, &QTcpSocket::readyRead, this, &TCPRecvServer::slotReadData);
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

void TCPRecvServer::slotNewConnection()
{
    while (p_tcpServer->hasPendingConnections())
    {
        QTcpSocket* clientSocket = p_tcpServer->nextPendingConnection();
        m_clientList.append(clientSocket);
        m_rxBuffers.insert(clientSocket, {});
        connect(clientSocket, &QTcpSocket::readyRead, this, &TCPRecvServer::slotReadData);

        connect(clientSocket,
                &QTcpSocket::disconnected,
                this,
                [this, clientSocket]()
                {
                    m_clientList.removeOne(clientSocket);
                    if (m_lastSenderSocket == clientSocket)
                        m_lastSenderSocket = nullptr;
                    m_rxBuffers.remove(clientSocket);
                    m_incomingFiles.remove(clientSocket);
                    clientSocket->deleteLater();
                });
    }
}

void TCPRecvServer::slotReadData()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;

    // 记录最后一个发送数据的客户端
    m_lastSenderSocket = socket;

    while (socket->bytesAvailable() > 0)
    {
        m_rxBuffers[socket] += socket->readAll();
        QByteArray payload;
        while (ProtocolFrameCodec::decodeOne(m_rxBuffers[socket], payload))
        {
            QString strIp = socket->peerAddress().toString();
            quint16 port = socket->peerPort();
            FileMessageCodec::StreamMessage stream;
            QString error;
            if (FileMessageCodec::decodeStream(payload, stream, error))
            {
                if (stream.type == FileMessageCodec::StreamType::Start)
                {
                    IncomingFile state;
                    state.fileName = stream.fileName;
                    state.mimeType = stream.mimeType;
                    state.total = stream.totalSize;
                    state.path =
                        AppTempDirectory::path() + "/" + stream.fileId + "_" + QFileInfo(stream.fileName).fileName();
                    state.file = QSharedPointer<QFile>::create(state.path);
                    if (state.file->open(QIODevice::WriteOnly))
                        m_incomingFiles[socket].insert(stream.fileId, state);
                }
                else if (stream.type == FileMessageCodec::StreamType::Chunk &&
                         m_incomingFiles[socket].contains(stream.fileId))
                {
                    IncomingFile& state = m_incomingFiles[socket][stream.fileId];
                    state.file->seek(qint64(stream.offset));
                    qint64 n = state.file->write(stream.chunk);
                    if (n == stream.chunk.size())
                    {
                        state.received += quint64(n);
                        emit sigFileProgress(state.fileName, state.received, state.total);
                    }
                }
                else if (stream.type == FileMessageCodec::StreamType::End &&
                         m_incomingFiles[socket].contains(stream.fileId))
                {
                    IncomingFile state = m_incomingFiles[socket].take(stream.fileId);
                    state.file->flush();
                    state.file->close();
                    emit sigFilePathReceived(strIp, port, state.fileName, state.mimeType, state.path, state.received);
                }
            }
            else
            {
                FileMessageCodec::FileMessage file;
                if (FileMessageCodec::decode(payload, file, error))
                    emit sigFileReceived(strIp, port, file.fileName, file.mimeType, file.data);
                else
                    emit sigDataReceived(strIp, port, QString::fromUtf8(payload));
            }
        }
    }
}

void TCPRecvServer::sendReply(QString strData, QString& strErrorMessage)
{
    if (!m_lastSenderSocket || m_lastSenderSocket->state() != QAbstractSocket::ConnectedState)
    {
        strErrorMessage = "没有可回复的目标（客户端已断开连接）";
        return;
    }

    QByteArray replyData = ProtocolFrameCodec::encode(strData.toUtf8());
    qint64 result = m_lastSenderSocket->write(replyData);

    if (result > 0)
    {
        if (m_lastSenderSocket->waitForBytesWritten(3000))
        {
            strErrorMessage = QString("TCP回复成功！字节数：%1").arg(result);
        }
        else
        {
            strErrorMessage = "TCP回复超时！";
        }
    }
    else
    {
        strErrorMessage = QString("TCP回复失败：%1").arg(m_lastSenderSocket->errorString());
    }
}

void TCPRecvServer::sendFileReply(QString filePath, QString& strErrorMessage)
{
    if (!m_lastSenderSocket || m_lastSenderSocket->state() != QAbstractSocket::ConnectedState)
    {
        strErrorMessage = QStringLiteral("没有可回复的TCP连接");
        return;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        strErrorMessage = file.errorString();
        return;
    }
    QFileInfo info(file);
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    quint64 total = quint64(file.size());
    QString mime = QMimeDatabase().mimeTypeForFile(info).name();
    auto writeFrame = [this](const QByteArray& p)
    {
        QByteArray f = ProtocolFrameCodec::encode(p);
        return m_lastSenderSocket->write(f) == f.size();
    };
    if (!writeFrame(FileMessageCodec::encodeStreamStart(id, info.fileName(), mime, total)))
    {
        strErrorMessage = QStringLiteral("TCP文件回复开始失败");
        return;
    }
    quint64 offset = 0;
    while (!file.atEnd())
    {
        QByteArray chunk = file.read(1024 * 1024);
        if (!writeFrame(FileMessageCodec::encodeStreamChunk(id, offset, chunk)))
        {
            strErrorMessage = QStringLiteral("TCP文件回复分片失败");
            return;
        }
        offset += quint64(chunk.size());
        emit sigFileProgress(info.fileName(), offset, total);
        while (m_lastSenderSocket->bytesToWrite() > 16 * 1024 * 1024)
            if (!m_lastSenderSocket->waitForBytesWritten(30000))
            {
                strErrorMessage = QStringLiteral("TCP文件回复超时");
                return;
            }
    }
    writeFrame(FileMessageCodec::encodeStreamEnd(id));
    strErrorMessage = QStringLiteral("TCP文件回复完成");
}
