int insight9_receive_get_camera_calib(int cam_idx, camera_calib *calib) {
    if (!calib || cam_idx < 0 || cam_idx >= viewer::kCalibCamCount) return -1;
    std::lock_guard<std::recursive_mutex> lock(g_ctx.xuMutex);
    viewer::camera_calib result{};
    if (g_ctx.xu && g_ctx.xu->readCameraCalib(static_cast<uint8_t>(cam_idx), result)) {
        memcpy(calib, &result, sizeof(camera_calib));
        return 0;
    }
    // A successfully opened XU may expose parameters but not calibration. Test the
    // requested selector on every candidate, rather than reopening RGB repeatedly.
    for (const auto& path : xuPathsForDevice()) {
        auto* xu = new (std::nothrow) viewer::ExtensionUnitControl();
        if (!xu) return -1;
        if (xu->open(path) && xu->readCameraCalib(static_cast<uint8_t>(cam_idx), result)) {
            if (g_ctx.xu) delete g_ctx.xu;
            g_ctx.xu = xu;
            memcpy(calib, &result, sizeof(camera_calib));
            printf("[XU] Calibration camera=%d selector=0x%02x read from %s\n", cam_idx, 0x14 + cam_idx, path.c_str());
            return 0;
        }
        delete xu;
    }
    fprintf(stderr, "[XU] Calibration camera=%d selector=0x%02x failed on all interfaces\n", cam_idx, 0x14 + cam_idx);
    return -1;
}

