#pragma once
#include <QFile>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QSharedPointer>
class PausableFileDevice;

class HTTPSendServer : public QObject
{
    Q_OBJECT
public:
    explicit HTTPSendServer(QObject* parent = nullptr);
    ~HTTPSendServer() override;

    void sendHttpData(QString strIp, QString strPort, QString strData, QString& strErrorMessage);
    void sendHttpFile(QString strIp, QString strPort, QString filePath, QString& strErrorMessage);
    void pauseFileTransfer();
    void resumeFileTransfer();

signals:
    void sigFileReceived(QString strIp, quint16 port, QString fileName, QString mimeType, QByteArray data);
    void sigFileProgress(QString fileName, quint64 completed, quint64 total);
    void sigFilePathReceived(
        QString strIp, quint16 port, QString fileName, QString mimeType, QString localPath, quint64 size);
    void sigReplyReceived(QString strIp, quint16 port, QString strData);

private slots:
    void slotReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* p_networkManager = nullptr;
    struct DownloadState
    {
        QString fileName;
        QString mimeType;
        QString path;
        quint64 total = 0;
        quint64 received = 0;
        QSharedPointer<QFile> file;
    };
    QHash<QNetworkReply*, DownloadState> m_downloads;
    void prepareReply(QNetworkReply* reply);
    PausableFileDevice* m_activeUpload = nullptr;
};
