#include "TCPSendServer.h"

#include "../../Common/AppTempDirectory.h"
#include "../../Common/FileMessageCodec.h"
#include "../../Common/ProtocolFrameCodec.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>

TCPSendServer::TCPSendServer(QObject* parent)
{
    p_tcpSocket = new QTcpSocket(this);
    m_lastPort = 0;
    connect(p_tcpSocket, &QTcpSocket::readyRead, this, &TCPSendServer::slotReadReply);
}

TCPSendServer::~TCPSendServer()
{
    disconnectHost();
}

void TCPSendServer::sendTcpData(QString strIp, QString strPort, QString strData, QString& strErrorMessage)
{
    quint16 port = strPort.toUShort();

    // 如果目标地址或端口变了，先断开重连
    if (p_tcpSocket->state() != QAbstractSocket::UnconnectedState && (m_lastIp != strIp || m_lastPort != port))
    {
        p_tcpSocket->disconnectFromHost();
        p_tcpSocket->waitForDisconnected(3000);
    }

    // 如果未连接，则连接
    if (p_tcpSocket->state() == QAbstractSocket::UnconnectedState)
    {
        p_tcpSocket->connectToHost(strIp, port);
        if (!p_tcpSocket->waitForConnected(3000))
        {
            strErrorMessage = QString("TCP连接失败！错误信息：%1").arg(p_tcpSocket->errorString());
            return;
        }
        m_lastIp = strIp;
        m_lastPort = port;
    }

    // 发送数据
    QByteArray sendData = ProtocolFrameCodec::encode(strData.toUtf8());
    qint64 result = p_tcpSocket->write(sendData);

    if (result > 0)
    {
        if (p_tcpSocket->waitForBytesWritten(3000))
        {
            strErrorMessage = QString("TCP发送成功！字节数：%1").arg(result);
        }
        else
        {
            strErrorMessage = QString("TCP发送超时！");
        }
    }
    else
    {
        strErrorMessage = QString("TCP发送失败！错误信息：%1").arg(p_tcpSocket->errorString());
    }

    // 注意：不再断开连接，保持长连接以接收回复
}

void TCPSendServer::sendTcpFile(QString strIp, QString strPort, QString filePath, QString& strErrorMessage)
{
    m_filePaused = false;
    quint16 port = strPort.toUShort();
    if (p_tcpSocket->state() != QAbstractSocket::UnconnectedState && (m_lastIp != strIp || m_lastPort != port))
        disconnectHost();
    if (p_tcpSocket->state() == QAbstractSocket::UnconnectedState)
    {
        p_tcpSocket->connectToHost(strIp, port);
        if (!p_tcpSocket->waitForConnected(3000))
        {
            strErrorMessage = p_tcpSocket->errorString();
            return;
        }
        m_lastIp = strIp;
        m_lastPort = port;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        strErrorMessage = QStringLiteral("无法读取文件：%1").arg(file.errorString());
        return;
    }
    const QFileInfo info(file);
    const quint64 total = quint64(file.size());
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString mime = QMimeDatabase().mimeTypeForFile(info).name();
    auto writeFrame = [this](const QByteArray& payload) -> bool
    {
        QByteArray frame = ProtocolFrameCodec::encode(payload);
        return p_tcpSocket->write(frame) == frame.size();
    };
    if (!writeFrame(FileMessageCodec::encodeStreamStart(id, info.fileName(), mime, total)))
    {
        strErrorMessage = QStringLiteral("TCP文件开始帧发送失败");
        return;
    }
    const qint64 chunkSize = 1024 * 1024;
    quint64 offset = 0;
    while (!file.atEnd())
    {
        while (m_filePaused)
        {
            QCoreApplication::processEvents();
            QThread::msleep(40);
            if (p_tcpSocket->state() != QAbstractSocket::ConnectedState)
            {
                strErrorMessage = QStringLiteral("TCP连接已断开");
                return;
            }
        }
        QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty() && file.error() != QFile::NoError)
        {
            strErrorMessage = file.errorString();
            return;
        }
        if (!writeFrame(FileMessageCodec::encodeStreamChunk(id, offset, chunk)))
        {
            strErrorMessage = QStringLiteral("TCP文件分片发送失败");
            return;
        }
        offset += quint64(chunk.size());
        emit sigFileProgress(info.fileName(), offset, total);
        // 限制Qt发送缓存，避免4GB文件一次性堆积在内存中。
        while (p_tcpSocket->bytesToWrite() > 16 * 1024 * 1024)
            if (!p_tcpSocket->waitForBytesWritten(30000))
            {
                strErrorMessage = QStringLiteral("TCP发送等待超时");
                return;
            }
    }
    if (!writeFrame(FileMessageCodec::encodeStreamEnd(id)))
    {
        strErrorMessage = QStringLiteral("TCP文件结束帧发送失败");
        return;
    }
    while (p_tcpSocket->bytesToWrite() > 0)
        if (!p_tcpSocket->waitForBytesWritten(30000))
        {
            strErrorMessage = QStringLiteral("TCP文件发送完成等待超时");
            return;
        }
    strErrorMessage = QStringLiteral("TCP文件发送完成：%1（%2字节）").arg(info.fileName()).arg(total);
}

void TCPSendServer::pauseFileTransfer()
{
    m_filePaused = true;
}
void TCPSendServer::resumeFileTransfer()
{
    m_filePaused = false;
}

void TCPSendServer::disconnectHost()
{
    if (p_tcpSocket->state() != QAbstractSocket::UnconnectedState)
    {
        p_tcpSocket->disconnectFromHost();
        p_tcpSocket->waitForDisconnected(3000);
    }
    m_lastPort = 0;
}

void TCPSendServer::slotReadReply()
{
    while (p_tcpSocket->bytesAvailable() > 0)
    {
        m_rxBuffer += p_tcpSocket->readAll();
        QByteArray payload;
        while (ProtocolFrameCodec::decodeOne(m_rxBuffer, payload))
        {
            QString strIp = p_tcpSocket->peerAddress().toString();
            quint16 port = p_tcpSocket->peerPort();
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
                        m_incomingFiles.insert(stream.fileId, state);
                }
                else if (stream.type == FileMessageCodec::StreamType::Chunk && m_incomingFiles.contains(stream.fileId))
                {
                    IncomingFile& state = m_incomingFiles[stream.fileId];
                    state.file->seek(qint64(stream.offset));
                    qint64 n = state.file->write(stream.chunk);
                    if (n == stream.chunk.size())
                    {
                        state.received += quint64(n);
                        emit sigFileProgress(state.fileName, state.received, state.total);
                    }
                }
                else if (stream.type == FileMessageCodec::StreamType::End && m_incomingFiles.contains(stream.fileId))
                {
                    IncomingFile state = m_incomingFiles.take(stream.fileId);
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
                    emit sigReplyReceived(strIp, port, QString::fromUtf8(payload));
            }
        }
    }
}
