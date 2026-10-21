#include "device_diagnostics.h"
#include "diagnostics.h"
#include <QtWidgets>
#include <QtConcurrent>
#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <dshow.h>
#include <vector>

namespace {
QStringList property(HDEVINFO devices, SP_DEVINFO_DATA &device, DWORD key) {
    DWORD bytes=0,type=0;
    SetupDiGetDeviceRegistryPropertyW(devices,&device,key,&type,nullptr,0,&bytes);
    if(!bytes) return {};
    std::vector<wchar_t> buffer(bytes/sizeof(wchar_t)+2,0);
    if(!SetupDiGetDeviceRegistryPropertyW(devices,&device,key,&type,
        reinterpret_cast<BYTE*>(buffer.data()),DWORD(buffer.size()*sizeof(wchar_t)),nullptr)) return {};
    if(type!=REG_SZ && type!=REG_MULTI_SZ) return {};
    QStringList values;
    for(size_t pos=0;pos<buffer.size() && buffer[pos];) {
        const auto value=QString::fromWCharArray(buffer.data()+pos);
        values.append(value);pos+=size_t(value.size())+1;
        if(type!=REG_MULTI_SZ) break;
    }
    return values;
}
QString instanceId(HDEVINFO devices, SP_DEVINFO_DATA &device) {
    DWORD size=0;
    SetupDiGetDeviceInstanceIdW(devices,&device,nullptr,0,&size);
    if(!size) return {};
    std::vector<wchar_t> buffer(size+1,0);
    if(!SetupDiGetDeviceInstanceIdW(devices,&device,buffer.data(),DWORD(buffer.size()),nullptr)) return {};
    return QString::fromWCharArray(buffer.data());
}
QString hexError(HRESULT hr) {return QString("0x%1").arg(quint32(hr),8,16,QLatin1Char('0'));}
QString bagText(IPropertyBag *bag,const wchar_t *key) {
    VARIANT value;VariantInit(&value);QString result;
    if(SUCCEEDED(bag->Read(key,&value,nullptr)) && value.vt==VT_BSTR)
        result=QString::fromWCharArray(value.bstrVal);
    VariantClear(&value);return result;
}
}
#endif

QString collectDeviceDiagnostics() {
    QString report="DeepMirror 设备诊断\n时间："+QDateTime::currentDateTime().toString(Qt::ISODate)+"\n系统："+QSysInfo::prettyProductName()+"\n";
#ifdef DEEPMIRROR_WINDOWS_SDK
#ifdef DEEPMIRROR_SPLIT_USB
    report+="当前 SDK 筛选：VID_3652 / PID_0104；RGB MI_02，深度 MI_06\n";
#else
    report+="当前 SDK 筛选：VID_3652 / PID_0B5C；RGB MI_03/MI_04，深度 MI_00/MI_06/MI_08\n";
#endif
#else
    report+="当前构建未启用 Windows SDK 采集后端。\n";
#endif
    report+="SDK 日志："+(sdkDiagnosticPath().isEmpty()?QString("无"):sdkDiagnosticPath())+"\n";
#ifdef _WIN32
    report+="\n【Windows 当前设备】\n范围：Camera/Image 类、usbvideo 服务、VID_3652 设备，以及有问题代码的 USB 设备。\n";
    const auto devices=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_ALLCLASSES|DIGCF_PRESENT);
    int count=0,vendorCount=0;
    if(devices==INVALID_HANDLE_VALUE) {
        report+=QString("设备枚举失败：Win32=%1\n").arg(GetLastError());
    } else {
        SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);
        for(DWORD index=0;;++index) {
            if(!SetupDiEnumDeviceInfo(devices,index,&device)) {
                const auto error=GetLastError();
                if(error!=ERROR_NO_MORE_ITEMS) report+=QString("设备列表不完整：Win32=%1\n").arg(error);
                break;
            }
            const QString id=instanceId(devices,device);
            const QStringList hardware=property(devices,device,SPDRP_HARDWAREID);
            const QString klass=property(devices,device,SPDRP_CLASS).join("; ");
            const QString service=property(devices,device,SPDRP_SERVICE).join("; ");
            ULONG flags=0,problem=0;
            const auto cr=CM_Get_DevNode_Status(&flags,&problem,device.DevInst,0);
            const bool vendor=(id+hardware.join(" ")).contains("VID_3652",Qt::CaseInsensitive);
            const bool failedUsb=id.startsWith("USB\\",Qt::CaseInsensitive) && cr==CR_SUCCESS && (flags&DN_HAS_PROBLEM);
            if(!vendor && !failedUsb && klass.compare("Camera",Qt::CaseInsensitive)!=0 && klass.compare("Image",Qt::CaseInsensitive)!=0 && service.compare("usbvideo",Qt::CaseInsensitive)!=0) continue;
            ++count;if(vendor)++vendorCount;
            QString name=property(devices,device,SPDRP_FRIENDLYNAME).join("; ");
            if(name.isEmpty()) name=property(devices,device,SPDRP_DEVICEDESC).join("; ");
            report+=QString("\n[%1] %2\n类别：%3；驱动服务：%4\n").arg(count).arg(name,klass,service);
            if(cr==CR_SUCCESS) report+=QString("状态：%1；问题代码：%2；DevNode 标志：0x%3\n").arg((flags&DN_HAS_PROBLEM)?"有问题":((flags&DN_STARTED)?"已启动":"未启动")).arg(problem).arg(flags,0,16);
            else report+=QString("状态查询失败：CONFIGRET=0x%1\n").arg(cr,0,16);
            report+="硬件 ID：\n  "+(hardware.isEmpty()?QString("不可用"):hardware.join("\n  "))+"\n设备实例 ID："+id+"\n";
        }
        SetupDiDestroyDeviceInfoList(devices);
        report+=QString("\n符合上述范围：%1 个；其中 VID_3652：%2 个。\n").arg(count).arg(vendorCount);
    }
    report+="\n【DirectShow 视频输入】\n列出所有视频输入，不按厂商或 PID 筛选；出现于此处不代表支持 SDK 所需格式或标定。\n";
    const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com)) {report+="COM 初始化失败："+hexError(com)+"\n";return report;}
    ICreateDevEnum *enumerator=nullptr;IEnumMoniker *monikers=nullptr;
    HRESULT hr=CoCreateInstance(CLSID_SystemDeviceEnum,nullptr,CLSCTX_INPROC_SERVER,IID_ICreateDevEnum,reinterpret_cast<void**>(&enumerator));
    int inputs=0;
    if(SUCCEEDED(hr)) {
        hr=enumerator->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,&monikers,0);
        if(hr==S_OK && monikers) {
            IMoniker *moniker=nullptr;
            while((hr=monikers->Next(1,&moniker,nullptr))==S_OK) {
                ++inputs;IPropertyBag *bag=nullptr;
                const auto bind=moniker->BindToStorage(nullptr,nullptr,IID_IPropertyBag,reinterpret_cast<void**>(&bag));
                if(SUCCEEDED(bind)) {
                    report+=QString("\n[%1] %2\nDevicePath：%3\n").arg(inputs).arg(bagText(bag,L"FriendlyName"),bagText(bag,L"DevicePath"));
                    bag->Release();
                } else report+=QString("\n[%1] 属性读取失败：%2\n").arg(inputs).arg(hexError(bind));
                moniker->Release();
            }
            if(FAILED(hr)) report+="视频列表不完整："+hexError(hr)+"\n";
            monikers->Release();
        }
        enumerator->Release();
    }
    if(FAILED(hr)) report+="视频枚举失败："+hexError(hr)+"\n";
    report+=QString("\nDirectShow 视频输入共 %1 个。\n").arg(inputs);
    CoUninitialize();
    report+="\n排查提示：VID_3652 存在但没有对应视频输入时，检查 UVC 模式和视频驱动；实际 PID 与当前筛选不同，需要核对设备型号和采集适配。设备存在与否不等同于标定可读。\n";
#else
    report+="\n当前系统不是 Windows。请在 Windows 版本中查看真实的设备和硬件 ID；Mac 模拟模式可照常使用。\n";
#endif
    return report;
}

void showDeviceDiagnostics(QWidget *parent) {
    QDialog dialog(parent);dialog.setWindowTitle("设备诊断");dialog.resize(900,640);
    auto layout=new QVBoxLayout(&dialog);
    auto hint=new QLabel("查看 Windows 识别到的设备和硬件 ID。插拔或切换模式后点击刷新；此操作只读取设备信息。",&dialog);
    hint->setWordWrap(true);layout->addWidget(hint);
    auto text=new QPlainTextEdit(&dialog);text->setReadOnly(true);text->setLineWrapMode(QPlainTextEdit::NoWrap);text->setAccessibleName("设备诊断信息");layout->addWidget(text,1);
    auto buttons=new QHBoxLayout;auto refresh=new QPushButton("刷新设备",&dialog);auto copy=new QPushButton("复制诊断信息",&dialog);auto close=new QPushButton("关闭",&dialog);
    buttons->addWidget(refresh);buttons->addWidget(copy);buttons->addStretch();buttons->addWidget(close);layout->addLayout(buttons);
    QFutureWatcher<QString> watcher;
    auto scan=[&]{refresh->setEnabled(false);copy->setEnabled(false);text->setPlainText("正在读取设备信息…");watcher.setFuture(QtConcurrent::run(collectDeviceDiagnostics));};
    QObject::connect(&watcher,&QFutureWatcher<QString>::finished,&dialog,[&]{text->setPlainText(watcher.result());refresh->setEnabled(true);copy->setEnabled(true);});
    QObject::connect(refresh,&QPushButton::clicked,&dialog,scan);
    QObject::connect(copy,&QPushButton::clicked,&dialog,[&]{QApplication::clipboard()->setText(text->toPlainText());});
    QObject::connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);
    scan();dialog.exec();
    // The worker owns no UI objects; destroying its watcher does not cancel enumeration.
}
