#include "language-feedback.hpp"
#include <QFile>
#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <stdexcept>
namespace {
bool code(const QString &value){return QRegularExpression(QStringLiteral("^[a-z]{2,3}(?:-[a-z0-9]{2,12})?$")).match(value).hasMatch();}
}
QJsonArray LanguageFeedback::read(const QString &path)
{
 QFile file(path);
 if(!file.open(QIODevice::ReadOnly) || file.size()>2*1024*1024)throw std::runtime_error("Cannot read feedback pack (maximum 2 MiB)");
 QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);const auto object=doc.object();
 if(error.error!=QJsonParseError::NoError || object.value("format")!="bh-language-feedback" || object.value("formatVersion").toInt()!=1 || !object.value("examples").isArray() || object.value("examples").toArray().size()>200)throw std::runtime_error("Expected a bh-language-feedback v1 pack with at most 200 examples");
 QJsonArray rows;
 for(const auto &v:object.value("examples").toArray()){
  const auto row=v.toObject();const auto text=row.value("text").toString(),status=row.value("status").toString();
  if(text.trimmed().isEmpty() || text.size()>2000 || !code(row.value("targetLanguage").toString()) || (status!="approved" && status!="pending") || (status=="approved" && !code(row.value("language").toString())))throw std::runtime_error("Invalid feedback example, language or review status");
  rows.append(row);
 }
 return rows;
}
QJsonObject LanguageFeedback::pack(const QJsonArray &reviews)
{
 QJsonArray examples;
 for(const auto &v:reviews){const auto row=v.toObject();examples.append(QJsonObject{{"text",row.value("text")},{"language",row.value("language")},{"targetLanguage",row.value("target")},{"providerLanguage",row.value("providerLanguage")},{"status","approved"},{"set",row.value("set").toString("train")}});}
 return {{"format","bh-language-feedback"},{"formatVersion",1},{"createdAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},{"examples",examples}};
}
void LanguageFeedback::write(const QString &path,const QJsonArray &reviews)
{
 const auto bytes=QJsonDocument(pack(reviews)).toJson();
 if(bytes.size()>2*1024*1024)throw std::runtime_error("Feedback export exceeds 2 MiB");
 QSaveFile file(path);if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())throw std::runtime_error("Cannot save feedback export");
}
