#include "uvc_discovery.h"
#include "windows_device_path.h"
#include "xu_descriptor.h"
#include <QCoreApplication>
#include <iostream>
#define CHECK(c) do{if(!(c)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#c<<'\n';return 1;}}while(false)
UvcSource source(QString id,QString physical,QVector<UvcFormat> formats) {
 return {"UVC Camera","\\\\?\\usb#vid_3652&pid_0104&mi_"+id+"#"+physical,physical,{},formats};
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);
 uint8_t descriptor[20]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,24,2,1,2},guid[16]{};
 CHECK(parseXuDescriptorGuid(descriptor,sizeof(descriptor),guid));CHECK(std::memcmp(guid,descriptor,16)==0);
 CHECK(!parseXuDescriptorGuid(descriptor,19,guid));CHECK(!parseXuDescriptorGuid(nullptr,20,guid));
 descriptor[16]=0;CHECK(!parseXuDescriptorGuid(descriptor,20,guid));descriptor[16]=24;
 std::memset(descriptor,0,16);CHECK(!parseXuDescriptorGuid(descriptor,20,guid));
 const std::string mf="\\\\?\\usb#vid_3652&pid_0104&mi_02#instance-a#{camera-guid}\\global";
 const std::string ds="@device:pnp:\\\\?\\USB#VID_3652&PID_0104&MI_02#INSTANCE-A#{capture-guid}\\global";
 CHECK(sameWindowsVideoFunction(mf,ds));
 auto different=ds;different.replace(different.find("MI_02"),5,"MI_06");CHECK(!sameWindowsVideoFunction(mf,different));
 different=ds;different.replace(different.find("INSTANCE-A"),10,"INSTANCE-B");CHECK(!sameWindowsVideoFunction(mf,different));
 CHECK(!sameWindowsVideoFunction("UVC Camera","UVC Camera"));
 const UvcFormat rgb{"MJPG",1088,1920,30,0},dep{"Z16",544,642,30,0};
 auto a=source("02","physical-A",{rgb}),b=source("06","physical-A",{dep});
 auto gray=source("04","physical-A",{{"Y800",544,1281,20,0}});
 auto pairs=matchUvcSources({a,b,gray});CHECK(pairs.size()==1);CHECK(pairs[0].controls.size()==3);CHECK(pairs[0].rgb.path==a.path);CHECK(pairs[0].depth.path==b.path);
 // Never pair RGB from one physical camera with depth from another.
 auto other=b;other.physicalId="physical-B";CHECK(matchUvcSources({a,other}).isEmpty());
 other=source("06","physical-B",{dep});auto colorOther=source("02","physical-B",{rgb});
 CHECK(matchUvcSources({a,b,colorOther,other}).size()==2);
 // Webcam, unknown physical identity, failed probes, gray and non-zero streams are excluded.
 auto webcam=a;webcam.path.replace("3652","13d3");CHECK(matchUvcSources({webcam,b}).isEmpty());
 auto unknown=a;unknown.physicalId.clear();CHECK(matchUvcSources({unknown,b}).isEmpty());
 auto busy=a;busy.error="busy";CHECK(matchUvcSources({busy,b}).isEmpty());
 auto wrong=b;wrong.formats={{"Y800",544,642,30,0}};CHECK(matchUvcSources({a,wrong}).isEmpty());
 wrong.formats={{"Z16",544,642,30,1}};CHECK(matchUvcSources({a,wrong}).isEmpty());
 wrong.formats={{"Z16",544,642,60,0}};CHECK(matchUvcSources({a,wrong}).isEmpty());
 wrong.formats={{"Z16",544,640,20,0}};CHECK(matchUvcSources({a,wrong}).size()==1);
 auto lower=a;lower.formats.append({"MJPG",640,480,30,0});lower.formats.append({"MJPG",1088,1920,15,0});
 pairs=matchUvcSources({lower,b});CHECK(pairs[0].rgbFormat.width==1088);CHECK(pairs[0].rgbFormat.fps==30);
 std::cout<<"PASS: RGB/depth capability matching, physical-device isolation and ambiguity preservation\n";return 0;
}
