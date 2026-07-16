#include "UDPSendServer.h"

#include "../../Common/ProtocolFrameCodec.h"

UDPSendServer::UDPSendServer(QObject* parent)
{
    p_udpSocket = new QUdpSocket(this);
    // 开启广播权限（如果需要向整个局域网广播）
    p_udpSocket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    // 监听回复数据
    connect(p_udpSocket, &QUdpSocket::readyRead, this, &UDPSendServer::slotReadReply);
}

UDPSendServer::~UDPSendServer() = default;

void UDPSendServer::sendUdpData(QString strIp, QString strPort, QString strData, QString& strErrorMessage)
{
    QHostAddress targetIp(strIp);
    quint16 targetPort = strPort.toUInt();
    // 先编码成带长度和CRC16的应用层帧，接收端可验证完整性。
    QByteArray sendData = ProtocolFrameCodec::encode(strData.toUtf8());

    qint64 result = p_udpSocket->writeDatagram(sendData, targetIp, targetPort);

    if (result > 0)
    {
        strErrorMessage = QString("UDP发送成功！字节数：%1").arg(result);
    }
    else
    {
        strErrorMessage = QString("UDP发送失败！错误信息：%1").arg(p_udpSocket->errorString());
    }
}

void UDPSendServer::slotReadReply()
{
    while (p_udpSocket->hasPendingDatagrams())
    {
        QByteArray buffer;
        buffer.resize(p_udpSocket->pendingDatagramSize());

        QHostAddress senderIp;
        quint16 senderPort;

        p_udpSocket->readDatagram(buffer.data(), buffer.size(), &senderIp, &senderPort);

        QString recvData = QString::fromUtf8(buffer);
        emit sigReplyReceived(senderIp.toString(), senderPort, recvData);
    }
}
