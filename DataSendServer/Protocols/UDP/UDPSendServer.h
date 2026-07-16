#pragma once
#include <QUdpSocket>
#include <qobject.h>

class UDPSendServer : public QObject
{
    Q_OBJECT
public:
    explicit UDPSendServer(QObject* parent = nullptr);
    ~UDPSendServer() override;

    void sendUdpData(QString strIp, QString strPort, QString strData, QString& strErrorMessage); // 发送数据

signals:
    void sigReplyReceived(QString strIp, quint16 port, QString strData); // 收到回复信号

private slots:
    void slotReadReply();

private:
    QUdpSocket* p_udpSocket = nullptr;
};
