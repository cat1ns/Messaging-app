#pragma once
#include <QFile>
#include <QHash>
#include <QSharedPointer>
#include <QTcpSocket>
#include <qobject.h>

class TCPSendServer : public QObject
{
    Q_OBJECT
public:
    explicit TCPSendServer(QObject* parent = nullptr);
    ~TCPSendServer() override;

    void sendTcpData(QString strIp, QString strPort, QString strData, QString& strErrorMessage);
    void sendTcpFile(QString strIp, QString strPort, QString filePath, QString& strErrorMessage);
    void disconnectHost();
    void pauseFileTransfer();
    void resumeFileTransfer();

signals:
    void sigFileReceived(QString strIp, quint16 port, QString fileName, QString mimeType, QByteArray data);
    void sigFileProgress(QString fileName, quint64 completed, quint64 total);
    void sigFilePathReceived(
        QString strIp, quint16 port, QString fileName, QString mimeType, QString localPath, quint64 size);
    void sigReplyReceived(QString strIp, quint16 port, QString strData); // 收到回复信号

private slots:
    void slotReadReply();

private:
    QTcpSocket* p_tcpSocket = nullptr;
    QString m_lastIp;
    quint16 m_lastPort = 0;
    QByteArray m_rxBuffer; // TCP是字节流，必须缓存后按长度拆帧
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
