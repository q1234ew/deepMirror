#include "processing.h"
#include <QGuiApplication>
#include <QBuffer>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QtEndian>
#include <iostream>
#include <cmath>
#define CHECK(c) do {if(!(c)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#c<<'\n';return 1;}}while(false)
int main(int argc,char **argv){QGuiApplication app(argc,argv);
 FramePairer pairer;RawImage a,b;a.arrivalUs=100000;b.arrivalUs=120000;
 pairer.push(true,a);pairer.push(false,b);RawImage c,d;CHECK(pairer.take(c,d));CHECK(!pairer.take(c,d));
 a.arrivalUs=200000;b.arrivalUs=250000;pairer.push(true,a);pairer.push(false,b);CHECK(!pairer.take(c,d));CHECK(pairer.dropped==1);
 a.arrivalUs=251000;pairer.push(true,a);CHECK(pairer.take(c,d));
 FramePairer bounded;for(int i=0;i<20;++i)bounded.push(true,a);CHECK(bounded.dropped==14);
 Calibration cal;cal.left=cal.rgb={2,2,1,1,0,0};
 QImage color(2,2,QImage::Format_RGB32);color.fill(QColor(40,120,210));QByteArray jpg;QBuffer buffer(&jpg);buffer.open(QIODevice::WriteOnly);CHECK(color.save(&buffer,"JPEG"));
 a={jpg,2,2,0x47504a4d,234,100000};b={QByteArray(16,0),2,4,0x0036315a,240,101000};
 for(int i=0;i<4;++i)qToLittleEndian<quint16>(1000,reinterpret_cast<uchar*>(b.bytes.data())+i*2);
 // Metadata rows must not become points or depth pixels.
 for(int i=4;i<8;++i)qToLittleEndian<quint16>(65000,reinterpret_cast<uchar*>(b.bytes.data())+i*2);
 QString error;auto f=processPair(a,b,cal,error);CHECK(f);CHECK(error.isEmpty());CHECK(f->depth.size()==QSize(2,2));CHECK(f->points.size()==4);CHECK(f->pairDeltaUs==-1000);CHECK(f->rgbTimestampUs==234);CHECK(f->source=="windows_sdk");CHECK(std::abs(f->points[3].z-1)<1e-6);CHECK(std::abs(f->points[3].x-1)<1e-6);CHECK(f->points[0].color==f->rgb.pixel(0,0));
 // Exact RGB->left inverse: +1m RGB-origin offset shifts left projection by -1 pixel.
 cal.translation={1,0,0};f=processPair(a,b,cal,error);CHECK(f&&f->points.size()==2);CHECK(std::abs(f->points[0].x-1)<1e-6);
 cal.translation={0,0,0};auto bad=b;bad.bytes.chop(1);CHECK(!processPair(a,bad,cal,error));bad=b;bad.height=3;CHECK(!processPair(a,bad,cal,error));bad=b;bad.format=0;CHECK(!processPair(a,bad,cal,error));auto badRgb=a;badRgb.bytes.chop(1);CHECK(!processPair(badRgb,b,cal,error));cal.rgb.width=3;CHECK(!processPair(a,b,cal,error));cal.rgb.width=2;cal.left.fx=0;CHECK(!processPair(a,b,cal,error));cal.left.fx=1;
 // SDK-compatible nonstandard APP1 timestamp segment is removed before JPEG decoding.
 auto wrapped=a;wrapped.bytes.insert(2,QByteArray::fromHex("ffe1000054535f5f0000000000000000"));CHECK(processPair(wrapped,b,cal,error));
 qToLittleEndian<quint16>(0,reinterpret_cast<uchar*>(b.bytes.data()));f=processPair(a,b,cal,error);CHECK(f&&f->points.size()==3);
 QTemporaryDir output;CHECK(output.isValid());CHECK(saveFrame(*f,output.path()).isEmpty());
 auto groups=QDir(output.path()).entryList(QDir::Dirs|QDir::NoDotAndDotDot);CHECK(groups.size()==1);
 QFile meta(output.path()+"/"+groups.first()+"/metadata.json");CHECK(meta.open(QIODevice::ReadOnly));auto saved=QJsonDocument::fromJson(meta.readAll()).object();
 CHECK(saved["source"]=="windows_sdk");CHECK(saved["pairing_clock"]=="host_monotonic_callback_arrival");CHECK(saved["rgb_timestamp_us"]=="234");CHECK(saved["pair_delta_us"].toInt()==-1000);CHECK(saved["depth_height"].toInt()==2);
 std::cout<<"PASS: bounded pairing, tolerance/drop behavior, depth metadata crop, inverse extrinsic, color projection, malformed inputs\n";
 return 0;
}
