bool ExtensionUnitControl::open(const std::string& devicePath) {
    close();
    printf("[XU] Probing device path: %s\n", devicePath.c_str());
    if (!bindFilterByDevicePath(devicePath)) {
        fprintf(stderr, "[XU] DirectShow filter binding failed: %s\n", devicePath.c_str());
        return false;
    }
    IKsTopologyInfo* topology = nullptr;
    HRESULT hr = filter_->QueryInterface(kIidIKsTopologyInfo, reinterpret_cast<void**>(&topology));
    if (FAILED(hr) || !topology) {
        fprintf(stderr, "[XU] IKsTopologyInfo unavailable: hr=0x%08lx\n", static_cast<unsigned long>(hr));
        close();
        return false;
    }
    DWORD count = 0;
    hr = topology->get_NumNodes(&count);
    if (FAILED(hr)) {
        fprintf(stderr, "[XU] get_NumNodes failed: hr=0x%08lx\n", static_cast<unsigned long>(hr));
        topology->Release();
        close();
        return false;
    }
    printf("[XU] Topology nodes: %lu\n", count);
    for (DWORD i = 0; i < count; ++i) {
        GUID type = GUID_NULL;
        if (FAILED(topology->get_NodeType(i, &type))) continue;
        printf("[XU] node=%lu type={%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n",
            i, type.Data1, type.Data2, type.Data3,
            type.Data4[0], type.Data4[1], type.Data4[2], type.Data4[3],
            type.Data4[4], type.Data4[5], type.Data4[6], type.Data4[7]);
        if (!InlineIsEqualGUID(type, kKsNodeTypeDevSpecific)) continue;
        nodeId_ = i;
        // UVC extension property sets belong to the topology node, not the filter.
        hr = topology->CreateNodeInstance(i, IID_IKsControl, reinterpret_cast<void**>(&ksControl_));
        if (SUCCEEDED(hr) && ksControl_) {
            if (probeExtensionGuid()) {
                printf("[XU] Bound node IKsControl: node=%lu guid=%s path=%s\n", i, getActiveGuidString().c_str(), devicePath.c_str());
                topology->Release();
                return true;
            }
            ksControl_->Release();
            ksControl_ = nullptr;
        } else {
            fprintf(stderr, "[XU] CreateNodeInstance IKsControl failed: node=%lu hr=0x%08lx\n", i, static_cast<unsigned long>(hr));
        }
        // Retain compatibility with driver stacks exposing a working filter-level proxy.
        hr = filter_->QueryInterface(IID_IKsControl, reinterpret_cast<void**>(&ksControl_));
        if (SUCCEEDED(hr) && ksControl_) {
            if (probeExtensionGuid()) {
                printf("[XU] Bound filter fallback: node=%lu guid=%s path=%s\n", i, getActiveGuidString().c_str(), devicePath.c_str());
                topology->Release();
                return true;
            }
            ksControl_->Release();
            ksControl_ = nullptr;
        }
    }
    topology->Release();
    fprintf(stderr, "[XU] No compatible extension node/GUID found: %s\n", devicePath.c_str());
    close();
    return false;
}

