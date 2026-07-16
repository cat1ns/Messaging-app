#include "ModbusTcpServer.h"

#include <QRegularExpression>

// Modbus TCP服务端流程：监听连接 → 缓存并切出完整帧 → 按功能码读写寄存器 → 返回响应。
// slotReadData负责TCP收包，processRequest负责Modbus业务，socket->write负责回包。

ModbusTcpServer::ModbusTcpServer(QObject* parent)
    : QObject(parent), m_server(new QTcpServer(this)), m_holdingRegisters(1000, 0)
{
    // 监听端口后，客户端通过IP+端口建立TCP连接，服务端接收连接并创建socket。
    connect(m_server, &QTcpServer::newConnection, this, &ModbusTcpServer::slotNewConnection);
}

ModbusTcpServer::~ModbusTcpServer()
{
    stopListen();
}

void ModbusTcpServer::appendU16(QByteArray& data, quint16 value)
{
    // Modbus字段使用大端序：16位数值先写高字节，再写低字节。
    data.append(char(value >> 8));
    data.append(char(value & 0xff));
}

quint16 ModbusTcpServer::readU16(const QByteArray& data, int offset)
{
    // 将报文中的两个网络字节恢复成地址、数量或寄存器值。
    return (quint16(quint8(data.at(offset))) << 8) | quint8(data.at(offset + 1));
}

bool ModbusTcpServer::startListen(quint16 port, QString& errorMessage)
{
    if (m_server->isListening())
    {
        errorMessage = "Modbus TCP已经在监听中";
        return false;
    }
    // 服务端监听指定端口，客户端随后通过IP+端口建立TCP连接。
    if (!m_server->listen(QHostAddress::Any, port))
    {
        errorMessage = QString("Modbus TCP监听失败：%1").arg(m_server->errorString());
        return false;
    }
    errorMessage = QString("Modbus TCP监听成功，端口：%1").arg(port);
    return true;
}

void ModbusTcpServer::stopListen()
{
    for (QTcpSocket* socket : m_clients)
    {
        socket->disconnect(this);
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    m_clients.clear();
    m_buffers.clear();
    m_server->close();
}

void ModbusTcpServer::slotNewConnection()
{
    while (m_server->hasPendingConnections())
    {
        // 获得客户端socket，保存到m_clients列表，并为其创建独立缓存。
        QTcpSocket* socket = m_server->nextPendingConnection();
        m_clients.append(socket);
        m_buffers.insert(socket, QByteArray());
        // 连接信号槽：客户端socket有数据可读时，调用slotReadData处理；客户端断开时，清理缓存并删除socket。
        connect(socket, &QTcpSocket::readyRead, this, &ModbusTcpServer::slotReadData);
        connect(socket,
                &QTcpSocket::disconnected,
                this,
                [this, socket]()
                {
                    m_clients.removeOne(socket);
                    m_buffers.remove(socket);
                    socket->deleteLater();
                });
    }
}

QByteArray ModbusTcpServer::exceptionResponse(const QByteArray& frame, quint8 function, quint8 exceptionCode) const
{
    // 异常响应：功能码最高位置1，后跟异常码。
    QByteArray response = frame.left(4);
    appendU16(response, 3);
    response.append(frame.at(6));
    response.append(char(function | 0x80));
    response.append(char(exceptionCode));
    return response;
}

QByteArray ModbusTcpServer::processRequest(const QByteArray& frame, QString& summary)
{
    // 处理Modbus TCP请求报文，返回响应报文或异常响应报文，并生成summary用于显示。

    if (frame.size() < 8 || readU16(frame, 2) != 0)
        return {};

    // 获得对应的功能码，位于第7字节：0x03读，0x10写。
    quint8 function = quint8(frame.at(7));
    if (function == 0x03)
    {
        // 读取请求至少12字节：MBAP(7)+功能码(1)+地址(2)+数量(2)。
        if (frame.size() < 12)
            return {};

        // 解析请求中的地址和数量，检查范围是否合法。
        quint16 address = readU16(frame, 8);
        quint16 count = readU16(frame, 10);
        if (count == 0 || count > 125 || int(address) + int(count) > m_holdingRegisters.size())
            return exceptionResponse(frame, function, 0x02);

        // 构造响应：MBAP(7)+功能码(1)+字节数(1)+寄存器值(2*count)。
        QByteArray pdu;
        pdu.append(char(function));
        pdu.append(char(count * 2));

        // 读取寄存器的值按大端序写入pdu，并记录到values列表用于summary显示。
        QStringList values;
        for (int i = 0; i < count; ++i)
        {
            // 寄存器读值
            quint16 value = m_holdingRegisters.at(address + i);
            appendU16(pdu, value);
            values << QString::number(value);
        }
        // 写入MBAP头部：Transaction ID(2)+Protocol ID(2)+Length(2)+Unit ID(1)。
        QByteArray response = frame.left(4);
        appendU16(response, quint16(1 + pdu.size()));
        response.append(frame.at(6));
        // 写入功能码和寄存器值。
        response.append(pdu);
        summary =
            QString("Modbus读保持寄存器：地址%1，数量%2，返回[%3]").arg(address).arg(count).arg(values.join(", "));
        return response;
    }
    if (function == 0x10)
    {
        // 请求至少13字节：MBAP(7)+功能码(1)+地址(2)+数量(2)+字节数(1)+数据(N)。
        if (frame.size() < 13)
            return {};

        quint16 address = readU16(frame, 8);
        quint16 count = readU16(frame, 10);
        quint8 byteCount = quint8(frame.at(12));
        if (count == 0 || count > 123 || byteCount != count * 2 || frame.size() < 13 + byteCount ||
            int(address) + int(count) > m_holdingRegisters.size())
            return exceptionResponse(frame, function, 0x03);

        // 写入寄存器数值，并将写入的值保存到本地寄存器m_holdingRegisters中，同时记录到values列表用于summary显示。
        QByteArray text;
        QStringList values;
        for (int i = 0; i < count; ++i)
        {
            quint16 value = readU16(frame, 13 + i * 2);
            // 写入数值至本地寄存器
            m_holdingRegisters[address + i] = value;
            values << QString::number(value);
            text.append(char(value >> 8));
            text.append(char(value & 0xff));
        }

        while (!text.isEmpty() && text.endsWith('\0'))
            text.chop(1);

        // 构造响应：MBAP(7)+功能码(1)+地址(2)+数量(2)。
        QByteArray response = frame.left(4);
        appendU16(response, 6);
        response.append(frame.at(6));
        response.append(char(function));
        appendU16(response, address);
        appendU16(response, count);
        // 尝试将写入的字节数据解码为UTF-8字符串，如果包含替换字符，则说明不是有效的UTF-8编码。
        QString decoded = QString::fromUtf8(text);
        summary = decoded.contains(QChar::ReplacementCharacter)
                      ? QString("Modbus写多个保持寄存器：地址%1，值[%2]").arg(address).arg(values.join(", "))
                      : QString("Modbus写多个保持寄存器：地址%1，值[%2]").arg(address).arg(values.join(", "));

        return response;
    }
    return exceptionResponse(frame, function, 0x01);
}

void ModbusTcpServer::slotReadData()
{
    // 响应客户端socket的readyRead信号，处理Modbus TCP请求。
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;
    // 读取socket缓冲区的所有数据，追加到当前socket的缓存中,处理拆包/粘包。
    QByteArray& buffer = m_buffers[socket];
    buffer.append(socket->readAll());
    while (buffer.size() >= 7)
    {
        // MBAP固定7字节，Length位于偏移4；完整帧长度=6+Length。
        quint16 length = readU16(buffer, 4);
        int frameLength = 6 + length;
        if (length < 2 || length > 260)
        {
            buffer.clear();
            return;
        }
        if (buffer.size() < frameLength)
            return;
        // 提取一帧数据，移除缓存中已处理的部分。
        QByteArray frame = buffer.left(frameLength);
        buffer.remove(0, frameLength);

        // 解析请求帧，生成响应帧以及UI显示的summary。
        QString summary;
        QByteArray response = processRequest(frame, summary);

        // 向客户端socket发送响应，若是广播请求则response为空，不发送。
        if (!response.isEmpty())
            socket->write(response);
        // 发送到UI显示的summary，包含读写寄存器的地址、数量和值等信息。
        if (!summary.isEmpty())
            emit sigDataReceived(socket->peerAddress().toString(), socket->peerPort(), summary);
    }
}

bool ModbusTcpServer::setRegistersFromText(const QString& data, QString& errorMessage)
{
    // 本地寄存器信息读写
    QStringList parts = data.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    quint16 address = 0;
    QList<quint16> values;
    if (!parts.isEmpty() && parts.first().compare("READ", Qt::CaseInsensitive) == 0)
    {
        if (parts.size() != 3)
        {
            errorMessage = "格式：READ 起始地址 数量";
            return false;
        }
        bool okAddress = false, okCount = false;
        address = parts.at(1).toUShort(&okAddress, 0);
        quint16 count = parts.at(2).toUShort(&okCount, 0);
        if (!okAddress || !okCount || count == 0 || count > 125 ||
            int(address) + int(count) > m_holdingRegisters.size())
        {
            errorMessage = "读取地址或数量无效";
            return false;
        }
        QStringList result;
        for (int i = 0; i < count; ++i)
            result << QString::number(m_holdingRegisters.at(address + i));
        errorMessage =
            QString("本地寄存器读取成功：地址%1，数量%2，值[%3]").arg(address).arg(count).arg(result.join(", "));
        return true;
    }
    if (!parts.isEmpty() && parts.first().compare("WRITE", Qt::CaseInsensitive) == 0)
    {
        if (parts.size() < 3)
        {
            errorMessage = "格式：WRITE 起始地址 值1 值2...";
            return false;
        }
        bool ok = false;
        address = parts.at(1).toUShort(&ok, 0);
        if (!ok)
        {
            errorMessage = "起始地址无效";
            return false;
        }
        for (int i = 2; i < parts.size(); ++i)
        {
            quint16 value = parts.at(i).toUShort(&ok, 0);
            if (!ok)
            {
                errorMessage = QString("寄存器值无效：%1").arg(parts.at(i));
                return false;
            }
            values.append(value);
        }
    }
    else
    {
        QByteArray text = data.toUtf8();
        for (int i = 0; i < text.size(); i += 2)
        {
            quint16 value = quint16(quint8(text.at(i))) << 8;
            if (i + 1 < text.size())
                value |= quint8(text.at(i + 1));
            values.append(value);
        }
    }
    if (values.isEmpty() || int(address) + values.size() > m_holdingRegisters.size())
    {
        errorMessage = "寄存器数据为空或超出0~999范围";
        return false;
    }
    // 将解析出的寄存器值写入本地寄存器。
    for (int i = 0; i < values.size(); ++i)
        m_holdingRegisters[address + i] = values.at(i);
    errorMessage =
        QString("已更新保持寄存器：地址%1，数量%2；客户端可用 READ %1 %2 读取").arg(address).arg(values.size());
    return true;
}

void ModbusTcpServer::sendReply(const QString& data, QString& errorMessage)
{
    setRegistersFromText(data, errorMessage);
}
