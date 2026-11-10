#include "native-language-detector.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>
int main(int argc,char **argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;NativeLanguageDetector detector(QString::fromLocal8Bit(argv[1]));QTextStream input(stdin),output(stdout);while(!input.atEnd()){auto line=input.readLine();if(line.isEmpty())continue;try{output<<QJsonDocument(detector.detect(QJsonDocument::fromJson(line.toUtf8()).object())).toJson(QJsonDocument::Compact)<<"\n";}catch(const std::exception &e){output<<QJsonDocument(QJsonObject{{"error",e.what()}}).toJson(QJsonDocument::Compact)<<"\n";}}}
