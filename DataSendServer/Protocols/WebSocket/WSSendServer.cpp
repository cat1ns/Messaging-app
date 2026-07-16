#include "WSSendServer.h"

#include "../../Common/AppTempDirectory.h"
#include "../../Common/FileMessageCodec.h"
#include "../../Common/ProtocolFrameCodec.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QUuid>

WSSendServer::WSSendServer(QObject* parent) : QObject(parent)
{
    p_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_lastPort = 0;
    m_connecting = false;

    connect(p_webSocket, &QWebSocket::connected, this, &WSSendServer::onConnected);
    connect(p_webSocket, &QWebSocket::textMessageReceived, this, &WSSendServer::onTextMessageReceived);
    connect(p_webSocket, &QWebSocket::binaryMessageReceived, this, &WSSendServer::onBinaryMessageReceived);
    connect(p_webSocket, &QWebSocket::errorOccurred, this, &WSSendServer::onError);
    connect(p_webSocket, &QWebSocket::disconnected, this, &WSSendServer::onDisconnected);
}

WSSendServer::~WSSendServer()
{
    disconnectHost();
}

void WSSendServer::sendWsData(QString strIp, QString strPort, QString strData, QString& strErrorMessage)
{
    quint16 port = strPort.toUShort();

    // 如果已经连接且目标未变，直接发送
    if (p_webSocket->state() == QAbstractSocket::ConnectedState && m_lastIp == strIp && m_lastPort == port)
    {
        qint64 result =
            p_webSocket->sendTextMessage(QString::fromLatin1(ProtocolFrameCodec::encode(strData.toUtf8()).toBase64()));
        if (result > 0)
        {
            strErrorMessage = QString("WebSocket发送成功！字节数：%1").arg(result);
        }
        else
        {
            strErrorMessage = QString("WebSocket发送失败！错误信息：%1").arg(p_webSocket->errorString());
        }
        return;
    }

    // 目标变了，先断开
    if (p_webSocket->state() != QAbstractSocket::UnconnectedState)
    {
        p_webSocket->close();
    }

    // 保存待发送数据，连接成功后发送
    m_pendingData = strData;
    m_lastIp = strIp;
    m_lastPort = port;
    m_connecting = true;

    QString urlStr = QString("ws://%1:%2").arg(strIp).arg(strPort);
    p_webSocket->open(QUrl(urlStr));

    strErrorMessage = QString("WebSocket正在连接到 %1...").arg(urlStr);
}

void WSSendServer::disconnectHost()
{
    m_connecting = false;
    m_pendingData.clear();
    m_pendingFilePath.clear();
    if (p_webSocket->state() != QAbstractSocket::UnconnectedState)
    {
        p_webSocket->close();
    }
    m_lastPort = 0;
}

void WSSendServer::onConnected()
{
    m_connecting = false;
    if (!m_pendingData.isEmpty())
    {
        p_webSocket->sendTextMessage(
            QString::fromLatin1(ProtocolFrameCodec::encode(m_pendingData.toUtf8()).toBase64()));
        m_pendingData.clear();
    }
    if (!m_pendingFilePath.isEmpty())
    {
        QString path = m_pendingFilePath;
        m_pendingFilePath.clear();
        sendFileChunks(path);
    }
}

void WSSendServer::sendFileChunks(const QString& filePath, QString* error)
{
    m_filePaused = false;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = file.errorString();
        return;
    }
    QFileInfo info(file);
    quint64 total = quint64(file.size()), offset = 0;
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString mime = QMimeDatabase().mimeTypeForFile(info).name();
    p_webSocket->sendBinaryMessage(
        ProtocolFrameCodec::encode(FileMessageCodec::encodeStreamStart(id, info.fileName(), mime, total)));
    while (!file.atEnd())
    {
        while (m_filePaused)
        {
            QCoreApplication::processEvents();
            QThread::msleep(40);
            if (p_webSocket->state() != QAbstractSocket::ConnectedState)
            {
                if (error)
                    *error = QStringLiteral("WebSocket连接已断开");
                return;
            }
        }
        QByteArray chunk = file.read(1024 * 1024);
        p_webSocket->sendBinaryMessage(
            ProtocolFrameCodec::encode(FileMessageCodec::encodeStreamChunk(id, offset, chunk)));
        offset += quint64(chunk.size());
        emit sigFileProgress(info.fileName(), offset, total);
        QCoreApplication::processEvents();
    }
    p_webSocket->sendBinaryMessage(ProtocolFrameCodec::encode(FileMessageCodec::encodeStreamEnd(id)));
    if (error)
        error->clear();
}

void WSSendServer::pauseFileTransfer()
{
    m_filePaused = true;
}
void WSSendServer::resumeFileTransfer()
{
    m_filePaused = false;
}

void WSSendServer::sendWsFile(QString strIp, QString strPort, QString filePath, QString& strErrorMessage)
{
    quint16 port = strPort.toUShort();
    if (p_webSocket->state() == QAbstractSocket::ConnectedState && m_lastIp == strIp && m_lastPort == port)
    {
        sendFileChunks(filePath, &strErrorMessage);
        if (strErrorMessage.isEmpty())
            strErrorMessage = QStringLiteral("WebSocket文件已发送");
        return;
    }
    if (p_webSocket->state() != QAbstractSocket::UnconnectedState)
        p_webSocket->close();
    m_pendingFilePath = filePath;
    m_lastIp = strIp;
    m_lastPort = port;
    m_connecting = true;
    p_webSocket->open(QUrl(QString("ws://%1:%2").arg(strIp).arg(strPort)));
    strErrorMessage = QStringLiteral("WebSocket正在连接并准备发送文件");
}

void WSSendServer::onBinaryMessageReceived(const QByteArray& message)
{
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
                m_incomingFiles.insert(stream.fileId, s);
        }
        else if (stream.type == FileMessageCodec::StreamType::Chunk && m_incomingFiles.contains(stream.fileId))
        {
            IncomingFile& s = m_incomingFiles[stream.fileId];
            s.file->seek(qint64(stream.offset));
            qint64 n = s.file->write(stream.chunk);
            s.received += quint64(qMax<qint64>(0, n));
            emit sigFileProgress(s.fileName, s.received, s.total);
        }
        else if (stream.type == FileMessageCodec::StreamType::End && m_incomingFiles.contains(stream.fileId))
        {
            IncomingFile s = m_incomingFiles.take(stream.fileId);
            s.file->close();
            emit sigFilePathReceived(m_lastIp, m_lastPort, s.fileName, s.mimeType, s.path, s.received);
        }
    }
    else
    {
        FileMessageCodec::FileMessage file;
        if (FileMessageCodec::decode(payload, file, error))
            emit sigFileReceived(m_lastIp, m_lastPort, file.fileName, file.mimeType, file.data);
    }
}

void WSSendServer::onTextMessageReceived(const QString& message)
{
    QString strIp = m_lastIp;
    quint16 port = m_lastPort;
    QByteArray frame = QByteArray::fromBase64(message.toLatin1()), payload;
    if (ProtocolFrameCodec::decodeOne(frame, payload))
        emit sigReplyReceived(strIp, port, QString::fromUtf8(payload));
}

void WSSendServer::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QString errorMsg = QString("WebSocket错误：%1").arg(p_webSocket->errorString());
    emit sigReplyReceived(m_lastIp, m_lastPort, errorMsg);
    m_connecting = false;
    m_pendingData.clear();
}

void WSSendServer::onDisconnected()
{
    m_connecting = false;
}
