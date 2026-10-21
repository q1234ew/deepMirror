bool ExtensionUnitControl::probeExtensionGuid() {
    // Ask usbvideo.sys for the actual extension descriptor on this node.
    // The returned payload starts with guidExtensionCode, bNumControls,
    // bNrInPins, then baSourceID[bNrInPins] (Microsoft XU INFO contract).
    KSP_NODE info{};
    info.Property.Set=PROPSETID_VIDCAP_EXTENSION_UNIT;
    info.Property.Id=KSPROPERTY_EXTENSION_UNIT_INFO;
    info.Property.Flags=KSPROPERTY_TYPE_GET|KSPROPERTY_TYPE_TOPOLOGY;
    info.NodeId=nodeId_;
    ULONG required=0;
    HRESULT hr=ksControl_->KsProperty(reinterpret_cast<PKSPROPERTY>(&info),sizeof(info),nullptr,0,&required);
    printf("[XU] Descriptor size: node=%lu hr=0x%08lx bytes=%lu\n",nodeId_,static_cast<unsigned long>(hr),required);
    const ULONG capacity=required?required:512;
    bool descriptorKnown=false;
    if(capacity<=65536) {
        std::vector<uint8_t> descriptor(capacity,0);ULONG returned=0;
        hr=ksControl_->KsProperty(reinterpret_cast<PKSPROPERTY>(&info),sizeof(info),descriptor.data(),capacity,&returned);
        printf("[XU] Descriptor read: node=%lu hr=0x%08lx bytes=%lu\n",nodeId_,static_cast<unsigned long>(hr),returned);
        if(SUCCEEDED(hr)&&returned<=capacity&&parseXuDescriptorGuid(descriptor.data(),returned,activeExtGuidBytes_)) {
            descriptorKnown=true;
            printf("[XU] Device extension GUID=%s controls=%u inputPins=%u node=%lu\n",getActiveGuidString().c_str(),unsigned(descriptor[16]),unsigned(descriptor[17]),nodeId_);
        }
    }
    auto probe=[this]() {
        uint8_t camId=0;
        if(query(kActiveCameraSelector,KSPROPERTY_TYPE_GET,&camId,sizeof(camId)))return true;
        camera_params params{};
        if(query(kCameraParamsSelector,KSPROPERTY_TYPE_GET,&params,sizeof(params)))return true;
        // Calibration may exist even when active-camera/parameter controls do not.
        // GET only: do not select a chunk or modify any camera configuration here.
        uint8_t block[kCalibChunkSize]{};
        return query(kCameraCalibSelectorBase,KSPROPERTY_TYPE_GET,block,sizeof(block));
    };
    if(descriptorKnown) {
        if(probe())return true;
        printf("[XU] Device GUID resolved but SDK read selectors unavailable: node=%lu\n",nodeId_);
    } else {
        printf("[XU] Descriptor unavailable; trying pinned SDK compatibility GUIDs: node=%lu\n",nodeId_);
        for(const GUID* candidate:kCandidateExtGuids) {
            std::memcpy(activeExtGuidBytes_,candidate,sizeof(activeExtGuidBytes_));
            printf("[XU] Compatibility GUID=%s node=%lu\n",getActiveGuidString().c_str(),nodeId_);
            if(probe())return true;
        }
    }
    std::memset(activeExtGuidBytes_,0,sizeof(activeExtGuidBytes_));return false;
}

