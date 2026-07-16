#pragma once

#include "../Protocols/HTTP/HTTPRecvServer.h"
#include "../Protocols/Modbus/ModbusTcpServer.h"
#include "../Protocols/TCP/TCPRecvServer.h"
#include "../Protocols/UDP/UDPRecvServer.h"
#include "../Protocols/WebSocket/WSRecvServer.h"
#include "ui_DataReceiveServer.h"

#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QtWidgets/QMainWindow>

class DataReceiveServer : public QMainWindow
{
    Q_OBJECT

public:
    explicit DataReceiveServer(QWidget* parent = nullptr);
    ~DataReceiveServer() override;

public slots:
    void on_pushButtonStart_clicked();
    void on_pushButtonStop_clicked();
    void onDataReceived(QString strIp, quint16 port, QString strData);
    void onFileReceived(QString strIp, quint16 port, QString fileName, QString mimeType, QByteArray data);
    void onFilePathReceived(
        QString strIp, quint16 port, QString fileName, QString mimeType, QString localPath, quint64 size);
    void onFileProgress(QString fileName, quint64 completed, quint64 total);
    void on_pushButtonReply_clicked();

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
    bool isFileReplyMode() const;
    void connectSignals();
    void startReceiver(Protocol protocol, quint16 port, QString& errorMessage);
    void stopReceiver(Protocol protocol);
    void sendTextReply(Protocol protocol, const QString& data, QString& errorMessage);
    void sendFileReply(Protocol protocol, QString& errorMessage);
    void showReceivedFile(
        const QString& ip, quint16 port, const QString& fileName, const QString& mimeType, quint64 size);
    void setupModbusUi();
    void updateModbusUi();
    QString buildModbusOperation(QString& errorMessage) const;
    void setupFileUi();
    void updateFileUi();
    void setupWideLayout();
    void chooseReplyFile();
    void chooseReplyFolder();
    void saveReceivedFile();
    void openReceivedFile();

    UDPRecvServer* p_UDPReceiver = nullptr;
    TCPRecvServer* p_TCPReceiver = nullptr;
    HTTPRecvServer* p_HTTPReceiver = nullptr;
    WSRecvServer* p_WSReceiver = nullptr;
    ModbusTcpServer* p_ModbusReceiver = nullptr;
    QWidget* m_modbusPanel = nullptr;
    QComboBox* m_modbusMode = nullptr;
    QSpinBox* m_modbusAddress = nullptr;
    QLabel* m_modbusValueLabel = nullptr;
    QLineEdit* m_modbusValues = nullptr;
    QComboBox* m_replyMode = nullptr;
    QPushButton* m_chooseReplyFileButton = nullptr;
    QPushButton* m_chooseReplyFolderButton = nullptr;
    QLabel* m_replyFileLabel = nullptr;
    QPushButton* m_saveReceivedButton = nullptr;
    QPushButton* m_openReceivedButton = nullptr;
    QString m_replyFilePath;
    QString m_receivedTempPath;
    QByteArray m_receivedFileData;
    QString m_receivedFileName;
    QString m_receivedSourcePath;
    QProgressBar* m_fileProgress = nullptr;
    QElapsedTimer m_fileSpeedTimer;
    Ui::DataReceiveServerClass ui;
};
