#include "uvc_discovery.h"
#include <algorithm>
#include <QRegularExpression>
namespace {
bool supported(const UvcSource &s) {
    static const QRegularExpression id("VID_3652&PID_0104(?:&|#|\\\\)",QRegularExpression::CaseInsensitiveOption);
    return id.match(s.path).hasMatch() && !s.physicalId.isEmpty();
}
bool usable(const UvcFormat &f,const QString &code) {
    // The current capture decoder accepts native MJPG and metric Z16 only.
    // Projection still requires valid device calibration at capture time.
    return f.stream==0 && f.code==code && f.width>0 && f.height>0 && f.width<=8192 && f.height<=8192 && f.fps>0 && f.fps<=30;
}
}
QVector<UvcPair> matchUvcSources(const QVector<UvcSource> &sources) {
    QVector<UvcPair> result;
    for(const auto &rgb:sources) {
        if(!supported(rgb)||!rgb.error.isEmpty())continue;
        for(const auto &depth:sources) {
            if(!supported(depth)||!depth.error.isEmpty()||rgb.path.compare(depth.path,Qt::CaseInsensitive)==0||rgb.physicalId.compare(depth.physicalId,Qt::CaseInsensitive)!=0)continue;
            QVector<UvcFormat> colors,depths;
            for(auto f:rgb.formats)if(usable(f,"MJPG"))colors.append(f);
            for(auto f:depth.formats)if(usable(f,"Z16") && f.width==544 && (f.height==642||f.height==640))depths.append(f);
            // Retain native modes. Prefer the previously verified size, then the highest frame rate.
            auto score=[](const UvcFormat &f){return (f.width==1088&&f.height==1920?1000000000LL:0LL)+qint64(f.width)*f.height*100+f.fps;};
            if(colors.isEmpty()||depths.isEmpty())continue;
            const auto color=*std::max_element(colors.begin(),colors.end(),[&](auto a,auto b){return score(a)<score(b);});
            const auto dep=*std::max_element(depths.begin(),depths.end(),[](auto a,auto b){return a.fps<b.fps;});
            UvcPair pair{rgb,depth,color,dep,{}};
            for(const auto &s:sources)if(supported(s)&&s.physicalId.compare(rgb.physicalId,Qt::CaseInsensitive)==0)pair.controls.append(s.path);
            result.append(pair);
        }
    }
    return result;
}
QString describeUvcPair(const UvcPair &p) {
    return QString("RGB %1×%2 @%3 MJPG / 深度 %4×%5 @%6 Z16\n设备：%7\nRGB：%8\n深度：%9")
        .arg(p.rgbFormat.width).arg(p.rgbFormat.height).arg(p.rgbFormat.fps)
        .arg(p.depthFormat.width).arg(p.depthFormat.height).arg(p.depthFormat.fps)
        .arg(p.rgb.physicalId,p.rgb.path,p.depth.path);
}
