#pragma once
#include <QCoreApplication>
#include <QDir>
namespace AppTempDirectory
{
inline QString path()
{
    QString p = QCoreApplication::applicationDirPath() + "/temp";
    QDir().mkpath(p);
    return p;
}
inline void cleanup()
{
    QDir dir(QCoreApplication::applicationDirPath() + "/temp");
    if (dir.exists())
        dir.removeRecursively();
}
} // namespace AppTempDirectory
