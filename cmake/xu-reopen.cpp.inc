static std::vector<std::string> xuPathsForDevice() {
    std::vector<std::string> paths;
    auto add = [&paths](const std::string& p) {
        if (!p.empty() && std::find(paths.begin(), paths.end(), p) == paths.end()) paths.push_back(p);
    };
    const auto slots = findUvcDevices(VENDOR_ID, PRODUCT_ID);
    for (const auto& p : slots) add(p);
    // Also enumerate the grayscale VideoControl function: split-interface devices
    // may expose XU calibration on it even though we do not capture its images.
    ICreateDevEnum* dev = nullptr;
    IEnumMoniker* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, reinterpret_cast<void**>(&dev));
    if (SUCCEEDED(hr) && dev && dev->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumerator, 0) == S_OK && enumerator) {
        IMoniker* moniker = nullptr;
        while (enumerator->Next(1, &moniker, nullptr) == S_OK) {
            IPropertyBag* bag = nullptr;
            if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_IPropertyBag, reinterpret_cast<void**>(&bag)))) {
                VARIANT value; VariantInit(&value);
                if (SUCCEEDED(bag->Read(L"DevicePath", &value, nullptr)) && value.vt == VT_BSTR) {
                    std::wstring wide(value.bstrVal);
                    std::string path(wide.begin(), wide.end()), upper = path;
                    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch){return static_cast<char>(std::toupper(ch));});
                    char vid[16], pid[16];
                    std::snprintf(vid, sizeof(vid), "VID_%04X", VENDOR_ID);
                    std::snprintf(pid, sizeof(pid), "PID_%04X", PRODUCT_ID);
                    if (upper.find(vid) != std::string::npos && upper.find(pid) != std::string::npos) add(path);
                }
                VariantClear(&value); bag->Release();
            }
            moniker->Release();
        }
        enumerator->Release();
    }
    if (dev) dev->Release();
    printf("[XU] Candidate VideoControl paths: %zu\n", paths.size());
    for (const auto& p : paths) printf("[XU] Candidate: %s\n", p.c_str());
    return paths;
}

static bool reopenXUControlLocked(const char* reason) {
    if (g_ctx.xu) { delete g_ctx.xu; g_ctx.xu = nullptr; }
    for (const auto& path : xuPathsForDevice()) {
        auto* xu = new (std::nothrow) viewer::ExtensionUnitControl();
        if (!xu) return false;
        printf("[XU] Opening candidate (%s): %s\n", reason ? reason : "unknown", path.c_str());
        if (xu->open(path)) { g_ctx.xu = xu; return true; }
        delete xu;
    }
    fprintf(stderr, "[XU] No compatible control interface (%s)\n", reason ? reason : "unknown");
    return false;
}

