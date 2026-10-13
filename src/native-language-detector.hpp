#pragma once
#include <QJsonObject>
#include <QString>
#include <memory>

// Offline CPU inference. Uses Qt Core already provided by OBS, no JS engine,
// subprocess, network access, or separately installed runtime.
class NativeLanguageDetector final {
public:
 explicit NativeLanguageDetector(const QString &dataDirectory);
 ~NativeLanguageDetector();
 NativeLanguageDetector(const NativeLanguageDetector &) = delete;
 NativeLanguageDetector &operator=(const NativeLanguageDetector &) = delete;
 QJsonObject detect(const QJsonObject &job) const;
 QJsonObject package() const;
 QJsonObject correctedPackage(const QString &text, const QString &language) const;
 QJsonObject ignoredPackage(const QString &text) const;
 static void writePackage(const QString &path, const QJsonObject &package);
private:
 struct Data;
 std::unique_ptr<Data> data_;
};
