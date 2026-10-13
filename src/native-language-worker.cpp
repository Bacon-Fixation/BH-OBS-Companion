#include "native-language-worker.hpp"
#include <exception>
#include <stdexcept>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QJsonDocument>

void NativeLanguageWorker::load(const QString &directory, int generation, const QString &storage)
{
 try {
  directory_=directory; storage_=storage;
  if (!detector_) {
   const QString source=!storage.isEmpty() && QFileInfo::exists(storage)?storage:directory;
   auto next=std::make_unique<NativeLanguageDetector>(source);
   QJsonArray reviews;
   if(source==storage){QFile file(storage);if(file.open(QIODevice::ReadOnly))reviews=QJsonDocument::fromJson(file.readAll()).object().value("localReviews").toArray();}
   if(reviews.size()>200)throw std::runtime_error("Too many local review examples");
   reviews_=reviews;detector_=std::move(next);
  }
  Q_EMIT ready(generation);
 } catch (const std::exception &error) {
  Q_EMIT failed(QString::fromUtf8(error.what()), generation);
 }
}
void NativeLanguageWorker::detect(const QJsonObject &job)
{
 try {
  if (!detector_) throw std::runtime_error("Language model is not loaded");
  Q_EMIT result(detector_->detect(job));
 } catch (const std::exception &error) {
  Q_EMIT failed(QString::fromUtf8(error.what()), job.value("_generation").toInt());
 }
}

namespace {
std::unique_ptr<NativeLanguageDetector> candidateFrom(const QJsonObject &package)
{
 QTemporaryDir temporary;
 if(!temporary.isValid())throw std::runtime_error("Cannot create model staging directory");
 const auto path=temporary.filePath("candidate.json");
 NativeLanguageDetector::writePackage(path,package);
 return std::make_unique<NativeLanguageDetector>(path);
}
QJsonObject readStored(const QString &path)
{
 QFile file(path);
 if(!file.open(QIODevice::ReadOnly)||file.size()>40*1024*1024)throw std::runtime_error("Cannot read saved model");
 return QJsonDocument::fromJson(file.readAll()).object();
}
}
void NativeLanguageWorker::commit(std::unique_ptr<NativeLanguageDetector> next, const QJsonArray &reviews)
{
 if(storage_.isEmpty())throw std::runtime_error("Model storage is unavailable");
 if(!QDir().mkpath(QFileInfo(storage_).absolutePath()))throw std::runtime_error("Cannot create model storage");
 auto package=next->package();package.insert("localReviews",reviews);
 // Save the previous active model for one-step undo. Disk and memory only switch
 // after the new model validates and the atomic save succeeds.
 if(detector_){auto previous=detector_->package();previous.insert("localReviews",reviews_);NativeLanguageDetector::writePackage(storage_+".previous",previous);}
 NativeLanguageDetector::writePackage(storage_,package);
 detector_=std::move(next);reviews_=reviews;candidate_.reset();candidateSafe_=false;
}
void NativeLanguageWorker::manage(const QString &operation, const QJsonObject &request, int generation)
{
 QJsonObject report{{"ok",false},{"operation",operation}};
 try {
  if(directory_.isEmpty())throw std::runtime_error("Start the detector before managing models");
  if(operation=="reset"){
   commit(std::make_unique<NativeLanguageDetector>(directory_),{});report.insert("message","Bundled model restored; automatic translation remains off.");
  }else if(operation=="import"){
   auto next=std::make_unique<NativeLanguageDetector>(request.value("path").toString());
   commit(std::move(next),{});report.insert("message","Model imported and saved locally. Review examples cleared; preview before enabling translation.");
  }else if(operation=="undo"){
   auto next=std::make_unique<NativeLanguageDetector>(storage_+".previous");
   const auto rows=readStored(storage_+".previous").value("localReviews").toArray();
   if(rows.size()>200)throw std::runtime_error("Too many saved reviews");
   commit(std::move(next),rows);report.insert("message","Previous model restored.");
  }else{
   if(!detector_)throw std::runtime_error("Start the detector or restore the bundled model first");
   if(operation=="export"){
    const auto path=request.value("path").toString();
    if(QFileInfo(path).absoluteFilePath()==QFileInfo(storage_).absoluteFilePath() || QFileInfo(path).absoluteFilePath()==QFileInfo(storage_+".previous").absoluteFilePath())throw std::runtime_error("Choose an export file outside active model storage");
    NativeLanguageDetector::writePackage(path,detector_->package());report.insert("message","Model exported without raw review messages.");
   }else if(operation=="preview"){
    candidate_.reset();candidateSafe_=false;
    const QString text=request.value("text").toString();const QString label=request.value("language").toString().trimmed().toLower();
    auto next=candidateFrom(detector_->correctedPackage(text,label));
    const auto job=QJsonObject{{"id","review"},{"text",text},{"target",request.value("target").toString("en")}};
    const auto before=detector_->detect(job), after=next->detect(job);
    QJsonArray examples;
    for(const auto &v:reviews_)if(v.toObject().value("text").toString()!=text)examples.append(v);
    if(examples.size()>=200)examples.removeAt(0);
    examples.append(QJsonObject{{"text",text},{"target",job.value("target")},{"language",label}});
    int regressions=0,improvements=0,falseBefore=0,falseAfter=0;
    for(const auto &v:examples){
     const auto e=v.toObject();const QJsonObject test{{"id","review-check"},{"text",e.value("text")},{"target",e.value("target")}};
     const auto a=detector_->detect(test),b=next->detect(test);
     const bool wanted=e.value("language").toString()!=a.value("target").toString();
     const bool fa=a.value("translate").toBool()&&!wanted,fb=b.value("translate").toBool()&&!wanted;
     const bool correctA=a.value("language")==e.value("language"),correctB=b.value("language")==e.value("language");
     falseBefore+=fa;falseAfter+=fb;
     if((!fa&&fb)||(correctA&&!correctB))++regressions;
     if((fa&&!fb)||(!correctA&&correctB))++improvements;
    }
    candidateSafe_=regressions==0;candidateReviews_=examples;candidate_=std::move(next);
    report.insert("canApply",candidateSafe_);report.insert("before",before);report.insert("after",after);
    report.insert("message",QString("Preview: %1 → %2; score %3% → %4%. %5 review examples: %6 improvements, %7 regressions; false triggers %8 → %9. %10")
      .arg(before.value("language").toString(),after.value("language").toString())
      .arg(before.value("confidence").toDouble()*100,0,'f',1).arg(after.value("confidence").toDouble()*100,0,'f',1)
      .arg(examples.size()).arg(improvements).arg(regressions).arg(falseBefore).arg(falseAfter)
      .arg(candidateSafe_?"Apply to save this correction.":"Apply blocked: fix the example or use ignore-text instead."));
   }else if(operation=="apply"){
    if(!candidate_||!candidateSafe_)throw std::runtime_error("Preview a correction with no review regressions before applying");
    commit(std::move(candidate_),candidateReviews_);report.insert("message","Correction applied and saved. Automatic translation remains off.");
   }else if(operation=="ignore"){
    commit(candidateFrom(detector_->ignoredPackage(request.value("text").toString())),reviews_);
    report.insert("message","Normalized text is now ignored locally. Undo can restore the previous model.");
   }else throw std::runtime_error("Unknown model operation");
  }
  report.insert("ok",true);
 }catch(const std::exception &error){report.insert("message",QString::fromUtf8(error.what()));}
 report.insert("modelReady",bool(detector_));report.insert("reviewCount",reviews_.size());
 Q_EMIT managed(report,generation);
}
