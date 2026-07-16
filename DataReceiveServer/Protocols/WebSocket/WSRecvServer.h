#pragma once

#include <QFile>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSharedPointer>
#include <QWebSocket>
#include <QWebSocketServer>

class WSRecvServer : public QObject
{
    Q_OBJECT

public:
    explicit WSRecvServer(QObject* parent = nullptr);
    ~WSRecvServer() override;

    bool startListen(quint16 port, QString& strErrorMessage);
    void stopListen();
    void sendReply(QString strData, QString& strErrorMessage);
    void sendFileReply(QString filePath, QString& strErrorMessage);

signals:
    void sigFileReceived(QString strIp, quint16 port, QString fileName, QString mimeType, QByteArray data);
    void sigFileProgress(QString fileName, quint64 completed, quint64 total);
    void sigFilePathReceived(
        QString strIp, quint16 port, QString fileName, QString mimeType, QString localPath, quint64 size);
    void sigDataReceived(QString strIp, quint16 port, QString strData);

private slots:
    void slotNewConnection();
    void slotTextMessageReceived(const QString& message);
    void slotBinaryMessageReceived(const QByteArray& message);
    void slotDisconnected();

private:
    QWebSocketServer* p_wsServer = nullptr;
    QList<QWebSocket*> m_clientList;
    QWebSocket* m_lastSenderSocket = nullptr;
    bool m_isListening = false;
    void sendFileChunks(QWebSocket* socket, const QString& filePath, QString& error);
    struct IncomingFile
    {
        QString fileName;
        QString mimeType;
        QString path;
        quint64 total = 0;
        quint64 received = 0;
        QSharedPointer<QFile> file;
    };
    QHash<QWebSocket*, QHash<QString, IncomingFile>> m_incomingFiles;
};
