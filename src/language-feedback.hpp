#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
namespace LanguageFeedback {
 QJsonArray read(const QString &path);
 QJsonObject pack(const QJsonArray &reviews);
 void write(const QString &path, const QJsonArray &reviews);
}
