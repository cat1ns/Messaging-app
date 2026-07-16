#include "DataReceiveServer.h"

#include "../Common/AppTempDirectory.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

DataReceiveServer::DataReceiveServer(QWidget* parent) : QMainWindow(parent)
{
    ui.setupUi(this);
    setupWideLayout();
    p_UDPReceiver = new UDPRecvServer(this);
    p_TCPReceiver = new TCPRecvServer(this);
    p_HTTPReceiver = new HTTPRecvServer(this);
    p_WSReceiver = new WSRecvServer(this);
    p_ModbusReceiver = new ModbusTcpServer(this);
    connectSignals();

    ui.lineEdit_Port->setText("8888");
    ui.comboBox_protocol->addItems({"UDP", "TCP", "HTTP", "WebSocket", "Modbus TCP"});
    ui.pushButton_Reply->setEnabled(false);
    ui.textEdit_Reply->setPlainText(QStringLiteral("我已收到信息"));
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
}

void DataReceiveServer::connectSignals()
{
    connect(ui.pushButton_Start, &QPushButton::clicked, this, &DataReceiveServer::on_pushButtonStart_clicked);
    connect(ui.pushButton_Stop, &QPushButton::clicked, this, &DataReceiveServer::on_pushButtonStop_clicked);
    connect(ui.pushButton_Reply, &QPushButton::clicked, this, &DataReceiveServer::on_pushButtonReply_clicked);

    connect(p_UDPReceiver, &UDPRecvServer::sigDataReceived, this, &DataReceiveServer::onDataReceived);
    connect(p_TCPReceiver, &TCPRecvServer::sigDataReceived, this, &DataReceiveServer::onDataReceived);
    connect(p_HTTPReceiver, &HTTPRecvServer::sigDataReceived, this, &DataReceiveServer::onDataReceived);
    connect(p_WSReceiver, &WSRecvServer::sigDataReceived, this, &DataReceiveServer::onDataReceived);
    connect(p_ModbusReceiver, &ModbusTcpServer::sigDataReceived, this, &DataReceiveServer::onDataReceived);

    connect(p_TCPReceiver, &TCPRecvServer::sigFileReceived, this, &DataReceiveServer::onFileReceived);
    connect(p_TCPReceiver, &TCPRecvServer::sigFilePathReceived, this, &DataReceiveServer::onFilePathReceived);
    connect(p_TCPReceiver, &TCPRecvServer::sigFileProgress, this, &DataReceiveServer::onFileProgress);
    connect(p_HTTPReceiver, &HTTPRecvServer::sigFileReceived, this, &DataReceiveServer::onFileReceived);
    connect(p_HTTPReceiver, &HTTPRecvServer::sigFilePathReceived, this, &DataReceiveServer::onFilePathReceived);
    connect(p_HTTPReceiver, &HTTPRecvServer::sigFileProgress, this, &DataReceiveServer::onFileProgress);
    connect(p_WSReceiver, &WSRecvServer::sigFileReceived, this, &DataReceiveServer::onFileReceived);
    connect(p_WSReceiver, &WSRecvServer::sigFilePathReceived, this, &DataReceiveServer::onFilePathReceived);
    connect(p_WSReceiver, &WSRecvServer::sigFileProgress, this, &DataReceiveServer::onFileProgress);
}

DataReceiveServer::Protocol DataReceiveServer::currentProtocol() const
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

bool DataReceiveServer::supportsFileTransfer(Protocol protocol)
{
    return protocol == Protocol::Tcp || protocol == Protocol::Http || protocol == Protocol::WebSocket;
}

bool DataReceiveServer::isFileReplyMode() const
{
    return m_replyMode && supportsFileTransfer(currentProtocol()) &&
           m_replyMode->currentText() == QStringLiteral("文件回复");
}

void DataReceiveServer::setupWideLayout()
{
    setFixedSize(560, 850);
    ui.frameWindow->setGeometry(0, 0, 560, 850);
    ui.frameTitleBar->setGeometry(1, 1, 558, 38);
    ui.label_Dot1->move(497, 14);
    ui.label_Dot2->move(516, 14);
    ui.label_Dot3->move(535, 14);
    ui.label_Title->setGeometry(0, 55, 560, 24);
    ui.frameConfig->setGeometry(18, 95, 524, 112);
    ui.frameConfigHeader->setGeometry(1, 1, 522, 34);
    ui.lineEdit_Port->setGeometry(78, 58, 155, 28);
    ui.label_Protocol->setGeometry(280, 58, 70, 28);
    ui.comboBox_protocol->setGeometry(360, 58, 145, 28);
    ui.frameData->setGeometry(18, 221, 524, 260);
    ui.frameDataHeader->setGeometry(1, 1, 522, 34);
    ui.textEdit_Receiver->setGeometry(20, 49, 484, 194);
    ui.frameReply->setGeometry(18, 495, 524, 230);
    ui.frameReplyHeader->setGeometry(1, 1, 522, 34);
    ui.textEdit_Reply->setGeometry(20, 49, 484, 164);
    ui.pushButton_Start->setGeometry(18, 745, 164, 42);
    ui.pushButton_Stop->setGeometry(198, 745, 164, 42);
    ui.pushButton_Reply->setGeometry(378, 745, 164, 42);
}

void DataReceiveServer::setupFileUi()
{
    m_replyMode = new QComboBox(ui.frameReply);
    m_replyMode->addItems({QStringLiteral("文本回复"), QStringLiteral("文件回复")});
    m_replyMode->setGeometry(20, 46, 105, 32);
    m_chooseReplyFileButton = new QPushButton(QStringLiteral("选择文件"), ui.frameReply);
    m_chooseReplyFileButton->setGeometry(145, 46, 110, 32);
    m_chooseReplyFileButton->setStyleSheet(QStringLiteral("font-size:13px;"));
    m_chooseReplyFileButton->setEnabled(false);
    m_chooseReplyFolderButton = new QPushButton(QStringLiteral("选择文件夹"), ui.frameReply);
    m_chooseReplyFolderButton->setGeometry(270, 46, 120, 32);
    m_chooseReplyFolderButton->setStyleSheet(QStringLiteral("font-size:13px;"));
    m_chooseReplyFolderButton->setEnabled(false);
    m_replyFileLabel = new QLabel(QStringLiteral("尚未选择文件"), ui.frameReply);
    m_replyFileLabel->setGeometry(20, 82, 484, 25);
    m_replyFileLabel->setStyleSheet(QStringLiteral("color:#5550C5;font-size:12px;background:#F7F7FF;border:1px solid "
                                                   "#D4CFFF;border-radius:5px;padding-left:8px;"));
    // 文件操作按钮必须放在接收卡片内部。主窗口宽度只有420，使用主窗口坐标会落到可视区域之外。
    ui.textEdit_Receiver->setGeometry(20, 49, 484, 194);
    m_saveReceivedButton = new QPushButton(QStringLiteral("下载/另存为"), ui.frameData);
    m_saveReceivedButton->setGeometry(284, 214, 105, 32);
    m_saveReceivedButton->setStyleSheet(QStringLiteral("font-size:13px;"));
    m_saveReceivedButton->hide();
    m_openReceivedButton = new QPushButton(QStringLiteral("打开查看"), ui.frameData);
    m_openReceivedButton->setGeometry(399, 214, 105, 32);
    m_openReceivedButton->setStyleSheet(QStringLiteral("font-size:13px;"));
    m_openReceivedButton->hide();
    m_fileProgress = new QProgressBar(ui.frameData);
    m_fileProgress->setGeometry(20, 218, 250, 24);
    m_fileProgress->setRange(0, 10000);
    m_fileProgress->setTextVisible(true);
    m_fileProgress->hide();
    connect(m_replyMode,
            &QComboBox::currentTextChanged,
            this,
            [this](const QString& m)
            {
                bool f = m == QStringLiteral("文件回复");
                m_chooseReplyFileButton->setEnabled(f);
                m_chooseReplyFolderButton->setEnabled(f);
                ui.textEdit_Reply->setEnabled(!f);
            });
    connect(m_chooseReplyFileButton, &QPushButton::clicked, this, &DataReceiveServer::chooseReplyFile);
    connect(m_chooseReplyFolderButton, &QPushButton::clicked, this, &DataReceiveServer::chooseReplyFolder);
    connect(m_saveReceivedButton, &QPushButton::clicked, this, &DataReceiveServer::saveReceivedFile);
    connect(m_openReceivedButton, &QPushButton::clicked, this, &DataReceiveServer::openReceivedFile);
    updateFileUi();
}
void DataReceiveServer::updateFileUi()
{
    const bool supported = supportsFileTransfer(currentProtocol());
    m_replyMode->setVisible(supported);
    m_chooseReplyFileButton->setVisible(supported);
    m_chooseReplyFolderButton->setVisible(supported);
    m_replyFileLabel->setVisible(supported);
    if (!supported)
        m_replyMode->setCurrentIndex(0);
    ui.textEdit_Reply->setGeometry(supported ? QRect(20, 116, 484, 96) : QRect(20, 49, 484, 164));
}
void DataReceiveServer::chooseReplyFile()
{
    m_replyFilePath = QFileDialog::getOpenFileName(this, QStringLiteral("选择回复文件"));
    if (!m_replyFilePath.isEmpty())
    {
        m_replyFileLabel->setText(QFileInfo(m_replyFilePath).fileName());
        m_replyFileLabel->setToolTip(m_replyFilePath);
    }
}
void DataReceiveServer::chooseReplyFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择回复文件夹"));
    if (dir.isEmpty())
        return;
    QString zip = AppTempDirectory::path() + "/" + QFileInfo(dir).fileName() + "_reply.zip";
    QFile::remove(zip);
    QString script = QString("Compress-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                         .arg(QString(dir).replace("'", "''"), QString(zip).replace("'", "''"));
    if (QProcess::execute("powershell", {"-NoProfile", "-Command", script}) == 0)
    {
        m_replyFilePath = zip;
        m_replyFileLabel->setText(QFileInfo(zip).fileName());
        m_replyFileLabel->setToolTip(zip);
    }
    else
        ui.textEdit_Receiver->setText(QStringLiteral("文件夹压缩失败"));
}
void DataReceiveServer::saveReceivedFile()
{
    if (m_receivedFileData.isEmpty() && m_receivedSourcePath.isEmpty())
        return;
    QString p = QFileDialog::getSaveFileName(this, QStringLiteral("下载/保存接收文件"), m_receivedFileName);
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
        ui.statusBar->showMessage(QStringLiteral("文件下载成功：%1").arg(p), 5000);
        QMessageBox::information(
            this, QStringLiteral("下载完成"), QStringLiteral("文件下载成功！\n保存路径：%1").arg(p));
    }
    else
        QMessageBox::warning(
            this, QStringLiteral("下载失败"), QStringLiteral("文件保存失败，请检查磁盘空间和保存路径。"));
}
void DataReceiveServer::openReceivedFile()
{
    if (m_receivedTempPath.isEmpty())
    {
        saveReceivedFile();
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_receivedTempPath)))
        ui.statusBar->showMessage(QStringLiteral("系统没有可打开该文件的应用"), 5000);
}
void DataReceiveServer::onFileReceived(QString strIp, quint16 port, QString fileName, QString mimeType, QByteArray data)
{
    m_receivedFileName = fileName;
    m_receivedFileData = data;
    m_receivedTempPath = AppTempDirectory::path() + "/" + fileName;
    QFile f(m_receivedTempPath);
    if (f.open(QIODevice::WriteOnly))
    {
        f.write(data);
        f.close();
    }
    showReceivedFile(strIp, port, fileName, mimeType, quint64(data.size()));
}
void DataReceiveServer::onFilePathReceived(
    QString strIp, quint16 port, QString fileName, QString mimeType, QString localPath, quint64 size)
{
    m_receivedFileName = fileName;
    m_receivedFileData.clear();
    m_receivedSourcePath = localPath;
    m_receivedTempPath = localPath;
    showReceivedFile(strIp, port, fileName, mimeType, size);
    m_fileProgress->setValue(10000);
    m_fileProgress->show();
}

void DataReceiveServer::showReceivedFile(
    const QString& ip, quint16 port, const QString& fileName, const QString& mimeType, quint64 size)
{
    const QString receiveTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    ui.textEdit_Receiver->setGeometry(20, 49, 484, 154);
    ui.textEdit_Receiver->setText(QStringLiteral("收到文件：%1\n类型：%2\n来源：%3:%4\n接收时间：%5\n大小：%6 字节")
                                      .arg(fileName, mimeType, ip)
                                      .arg(port)
                                      .arg(receiveTime)
                                      .arg(size));
    m_saveReceivedButton->show();
    m_openReceivedButton->show();
    m_saveReceivedButton->raise();
    m_openReceivedButton->raise();
    ui.pushButton_Reply->setEnabled(true);
    raise();
    activateWindow();
}
void DataReceiveServer::onFileProgress(QString fileName, quint64 completed, quint64 total)
{
    if (total == 0)
        return;
    if (!m_fileSpeedTimer.isValid() || completed <= 1024 * 1024)
        m_fileSpeedTimer.restart();
    double seconds = qMax(0.001, m_fileSpeedTimer.elapsed() / 1000.0);
    double speed = completed / 1048576.0 / seconds;
    m_fileProgress->show();
    m_fileProgress->setValue(int(completed * 10000 / total));
    m_fileProgress->setFormat(QStringLiteral("%1  %2%  %3/%4 MB  %5 MB/s")
                                  .arg(fileName)
                                  .arg(completed * 100 / total)
                                  .arg(completed / 1048576.0, 0, 'f', 1)
                                  .arg(total / 1048576.0, 0, 'f', 1)
                                  .arg(speed, 0, 'f', 1));
    QApplication::processEvents();
}

void DataReceiveServer::setupModbusUi()
{
    m_modbusPanel = new QWidget(ui.frameReply);
    m_modbusPanel->setGeometry(ui.textEdit_Reply->geometry());
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

void DataReceiveServer::updateModbusUi()
{
    const bool isModbus = ui.comboBox_protocol->currentText() == "Modbus TCP";
    ui.textEdit_Reply->setVisible(!isModbus);
    m_modbusPanel->setVisible(isModbus);
    ui.pushButton_Reply->setText(isModbus ? QStringLiteral("执行") : QStringLiteral("回复"));
    ui.pushButton_Reply->setEnabled(isModbus);
    if (!isModbus)
        return;

    const bool isRead = m_modbusMode->currentText() == QStringLiteral("读取");
    m_modbusValueLabel->setText(isRead ? QStringLiteral("读取数量") : QStringLiteral("写入值"));
    m_modbusValues->setPlaceholderText(isRead ? QStringLiteral("1~125，例如：10")
                                              : QStringLiteral("以英文逗号分隔，例如：100,200,0x1234"));
    if (m_modbusValues->text().isEmpty())
        m_modbusValues->setText(isRead ? "1" : "0");
}

QString DataReceiveServer::buildModbusOperation(QString& errorMessage) const
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

DataReceiveServer::~DataReceiveServer()
{
    p_UDPReceiver->stopListen();
    p_TCPReceiver->stopListen();
    p_HTTPReceiver->stopListen();
    p_WSReceiver->stopListen();
    p_ModbusReceiver->stopListen();
    AppTempDirectory::cleanup();
}

void DataReceiveServer::startReceiver(Protocol protocol, quint16 port, QString& errorMessage)
{
    switch (protocol)
    {
    case Protocol::Tcp:
        p_TCPReceiver->startListen(port, errorMessage);
        break;
    case Protocol::Http:
        p_HTTPReceiver->startListen(port, errorMessage);
        break;
    case Protocol::WebSocket:
        p_WSReceiver->startListen(port, errorMessage);
        break;
    case Protocol::ModbusTcp:
        p_ModbusReceiver->startListen(port, errorMessage);
        break;
    case Protocol::Udp:
        p_UDPReceiver->startListen(port, errorMessage);
        break;
    }
}

void DataReceiveServer::stopReceiver(Protocol protocol)
{
    switch (protocol)
    {
    case Protocol::Tcp:
        p_TCPReceiver->stopListen();
        break;
    case Protocol::Http:
        p_HTTPReceiver->stopListen();
        break;
    case Protocol::WebSocket:
        p_WSReceiver->stopListen();
        break;
    case Protocol::ModbusTcp:
        p_ModbusReceiver->stopListen();
        break;
    case Protocol::Udp:
        p_UDPReceiver->stopListen();
        break;
    }
}

void DataReceiveServer::sendTextReply(Protocol protocol, const QString& data, QString& errorMessage)
{
    switch (protocol)
    {
    case Protocol::Tcp:
        p_TCPReceiver->sendReply(data, errorMessage);
        break;
    case Protocol::Http:
        p_HTTPReceiver->sendReply(data, errorMessage);
        break;
    case Protocol::WebSocket:
        p_WSReceiver->sendReply(data, errorMessage);
        break;
    case Protocol::ModbusTcp:
        p_ModbusReceiver->sendReply(data, errorMessage);
        break;
    case Protocol::Udp:
        p_UDPReceiver->sendReply(data, errorMessage);
        break;
    }
}

void DataReceiveServer::sendFileReply(Protocol protocol, QString& errorMessage)
{
    switch (protocol)
    {
    case Protocol::Tcp:
        p_TCPReceiver->sendFileReply(m_replyFilePath, errorMessage);
        break;
    case Protocol::Http:
        p_HTTPReceiver->sendFileReply(m_replyFilePath, errorMessage);
        break;
    case Protocol::WebSocket:
        p_WSReceiver->sendFileReply(m_replyFilePath, errorMessage);
        break;
    default:
        errorMessage = QStringLiteral("当前协议不支持文件回复");
        break;
    }
}

void DataReceiveServer::on_pushButtonStart_clicked()
{
    const quint16 port = ui.lineEdit_Port->text().toUShort();
    QString errorMessage;
    startReceiver(currentProtocol(), port, errorMessage);
    ui.textEdit_Receiver->setText(errorMessage);
}

void DataReceiveServer::on_pushButtonStop_clicked()
{
    stopReceiver(currentProtocol());
    ui.pushButton_Reply->setEnabled(false);
    ui.textEdit_Receiver->setText(QStringLiteral("已停止接收"));
}

void DataReceiveServer::onDataReceived(QString strIp, quint16 port, QString strData)
{
    m_saveReceivedButton->hide();
    m_openReceivedButton->hide();
    m_fileProgress->hide();
    ui.textEdit_Receiver->setGeometry(20, 49, 484, 194);
    const QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss");
    const QString dataHtml = strData.toHtmlEscaped().replace("\n", "<br>");

    const QString messageHtml =
        QString("<table cellspacing='0' cellpadding='0'>"
                "<tr><td style='color:#5550c5; padding-right:18px; white-space:nowrap;'>来源 IP</td><td>%1</td></tr>"
                "<tr><td style='color:#5550c5; padding-right:18px; white-space:nowrap;'>端口</td><td>%2</td></tr>"
                "<tr><td style='color:#5550c5; padding-right:18px; white-space:nowrap;'>时间</td><td>%3</td></tr>"
                "<tr><td style='color:#5550c5; padding-right:18px; white-space:nowrap;'>信息</td><td>%4</td></tr>"
                "</table>")
            .arg(strIp.section(":", -1))
            .arg(port)
            .arg(currentTime)
            .arg(dataHtml);

    ui.textEdit_Receiver->setHtml(messageHtml);
    ui.pushButton_Reply->setEnabled(true);
    raise();
    activateWindow();
}

void DataReceiveServer::on_pushButtonReply_clicked()
{
    const Protocol protocol = currentProtocol();
    QString errorMessage;

    if (isFileReplyMode())
    {
        if (m_replyFilePath.isEmpty())
        {
            ui.textEdit_Receiver->setText(QStringLiteral("请先选择回复文件"));
            return;
        }

        sendFileReply(protocol, errorMessage);
        ui.textEdit_Receiver->setText(errorMessage);
        return;
    }

    QString replyData = ui.textEdit_Reply->toPlainText();
    if (protocol == Protocol::ModbusTcp)
        replyData = buildModbusOperation(errorMessage);

    if (replyData.isEmpty())
    {
        ui.textEdit_Receiver->setText(errorMessage.isEmpty() ? QStringLiteral("请先输入回复内容！") : errorMessage);
        return;
    }

    sendTextReply(protocol, replyData, errorMessage);
    const QString currentTime = QDateTime::currentDateTime().toString("hh:mm:ss");
    ui.textEdit_Receiver->setText(QStringLiteral("[%1] 回复结果：%2").arg(currentTime, errorMessage));
}
