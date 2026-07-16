#pragma once

#include <QObject>
#include <QTcpSocket>

class ModbusTcpClient : public QObject
{
    Q_OBJECT
public:
    explicit ModbusTcpClient(QObject* parent = nullptr);
    ~ModbusTcpClient() override;

    void sendModbusData(const QString& ip, const QString& port, const QString& command, QString& errorMessage);
    void disconnectHost();

signals:
    void sigReplyReceived(QString ip, quint16 port, QString data);

private slots:
    void slotReadReply();

private:
    QByteArray buildRequest(const QString& command, QString& description, QString& errorMessage);
    QString formatResponse(const QByteArray& frame) const;
    static void appendU16(QByteArray& data, quint16 value);
    static quint16 readU16(const QByteArray& data, int offset);

    QTcpSocket* m_socket = nullptr;
    QByteArray m_receiveBuffer;
    QString m_lastIp;
    quint16 m_lastPort = 0;
    quint16 m_transactionId = 0;
};
