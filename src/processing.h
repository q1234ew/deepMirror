#pragma once
#include "capture.h"
#include <QByteArray>
#include <array>
#include <deque>
struct CameraModel {
    int width=0,height=0;
    double fx=0,fy=0,cx=0,cy=0;
};
struct Calibration {
    CameraModel left,rgb;
    // Maps RGB coordinates to the left optical frame; SDK/ROS parent <- child.
    std::array<double,9> rotation{1,0,0,0,1,0,0,0,1};
    std::array<double,3> translation{0,0,0};
    QJsonObject metadata;
};
struct RawImage {
    QByteArray bytes;
    int width=0,height=0;
    unsigned format=0;
    quint64 sdkUs=0,arrivalUs=0;
};
// Bounded, one-to-one matching on a shared monotonic host clock. Not exposure synchronization.
class FramePairer {
    std::deque<RawImage> rgb,depth;
public:
    quint64 dropped=0;
    bool ready() const { return !rgb.empty() && !depth.empty(); }
    void push(bool isRgb,RawImage image);
    bool take(RawImage &color,RawImage &range,quint64 toleranceUs=20000);
};
std::shared_ptr<Frame> processPair(const RawImage&,const RawImage&,const Calibration&,QString &error);
