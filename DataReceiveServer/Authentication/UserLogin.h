#pragma once

#include "ui_UserLogin.h"

#include <QDialog>
#include <QSqlDatabase>

class UserLogin : public QDialog
{
    Q_OBJECT

public:
    explicit UserLogin(QWidget* parent = nullptr);
    ~UserLogin() override;

private slots:
    void on_pushButton_OK_clicked();
    void on_pushButton_Add_clicked();
    void on_pushButton_Cannel_clicked();

private:
    void initDatabase();
    QString hashPassword(const QString& password) const;
    bool saveUserToDatabase(const QString& username,
                            const QString& passwordHash,
                            const QString& role,
                            QString* errorMessage = nullptr);
    bool loadUserFromDatabase(const QString& username,
                              QString* passwordHash,
                              QString* role,
                              QString* errorMessage = nullptr);
    void enterAdminMode();

    Ui::UserLoginClass ui;
    QSqlDatabase m_db;
    QString m_connectionName;
    bool m_isAdminLoggedIn = false;
};
