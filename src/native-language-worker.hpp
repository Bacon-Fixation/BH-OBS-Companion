#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>
#include "native-language-detector.hpp"

class NativeLanguageWorker final : public QObject {
 Q_OBJECT
public:
 using QObject::QObject;
 void load(const QString &directory, int generation, const QString &storage = {});
 void manage(const QString &operation, const QJsonObject &request, int generation);
 void detect(const QJsonObject &job);
Q_SIGNALS:
 void ready(int generation);
 void managed(const QJsonObject &report, int generation);
 void result(const QJsonObject &result);
 void failed(const QString &message, int generation);
private:
 std::unique_ptr<NativeLanguageDetector> detector_, candidate_;
 QString directory_, storage_;
 QJsonArray reviews_, candidateReviews_;
 bool candidateSafe_ = false;
 void commit(std::unique_ptr<NativeLanguageDetector> next, const QJsonArray &reviews);
};
