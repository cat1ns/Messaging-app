#include "UserLogin.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QVBoxLayout>

UserLogin::UserLogin(QWidget* parent)
    : QDialog(parent), m_connectionName(QString("user_login_connection_%1").arg(reinterpret_cast<quintptr>(this)))
{
    ui.setupUi(this);

    ui.lineEdit_PassWorld->setEchoMode(QLineEdit::Password);

    initDatabase();

    connect(ui.pushButton_OK, &QPushButton::clicked, this, &UserLogin::on_pushButton_OK_clicked);
    connect(ui.pushButton_Add, &QPushButton::clicked, this, &UserLogin::on_pushButton_Add_clicked);
    connect(ui.pushButton_Cannel, &QPushButton::clicked, this, &UserLogin::on_pushButton_Cannel_clicked);
}

UserLogin::~UserLogin()
{
    if (m_db.isOpen())
        m_db.close();

    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

void UserLogin::initDatabase()
{
    const QString dbPath = QCoreApplication::applicationDirPath() + "/users.db";
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open())
    {
        QMessageBox::critical(this, "数据库错误", "无法打开数据库：" + m_db.lastError().text());
        return;
    }

    QSqlQuery query(m_db);
    if (!query.exec("CREATE TABLE IF NOT EXISTS users ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "username TEXT UNIQUE NOT NULL, "
                    "password TEXT NOT NULL, "
                    "role TEXT NOT NULL DEFAULT 'user'"
                    ")"))
    {
        QMessageBox::critical(this, "数据库错误", "无法创建用户表：" + query.lastError().text());
        return;
    }

    QString adminPassword;
    QString adminRole;
    if (!loadUserFromDatabase("admin", &adminPassword, &adminRole))
    {
        QString errorMessage;
        if (!saveUserToDatabase("admin", hashPassword("admin"), "admin", &errorMessage))
            qDebug() << "Insert admin failed:" << errorMessage;
    }
}

QString UserLogin::hashPassword(const QString& password) const
{
    return QString(QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool UserLogin::saveUserToDatabase(const QString& username,
                                   const QString& passwordHash,
                                   const QString& role,
                                   QString* errorMessage)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO users (username, password, role) VALUES (:name, :pass, :role)");
    query.bindValue(":name", username);
    query.bindValue(":pass", passwordHash);
    query.bindValue(":role", role);

    if (query.exec())
        return true;

    if (errorMessage)
        *errorMessage = query.lastError().text();
    return false;
}

bool UserLogin::loadUserFromDatabase(const QString& username,
                                     QString* passwordHash,
                                     QString* role,
                                     QString* errorMessage)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT password, role FROM users WHERE username = :name");
    query.bindValue(":name", username);

    if (!query.exec())
    {
        if (errorMessage)
            *errorMessage = query.lastError().text();
        return false;
    }

    if (!query.next())
        return false;

    if (passwordHash)
        *passwordHash = query.value(0).toString();
    if (role)
        *role = query.value(1).toString();
    return true;
}

void UserLogin::enterAdminMode()
{
    m_isAdminLoggedIn = true;
    ui.pushButton_OK->setText("进入系统");
    ui.pushButton_Add->setEnabled(true);
    ui.lineEdit_User->clear();
    ui.lineEdit_PassWorld->clear();
    ui.lineEdit_User->setPlaceholderText("输入新账号");
    ui.lineEdit_PassWorld->setPlaceholderText("输入新密码");
    ui.label_GroupLogin->setText("管理员已登录");
    ui.label_Title->setText("管理员模式");
    ui.label_Status->setText("管理员已登录，可新增账号，点击“进入系统”继续。");
}

void UserLogin::on_pushButton_OK_clicked()
{
    if (m_isAdminLoggedIn)
    {
        accept();
        return;
    }

    const QString username = ui.lineEdit_User->text().trimmed();
    const QString password = ui.lineEdit_PassWorld->text();
    if (username.isEmpty() || password.isEmpty())
    {
        QMessageBox::warning(this, "提示", "请输入账号和密码。");
        return;
    }

    QString storedPassword;
    QString role;
    QString errorMessage;
    if (!loadUserFromDatabase(username, &storedPassword, &role, &errorMessage))
    {
        QMessageBox::warning(
            this, "登录失败", errorMessage.isEmpty() ? "账号不存在。" : "读取数据库失败：" + errorMessage);
        return;
    }

    if (storedPassword != hashPassword(password))
    {
        QMessageBox::warning(this, "登录失败", "密码错误。");
        return;
    }

    if (role == "admin")
        enterAdminMode();
    else
        accept();
}

void UserLogin::on_pushButton_Add_clicked()
{
    if (!m_isAdminLoggedIn)
    {
        QDialog adminDlg(this);
        adminDlg.setWindowTitle("管理员验证");
        adminDlg.setFixedSize(300, 150);

        auto* layout = new QVBoxLayout(&adminDlg);
        auto* lblUser = new QLabel("管理员账号");
        auto* editUser = new QLineEdit("admin");
        auto* lblPass = new QLabel("管理员密码");
        auto* editPass = new QLineEdit();
        editPass->setEchoMode(QLineEdit::Password);
        auto* btnLayout = new QHBoxLayout();
        auto* btnOk = new QPushButton("验证");
        auto* btnCancel = new QPushButton("取消");

        btnLayout->addWidget(btnOk);
        btnLayout->addWidget(btnCancel);
        layout->addWidget(lblUser);
        layout->addWidget(editUser);
        layout->addWidget(lblPass);
        layout->addWidget(editPass);
        layout->addLayout(btnLayout);

        connect(btnOk,
                &QPushButton::clicked,
                &adminDlg,
                [&adminDlg, editUser, editPass, this]()
                {
                    const QString username = editUser->text().trimmed();
                    const QString password = editPass->text();
                    if (username.isEmpty() || password.isEmpty())
                    {
                        QMessageBox::warning(&adminDlg, "提示", "请输入管理员账号和密码。");
                        return;
                    }

                    QString storedPassword;
                    QString role;
                    if (loadUserFromDatabase(username, &storedPassword, &role) &&
                        storedPassword == hashPassword(password) && role == "admin")
                        adminDlg.accept();
                    else
                        QMessageBox::warning(&adminDlg, "验证失败", "管理员账号或密码错误。");
                });
        connect(btnCancel, &QPushButton::clicked, &adminDlg, &QDialog::reject);

        if (adminDlg.exec() != QDialog::Accepted)
            return;

        enterAdminMode();
    }

    const QString username = ui.lineEdit_User->text().trimmed();
    const QString password = ui.lineEdit_PassWorld->text();
    if (username.isEmpty() || password.isEmpty())
    {
        QMessageBox::warning(this, "提示", "请输入要新增的账号和密码。");
        return;
    }

    QString errorMessage;
    if (saveUserToDatabase(username, hashPassword(password), "user", &errorMessage))
    {
        QMessageBox::information(this, "成功", QString("账号“%1”添加成功。").arg(username));
        ui.lineEdit_User->clear();
        ui.lineEdit_PassWorld->clear();
        ui.lineEdit_User->setFocus();
        return;
    }

    if (errorMessage.contains("UNIQUE", Qt::CaseInsensitive))
        QMessageBox::warning(this, "添加失败", "该账号已存在，请使用其他账号名。");
    else
        QMessageBox::warning(this, "添加失败", "添加账号失败：" + errorMessage);
}

void UserLogin::on_pushButton_Cannel_clicked()
{
    reject();
}
