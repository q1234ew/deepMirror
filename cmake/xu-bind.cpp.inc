bool ExtensionUnitControl::bindFilterByDevicePath(const std::string& devicePath) {
    ICreateDevEnum* devEnum = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum,nullptr,CLSCTX_INPROC_SERVER,
        IID_ICreateDevEnum,reinterpret_cast<void**>(&devEnum));
    if(FAILED(hr)||!devEnum) {
        printf("[XU] Create system enumerator failed: hr=0x%08lx\n",static_cast<unsigned long>(hr));return false;
    }
    // VideoInputDeviceCategory and WDM capture may expose different category
    // suffixes from MF's KSCATEGORY_VIDEO_CAMERA symbolic links.
    const GUID categories[]={CLSID_VideoInputDeviceCategory,KSCATEGORY_CAPTURE};
    for(const auto &category:categories) {
        IEnumMoniker* enumerator=nullptr;
        hr=devEnum->CreateClassEnumerator(category,&enumerator,0);
        printf("[XU] Enumerate category {%08lx}: hr=0x%08lx\n",category.Data1,static_cast<unsigned long>(hr));
        if(hr!=S_OK||!enumerator)continue;
        IMoniker* moniker=nullptr;unsigned count=0;
        while((hr=enumerator->Next(1,&moniker,nullptr))==S_OK) {
            ++count;IPropertyBag* bag=nullptr;
            const auto bagHr=moniker->BindToStorage(nullptr,nullptr,IID_IPropertyBag,reinterpret_cast<void**>(&bag));
            if(SUCCEEDED(bagHr)&&bag) {
                VARIANT value;VariantInit(&value);
                const auto readHr=bag->Read(L"DevicePath",&value,nullptr);
                if(SUCCEEDED(readHr)&&value.vt==VT_BSTR) {
                    const std::wstring wide=value.bstrVal?value.bstrVal:L"";
                    const int length=WideCharToMultiByte(CP_UTF8,0,wide.c_str(),int(wide.size()),nullptr,0,nullptr,nullptr);
                    std::string current(size_t(length),'\0');
                    if(length)WideCharToMultiByte(CP_UTF8,0,wide.c_str(),int(wide.size()),&current[0],length,nullptr,nullptr);
                    if(sameWindowsVideoFunction(current,devicePath)) {
                        printf("[XU] Matched DirectShow control path: %s\n",current.c_str());
                        IBaseFilter* filter=nullptr;
                        const auto bindHr=moniker->BindToObject(nullptr,nullptr,IID_IBaseFilter,reinterpret_cast<void**>(&filter));
                        printf("[XU] BindToObject: hr=0x%08lx\n",static_cast<unsigned long>(bindHr));
                        if(SUCCEEDED(bindHr)&&filter) {
                            filter_=filter;VariantClear(&value);bag->Release();moniker->Release();enumerator->Release();devEnum->Release();return true;
                        }
                    }
                } else printf("[XU] DevicePath unavailable: hr=0x%08lx vt=%u\n",static_cast<unsigned long>(readHr),unsigned(value.vt));
                VariantClear(&value);bag->Release();
            } else printf("[XU] BindToStorage failed: hr=0x%08lx\n",static_cast<unsigned long>(bagHr));
            moniker->Release();
        }
        printf("[XU] Category entries=%u, end hr=0x%08lx\n",count,static_cast<unsigned long>(hr));
        enumerator->Release();
    }
    devEnum->Release();
    printf("[XU] No bindable DirectShow control for selected video function: %s\n",devicePath.c_str());
    return false;
}

