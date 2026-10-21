#include "diagnostics.h"
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QUuid>
#include <cstdio>
#ifdef _WIN32
#include <io.h>
#endif
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
    // GUI processes can start with an invalid stderr descriptor. Initialize it,
    // then duplicate stdout's open file; a second fopen of the log can fail sharing.
    const auto stderrOpen=_wfreopen_s(&stream,L"NUL",L"w",stderr);
    if(stderrOpen==0) {
        if(_dup2(_fileno(stdout),_fileno(stderr))!=0)
            fprintf(stdout,"[Diagnostics] stderr redirection failed\n");
    } else fprintf(stdout,"[Diagnostics] stderr initialization failed: %d\n",int(stderrOpen));
    setvbuf(stdout,nullptr,_IONBF,0);setvbuf(stderr,nullptr,_IONBF,0);
    fprintf(stdout,"DeepMirror SDK diagnostics %s\nNode-level XU controls, multi-interface calibration fallback enabled.\n",QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8().constData());
#endif
}
