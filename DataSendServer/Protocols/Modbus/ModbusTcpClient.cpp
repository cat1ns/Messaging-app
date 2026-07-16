#include "ModbusTcpClient.h"

#include <QRegularExpression>

// Modbus TCP客户端流程：界面命令 → 组装MBAP/PDU → TCP发送 → 缓存拆帧 → 解析响应。
// 0x03读取保持寄存器；0x10写多个保持寄存器。网络字段均按大端序编码。

ModbusTcpClient::ModbusTcpClient(QObject* parent)
    : QObject(parent), m_socket(new QTcpSocket(this)), m_lastPort(0), m_transactionId(0)
{
    // 用于接收Modbus TCP响应数据
    connect(m_socket, &QTcpSocket::readyRead, this, &ModbusTcpClient::slotReadReply);
}

ModbusTcpClient::~ModbusTcpClient()
{
    disconnectHost();
}

void ModbusTcpClient::appendU16(QByteArray& data, quint16 value)
{
    // Modbus TCP的16位字段采用大端序（高字节在前）。
    // 将一个quint16寄存器值拆分为两个字节并追加到QByteArray中。例如0x1234会被编码成12 34
    data.append(char(value >> 8));
    data.append(char(value & 0xff));
}

quint16 ModbusTcpClient::readU16(const QByteArray& data, int offset)
{
    // 将网络上的两个大端字节恢复为一个quint16寄存器值。
    // 例如数据包中12 34会被解码为0x1234
    return (quint16(quint8(data.at(offset))) << 8) | quint8(data.at(offset + 1));
}

QByteArray ModbusTcpClient::buildRequest(const QString& command, QString& description, QString& errorMessage)
{
    // 解析命令字符串，拆分为各个部分，忽略多余空格。
    const QStringList parts = command.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QByteArray pdu;

    if (!parts.isEmpty() && parts.first().compare("READ", Qt::CaseInsensitive) == 0)
    {
        if (parts.size() != 3)
        {
            errorMessage = "Modbus READ格式：READ 起始地址 数量，例如 READ 0 10";
            return {};
        }
        bool okAddress = false, okCount = false;
        // 将起始地址和数量从字符串转换为无符号整数。
        quint16 address = parts.at(1).toUShort(&okAddress, 0);
        quint16 count = parts.at(2).toUShort(&okCount, 0);
        if (!okAddress || !okCount || count == 0 || count > 125)
        {
            errorMessage = "Modbus读取参数无效，寄存器数量范围为1~125";
            return {};
        }
        // PDU=功能码+起始地址+数量。
        //  例如读取地址0~9的10个寄存器，PDU=03 00 00 00 0A(03为读取模式,00 00为起始地址,00 0A为数量)
        pdu.append(char(0x03));
        appendU16(pdu, address);
        appendU16(pdu, count);
        description = QString("读取保持寄存器：地址%1，数量%2").arg(address).arg(count);
    }
    else
    {
        quint16 address = 0;
        QList<quint16> values;
        if (!parts.isEmpty() && parts.first().compare("WRITE", Qt::CaseInsensitive) == 0)
        {
            if (parts.size() < 3)
            {
                errorMessage = "Modbus WRITE格式：WRITE 起始地址 值1 值2...";
                return {};
            }
            bool okAddress = false;
            // 将起始地址从字符串转换为无符号整数。
            address = parts.at(1).toUShort(&okAddress, 0);
            if (!okAddress)
            {
                errorMessage = "Modbus写入地址无效";
                return {};
            }
            // 将每个寄存器值从字符串转换为无符号整数，并追加到values列表中。i从2开始，因为parts[0]是"WRITE"，parts[1]是起始地址。
            for (int i = 2; i < parts.size(); ++i)
            {
                bool ok = false;
                quint16 value = parts.at(i).toUShort(&ok, 0);
                if (!ok)
                {
                    errorMessage = QString("无效寄存器值：%1").arg(parts.at(i));
                    return {};
                }
                // 将每个寄存器值追加到列表中。
                values.append(value);
            }
        }
        else
        {
            // 如果命令不是以"READ"或"WRITE"开头，则尝试将整个命令字符串作为十六进制数据解析。
            const QByteArray text = command.toUtf8();
            for (int i = 0; i < text.size(); i += 2)
            {
                quint16 value = quint16(quint8(text.at(i))) << 8;
                if (i + 1 < text.size())
                    value |= quint8(text.at(i + 1));
                values.append(value);
            }
        }
        if (values.isEmpty() || values.size() > 123)
        {
            errorMessage = "Modbus单次写入寄存器数量范围为1~123";
            return {};
        }
        // PDU=功能码+地址+数量+字节数+寄存器数据。
        // 例如写入地址0~9的10个寄存器，PDU=10 00 00 00 0A 14(10为写入模式,00 00为起始地址,00 0A为数量,14为数据字节数)
        pdu.append(char(0x10));
        appendU16(pdu, address);
        appendU16(pdu, quint16(values.size()));
        // 每个寄存器16位=2字节，所以数据字节数=寄存器数量*2。 例如写入10个寄存器，数据字节数=10*2=20=0x14。
        pdu.append(char(values.size() * 2));
        // 将每个寄存器值追加到PDU中，按大端序编码。
        for (quint16 value : values)
            appendU16(pdu, value);
        description = QString("写入保持寄存器：地址%1，数量%2").arg(address).arg(values.size());
    }

    // 组装Modbus TCP的MBAP头：事务号、协议号、长度、Unit ID。
    QByteArray frame;
    // 事务号递增，确保每个请求都有唯一标识。2字节长度
    appendU16(frame, ++m_transactionId);
    // 协议号固定为0，表示Modbus协议。2字节长度
    appendU16(frame, 0);
    // Length字段表示Unit ID和PDU的总长度,例如读取10个寄存器，PDU长度=5，Unit ID=1，所以Length=1+5=6。。2字节长度
    appendU16(frame, quint16(1 + pdu.size()));
    // Unit Identifier字段用于标识Modbus从站设备，当前项目固定为1。1字节长度
    frame.append(char(1));
    // 将PDU追加到MBAP头之后，形成完整的Modbus TCP请求帧。
    frame.append(pdu);
    return frame;
}

void ModbusTcpClient::sendModbusData(const QString& ip,
                                     const QString& portText,
                                     const QString& command,
                                     QString& errorMessage)
{
    // 一次Modbus操作从这里进入网络层：先生成请求，再建立/复用TCP连接，最后write二进制帧。

    // 解析端口号，确保是有效的无符号整数。标准Modbus 。
    quint16 port = portText.toUShort();
    if (port == 0)
    {
        errorMessage = "Modbus TCP端口无效（标准端口为502）";
        return;
    }

    // 构建Modbus TCP请求帧，包含MBAP头和PDU。description用于描述请求内容，便于日志记录。
    QString description;
    QByteArray request = buildRequest(command, description, errorMessage);
    if (request.isEmpty())
        return;

    // 如果当前socket已经连接，并且目标IP或端口与上次不同，则先断开旧连接。
    if (m_socket->state() != QAbstractSocket::UnconnectedState && (m_lastIp != ip || m_lastPort != port))
        disconnectHost();
    // 如果socket未连接，则尝试连接到指定的IP和端口。
    if (m_socket->state() == QAbstractSocket::UnconnectedState)
    {
        // 连接到指定的IP和端口，等待最多3秒钟。如果连接失败，则返回错误信息。
        m_socket->connectToHost(ip, port);
        if (!m_socket->waitForConnected(3000))
        {
            errorMessage = QString("Modbus TCP连接失败：%1").arg(m_socket->errorString());
            return;
        }
        m_lastIp = ip;
        m_lastPort = port;
    }

    // 发送请求帧到服务器，并等待最多3秒钟确认写入完成。如果写入失败或超时，则返回错误信息。
    // request是MBAP+PDU的二进制Modbus TCP ADU。
    qint64 written = m_socket->write(request);
    if (written != request.size() || !m_socket->waitForBytesWritten(3000))
        errorMessage = QString("Modbus TCP发送失败：%1").arg(m_socket->errorString());
    else
        errorMessage = QString("Modbus请求已发送：%1，事务号%2").arg(description).arg(m_transactionId);
}

QString ModbusTcpClient::formatResponse(const QByteArray& frame) const
{
    // 解析Modbus TCP响应帧，提取事务号、功能码和数据/异常码，并返回可读的字符串描述。

    // MBAP头7字节+PDU至少2字节（功能码+数据/异常码），否则无效。
    if (frame.size() < 9)
        return "无效Modbus TCP响应";
    // 读取事务号，位于MBAP头的前2字节。
    quint16 transactionId = readU16(frame, 0);
    // 读取功能码，位于PDU的第一个字节（MBAP头之后）。
    quint8 function = quint8(frame.at(7));

    // 如果功能码的最高位为1，表示异常响应。异常码位于PDU的第二个字节（MBAP头之后）。
    if (function & 0x80)
        return QString("Modbus异常响应：事务号%1，功能码0x%2，异常码0x%3")
            .arg(transactionId)
            .arg(function & 0x7f, 2, 16, QLatin1Char('0'))
            .arg(quint8(frame.at(8)), 2, 16, QLatin1Char('0'));

    if (function == 0x03)
    {
        // 0x03响应：功能码+字节数+寄存器数据；每2字节恢复一个quint16。
        int byteCount = quint8(frame.at(8));
        // 读取寄存器值，位于PDU的第三个字节（MBAP头之后），每2字节恢复一个quint16。
        QStringList values;
        for (int i = 0; i + 1 < byteCount && 9 + i + 1 < frame.size(); i += 2)
            values << QString::number(readU16(frame, 9 + i));
        return QString("Modbus读取成功：事务号%1，寄存器值 [%2]").arg(transactionId).arg(values.join(", "));
    }

    if (function == 0x10 && frame.size() >= 12)
        // 读取起始地址和数量，位于PDU的第二个和第三个字节（MBAP头之后）。
        return QString("Modbus写入成功：事务号%1，起始地址%2，数量%3")
            .arg(transactionId)
            .arg(readU16(frame, 8))
            .arg(readU16(frame, 10));

    return QString("Modbus响应：事务号%1，功能码0x%2").arg(transactionId).arg(function, 2, 16, QLatin1Char('0'));
}

void ModbusTcpClient::slotReadReply()
{
    // 响应服务端的Modbus TCP数据。该槽函数在socket有数据可读时被触发。

    // TCP是字节流：一次readAll可能是半帧、一帧或多帧，因此必须先进入缓存。
    // 将socket接收缓冲区的所有数据追加到本地缓存中。
    m_receiveBuffer.append(m_socket->readAll());
    // 解析缓存中的完整Modbus TCP帧，提取MBAP头和PDU，并发射信号给上层处理。
    while (m_receiveBuffer.size() >= 7)
    {
        // MBAP固定7字节；Length位于偏移4，完整帧长度=6+Length。
        quint16 length = readU16(m_receiveBuffer, 4);
        int frameLength = 6 + length;
        if (length < 2 || m_receiveBuffer.size() < frameLength)
            return;
        // 提取一帧并从缓存删除；删除后while可继续处理粘在后面的下一帧。
        QByteArray frame = m_receiveBuffer.left(frameLength);
        m_receiveBuffer.remove(0, frameLength);
        // 将协议层响应转换成界面可读文本，统一回流到DataSendServer。
        emit sigReplyReceived(m_socket->peerAddress().toString(), m_socket->peerPort(), formatResponse(frame));
    }
}

void ModbusTcpClient::disconnectHost()
{
    m_receiveBuffer.clear();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
    {
        m_socket->disconnectFromHost();
        if (m_socket->state() != QAbstractSocket::UnconnectedState)
            m_socket->waitForDisconnected(1000);
    }
    m_lastIp.clear();
    m_lastPort = 0;
}
