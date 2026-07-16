#pragma once
#include <QFile>
#include <QHash>
#include <QObject>
#include <QSharedPointer>
#include <QTimer>
#include <QWebSocket>

class WSSendServer : public QObject
{
    Q_OBJECT
public:
    explicit WSSendServer(QObject* parent = nullptr);
    ~WSSendServer() override;

    void sendWsData(QString strIp, QString strPort, QString strData, QString& strErrorMessage);
    void sendWsFile(QString strIp, QString strPort, QString filePath, QString& strErrorMessage);
    void disconnectHost();
    void pauseFileTransfer();
    void resumeFileTransfer();

signals:
    void sigFileReceived(QString strIp, quint16 port, QString fileName, QString mimeType, QByteArray data);
    void sigFileProgress(QString fileName, quint64 completed, quint64 total);
    void sigFilePathReceived(
        QString strIp, quint16 port, QString fileName, QString mimeType, QString localPath, quint64 size);
    void sigReplyReceived(QString strIp, quint16 port, QString strData);

private slots:
    void onConnected();
    void onTextMessageReceived(const QString& message);
    void onBinaryMessageReceived(const QByteArray& message);
    void onError(QAbstractSocket::SocketError error);
    void onDisconnected();

private:
    QWebSocket* p_webSocket = nullptr;
    QString m_lastIp;
    quint16 m_lastPort = 0;
    QString m_pendingData;
    QString m_pendingFilePath;
    bool m_connecting = false;
    void sendFileChunks(const QString& filePath, QString* error = nullptr);
    struct IncomingFile
    {
        QString fileName;
        QString mimeType;
        QString path;
        quint64 total = 0;
        quint64 received = 0;
        QSharedPointer<QFile> file;
    };
    QHash<QString, IncomingFile> m_incomingFiles;
    bool m_filePaused = false;
};
