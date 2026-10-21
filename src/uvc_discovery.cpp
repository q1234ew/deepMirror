#include "uvc_discovery.h"
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <wrl/client.h>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace {
QString errorCode(HRESULT hr){return QString("0x%1").arg(quint32(hr),8,16,QLatin1Char('0'));}
QString attribute(IMFAttributes *a,const GUID &key) {
    wchar_t *value=nullptr;UINT32 length=0;
    if(FAILED(a->GetAllocatedString(key,&value,&length)))return {};
    const auto text=QString::fromWCharArray(value,int(length));CoTaskMemFree(value);return text;
}
QString physicalDevice(const QString &path) {
    const auto set=SetupDiCreateDeviceInfoList(nullptr,nullptr);
    if(set==INVALID_HANDLE_VALUE)return {};
    SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);
    SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);QString physical;
    const auto wide=path.toStdWString();DWORD bytes=0;
    if(SetupDiOpenDeviceInterfaceW(set,wide.c_str(),0,&iface)) {
        SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&bytes,nullptr);
        std::vector<BYTE> storage(bytes);
        if(bytes>=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());detail->cbSize=sizeof(*detail);
            if(SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,bytes,nullptr,&device)) {
                DEVINST node=device.DevInst;
                for(int level=0;level<12;++level) {
                    wchar_t id[MAX_DEVICE_ID_LEN]{};
                    if(CM_Get_Device_IDW(node,id,MAX_DEVICE_ID_LEN,0)!=CR_SUCCESS)break;
                    QString value=QString::fromWCharArray(id).toUpper();
                    // Stop at the physical composite device, shared by all of its video functions.
                    if(value.startsWith("USB\\VID_")&&!value.section('\\',1,1).contains("&MI_")){physical=value;break;}
                    DEVINST parent=0;if(CM_Get_Parent(&parent,node,0)!=CR_SUCCESS)break;node=parent;
                }
            }
        }
    }
    SetupDiDestroyDeviceInfoList(set);return physical;
}
void probe(IMFActivate *activation,UvcSource &out) {
    ComPtr<IMFMediaSource> source;
    HRESULT hr=activation->ActivateObject(IID_PPV_ARGS(&source));
    if(FAILED(hr)){out.error="无法读取格式（占用、权限或驱动）："+errorCode(hr);return;}
    ComPtr<IMFPresentationDescriptor> presentation;
    hr=source->CreatePresentationDescriptor(&presentation);
    if(SUCCEEDED(hr)) {
        DWORD count=0;hr=presentation->GetStreamDescriptorCount(&count);
        if(SUCCEEDED(hr))for(DWORD stream=0;stream<count;++stream) {
            BOOL selected=FALSE;ComPtr<IMFStreamDescriptor> descriptor;
            if(FAILED(presentation->GetStreamDescriptorByIndex(stream,&selected,&descriptor)))continue;
            ComPtr<IMFMediaTypeHandler> handler;
            if(FAILED(descriptor->GetMediaTypeHandler(&handler)))continue;
            DWORD types=0;handler->GetMediaTypeCount(&types);
            for(DWORD index=0;index<types;++index) {
                ComPtr<IMFMediaType> type;
                if(FAILED(handler->GetMediaTypeByIndex(index,&type)))continue;
                GUID major{},sub{};UINT32 w=0,h=0,num=0,den=0;
                if(FAILED(type->GetMajorType(&major))||major!=MFMediaType_Video||FAILED(type->GetGUID(MF_MT_SUBTYPE,&sub)))continue;
                MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&w,&h);MFGetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,&num,&den);
                QString code;
                const GUID z16={0x2036315a,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
                if(sub==MFVideoFormat_MJPG)code="MJPG";
                else if(sub==z16)code="Z16";
                else {
                    const auto fourcc=sub.Data1;code=QString::fromLatin1(reinterpret_cast<const char*>(&fourcc),4).trimmed();
                    if(code=="MJPG"||code=="Z16")code+="（未支持的子类型 GUID）";
                }
                // SDK uses integer FPS and rejects rates that differ by >= 0.1.
                const double fps=den?double(num)/den:0;const int rounded=qRound(fps);
                out.formats.append({code,int(w),int(h),std::abs(fps-rounded)<.1?rounded:0,int(stream)});
            }
        }
    }
    if(FAILED(hr))out.error="读取视频格式失败："+errorCode(hr);
    if(out.formats.isEmpty()&&out.error.isEmpty())out.error="没有可读取的视频格式";
    presentation.Reset();source->Shutdown();source.Reset();activation->ShutdownObject();
}
}
#endif
UvcDiscovery discoverUvcSources() {
    UvcDiscovery result;
    result.report="UVC 自动发现：按实际格式匹配同一物理设备的 RGB 与深度。\n当前采集支持 Insight7 VID_3652/PID_0104，MJPG RGB + 544×640/642 Z16 深度；需实际标定。\n";
#ifdef _WIN32
    const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com)){result.report+="COM 初始化失败："+errorCode(com);return result;}
    HRESULT hr=MFStartup(MF_VERSION);
    if(FAILED(hr)){result.report+="Media Foundation 初始化失败："+errorCode(hr);CoUninitialize();return result;}
    ComPtr<IMFAttributes> attributes;IMFActivate **devices=nullptr;UINT32 count=0;
    hr=MFCreateAttributes(&attributes,1);
    if(SUCCEEDED(hr))hr=attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if(SUCCEEDED(hr))hr=MFEnumDeviceSources(attributes.Get(),&devices,&count);
    if(FAILED(hr))result.report+="视频源枚举失败："+errorCode(hr)+"\n";
    else for(UINT32 i=0;i<count;++i) {
        UvcSource item;item.name=attribute(devices[i],MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
        item.path=attribute(devices[i],MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
        item.physicalId=physicalDevice(item.path);
        probe(devices[i],item);
        result.report+=QString("\n[%1] %2\n路径：%3\n物理设备：%4\n").arg(i+1).arg(item.name,item.path,item.physicalId.isEmpty()?"未确认，不能自动配对":item.physicalId);
        if(!item.error.isEmpty())result.report+=item.error+"\n";
        QStringList formats;
        for(const auto &f:item.formats)formats.append(QString("stream %1: %2 %3×%4 @%5").arg(f.stream).arg(f.code).arg(f.width).arg(f.height).arg(f.fps));
        formats.removeDuplicates();result.report+=formats.join("\n")+"\n";
        result.sources.append(item);devices[i]->Release();
    }
    CoTaskMemFree(devices);attributes.Reset();MFShutdown();CoUninitialize();
    result.pairs=matchUvcSources(result.sources);
#if defined(DEEPMIRROR_WINDOWS_SDK) && !defined(DEEPMIRROR_SPLIT_USB)
    result.pairs.clear();
    result.report+="当前为旧版复合流构建；自动匹配需要 Insight7 独立深度接口构建。\n";
#endif
    result.report+=QString("\n可用 RGB / 深度组合：%1\n").arg(result.pairs.size());
#else
    result.report+="此功能需要 Windows；Mac 可继续使用模拟模式。\n";
#endif
    return result;
}
