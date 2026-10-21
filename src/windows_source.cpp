#include "processing.h"
#include "Insight9SDK.h"
#include <QJsonArray>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstring>
#include <cmath>
#ifdef _WIN32
#include <objbase.h>
#endif
namespace {
quint64 nowUs(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
QJsonArray array(const float *p,int n){QJsonArray a;for(int i=0;i<n;++i)a.append(p[i]);return a;}
QJsonObject describe(const camera_calib &c){QJsonArray t,q;for(double v:c.extrinsics.translation)t.append(v);for(double v:c.extrinsics.rotation)q.append(v);
 return {{"width",int(c.intrinsics.width)},{"height",int(c.intrinsics.height)},{"frame_id",QString::fromLatin1(c.intrinsics.frame_id,int(strnlen(c.intrinsics.frame_id,32)))},{"K",array(c.intrinsics.k,9)},{"D",array(c.intrinsics.d,4)},{"R",array(c.intrinsics.r,9)},{"P",array(c.intrinsics.p,12)},{"parent_frame",QString::fromLatin1(c.extrinsics.parent_frame_id,int(strnlen(c.extrinsics.parent_frame_id,32)))},{"child_frame",QString::fromLatin1(c.extrinsics.child_frame_id,int(strnlen(c.extrinsics.child_frame_id,32)))},{"translation_m",t},{"quaternion_xyzw",q}};
}
CameraModel model(const camera_calib &c){return {int(c.intrinsics.width),int(c.intrinsics.height),c.intrinsics.k[0],c.intrinsics.k[4],c.intrinsics.k[2],c.intrinsics.k[5]};}
bool loadCalibration(Calibration &out,QString &err){
 camera_calib l{},r{};
 if(insight9_receive_get_camera_calib(INSIGHT9_CALIB_CAM_LEFT,&l)||insight9_receive_get_camera_calib(INSIGHT9_CALIB_CAM_RGB,&r)){err="无法读取左右/RGB 标定，请确认固件和 UVC 扩展控制可用";return false;}
 // Rectified projection is the vendor alignment API's contract. Reject other models.
 for(const auto *c:{&l,&r}){
  if(std::abs(c->intrinsics.p[3])>.01||std::abs(c->intrinsics.p[7])>.01||std::abs(c->intrinsics.p[11])>.01){err="不支持非零投影平移的标定";return false;}
  if(c->intrinsics.width>8192||c->intrinsics.height>8192||!c->intrinsics.width||!c->intrinsics.height){err="标定尺寸异常";return false;}
  const int pi[]={0,1,2,4,5,6,8,9,10};
  for(int i=0;i<9;++i)if(!std::isfinite(c->intrinsics.k[i])||!std::isfinite(c->intrinsics.p[pi[i]])||std::abs(c->intrinsics.k[i]-c->intrinsics.p[pi[i]])>.01){err="标定 P 与 K 不一致，需要先验证图像去畸变模型";return false;}
 }
 if(!l.intrinsics.frame_id[0]||!r.intrinsics.frame_id[0]){err="标定坐标系名称缺失";return false;}
 if(std::strncmp(r.extrinsics.parent_frame_id,l.intrinsics.frame_id,32)!=0||std::strncmp(r.extrinsics.child_frame_id,r.intrinsics.frame_id,32)!=0){err="RGB 外参父子坐标系不匹配，拒绝猜测变换方向";return false;}
 double norm=0;for(double v:r.extrinsics.rotation)norm+=v*v;
 if(!std::isfinite(norm)||std::abs(norm-1)>.02){err="外参四元数无效";return false;}
 double q[4];for(int i=0;i<4;++i)q[i]=r.extrinsics.rotation[i]/std::sqrt(norm);
 double x=q[0],y=q[1],z=q[2],w=q[3];
 out.left=model(l);out.rgb=model(r);
 out.rotation={1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)};
 for(int i=0;i<3;++i){if(!std::isfinite(r.extrinsics.translation[i])){err="外参平移无效";return false;}out.translation[i]=r.extrinsics.translation[i];}
 out.metadata={{"point_frame_id",QString::fromLatin1(l.intrinsics.frame_id,int(strnlen(l.intrinsics.frame_id,32)))},{"left_calibration",describe(l)},{"rgb_calibration",describe(r)},{"sdk_revision","58395177ba3ecd077309e87549022fdcec3a9a4f"},{"rectification_assumption","vendor API contract: rectified streams, P equals K; verify visually on hardware"}};
 return true;
}
class WindowsSource final: public FrameSource {
 std::atomic<bool> stop{false},done{false};std::thread worker;
 mutable std::mutex mutex;std::condition_variable cv;
 FramePairer pairer;std::shared_ptr<Frame> latest;QString message="正在初始化 Windows SDK…";
 quint64 callbacks=0,rejected=0,sequence=0;
 void report(const QString &s){std::lock_guard<std::mutex> lock(mutex);message=s;}
 static void callback(int id,uint8_t*data,size_t size,int w,int h,unsigned fmt,uint64_t ts,void*user) noexcept {
  auto self=static_cast<WindowsSource*>(user);
  if(self->stop||!data||(id!=INSIGHT9_CAM_RGB&&id!=INSIGHT9_CAM_DEPTH))return;
  try {
   if(w<=0||h<=0||w>8192||h>8192||size>64*1024*1024)return;
   RawImage raw;raw.arrivalUs=nowUs();raw.sdkUs=ts;raw.width=w;raw.height=h;raw.format=fmt;raw.bytes=QByteArray(reinterpret_cast<char*>(data),qsizetype(size));
   {std::lock_guard<std::mutex> lock(self->mutex);self->pairer.push(id==INSIGHT9_CAM_RGB,std::move(raw));++self->callbacks;}
   self->cv.notify_one();
  }catch(...){/* Never unwind through vendor callback threads. */}
 }
 void run(){
  bool initialized=false;
#ifdef _WIN32
  const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  if(FAILED(com)){report("COM 初始化失败");done=true;return;}
#endif
  try {
   insight9_config_t config{};config.rgb_config={1088,1920,30,PixelFormat::MJPEG};
   #ifdef DEEPMIRROR_SPLIT_USB
   config.depth_config={544,642,30,PixelFormat::Z16};config.gray_config={544,642,30,PixelFormat::GREY};
#else
   config.depth_config={544,640,30,PixelFormat::Z16};config.gray_config={544,640,30,PixelFormat::Y8I};
#endif
   // The pinned SDK opens native UVC formats; no parameter/depth-enable writes are made.
   int result=insight9_receive_init(&config);initialized=true;
   if(result){report("SDK 初始化失败，请检查驱动、设备连接与 USB 模式");}
   else {
    Calibration calibration;QString error;
    if(!loadCalibration(calibration,error))report(error);
    else {
     insight9_receive_register_image_callback(callback,this);
     if(insight9_receive_start())report("SDK 启动失败");
     else {
      auto last=nowUs();report("等待 RGB / 深度；如无深度，请检查网页深度开关");
      while(!stop){RawImage rgb,depth;bool paired=false;quint64 dropped=0;
       {std::unique_lock<std::mutex> lock(mutex);cv.wait_for(lock,std::chrono::milliseconds(80),[this]{return stop.load()||pairer.ready();});if(stop)break;paired=pairer.take(rgb,depth);dropped=pairer.dropped;}
       if(!paired){if(nowUs()-last>3000000)report("超过 3 秒无有效配对：检查深度开关、格式、USB 或设备连接");continue;}
       auto frame=processPair(rgb,depth,calibration,error);
       if(!frame){++rejected;report(error);continue;}
       frame->sequence=++sequence;frame->details.insert("pairing_tolerance_us",20000);frame->details.insert("queue_dropped",QString::number(dropped));frame->details.insert("rejected_pairs",QString::number(rejected));
       {std::lock_guard<std::mutex> lock(mutex);latest=std::move(frame);message=QString("真实采集 · 主机到达时间配对 · 丢弃 %1 / 拒绝 %2").arg(dropped).arg(rejected);}
       last=nowUs();
      }
     }
    }
   }
  }catch(const std::exception &e){report(QString("采集异常：%1").arg(e.what()));}catch(...){report("采集发生未知异常");}
  if(initialized){insight9_receive_stop();insight9_receive_register_image_callback(nullptr,nullptr);insight9_receive_cleanup();}
  #ifdef _WIN32
  CoUninitialize();
#endif
  done=true;
 }
public:
 WindowsSource(){worker=std::thread([this]{run();});}
 ~WindowsSource()override{requestStop();if(worker.joinable())worker.join();}
 void requestStop()override{stop=true;cv.notify_all();}
 bool finished()const override{return done;}
 std::shared_ptr<Frame> next()override{std::lock_guard<std::mutex> lock(mutex);auto value=latest;latest.reset();return value;}
 QString status()const override{std::lock_guard<std::mutex> lock(mutex);return message;}
};
}
std::unique_ptr<FrameSource> createWindowsSource(){return std::make_unique<WindowsSource>();}
