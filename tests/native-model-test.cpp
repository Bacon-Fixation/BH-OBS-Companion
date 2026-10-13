#include "native-language-detector.hpp"
#include "native-language-worker.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <cmath>
#include <stdexcept>

int main(int argc,char **argv)
{
 QCoreApplication app(argc,argv);int checks=0;
 auto check=[&](bool ok,const char *message){++checks;if(!ok)throw std::runtime_error(message);};
 if(argc!=2)return 2;
 try{
  QTemporaryDir temp;check(temp.isValid(),"temporary directory");
  const QString data=QString::fromLocal8Bit(argv[1]),path=temp.filePath("export.json"),active=temp.filePath("active.json");
  NativeLanguageDetector original(data);const auto base=original.package();
  NativeLanguageDetector::writePackage(path,base);NativeLanguageDetector imported(path);
  const QJsonObject job{{"id","tune"},{"text","Hola amigos como estan todos hoy"},{"target","en"}};
  check(imported.detect(job).value("translate").toBool(),"baseline Spanish should translate");
  check(imported.package()==base,"export/import exact model round trip");
  auto strict=job;strict.insert("minimumMargin",1.0);check(!imported.detect(strict).value("translate").toBool(),"ambiguity guard");
  strict=job;strict.insert("minimumWords",20);check(!imported.detect(strict).value("translate").toBool(),"minimum words guard");
  const auto tuned=original.correctedPackage(job.value("text").toString(),"es");
  const auto a=base.value("model").toObject().value("languages").toObject(),b=tuned.value("model").toObject().value("languages").toObject();
  check(a.value("es")!=b.value("es"),"correction changes real profile weights");
  check(a.value("en")==b.value("en"),"unrelated language profile untouched");
  double norm=0;const auto vector=b.value("es").toObject().value("vector").toObject();for(auto it=vector.begin();it!=vector.end();++it)norm+=it.value().toDouble()*it.value().toDouble();check(std::abs(norm-1)<1e-9,"updated vector normalized");
  NativeLanguageWorker worker;bool loaded=false;QJsonObject report;
  QObject::connect(&worker,&NativeLanguageWorker::ready,&app,[&](int){loaded=true;});
  QObject::connect(&worker,&NativeLanguageWorker::managed,&app,[&](const QJsonObject &r,int){report=r;});
  worker.load(data,1,active);check(loaded,"worker loaded");
  auto action=[&](const QString &op,const QJsonObject &request=QJsonObject{}){report={};worker.manage(op,request,2);check(!report.isEmpty(),"operation replied");return report.value("ok").toBool();};
  check(!action("apply"),"apply requires preview");
  check(action("preview",{{"text",job.value("text")},{"target","en"},{"language","es"}}),"correction preview");
  check(report.value("canApply").toBool(),"safe preview can apply");
  check(!QFile::exists(active),"preview does not persist model");
  check(action("apply"),"apply correction");check(report.value("reviewCount").toInt()==1,"review saved");
  NativeLanguageDetector afterApply(active);check(afterApply.package()==tuned,"applied candidate persisted");
  NativeLanguageWorker restarted;QJsonObject restartReport;
  QObject::connect(&restarted,&NativeLanguageWorker::managed,&app,[&](const QJsonObject &r,int){restartReport=r;});
  restarted.load(data,1,active);restarted.manage("export",{{"path",path}},2);
  check(restartReport.value("ok").toBool() && restartReport.value("reviewCount").toInt()==1,"restart restores custom model and reviews");
  NativeLanguageDetector restartModel(path);check(restartModel.package()==tuned,"restart exported tuned model");
  check(action("export",{{"path",path}}),"export trained model");
  QFile exportFile(path);exportFile.open(QIODevice::ReadOnly);const auto exported=QJsonDocument::fromJson(exportFile.readAll()).object();exportFile.close();
  check(!exported.contains("localReviews"),"raw reviews excluded from export");
  check(action("ignore",{{"text",job.value("text")}}),"ignore exact normalized text");
  NativeLanguageDetector ignored(active);check(!ignored.detect(job).value("translate").toBool(),"ignored phrase suppresses automation");
  check(action("undo"),"undo ignore");NativeLanguageDetector undone(active);check(undone.package()==tuned,"undo restores previous model");
  check(action("reset"),"restore bundled");NativeLanguageDetector reset(active);check(reset.package()==base,"reset exact bundle");
  auto invalid=base;auto meta=invalid.value("metadata").toObject();auto order=meta.value("modelOrder").toArray();order.append(order.first());meta.insert("modelOrder",order);invalid.insert("metadata",meta);
  NativeLanguageDetector::writePackage(path,invalid);
  check(!action("import",{{"path",path}}),"duplicate profile import rejected");NativeLanguageDetector kept(active);check(kept.package()==base,"failed import leaves active model untouched");
  NativeLanguageDetector::writePackage(path,tuned);check(action("import",{{"path",path}}),"valid import");NativeLanguageDetector finalModel(active);check(finalModel.package()==tuned,"import persistence");
  check(!action("preview",{{"text","hi"},{"target","en"},{"language","en"}}),"short correction rejected");
  check(!action("apply"),"failed preview invalidates old candidate");
  check(!action("export",{{"path",active}}),"export cannot overwrite active local review store");
  QTextStream(stdout)<<"Native model management: "<<checks<<" checks passed\n";return 0;
 }catch(const std::exception &error){QTextStream(stderr)<<"After "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
}
