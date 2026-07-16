#pragma once
#include <QByteArray>
#include <QtGlobal>
namespace ProtocolFrameCodec
{
// 对有效载荷计算CRC16(Modbus多项式0xA001)。
quint16 crc16(const QByteArray& data);
// 生成：帧头AA55 + 版本 + 4字节大端长度 + 载荷 + 2字节CRC16。
QByteArray encode(const QByteArray& payload);
// 从缓存中提取一帧；返回false表示数据不足、帧头错误或CRC错误。
bool decodeOne(QByteArray& buffer, QByteArray& payload);
} // namespace ProtocolFrameCodec
