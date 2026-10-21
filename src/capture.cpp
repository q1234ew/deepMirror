#include "capture.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QUuid>
#include <cmath>
#include <iostream>
std::shared_ptr<Frame> SimulatedSource::next() {
    auto f=std::make_shared<Frame>();
    f->sequence=++sequence; f->timestampUs=QDateTime::currentMSecsSinceEpoch()*1000;
    f->rgbTimestampUs=f->depthTimestampUs=f->timestampUs;
    constexpr int w=320,h=240;
    f->rgb=QImage(w,h,QImage::Format_RGB32);
    f->depth=QImage(w,h,QImage::Format_Grayscale16);
    f->depthPreview=QImage(w,h,QImage::Format_RGB32);
    f->points.reserve(w*h);
    double shift=25*std::sin(sequence*0.035);
    for(int y=0;y<h;++y) {
        auto d=reinterpret_cast<quint16*>(f->depth.scanLine(y));
        for(int x=0;x<w;++x) {
            double u=x-160-shift,v=y-120;
            bool sphere=u*u+v*v<65*65;
            int mm=sphere ? int(1250-std::sqrt(65*65-u*u-v*v)*5) : 1900+y*2;
            bool invalid=x<8||y<8;
            d[x]=invalid?0:mm;
            QRgb c=sphere?qRgb(65+x/3,160+y/4,215):qRgb(45+x/3,55+y/3,90+x/5);
            f->rgb.setPixel(x,y,c);
            double t=(mm-850)/1600.0;
            f->depthPreview.setPixelColor(x,y,invalid?QColor(18,24,34):QColor::fromHsvF((1-t)*0.65,0.8,0.95));
            if(!invalid) {float z=mm/1000.f; f->points.push_back({float((x-f->cx)*z/f->fx),float((y-f->cy)*z/f->fy),z,c});}
        }
    }
    return f;
}
QString saveFrame(const Frame &f,const QString &root) {
    QDir parent(root);
    if(!parent.exists()&&!QDir().mkpath(root)) return "无法创建保存目录";
    QString name=QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")+"_"+QString::number(f.sequence)+"_"+QUuid::createUuid().toString(QUuid::Id128).left(6);
    QString staging=parent.filePath("."+name+".partial");
    if(!QDir().mkpath(staging)) return "无法创建采集目录";
    auto fail=[&](QString s){QDir(staging).removeRecursively();return s;};
    if(!f.rgb.save(staging+"/rgb.png")||!f.depth.save(staging+"/depth_mm.png")||!f.depthPreview.save(staging+"/depth_preview.png")) return fail("图像保存失败");
    QFile ply(staging+"/cloud.ply");
    if(!ply.open(QIODevice::WriteOnly)) return fail(ply.errorString());
    QTextStream s(&ply); s.setRealNumberPrecision(7);
    s<<"ply\nformat ascii 1.0\ncomment coordinates in meters; x right y down z forward\nelement vertex "<<f.points.size()<<"\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n";
    for(const auto&p:f.points) s<<p.x<<' '<<p.y<<' '<<p.z<<' '<<qRed(p.color)<<' '<<qGreen(p.color)<<' '<<qBlue(p.color)<<'\n';
    s.flush(); if(s.status()!=QTextStream::Ok||!ply.flush()) return fail("点云写入失败"); ply.close();
    QJsonObject j=f.details;
    const QJsonObject common{{"source",f.source},{"sequence",double(f.sequence)},{"rgb_timestamp_us",QString::number(f.rgbTimestampUs)},{"depth_timestamp_us",QString::number(f.depthTimestampUs)},{"pair_delta_us",double(f.pairDeltaUs)},{"pairing_clock",f.clock},{"depth_unit","millimeter"},{"invalid_depth",0},{"point_unit","meter"},{"coordinate_frame","camera_optical: x right, y down, z forward"},{"width",f.rgb.width()},{"height",f.rgb.height()},{"depth_width",f.depth.width()},{"depth_height",f.depth.height()},{"fx",f.fx},{"fy",f.fy},{"cx",f.cx},{"cy",f.cy},{"points",f.points.size()}};
    for(auto it=common.begin();it!=common.end();++it)j.insert(it.key(),it.value());
    QSaveFile meta(staging+"/metadata.json");
    if(!meta.open(QIODevice::WriteOnly)||meta.write(QJsonDocument(j).toJson())<0||!meta.commit()) return fail("元数据保存失败");
    if(!parent.rename("."+name+".partial",name)) return fail("采集目录提交失败");
    return {};
}
int selfTest(const QString &root) {
    SimulatedSource source; auto f=source.next();
    if(f->points.size()!=312*232) return 1;
    for(const auto&p:f->points) if(!std::isfinite(p.x)||p.z<=0) return 2;
    QString err=saveFrame(*f,root); if(!err.isEmpty()){std::cerr<<err.toStdString();return 3;}
    auto dirs=QDir(root).entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Time);
    QString path=QDir(root).filePath(dirs.first());
    QImage depth(path+"/depth_mm.png"); depth=depth.convertToFormat(QImage::Format_Grayscale16);
    if(depth.size()!=f->depth.size())return 4;
    for(int y=0;y<depth.height();++y) for(int x=0;x<depth.width();++x)
        if(reinterpret_cast<const quint16*>(depth.constScanLine(y))[x]!=reinterpret_cast<const quint16*>(f->depth.constScanLine(y))[x])return 5;
    QFile png(path+"/depth_mm.png");png.open(QIODevice::ReadOnly);auto header=png.read(26);if(header.size()<26||quint8(header[24])!=16)return 6;
    QFile ply(path+"/cloud.ply");ply.open(QIODevice::ReadOnly);auto bytes=ply.readAll();if(!bytes.contains("element vertex 72384\n")||bytes.count('\n')!=72395)return 7;
    QImage rgb(path+"/rgb.png");if(rgb.convertToFormat(QImage::Format_RGB32)!=f->rgb)return 8;
    std::cout<<"PASS: 16-bit PNG exact roundtrip, RGB exact roundtrip, 72384 finite colored points, PLY vertex count\n";return 0;
}
