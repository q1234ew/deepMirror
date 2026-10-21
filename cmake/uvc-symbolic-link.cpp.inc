    std::string getSymbolicLink(const std::string& devicePath) {
        // MFCreateDeviceSource accepts the exact symbolic link selected by discovery.
        // Re-enumerating by VID/PID/MI can silently redirect to a different physical camera.
        return devicePath;
    }

