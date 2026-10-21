#include "diagnostics.h"
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QUuid>
#include <cstdio>
namespace { QString path; }
QString sdkDiagnosticPath(){return path;}
void initializeSdkDiagnostics(){
#ifdef _WIN32
    const QString folder=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/logs";
    if(!QDir().mkpath(folder))return;
    const QString candidate=folder+"/sdk-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+"-"+QUuid::createUuid().toString(QUuid::Id128).left(6)+".log";
    const auto wide=candidate.toStdWString();FILE *stream=nullptr;
    if(_wfreopen_s(&stream,wide.c_str(),L"a",stdout)!=0)return;
    path=candidate;
    _wfreopen_s(&stream,wide.c_str(),L"a",stderr);
    setvbuf(stdout,nullptr,_IONBF,0);setvbuf(stderr,nullptr,_IONBF,0);
    fprintf(stdout,"DeepMirror SDK diagnostics %s\nNode-level XU controls, multi-interface calibration fallback enabled.\n",QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8().constData());
#endif
}
