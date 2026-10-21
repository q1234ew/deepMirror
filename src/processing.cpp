#include "processing.h"
#include <QtEndian>
#include <cmath>
#include <limits>
void FramePairer::push(bool color,RawImage image) {
    auto &q=color?rgb:depth;
    q.push_back(std::move(image));
    while(q.size()>6){q.pop_front();++dropped;}
}
bool FramePairer::take(RawImage &c,RawImage &d,quint64 tolerance) {
    while(!rgb.empty()&&!depth.empty()) {
        const auto a=rgb.front().arrivalUs,b=depth.front().arrivalUs;
        if((a>b?a-b:b-a)<=tolerance){c=std::move(rgb.front());d=std::move(depth.front());rgb.pop_front();depth.pop_front();return true;}
        (a<b?rgb:depth).pop_front();++dropped;
    }
    return false;
}
static bool valid(const CameraModel &c){return c.width>0&&c.height>0&&std::isfinite(c.fx)&&std::isfinite(c.fy)&&std::isfinite(c.cx)&&std::isfinite(c.cy)&&c.fx>0&&c.fy>0;}
std::shared_ptr<Frame> processPair(const RawImage &rgb,const RawImage &depth,const Calibration &cal,QString &error) {
    error.clear();
    auto fail=[&](QString e)->std::shared_ptr<Frame>{error=e;return {};};
    if(!valid(cal.left)||!valid(cal.rgb))return fail("相机标定无效，停止生成点云");
    // SDK callback FOURCC uses 0x0036315a for Z16, not the padded UVC GUID.
    if(rgb.format!=0x47504a4d || depth.format!=0x0036315a)return fail("当前仅接收 MJPEG RGB + Z16 深度，请检查设备格式");
    if(depth.width!=cal.left.width||!(depth.height==cal.left.height||depth.height==cal.left.height+2))return fail("深度尺寸与标定不匹配，拒绝猜测缩放或裁剪");
    if(depth.bytes.size()!=qint64(depth.width)*depth.height*2)return fail("深度缓冲长度或行跨度异常");
    auto jpeg=rgb.bytes;
    if(jpeg.size()<4||quint8(jpeg[0])!=255||quint8(jpeg[1])!=216||quint8(jpeg[jpeg.size()-2])!=255||quint8(jpeg.back())!=217)return fail("RGB JPEG 不完整");
    // Same device-specific APP1 correction as the vendor's Windows example.
    if(jpeg.size()>18&&quint8(jpeg[2])==255&&quint8(jpeg[3])==225&&jpeg.mid(6,4)=="TS__")jpeg.remove(2,16);
    auto f=std::make_shared<Frame>();f->rgb=QImage::fromData(jpeg,"JPEG").convertToFormat(QImage::Format_RGB32);
    if(f->rgb.isNull())return fail("RGB JPEG 解码失败");
    if(f->rgb.width()!=cal.rgb.width||f->rgb.height()!=cal.rgb.height||rgb.width!=cal.rgb.width||rgb.height!=cal.rgb.height)return fail("RGB 尺寸与标定不匹配");
    f->source="windows_sdk";f->clock="host_monotonic_callback_arrival";f->rgbTimestampUs=rgb.sdkUs;f->depthTimestampUs=depth.sdkUs;
    f->timestampUs=depth.arrivalUs;f->pairDeltaUs=qint64(rgb.arrivalUs)-qint64(depth.arrivalUs);
    f->fx=cal.left.fx;f->fy=cal.left.fy;f->cx=cal.left.cx;f->cy=cal.left.cy;
    f->details=cal.metadata;
    f->details.insert("rgb_arrival_us",QString::number(rgb.arrivalUs));f->details.insert("depth_arrival_us",QString::number(depth.arrivalUs));
    f->details.insert("sdk_timestamp_provenance","device metadata or Media Foundation fallback; not exposed by SDK");
    f->details.insert("registration","rectified left depth projected into RGB using inverse RGB-to-left extrinsic; occluded/unmapped points omitted");
    f->details.insert("depth_metadata_rows_removed",depth.height-cal.left.height);
    f->depth=QImage(depth.width,cal.left.height,QImage::Format_Grayscale16);
    f->depthPreview=QImage(f->depth.size(),QImage::Format_RGB32);
    struct Projected {Point p;int u,v;double z;};QVector<Projected> projected;
    QVector<float> zbuffer(cal.rgb.width*cal.rgb.height,std::numeric_limits<float>::infinity());
    for(int y=0;y<cal.left.height;++y){auto dst=reinterpret_cast<quint16*>(f->depth.scanLine(y));
        for(int x=0;x<depth.width;++x){auto mm=qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(depth.bytes.constData())+2*(y*depth.width+x));dst[x]=mm;
            f->depthPreview.setPixelColor(x,y,mm?QColor::fromHsvF((1-qBound(0.,(mm-300.)/4700.,1.))*.65,.8,.95):QColor(18,24,34));
            if(!mm)continue;
            const double z=mm*.001,px=(x-cal.left.cx)*z/cal.left.fx,py=(y-cal.left.cy)*z/cal.left.fy;
            double a[3]={px-cal.translation[0],py-cal.translation[1],z-cal.translation[2]},r[3]={};
            for(int i=0;i<3;++i)for(int j=0;j<3;++j)r[i]+=cal.rotation[j*3+i]*a[j];
            if(!std::isfinite(r[0])||!std::isfinite(r[1])||!std::isfinite(r[2])||r[2]<=0)continue;
            double u=cal.rgb.fx*r[0]/r[2]+cal.rgb.cx,v=cal.rgb.fy*r[1]/r[2]+cal.rgb.cy;
            if(!std::isfinite(u)||!std::isfinite(v)||u<0||v<0||u>=cal.rgb.width||v>=cal.rgb.height)continue;
            int ix=int(u),iy=int(v);int index=iy*cal.rgb.width+ix;zbuffer[index]=qMin(zbuffer[index],float(r[2]));
            projected.push_back({{float(px),float(py),float(z),f->rgb.pixel(ix,iy)},ix,iy,r[2]});
        }
    }
    for(auto &p:projected)if(p.z<=zbuffer[p.v*cal.rgb.width+p.u]+.002)f->points.push_back(p.p);
    return f;
}
