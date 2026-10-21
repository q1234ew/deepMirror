bool ExtensionUnitControl::probeExtensionGuid() {
    auto probe=[this]() {
        uint8_t camId=0;
        if(query(kActiveCameraSelector,KSPROPERTY_TYPE_GET,&camId,sizeof(camId)))return true;
        camera_params params{};
        if(query(kCameraParamsSelector,KSPROPERTY_TYPE_GET,&params,sizeof(params)))return true;
        // GET only; calibration can exist without parameter/active-camera controls.
        uint8_t block[kCalibChunkSize]{};
        return query(kCameraCalibSelectorBase,KSPROPERTY_TYPE_GET,block,sizeof(block));
    };
    std::vector<std::array<uint8_t,16>> sets;
    // GUID_NULL + SETSUPPORT enumerates an array of supported property-set GUIDs.
    // Try an explicit topology request and the node object's implicit scope.
    for(bool topology:{true,false}) {
        KSP_NODE request{};request.Property.Set=GUID_NULL;
        request.Property.Flags=KSPROPERTY_TYPE_SETSUPPORT|(topology?KSPROPERTY_TYPE_TOPOLOGY:0);
        request.NodeId=nodeId_;
        const ULONG requestSize=topology?sizeof(request):sizeof(KSPROPERTY);
        ULONG required=0;
        HRESULT hr=ksControl_->KsProperty(reinterpret_cast<PKSPROPERTY>(&request),requestSize,nullptr,0,&required);
        printf("[XU] Property-set size: node=%lu topology=%d hr=0x%08lx bytes=%lu\n",nodeId_,int(topology),static_cast<unsigned long>(hr),required);
        const ULONG capacity=required?required:4096;
        if(capacity>65536)continue;
        std::vector<uint8_t> buffer(capacity,0);ULONG returned=0;
        hr=ksControl_->KsProperty(reinterpret_cast<PKSPROPERTY>(&request),requestSize,buffer.data(),capacity,&returned);
        printf("[XU] Property-set enumeration: node=%lu topology=%d hr=0x%08lx bytes=%lu\n",nodeId_,int(topology),static_cast<unsigned long>(hr),returned);
        if(FAILED(hr)||returned>capacity)continue;
        for(const auto &set:parseXuPropertySets(buffer.data(),returned))
            if(std::find(sets.begin(),sets.end(),set)==sets.end())sets.push_back(set);
    }
    bool descriptorKnown=false;
    for(const auto &set:sets) {
        std::memcpy(activeExtGuidBytes_,set.data(),16);
        printf("[XU] Enumerated property GUID=%s node=%lu\n",getActiveGuidString().c_str(),nodeId_);
        KSP_NODE info{};std::memcpy(&info.Property.Set,set.data(),16);
        info.Property.Id=KSPROPERTY_EXTENSION_UNIT_INFO;
        info.Property.Flags=KSPROPERTY_TYPE_GET|KSPROPERTY_TYPE_TOPOLOGY;info.NodeId=nodeId_;
        ULONG required=0;
        HRESULT hr=ksControl_->KsProperty(reinterpret_cast<PKSPROPERTY>(&info),sizeof(info),nullptr,0,&required);
        printf("[XU] Descriptor size: node=%lu hr=0x%08lx bytes=%lu\n",nodeId_,static_cast<unsigned long>(hr),required);
        const ULONG capacity=required?required:512;
        if(capacity>65536)continue;
        std::vector<uint8_t> descriptor(capacity,0);ULONG returned=0;
        hr=ksControl_->KsProperty(reinterpret_cast<PKSPROPERTY>(&info),sizeof(info),descriptor.data(),capacity,&returned);
        printf("[XU] Descriptor read: node=%lu hr=0x%08lx bytes=%lu\n",nodeId_,static_cast<unsigned long>(hr),returned);
        uint8_t actual[16]{};
        if(FAILED(hr)||returned>capacity||!parseXuDescriptorGuid(descriptor.data(),returned,actual)||std::memcmp(actual,set.data(),16)!=0)continue;
        descriptorKnown=true;std::memcpy(activeExtGuidBytes_,actual,16);
        printf("[XU] Device extension GUID=%s controls=%u inputPins=%u node=%lu\n",getActiveGuidString().c_str(),unsigned(descriptor[16]),unsigned(descriptor[17]),nodeId_);
        if(descriptor[16]<kCameraCalibSelectorBase+2) {
            printf("[XU] Device XU exposes too few controls for RGB-D calibration selectors\n");continue;
        }
        if(probe())return true;
        printf("[XU] Device GUID resolved but SDK read selectors unavailable: node=%lu\n",nodeId_);
    }
    if(!descriptorKnown) {
        printf("[XU] Descriptor unavailable; trying pinned SDK compatibility GUIDs: node=%lu\n",nodeId_);
        for(const GUID* candidate:kCandidateExtGuids) {
            std::memcpy(activeExtGuidBytes_,candidate,sizeof(activeExtGuidBytes_));
            printf("[XU] Compatibility GUID=%s node=%lu\n",getActiveGuidString().c_str(),nodeId_);
            if(probe())return true;
        }
    }
    std::memset(activeExtGuidBytes_,0,sizeof(activeExtGuidBytes_));return false;
}

