#pragma once
#include <QImage>
#include <QVector>
#include <QString>
#include <QJsonObject>
#include <memory>
struct Point { float x,y,z; QRgb color; };
struct Frame {
    QImage rgb, depth, depthPreview;
    QVector<Point> points;
    quint64 sequence=0, timestampUs=0;
    double fx=280, fy=280, cx=159.5, cy=119.5;
    QString source="simulation", clock="synthetic_host_wall_clock";
    quint64 rgbTimestampUs=0, depthTimestampUs=0;
    qint64 pairDeltaUs=0;
    QJsonObject details;
};
// Device backends return an owned, synchronized frame. Windows SDK adapter belongs here.
class FrameSource {
public:
    virtual ~FrameSource()=default;
    virtual std::shared_ptr<Frame> next()=0;
    virtual QString status() const { return "模拟采集"; }
    virtual void requestStop() {}
    virtual bool finished() const { return true; }
};
class SimulatedSource final: public FrameSource {
    quint64 sequence=0;
public:
    std::shared_ptr<Frame> next() override;
};
QString saveFrame(const Frame &frame, const QString &root);
int selfTest(const QString &root);

std::unique_ptr<FrameSource> createWindowsSource();
