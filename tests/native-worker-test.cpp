#include "native-language-worker.hpp"
#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <QTextStream>

int main(int argc, char **argv)
{
 QCoreApplication app(argc,argv);
 if(argc!=2)return 2;
 QThread thread; auto *worker=new NativeLanguageWorker(); worker->moveToThread(&thread);
 QObject::connect(&thread,&QThread::finished,worker,&QObject::deleteLater);
 bool expectedFailure=false,ready=false,received=false; int exitCode=1;
 const QString directory=QString::fromLocal8Bit(argv[1]);
 QObject::connect(worker,&NativeLanguageWorker::failed,&app,[&](const QString &,int generation){
  if(generation!=1 || expectedFailure){app.exit(1);return;}
  expectedFailure=true;
  QMetaObject::invokeMethod(worker,[worker,directory]{worker->load(directory,2);},Qt::QueuedConnection);
 });
 QObject::connect(worker,&NativeLanguageWorker::ready,&app,[&](int generation){
  if(generation!=2 || !expectedFailure || QThread::currentThread()!=app.thread()){app.exit(1);return;}
  ready=true;
  QMetaObject::invokeMethod(worker,[worker]{worker->detect({{"id","worker-test"},{"text","Hola amigos como estan todos hoy"},{"target","en"},{"_generation",2}});},Qt::QueuedConnection);
 });
 QObject::connect(worker,&NativeLanguageWorker::result,&app,[&](const QJsonObject &result){
  received=result.value("_generation").toInt()==2 && result.value("language").toString()=="es" && result.value("translate").toBool() && QThread::currentThread()==app.thread();
  app.exit(received?0:1);
 });
 QTimer::singleShot(10000,&app,[&]{app.exit(1);});
 thread.start();
 QMetaObject::invokeMethod(worker,[worker]{worker->load("/missing-bh-language-test-data",1);},Qt::QueuedConnection);
 exitCode=app.exec();thread.quit();thread.wait();
 QTextStream(stdout)<<"Worker load failure/recovery, queued result, generation and clean shutdown: "<<(exitCode==0 && ready && received?"PASS":"FAIL")<<"\n";
 return exitCode;
}
