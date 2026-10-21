// Exact paths selected by the application's native-format discovery.
static std::mutex deepmirrorPathsMutex;
static std::vector<std::string> deepmirrorSlots;
static std::vector<std::string> deepmirrorControls;
void deepmirrorSetUvcPaths(const std::string &rgb,const std::string &depth,const std::vector<std::string> &controls) {
    std::lock_guard<std::mutex> lock(deepmirrorPathsMutex);
    deepmirrorSlots.clear();deepmirrorControls.clear();
    if(!rgb.empty()&&!depth.empty()) {
        deepmirrorSlots.resize(UVC_NUM);
        deepmirrorSlots[RGB_UVC_ID]=rgb;deepmirrorSlots[COMPOSITE_UVC_ID]=depth;
        deepmirrorControls=controls;
    }
}
static std::vector<std::string> deepmirrorSelectedSlots() {
    std::lock_guard<std::mutex> lock(deepmirrorPathsMutex);return deepmirrorSlots;
}
static std::vector<std::string> deepmirrorSelectedControls() {
    std::lock_guard<std::mutex> lock(deepmirrorPathsMutex);return deepmirrorControls;
}
