#pragma once

#include <QObject>
#include <QUdpSocket>

class UDPRecvServer : public QObject
{
    Q_OBJECT

public:
    explicit UDPRecvServer(QObject* parent = nullptr);
    ~UDPRecvServer() override;

    bool startListen(quint16 port, QString& strErrorMessage);
    void stopListen();

    // 回复数据到最后一个发送方
    void sendReply(QString strData, QString& strErrorMessage);

signals:
    // 收到数据信号
    void sigDataReceived(QString strIp, quint16 port, QString strData);

private slots:
    void slotReadData();

private:
    QUdpSocket* p_udpSocket = nullptr;
    bool m_isListening = false;

    // 记录最后一个发送方信息，用于回复
    QHostAddress m_lastSenderIp;
    quint16 m_lastSenderPort = 0;
};
