#include "DataSendServer.h"

#include "../Common/AppTempDirectory.h"

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QUrl>

DataSendServer::DataSendServer(QWidget* parent) : QWidget(parent)
{
    ui.setupUi(this);
    setupWideLayout();
    p_udpSendServer = new UDPSendServer(this);
    p_tcpSendServer = new TCPSendServer(this);
    p_httpSendServer = new HTTPSendServer(this);
    p_wsSendServer = new WSSendServer(this);
    p_modbusClient = new ModbusTcpClient(this);
    m_timer = new QTimer(this);
    connectSignals();

    ui.lineEdit_IP->setText("127.0.0.1"); // 本机
    ui.lineEdit_Port->setText("8888");
    ui.comboBox_protocol->addItems({"UDP", "TCP", "HTTP", "WebSocket", "Modbus TCP"});
    ui.text_send->setPlainText(QDate::currentDate().toString("yyyyMMdd") + " 发送信息到客户端。");
    setupModbusUi();
    setupFileUi();
    connect(ui.comboBox_protocol,
            &QComboBox::currentTextChanged,
            this,
            [this]()
            {
                updateModbusUi();
                updateFileUi();
            });
    connect(m_modbusMode,
            &QComboBox::currentTextChanged,
            this,
            [this]()
            {
                updateModbusUi();
            });
    ui.pushButton_stop->setEnabled(false);
    updateIntervalHint();
}

void DataSendServer::connectSignals()
{
    connect(ui.pushButton_send, &QPushButton::clicked, this, &DataSendServer::on_pushButton_send_clicked);
    connect(ui.pushButton_stop, &QPushButton::clicked, this, &DataSendServer::on_pushButton_stop_clicked);
    connect(m_timer, &QTimer::timeout, this, &DataSendServer::onTimeout);

    connect(p_udpSendServer, &UDPSendServer::sigReplyReceived, this, &DataSendServer::onReplyReceived);
    connect(p_tcpSendServer, &TCPSendServer::sigReplyReceived, this, &DataSendServer::onReplyReceived);
    connect(p_httpSendServer, &HTTPSendServer::sigReplyReceived, this, &DataSendServer::onReplyReceived);
    connect(p_wsSendServer, &WSSendServer::sigReplyReceived, this, &DataSendServer::onReplyReceived);
    connect(p_modbusClient, &ModbusTcpClient::sigReplyReceived, this, &DataSendServer::onReplyReceived);

    connect(p_tcpSendServer, &TCPSendServer::sigFileReceived, this, &DataSendServer::onFileReceived);
    connect(p_tcpSendServer, &TCPSendServer::sigFilePathReceived, this, &DataSendServer::onFilePathReceived);
    connect(p_tcpSendServer, &TCPSendServer::sigFileProgress, this, &DataSendServer::onFileProgress);
    connect(p_httpSendServer, &HTTPSendServer::sigFileReceived, this, &DataSendServer::onFileReceived);
    connect(p_httpSendServer, &HTTPSendServer::sigFilePathReceived, this, &DataSendServer::onFilePathReceived);
    connect(p_httpSendServer, &HTTPSendServer::sigFileProgress, this, &DataSendServer::onFileProgress);
    connect(p_wsSendServer, &WSSendServer::sigFileReceived, this, &DataSendServer::onFileReceived);
    connect(p_wsSendServer, &WSSendServer::sigFilePathReceived, this, &DataSendServer::onFilePathReceived);
    connect(p_wsSendServer, &WSSendServer::sigFileProgress, this, &DataSendServer::onFileProgress);

    connect(ui.spinBox_freq, QOverload<int>::of(&QSpinBox::valueChanged), this, &DataSendServer::updateIntervalHint);
    connect(
        ui.spinBox_duration, QOverload<int>::of(&QSpinBox::valueChanged), this, &DataSendServer::updateIntervalHint);
}

DataSendServer::Protocol DataSendServer::currentProtocol() const
{
    const QString protocol = ui.comboBox_protocol->currentText();
    if (protocol == "TCP")
        return Protocol::Tcp;
    if (protocol == "HTTP")
        return Protocol::Http;
    if (protocol == "WebSocket")
        return Protocol::WebSocket;
    if (protocol == "Modbus TCP")
        return Protocol::ModbusTcp;
    return Protocol::Udp;
}

bool DataSendServer::supportsFileTransfer(Protocol protocol)
{
    return protocol == Protocol::Tcp || protocol == Protocol::Http || protocol == Protocol::WebSocket;
}

bool DataSendServer::isFileMode() const
{
    return m_sendMode && supportsFileTransfer(currentProtocol()) && m_sendMode->currentText() == QStringLiteral("文件");
}

void DataSendServer::setupWideLayout()
{
    setFixedSize(560, 920);
    ui.frame_TitleBar->setGeometry(0, 0, 560, 38);
    ui.label_Dot1->move(497, 13);
    ui.label_Dot2->move(516, 13);
    ui.label_Dot3->move(535, 13);
    ui.label_Title->setGeometry(0, 54, 560, 24);
    ui.frame_Target->setGeometry(18, 88, 524, 118);
    ui.label_GroupTarget->setGeometry(0, 0, 524, 34);
    ui.lineEdit_IP->setGeometry(70, 49, 180, 28);
    ui.lineEdit_Port->setGeometry(70, 84, 180, 28);
    ui.label_3->setGeometry(285, 49, 145, 28);
    ui.comboBox_protocol->setGeometry(360, 84, 145, 28);
    ui.frame_Timer->setGeometry(18, 216, 524, 108);
    ui.label_GroupTimer->setGeometry(0, 0, 524, 34);
    ui.spinBox_duration->setGeometry(150, 50, 75, 28);
    ui.label_5->setGeometry(285, 50, 105, 28);
    ui.spinBox_freq->setGeometry(400, 50, 85, 28);
    ui.label_interval_hint->setGeometry(20, 82, 484, 18);
    ui.frame_Send->setGeometry(18, 338, 524, 220);
    ui.label_GroupSend->setGeometry(0, 0, 524, 34);
    ui.text_send->setGeometry(20, 48, 484, 154);
    ui.pushButton_send->setGeometry(18, 572, 252, 42);
    ui.pushButton_stop->setGeometry(290, 572, 252, 42);
    ui.label_ErrorMessage->setGeometry(18, 624, 524, 38);
    ui.progressBar->setGeometry(18, 672, 524, 8);
    ui.frame_Reply->setGeometry(18, 692, 524, 210);
    ui.label_GroupReply->setGeometry(0, 0, 524, 34);
    ui.textEdit_ReplyDisplay->setGeometry(20, 48, 484, 145);
}

void DataSendServer::setupFileUi()
{
    const QString primaryFileButtonStyle = QStringLiteral(
        "QPushButton{color:#FFFFFF;background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #5450C9,stop:1 #22A486);"
        "border:none;border-radius:8px;font-size:13px;font-weight:500;min-height:0px;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #6762D8,stop:1 #28B997);}"
        "QPushButton:pressed{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #4541B3,stop:1 #1E8F76);}"
        "QPushButton:disabled{color:#9A9AB8;background:#E1E1E1;}");

    m_sendMode = new QComboBox(ui.frame_Send);
    m_sendMode->addItems({QStringLiteral("文本"), QStringLiteral("文件")});
    m_sendMode->setGeometry(20, 46, 105, 32);
    m_chooseFileButton = new QPushButton(QStringLiteral("选择文件"), ui.frame_Send);
    m_chooseFileButton->setGeometry(145, 46, 110, 32);
    m_chooseFileButton->setStyleSheet(primaryFileButtonStyle);
    m_chooseFolderButton = new QPushButton(QStringLiteral("选择文件夹"), ui.frame_Send);
    m_chooseFolderButton->setGeometry(270, 46, 120, 32);
    m_chooseFolderButton->setStyleSheet(primaryFileButtonStyle);
    m_filePathLabel = new QLabel(QStringLiteral("尚未选择文件"), ui.frame_Send);
    m_filePathLabel->setGeometry(20, 82, 484, 25);
    m_filePathLabel->setStyleSheet(QStringLiteral("color:#5550C5;font-size:12px;background:#F7F7FF;border:1px solid "
                                                  "#D4CFFF;border-radius:5px;padding-left:8px;"));
    m_saveReceivedButton = new QPushButton(QStringLiteral("下载/另存为"), ui.frame_Reply);
    m_saveReceivedButton->setGeometry(284, 164, 105, 30);
    m_saveReceivedButton->setStyleSheet(primaryFileButtonStyle);
    m_saveReceivedButton->hide();
    m_openReceivedButton = new QPushButton(QStringLiteral("打开查看"), ui.frame_Reply);
    m_openReceivedButton->setGeometry(399, 164, 105, 30);
    m_openReceivedButton->setStyleSheet(primaryFileButtonStyle);
    m_openReceivedButton->hide();
    m_pauseFileButton = new QPushButton(QStringLiteral("暂停传输"), this);
    m_pauseFileButton->setGeometry(198, 572, 164, 42);
    m_pauseFileButton->setStyleSheet(primaryFileButtonStyle);
    m_pauseFileButton->hide();
    m_pauseFileButton->setEnabled(false);

    connect(m_chooseFileButton, &QPushButton::clicked, this, &DataSendServer::chooseFile);
    connect(m_chooseFolderButton, &QPushButton::clicked, this, &DataSendServer::chooseFolder);
    connect(m_saveReceivedButton, &QPushButton::clicked, this, &DataSendServer::saveReceivedFile);
    connect(m_openReceivedButton, &QPushButton::clicked, this, &DataSendServer::openReceivedFile);
    connect(m_pauseFileButton, &QPushButton::clicked, this, &DataSendServer::onPauseFileTransfer);
    connect(m_sendMode,
            &QComboBox::currentTextChanged,
            this,
            [this](const QString& mode)
            {
                bool f = mode == QStringLiteral("文件");
                ui.text_send->setEnabled(!f);
                m_chooseFileButton->setEnabled(f);
                m_chooseFolderButton->setEnabled(f);
                updateFileUi();
            });
    m_chooseFileButton->setEnabled(false);
    m_chooseFolderButton->setEnabled(false);
    updateFileUi();
}

void DataSendServer::updateFileUi()
{
    const bool supported = supportsFileTransfer(currentProtocol());
    const bool fileMode = isFileMode();

    m_sendMode->setVisible(supported);
    m_chooseFileButton->setVisible(supported);
    m_chooseFolderButton->setVisible(supported);
    m_filePathLabel->setVisible(supported);

    if (!supported)
        m_sendMode->setCurrentIndex(0);
    // 支持文件的协议为模式选择区预留一行；其他协议恢复原始文本编辑区域。
    ui.text_send->setGeometry(supported ? QRect(20, 116, 484, 86) : QRect(20, 48, 484, 154));
    m_pauseFileButton->setVisible(fileMode);
    ui.pushButton_send->setGeometry(fileMode ? QRect(18, 572, 164, 42) : QRect(18, 572, 252, 42));
    ui.pushButton_stop->setGeometry(fileMode ? QRect(378, 572, 164, 42) : QRect(290, 572, 252, 42));
}

void DataSendServer::onPauseFileTransfer()
{
    m_fileTransferPaused = !m_fileTransferPaused;

    switch (currentProtocol())
    {
    case Protocol::Tcp:
        if (m_fileTransferPaused)
            p_tcpSendServer->pauseFileTransfer();
        else
            p_tcpSendServer->resumeFileTransfer();
        break;
    case Protocol::Http:
        if (m_fileTransferPaused)
            p_httpSendServer->pauseFileTransfer();
        else
            p_httpSendServer->resumeFileTransfer();
        break;
    case Protocol::WebSocket:
        if (m_fileTransferPaused)
            p_wsSendServer->pauseFileTransfer();
        else
            p_wsSendServer->resumeFileTransfer();
        break;
    default:
        return;
    }
    m_pauseFileButton->setText(m_fileTransferPaused ? QStringLiteral("继续传输") : QStringLiteral("暂停传输"));

    ui.label_ErrorMessage->setText(m_fileTransferPaused ? QStringLiteral("文件传输已暂停")
                                                        : QStringLiteral("文件传输已继续"));
}

void DataSendServer::chooseFile()
{
    m_selectedFilePath = QFileDialog::getOpenFileName(this, QStringLiteral("选择要发送的文件"));
    if (!m_selectedFilePath.isEmpty())
    {
        m_filePathLabel->setText(QFileInfo(m_selectedFilePath).fileName());
        m_filePathLabel->setToolTip(m_selectedFilePath);
    }
}
void DataSendServer::chooseFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择要发送的文件夹"));
    if (dir.isEmpty())
        return;
    QString zip = AppTempDirectory::path() + "/" + QFileInfo(dir).fileName() + ".zip";
    QFile::remove(zip);

    QString script = QString("Compress-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                         .arg(QString(dir).replace("'", "''"), QString(zip).replace("'", "''"));
    if (QProcess::execute("powershell", {"-NoProfile", "-Command", script}) == 0)
    {
        m_selectedFilePath = zip;
        m_filePathLabel->setText(QFileInfo(zip).fileName());
        m_filePathLabel->setToolTip(zip);
    }
    else
        ui.label_ErrorMessage->setText(QStringLiteral("文件夹压缩失败"));
}
void DataSendServer::saveReceivedFile()
{
    if (m_receivedFileData.isEmpty() && m_receivedSourcePath.isEmpty())
        return;

    QString p = QFileDialog::getSaveFileName(this, QStringLiteral("下载/保存回复文件"), m_receivedFileName);
    if (p.isEmpty())
        return;

    bool ok = false;
    if (!m_receivedSourcePath.isEmpty())
    {
        QFile::remove(p);
        ok = QFile::copy(m_receivedSourcePath, p);
    }
    else
    {
        QFile f(p);
        if (f.open(QIODevice::WriteOnly))
        {
            ok = f.write(m_receivedFileData) == m_receivedFileData.size();
            f.close();
        }
    }

    if (ok)
    {
        m_receivedTempPath = p;
        QMessageBox::information(
            this, QStringLiteral("下载完成"), QStringLiteral("文件下载成功！\n保存路径：%1").arg(p));
    }
    else
        QMessageBox::warning(
            this, QStringLiteral("下载失败"), QStringLiteral("文件保存失败，请检查磁盘空间和保存路径。"));
}
void DataSendServer::openReceivedFile()
{
    if (!m_receivedTempPath.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_receivedTempPath));
    else
        saveReceivedFile();
}
void DataSendServer::onFileReceived(QString strIp, quint16 port, QString fileName, QString mimeType, QByteArray data)
{
    Q_UNUSED(mimeType);
    m_receivedFileName = fileName;
    m_receivedFileData = data;
    m_receivedTempPath = AppTempDirectory::path() + "/" + fileName;
    QFile f(m_receivedTempPath);
    if (f.open(QIODevice::WriteOnly))
    {
        f.write(data);
        f.close();
    }
    showReceivedFile(strIp, port, fileName, quint64(data.size()));
}
void DataSendServer::onFilePathReceived(
    QString strIp, quint16 port, QString fileName, QString mimeType, QString localPath, quint64 size)
{
    Q_UNUSED(mimeType);
    m_receivedFileName = fileName;
    m_receivedFileData.clear();
    m_receivedSourcePath = localPath;
    m_receivedTempPath = localPath;
    showReceivedFile(strIp, port, fileName, size);
}

void DataSendServer::showReceivedFile(const QString& ip, quint16 port, const QString& fileName, quint64 size)
{
    ui.textEdit_ReplyDisplay->setGeometry(20, 48, 484, 105);
    ui.textEdit_ReplyDisplay->setText(
        QStringLiteral("收到文件回复：%1\n来源：%2:%3\n大小：%4 字节").arg(fileName, ip).arg(port).arg(size));
    m_saveReceivedButton->show();
    m_openReceivedButton->show();
    m_saveReceivedButton->raise();
    m_openReceivedButton->raise();
}
void DataSendServer::onFileProgress(QString fileName, quint64 completed, quint64 total)
{
    if (total == 0)
        return;
    if (!m_fileSpeedTimer.isValid() || completed <= 1024 * 1024)
        m_fileSpeedTimer.restart();
    double seconds = qMax(0.001, m_fileSpeedTimer.elapsed() / 1000.0);
    double speed = completed / 1048576.0 / seconds;
    ui.progressBar->setMaximum(10000);
    ui.progressBar->setValue(int(completed * 10000 / total));
    ui.label_ErrorMessage->setText(QStringLiteral("正在传输 %1：%2 / %3 MB（%4%）  %5 MB/s")
                                       .arg(fileName)
                                       .arg(completed / 1048576.0, 0, 'f', 1)
                                       .arg(total / 1048576.0, 0, 'f', 1)
                                       .arg(completed * 100 / total)
                                       .arg(speed, 0, 'f', 1));
    QApplication::processEvents();
}

void DataSendServer::setupModbusUi()
{
    m_modbusPanel = new QWidget(ui.frame_Send);
    m_modbusPanel->setGeometry(ui.text_send->geometry());
    QGridLayout* layout = new QGridLayout(m_modbusPanel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(8);

    m_modbusMode = new QComboBox(m_modbusPanel);
    m_modbusMode->addItems({QStringLiteral("读取"), QStringLiteral("写入")});
    m_modbusAddress = new QSpinBox(m_modbusPanel);
    m_modbusAddress->setRange(0, 999);
    m_modbusValueLabel = new QLabel(m_modbusPanel);
    m_modbusValues = new QLineEdit(m_modbusPanel);

    layout->addWidget(new QLabel(QStringLiteral("操作模式"), m_modbusPanel), 0, 0);
    layout->addWidget(m_modbusMode, 0, 1);
    layout->addWidget(new QLabel(QStringLiteral("起始寄存器"), m_modbusPanel), 0, 2);
    layout->addWidget(m_modbusAddress, 0, 3);
    layout->addWidget(m_modbusValueLabel, 1, 0);
    layout->addWidget(m_modbusValues, 1, 1, 1, 3);
    m_modbusPanel->hide();
}

void DataSendServer::updateModbusUi()
{
    const bool isModbus = ui.comboBox_protocol->currentText() == "Modbus TCP";
    ui.text_send->setVisible(!isModbus);
    m_modbusPanel->setVisible(isModbus);
    if (!isModbus)
        return;

    const bool isRead = m_modbusMode->currentText() == QStringLiteral("读取");
    m_modbusValueLabel->setText(isRead ? QStringLiteral("读取数量") : QStringLiteral("写入值"));

    m_modbusValues->setPlaceholderText(isRead ? QStringLiteral("1~125，例如：10")
                                              : QStringLiteral("以英文逗号分隔，例如：100,200,0x1234"));

    if (m_modbusValues->text().isEmpty())
        m_modbusValues->setText(isRead ? "1" : "0");
}

QString DataSendServer::buildModbusCommand(QString& errorMessage) const
{
    const int address = m_modbusAddress->value();
    if (m_modbusMode->currentText() == QStringLiteral("读取"))
    {
        bool ok = false;
        int count = m_modbusValues->text().trimmed().toInt(&ok);
        if (!ok || count < 1 || count > 125 || address + count > 1000)
        {
            errorMessage = QStringLiteral("读取数量必须为1~125，且不能超过寄存器999");
            return {};
        }
        return QString("READ %1 %2").arg(address).arg(count);
    }

    QStringList values = m_modbusValues->text().split(',', Qt::SkipEmptyParts);
    if (values.isEmpty() || values.size() > 123 || address + values.size() > 1000)
    {
        errorMessage = QStringLiteral("写入值数量必须为1~123，且不能超过寄存器999");
        return {};
    }
    for (QString& value : values)
    {
        value = value.trimmed();
        bool ok = false;
        value.toUShort(&ok, 0);
        if (!ok)
        {
            errorMessage = QStringLiteral("写入值无效：%1").arg(value);
            return {};
        }
    }
    return QString("WRITE %1 %2").arg(address).arg(values.join(' '));
}

DataSendServer::~DataSendServer()
{
    m_timer->stop();
    p_tcpSendServer->disconnectHost();
    p_wsSendServer->disconnectHost();
    p_modbusClient->disconnectHost();
    AppTempDirectory::cleanup();
}

void DataSendServer::updateIntervalHint()
{
    const int duration = ui.spinBox_duration->value();
    const int totalCount = ui.spinBox_freq->value();
    m_intervalMs = qMax(1, duration * 60000 / totalCount);
    ui.label_interval_hint->setText(
        QString("每次发送间隔 = %1ms（共 %2 次，持续 %3 分钟）").arg(m_intervalMs).arg(totalCount).arg(duration));
}

void DataSendServer::setUiEnabled(bool enabled)
{
    ui.lineEdit_IP->setEnabled(enabled);
    ui.lineEdit_Port->setEnabled(enabled);
    ui.comboBox_protocol->setEnabled(enabled);
    ui.text_send->setEnabled(enabled);
    m_modbusMode->setEnabled(enabled);
    m_modbusAddress->setEnabled(enabled);
    m_modbusValues->setEnabled(enabled);
    ui.spinBox_duration->setEnabled(enabled);
    ui.spinBox_freq->setEnabled(enabled);
    ui.pushButton_send->setEnabled(enabled);
    m_pauseFileButton->setEnabled(!enabled);
    m_sendMode->setEnabled(enabled);
    m_chooseFileButton->setEnabled(enabled && isFileMode());
    m_chooseFolderButton->setEnabled(enabled && isFileMode());
    ui.pushButton_stop->setEnabled(!enabled);
}

void DataSendServer::stopSending(const QString& message)
{
    m_timer->stop();
    setUiEnabled(true);
    ui.label_ErrorMessage->setText(message);
}

void DataSendServer::sendFile(Protocol protocol, const QString& ip, const QString& port, QString& errorMessage)
{
    switch (protocol)
    {
    case Protocol::Tcp:
        p_tcpSendServer->sendTcpFile(ip, port, m_selectedFilePath, errorMessage);
        break;
    case Protocol::Http:
        p_httpSendServer->sendHttpFile(ip, port, m_selectedFilePath, errorMessage);
        break;
    case Protocol::WebSocket:
        p_wsSendServer->sendWsFile(ip, port, m_selectedFilePath, errorMessage);
        break;
    default:
        errorMessage = QStringLiteral("当前协议不支持文件传输");
        break;
    }
}

void DataSendServer::sendText(
    Protocol protocol, const QString& ip, const QString& port, const QString& data, QString& errorMessage)
{
    switch (protocol)
    {
    case Protocol::Tcp:
        p_tcpSendServer->sendTcpData(ip, port, data, errorMessage);
        break;
    case Protocol::Http:
        p_httpSendServer->sendHttpData(ip, port, data, errorMessage);
        break;
    case Protocol::WebSocket:
        p_wsSendServer->sendWsData(ip, port, data, errorMessage);
        break;
    case Protocol::ModbusTcp:
        p_modbusClient->sendModbusData(ip, port, data, errorMessage);
        break;
    case Protocol::Udp:
        p_udpSendServer->sendUdpData(ip, port, data, errorMessage);
        break;
    }
}

void DataSendServer::on_pushButton_send_clicked()
{
    const QString ip = ui.lineEdit_IP->text().trimmed();
    const QString port = ui.lineEdit_Port->text().trimmed();
    if (ip.isEmpty() || port.isEmpty())
    {
        ui.label_ErrorMessage->setText(QStringLiteral("请填写 IP 和端口！"));
        return;
    }

    const int duration = ui.spinBox_duration->value();
    m_totalCount = isFileMode() ? 1 : ui.spinBox_freq->value();
    m_intervalMs = qMax(1, duration * 60000 / m_totalCount);
    m_sentCount = 0;
    ui.progressBar->setMaximum(m_totalCount);
    ui.progressBar->setValue(0);

    setUiEnabled(false);
    m_fileTransferPaused = false;
    m_pauseFileButton->setText(QStringLiteral("暂停传输"));
    ui.label_ErrorMessage->setText(QString("开始发送，共 %1 次，间隔 %2ms...").arg(m_totalCount).arg(m_intervalMs));

    onTimeout();

    if (isFileMode())
    {
        stopSending(ui.label_ErrorMessage->text());
        return;
    }

    if (!ui.pushButton_send->isEnabled())
        m_timer->start(m_intervalMs);
}

void DataSendServer::on_pushButton_stop_clicked()
{
    m_fileTransferPaused = false;
    p_tcpSendServer->resumeFileTransfer();
    p_httpSendServer->resumeFileTransfer();
    p_wsSendServer->resumeFileTransfer();

    m_pauseFileButton->setText(QStringLiteral("暂停传输"));

    p_tcpSendServer->disconnectHost();
    p_wsSendServer->disconnectHost();
    p_modbusClient->disconnectHost();
    stopSending(QString("已手动停止，已发送 %1 / %2 次").arg(m_sentCount).arg(m_totalCount));
}

void DataSendServer::onTimeout()
{
    if (m_sentCount >= m_totalCount)
    {
        stopSending(QString("发送完成！共发送 %1 次").arg(m_sentCount));
        return;
    }

    const QString ip = ui.lineEdit_IP->text().trimmed();
    const QString port = ui.lineEdit_Port->text().trimmed();
    const Protocol protocol = currentProtocol();
    QString data = ui.text_send->toPlainText();
    QString errorMessage;

    if (isFileMode())
    {
        if (m_selectedFilePath.isEmpty())
        {
            stopSending(QStringLiteral("请先选择文件或文件夹"));
            return;
        }

        sendFile(protocol, ip, port, errorMessage);
        m_sentCount++;
        ui.progressBar->setValue(m_sentCount);
        ui.label_ErrorMessage->setText(errorMessage);
        return;
    }

    if (protocol == Protocol::ModbusTcp)
    {
        data = buildModbusCommand(errorMessage);
        if (data.isEmpty())
        {
            stopSending(errorMessage);
            return;
        }
    }

    sendText(protocol, ip, port, data, errorMessage);
    m_sentCount++;

    const QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss");
    ui.label_ErrorMessage->setText(
        QString("[%1] %2 （%3/%4）").arg(currentTime).arg(errorMessage).arg(m_sentCount).arg(m_totalCount));
    ui.progressBar->setValue(m_sentCount);
    QApplication::processEvents();
}

void DataSendServer::onReplyReceived(QString strIp, quint16 port, QString strData)
{
    m_saveReceivedButton->hide();
    m_openReceivedButton->hide();
    ui.textEdit_ReplyDisplay->setGeometry(20, 48, 484, 145);
    const QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString html = QString("<table style='background-color:#e9ecec; border:1px solid #bdb7f5; border-radius:6px; "
                           "padding:6px; color:#303090;'>"
                           "<tr><td style='font-weight:bold;'>收到回复</td></tr>"
                           "<tr><td>来源：%1:%2</td></tr>"
                           "<tr><td>时间：%3</td></tr>"
                           "<tr><td>内容：%4</td></tr>"
                           "</table>")
                       .arg(strIp)
                       .arg(port)
                       .arg(currentTime)
                       .arg(strData.toHtmlEscaped());

    ui.textEdit_ReplyDisplay->setHtml(html);

    // 将窗口显示到最前面
    raise();
    activateWindow();
}
