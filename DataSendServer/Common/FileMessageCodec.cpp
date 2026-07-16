#include "FileMessageCodec.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <climits>

namespace
{
constexpr quint16 kFileMessageVersion = 1;
const QByteArray kFileMessageMagic("FT01", 4);
const QByteArray kStreamMessageMagic("FTS1", 4);

void writeMagic(QDataStream& stream, const QByteArray& magic)
{
    stream.writeRawData(magic.constData(), magic.size());
}
} // namespace

namespace FileMessageCodec
{
QByteArray encode(const QString& filePath, QString& error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        error = QStringLiteral("无法读取文件：%1").arg(file.errorString());
        return {};
    }

    const QByteArray fileData = file.readAll();
    const QFileInfo fileInfo(file);
    const QString mimeType = QMimeDatabase().mimeTypeForFile(fileInfo).name();

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    writeMagic(stream, kFileMessageMagic);
    stream << kFileMessageVersion << fileInfo.fileName() << mimeType << quint64(fileData.size());
    stream.writeRawData(fileData.constData(), fileData.size());

    error.clear();
    return payload;
}

bool isFileMessage(const QByteArray& payload)
{
    return payload.startsWith(kFileMessageMagic);
}

bool decode(const QByteArray& payload, FileMessage& message, QString& error)
{
    if (!isFileMessage(payload))
        return false;

    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);

    char magic[4];
    if (stream.readRawData(magic, sizeof(magic)) != sizeof(magic))
    {
        error = QStringLiteral("文件消息头不完整");
        return false;
    }

    quint16 version = 0;
    quint64 fileSize = 0;
    stream >> version >> message.fileName >> message.mimeType >> fileSize;
    if (version != kFileMessageVersion || fileSize > quint64(payload.size()) || fileSize > quint64(INT_MAX))
    {
        error = QStringLiteral("文件消息版本或长度无效");
        return false;
    }

    message.data.resize(int(fileSize));
    if (stream.readRawData(message.data.data(), int(fileSize)) != int(fileSize))
    {
        error = QStringLiteral("文件内容不完整");
        return false;
    }

    error.clear();
    return true;
}

QByteArray encodeStreamStart(const QString& fileId, const QString& fileName, const QString& mimeType, quint64 totalSize)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    writeMagic(stream, kStreamMessageMagic);
    stream << quint8(StreamType::Start) << fileId << fileName << mimeType << totalSize;
    return payload;
}

QByteArray encodeStreamChunk(const QString& fileId, quint64 offset, const QByteArray& chunk)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    writeMagic(stream, kStreamMessageMagic);
    stream << quint8(StreamType::Chunk) << fileId << offset << chunk;
    return payload;
}

QByteArray encodeStreamEnd(const QString& fileId)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    writeMagic(stream, kStreamMessageMagic);
    stream << quint8(StreamType::End) << fileId;
    return payload;
}

bool decodeStream(const QByteArray& payload, StreamMessage& message, QString& error)
{
    if (!payload.startsWith(kStreamMessageMagic))
        return false;

    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::BigEndian);

    char magic[4];
    quint8 streamType = 0;
    if (stream.readRawData(magic, sizeof(magic)) != sizeof(magic))
    {
        error = QStringLiteral("流式文件消息头不完整");
        return false;
    }

    stream >> streamType >> message.fileId;
    message.type = StreamType(streamType);
    switch (message.type)
    {
    case StreamType::Start:
        stream >> message.fileName >> message.mimeType >> message.totalSize;
        break;
    case StreamType::Chunk:
        stream >> message.offset >> message.chunk;
        break;
    case StreamType::End:
        break;
    default:
        error = QStringLiteral("未知流式文件消息类型");
        return false;
    }

    if (stream.status() != QDataStream::Ok)
    {
        error = QStringLiteral("流式文件消息内容不完整");
        return false;
    }

    error.clear();
    return true;
}
} // namespace FileMessageCodec
