#include "ProtocolFrameCodec.h"

namespace
{
constexpr int kHeaderSize = 7;
constexpr int kChecksumSize = 2;
constexpr quint32 kMaximumPayloadSize = 1024u * 1024u * 1024u;
const QByteArray kFrameMagic("\xAA\x55", 2);
} // namespace

namespace ProtocolFrameCodec
{
quint16 crc16(const QByteArray& data)
{
    quint16 checksum = 0xFFFF;
    for (char byte : data)
    {
        checksum ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit)
            checksum = (checksum & 1) ? quint16((checksum >> 1) ^ 0xA001) : quint16(checksum >> 1);
    }
    return checksum;
}

QByteArray encode(const QByteArray& payload)
{
    QByteArray frame = kFrameMagic;
    frame.append(char(1)); // 报文版本

    const quint32 payloadSize = quint32(payload.size());
    for (int shift = 24; shift >= 0; shift -= 8)
        frame.append(char((payloadSize >> shift) & 0xFF));

    frame.append(payload);
    const quint16 checksum = crc16(payload);
    frame.append(char(checksum >> 8));
    frame.append(char(checksum & 0xFF));
    return frame;
}

bool decodeOne(QByteArray& buffer, QByteArray& payload)
{
    const int magicOffset = buffer.indexOf(kFrameMagic);
    if (magicOffset < 0)
    {
        buffer.clear();
        return false;
    }
    if (magicOffset > 0)
        buffer.remove(0, magicOffset);

    if (buffer.size() < kHeaderSize)
        return false;

    const quint32 payloadSize = (quint32(quint8(buffer[3])) << 24) | (quint32(quint8(buffer[4])) << 16) |
                                (quint32(quint8(buffer[5])) << 8) | quint32(quint8(buffer[6]));
    if (payloadSize > kMaximumPayloadSize)
    {
        buffer.remove(0, kFrameMagic.size());
        return false;
    }

    const int frameSize = kHeaderSize + int(payloadSize) + kChecksumSize;
    if (buffer.size() < frameSize)
        return false;

    const QByteArray decodedPayload = buffer.mid(kHeaderSize, int(payloadSize));
    const quint16 receivedChecksum = (quint16(quint8(buffer[kHeaderSize + int(payloadSize)])) << 8) |
                                     quint16(quint8(buffer[kHeaderSize + int(payloadSize) + 1]));
    buffer.remove(0, frameSize);

    if (crc16(decodedPayload) != receivedChecksum)
        return false;

    payload = decodedPayload;
    return true;
}
} // namespace ProtocolFrameCodec
