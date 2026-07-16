#include "UDPRecvServer.h"

#include "../../Common/ProtocolFrameCodec.h"

#include <qtimer.h>

UDPRecvServer::UDPRecvServer(QObject* parent) : QObject(parent)
{
    p_udpSocket = new QUdpSocket(this);
    connect(p_udpSocket, &QUdpSocket::readyRead, this, &UDPRecvServer::slotReadData);
    m_isListening = false;
}

UDPRecvServer::~UDPRecvServer()
{
    stopListen();
}

bool UDPRecvServer::startListen(quint16 port, QString& strErrorMessage)
{
    if (m_isListening)
    {
        strErrorMessage = "已经在监听中";
        return false;
    }

    bool ok = p_udpSocket->bind(QHostAddress::Any, port);

    if (ok)
    {
        m_isListening = true;
        strErrorMessage = "监听成功，端口：" + QString::number(port);
        return true;
    }
    else
    {
        strErrorMessage = "监听失败：" + p_udpSocket->errorString();
        return false;
    }
}

void UDPRecvServer::stopListen()
{
    if (m_isListening)
    {
        p_udpSocket->close();
        m_isListening = false;
    }
}

void UDPRecvServer::slotReadData()
{
    while (p_udpSocket->hasPendingDatagrams())
    {
        QByteArray buffer;
        buffer.resize(p_udpSocket->pendingDatagramSize());

        QHostAddress senderIp;
        quint16 senderPort;

        // 读取数据，同时记录发送方信息
        p_udpSocket->readDatagram(buffer.data(), buffer.size(), &senderIp, &senderPort);

        // 保存最后一个发送方信息，用于回复
        m_lastSenderIp = senderIp;
        m_lastSenderPort = senderPort;

        QByteArray payload;
        if (!ProtocolFrameCodec::decodeOne(buffer, payload))
            continue; // CRC失败或帧不完整
        QString recvData = QString::fromUtf8(payload);
        emit sigDataReceived(senderIp.toString(), senderPort, recvData);
    }
}

void UDPRecvServer::sendReply(QString strData, QString& strErrorMessage)
{
    if (m_lastSenderIp.isNull() || m_lastSenderPort == 0)
    {
        strErrorMessage = "没有可回复的目标（尚未收到数据）";
        return;
    }

    QByteArray replyData = ProtocolFrameCodec::encode(strData.toUtf8());
    qint64 result = p_udpSocket->writeDatagram(replyData, m_lastSenderIp, m_lastSenderPort);

    if (result > 0)
    {
        strErrorMessage = QString("UDP回复成功！字节数：%1").arg(result);
    }
    else
    {
        strErrorMessage = QString("UDP回复失败：%1").arg(p_udpSocket->errorString());
    }
}
