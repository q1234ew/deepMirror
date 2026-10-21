#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
struct UvcFormat { QString code; int width=0,height=0,fps=0; int stream=0; };
struct UvcSource { QString name,path,physicalId,error; QVector<UvcFormat> formats; };
struct UvcPair { UvcSource rgb,depth; UvcFormat rgbFormat,depthFormat; QStringList controls; };
struct UvcDiscovery { QVector<UvcSource> sources; QVector<UvcPair> pairs; QString report; };
QVector<UvcPair> matchUvcSources(const QVector<UvcSource> &sources);
UvcDiscovery discoverUvcSources();
QString describeUvcPair(const UvcPair &pair);
