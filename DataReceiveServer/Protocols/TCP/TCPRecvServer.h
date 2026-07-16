#pragma once

#include <QFile>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>

class TCPRecvServer : public QObject
{
    Q_OBJECT

public:
    explicit TCPRecvServer(QObject* parent = nullptr);
    ~TCPRecvServer() override;

    bool startListen(quint16 port, QString& strErrorMessage);
    void stopListen();

    // 回复数据到最后一个发送方
    void sendReply(QString strData, QString& strErrorMessage);
    void sendFileReply(QString filePath, QString& strErrorMessage);

signals:
    void sigFileReceived(QString strIp, quint16 port, QString fileName, QString mimeType, QByteArray data);
    void sigFilePathReceived(
        QString strIp, quint16 port, QString fileName, QString mimeType, QString localPath, quint64 size);
    void sigFileProgress(QString fileName, quint64 completed, quint64 total);
    // 收到数据信号（与 UDP 版本接口一致）
    void sigDataReceived(QString strIp, quint16 port, QString strData);

private slots:
    void slotNewConnection();
    void slotReadData();

private:
    QTcpServer* p_tcpServer = nullptr;
    QList<QTcpSocket*> m_clientList;
    bool m_isListening = false;

    // 记录最后一个发送数据的客户端 socket，用于回复
    QTcpSocket* m_lastSenderSocket = nullptr;
    QHash<QTcpSocket*, QByteArray> m_rxBuffers;
    struct IncomingFile
    {
        QString fileName;
        QString mimeType;
        QString path;
        quint64 total = 0;
        quint64 received = 0;
        QSharedPointer<QFile> file;
    };
    QHash<QTcpSocket*, QHash<QString, IncomingFile>> m_incomingFiles;
};
