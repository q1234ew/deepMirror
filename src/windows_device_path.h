#pragma once
#include <string>
#include <algorithm>
#include <cctype>
// MF and DirectShow can expose different interface-category GUIDs for the same
// USB video function. Preserve VID/PID/MI and the entire instance identity.
inline std::string windowsVideoFunctionIdentity(std::string path) {
    std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c){return char(std::tolower(c));});
    const std::string prefix="@device:pnp:";
    if(path.compare(0,prefix.size(),prefix)==0)path.erase(0,prefix.size());
    if(path.compare(0,4,"\\\\?\\")!=0)return {};
    const auto category=path.find("#{");
    if(category==std::string::npos||path.find("vid_")==std::string::npos||path.find("pid_")==std::string::npos)return {};
    return path.substr(0,category);
}
inline bool sameWindowsVideoFunction(const std::string &a,const std::string &b) {
    const auto identity=windowsVideoFunctionIdentity(a);
    return !identity.empty()&&identity==windowsVideoFunctionIdentity(b);
}
