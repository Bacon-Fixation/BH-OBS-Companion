#include "native-language-detector.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <exception>

int main(int argc, char **argv)
{
 QCoreApplication app(argc, argv); QTextStream out(stdout), err(stderr);
 if (argc != 3) { err << "Usage: native-detector-test DATA_DIRECTORY FIXTURES_JSON\n"; return 2; }
 try {
  QElapsedTimer load; load.start(); NativeLanguageDetector detector(QString::fromLocal8Bit(argv[1]));
  out << "Model load: " << load.elapsed() << " ms\n";
  QFile file(QString::fromLocal8Bit(argv[2])); if (!file.open(QIODevice::ReadOnly)) return 2;
  const auto fixtures = QJsonDocument::fromJson(file.readAll()).array();
  if(fixtures.isEmpty()) return 2;
  int failures=0,checked=0; QVector<double> timings;
  for(const auto &entry:fixtures){
   const auto row=entry.toObject(), job=row.value("job").toObject(), expected=row.value("expected").toObject();
   const auto actual=detector.detect(job); ++checked; timings.append(actual.value("elapsedMs").toDouble());
   QStringList differences;
   for(const QString &key:QStringList{"normalized","language","translate","automationReliable"}) if(expected.value(key)!=actual.value(key)) differences.append(key+" expected="+QString::fromUtf8(QJsonDocument(QJsonArray{expected.value(key)}).toJson(QJsonDocument::Compact))+" actual="+QString::fromUtf8(QJsonDocument(QJsonArray{actual.value(key)}).toJson(QJsonDocument::Compact)));
   if(std::abs(expected.value("confidence").toDouble()-actual.value("confidence").toDouble())>0.000001) differences.append(QString("confidence expected=%1 actual=%2").arg(expected.value("confidence").toDouble(),0,'f',9).arg(actual.value("confidence").toDouble(),0,'f',9));
   if(!differences.isEmpty()){++failures;err<<job.value("id").toString()<<": "<<job.value("text").toString()<<"\n  "<<differences.join("; ")<<"\n";}
  }
  auto requireReject=[&](const QJsonObject &job){try{detector.detect(job);++failures;err<<"Accepted invalid job\n";}catch(const std::exception &){}; ++checked;};
  requireReject({{"id","long"},{"text",QString(2001,'x')},{"target","en"}});
  requireReject({{"id","bad-target"},{"text","Hello everyone how are you doing"},{"target","invalid-target"}});
  requireReject({{"id",true},{"text","some text here"},{"target","en"}});
  const auto high=detector.detect({{"id","threshold"},{"text","Hola amigos como estan todos hoy"},{"target","en"},{"threshold",1.0}});
  ++checked; if(high.value("translate").toBool()) ++failures;
  std::sort(timings.begin(),timings.end());
  out<<"Native checks: "<<checked<<"; failures: "<<failures<<"; median inference: "<<timings[timings.size()/2]<<" ms; p95: "<<timings[int(timings.size()*.95)]<<" ms\n";
  return failures?1:0;
 }catch(const std::exception &e){err<<e.what()<<"\n";return 2;}
}
