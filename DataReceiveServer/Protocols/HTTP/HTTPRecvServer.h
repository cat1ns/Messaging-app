#pragma once

#include <QFile>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>

class HTTPRecvServer : public QObject
{
    Q_OBJECT

public:
    explicit HTTPRecvServer(QObject* parent = nullptr);
    ~HTTPRecvServer() override;

    bool startListen(quint16 port, QString& strErrorMessage);
    void stopListen();
    void sendReply(QString strData, QString& strErrorMessage);
    void sendFileReply(QString filePath, QString& strErrorMessage);

signals:
    void sigFileReceived(QString strIp, quint16 port, QString fileName, QString mimeType, QByteArray data);
    void sigFilePathReceived(
        QString strIp, quint16 port, QString fileName, QString mimeType, QString localPath, quint64 size);
    void sigFileProgress(QString fileName, quint64 completed, quint64 total);
    void sigDataReceived(QString strIp, quint16 port, QString strData);

private slots:
    void slotNewConnection();
    void slotReadData();

private:
    QString parseHttpBody(const QByteArray& requestData) const;
    QByteArray makeHttpResponse(const QString& strData) const;

private:
    QTcpServer* p_tcpServer = nullptr;
    QList<QTcpSocket*> m_clientList;
    QTcpSocket* m_lastSenderSocket = nullptr;
    QHash<QTcpSocket*, QByteArray> m_requestBuffers;
    struct UploadState
    {
        QString fileName;
        QString mimeType;
        QString path;
        quint64 total = 0;
        quint64 received = 0;
        QSharedPointer<QFile> file;
    };
    QHash<QTcpSocket*, UploadState> m_uploadStates;
    bool m_isListening = false;
};
