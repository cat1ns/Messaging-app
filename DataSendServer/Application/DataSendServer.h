#pragma once

#include "../Protocols/HTTP/HTTPSendServer.h"
#include "../Protocols/Modbus/ModbusTcpClient.h"
#include "../Protocols/TCP/TCPSendServer.h"
#include "../Protocols/UDP/UDPSendServer.h"
#include "../Protocols/WebSocket/WSSendServer.h"
#include "ui_DataSendServer.h"

#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QtWidgets/QWidget>

class DataSendServer : public QWidget
{
    Q_OBJECT

public:
    explicit DataSendServer(QWidget* parent = nullptr);
    ~DataSendServer() override;

private:
    enum class Protocol
    {
        Udp,
        Tcp,
        Http,
        WebSocket,
        ModbusTcp
    };

    Protocol currentProtocol() const;
    static bool supportsFileTransfer(Protocol protocol);
    bool isFileMode() const;
    void connectSignals();
    void updateIntervalHint();
    void setUiEnabled(bool enabled);
    void stopSending(const QString& message);
    void sendFile(Protocol protocol, const QString& ip, const QString& port, QString& errorMessage);
    void sendText(
        Protocol protocol, const QString& ip, const QString& port, const QString& data, QString& errorMessage);
    void showReceivedFile(const QString& ip, quint16 port, const QString& fileName, quint64 size);
    void setupModbusUi();
    void updateModbusUi();
    QString buildModbusCommand(QString& errorMessage) const;
public slots:
    void on_pushButton_send_clicked();
    void on_pushButton_stop_clicked();
    void onPauseFileTransfer();
    void onTimeout();
    void onReplyReceived(QString strIp, quint16 port, QString strData);
    void onFileReceived(QString strIp, quint16 port, QString fileName, QString mimeType, QByteArray data);
    void onFilePathReceived(
        QString strIp, quint16 port, QString fileName, QString mimeType, QString localPath, quint64 size);
    void onFileProgress(QString fileName, quint64 completed, quint64 total);

private:
    void setupFileUi();
    void updateFileUi();
    void setupWideLayout();
    void chooseFile();
    void chooseFolder();
    void saveReceivedFile();
    void openReceivedFile();
    UDPSendServer* p_udpSendServer = nullptr;
    TCPSendServer* p_tcpSendServer = nullptr;
    HTTPSendServer* p_httpSendServer = nullptr;
    WSSendServer* p_wsSendServer = nullptr;
    ModbusTcpClient* p_modbusClient = nullptr;
    QTimer* m_timer = nullptr;
    int m_totalCount = 0;
    int m_sentCount = 0;
    int m_intervalMs = 0;
    QWidget* m_modbusPanel = nullptr;
    QComboBox* m_modbusMode = nullptr;
    QSpinBox* m_modbusAddress = nullptr;
    QLabel* m_modbusValueLabel = nullptr;
    QLineEdit* m_modbusValues = nullptr;
    QComboBox* m_sendMode = nullptr;
    QPushButton* m_chooseFileButton = nullptr;
    QPushButton* m_chooseFolderButton = nullptr;
    QPushButton* m_pauseFileButton = nullptr;
    QLabel* m_filePathLabel = nullptr;
    QPushButton* m_saveReceivedButton = nullptr;
    QPushButton* m_openReceivedButton = nullptr;
    QString m_selectedFilePath;
    QString m_receivedTempPath;
    QByteArray m_receivedFileData;
    QString m_receivedFileName;
    QString m_receivedSourcePath;
    QElapsedTimer m_fileSpeedTimer;
    bool m_fileTransferPaused = false;

    Ui::DataSendServerClass ui;
};
