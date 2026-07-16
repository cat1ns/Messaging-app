#pragma once

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVector>

class ModbusTcpServer : public QObject
{
    Q_OBJECT
public:
    explicit ModbusTcpServer(QObject* parent = nullptr);
    ~ModbusTcpServer() override;

    bool startListen(quint16 port, QString& errorMessage);
    void stopListen();
    void sendReply(const QString& data, QString& errorMessage);

signals:
    void sigDataReceived(QString ip, quint16 port, QString data);

private slots:
    void slotNewConnection();
    void slotReadData();

private:
    QByteArray processRequest(const QByteArray& frame, QString& summary);
    QByteArray exceptionResponse(const QByteArray& frame, quint8 function, quint8 exceptionCode) const;
    bool setRegistersFromText(const QString& data, QString& errorMessage);
    static void appendU16(QByteArray& data, quint16 value);
    static quint16 readU16(const QByteArray& data, int offset);

    QTcpServer* m_server = nullptr;
    QList<QTcpSocket*> m_clients;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QVector<quint16> m_holdingRegisters;
};
