#pragma once
#include <QByteArray>
#include <QString>

namespace FileMessageCodec
{
struct FileMessage
{
    QString fileName;
    QString mimeType;
    QByteArray data;
};
enum class StreamType : quint8
{
    Invalid = 0,
    Start = 1,
    Chunk = 2,
    End = 3
};
struct StreamMessage
{
    StreamType type = StreamType::Invalid;
    QString fileId;
    QString fileName;
    QString mimeType;
    quint64 totalSize = 0;
    quint64 offset = 0;
    QByteArray chunk;
};
QByteArray encode(const QString& filePath, QString& error);
bool decode(const QByteArray& payload, FileMessage& message, QString& error);
bool isFileMessage(const QByteArray& payload);
QByteArray encodeStreamStart(const QString& fileId,
                             const QString& fileName,
                             const QString& mimeType,
                             quint64 totalSize);
QByteArray encodeStreamChunk(const QString& fileId, quint64 offset, const QByteArray& chunk);
QByteArray encodeStreamEnd(const QString& fileId);
bool decodeStream(const QByteArray& payload, StreamMessage& message, QString& error);
} // namespace FileMessageCodec
