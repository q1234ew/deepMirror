#include "uvc_picker.h"
#include <QtWidgets>
#include <QtConcurrent>
std::optional<UvcPair> chooseUvcPair(QWidget *parent,bool autoAcceptSingle) {
    QDialog dialog(parent);dialog.setWindowTitle("发现并匹配 UVC 源");dialog.resize(900,650);
    auto layout=new QVBoxLayout(&dialog);
    auto hint=new QLabel("正在发现视频源并读取支持的格式…",&dialog);hint->setWordWrap(true);layout->addWidget(hint);
    auto combos=new QComboBox(&dialog);combos->setAccessibleName("匹配的 RGB 和深度组合");layout->addWidget(combos);
    auto details=new QPlainTextEdit(&dialog);details->setReadOnly(true);details->setLineWrapMode(QPlainTextEdit::NoWrap);details->setAccessibleName("UVC 发现详情");layout->addWidget(details,1);
    auto buttons=new QHBoxLayout;auto refresh=new QPushButton("重新发现",&dialog);auto copy=new QPushButton("复制发现结果",&dialog);auto use=new QPushButton("使用此组合",&dialog);auto cancel=new QPushButton("取消",&dialog);
    for(auto button:{refresh,copy,use,cancel}){button->setAutoDefault(false);buttons->addWidget(button);}layout->addLayout(buttons);
    UvcDiscovery found;QFutureWatcher<UvcDiscovery> watcher;
    auto update=[&]{const int index=combos->currentIndex();QString selected;if(index>=0&&index<found.pairs.size())selected="选中组合：\n"+describeUvcPair(found.pairs[index])+"\n\n";details->setPlainText(selected+found.report);};
    QObject::connect(combos,&QComboBox::currentIndexChanged,&dialog,[&](int){update();});
    auto scan=[&]{refresh->setEnabled(false);copy->setEnabled(false);use->setEnabled(false);combos->clear();hint->setText("正在发现视频源并读取支持的格式…");watcher.setFuture(QtConcurrent::run(discoverUvcSources));};
    QObject::connect(&watcher,&QFutureWatcher<UvcDiscovery>::finished,&dialog,[&]{
        found=watcher.result();
        for(const auto &pair:found.pairs)combos->addItem(QString("%1 / %2 — %3").arg(pair.rgb.name,pair.depth.name,pair.rgb.physicalId));
        use->setEnabled(!found.pairs.isEmpty());refresh->setEnabled(true);copy->setEnabled(true);update();
        hint->setText(found.pairs.isEmpty()?"未发现当前采集后端支持的 RGB / 深度组合。请复制发现结果排查；可在设备诊断中查看驱动状态。":(found.pairs.size()==1?"已匹配同一台设备的 RGB 与深度；启动后仍需读取标定。":"发现多个组合，请选择要采集的设备。"));
        if(autoAcceptSingle&&found.pairs.size()==1)dialog.accept();
    });
    QObject::connect(refresh,&QPushButton::clicked,&dialog,scan);
    QObject::connect(copy,&QPushButton::clicked,&dialog,[&]{QApplication::clipboard()->setText(details->toPlainText());});
    QObject::connect(use,&QPushButton::clicked,&dialog,&QDialog::accept);
    QObject::connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);
    scan();
    if(dialog.exec()==QDialog::Accepted&&combos->currentIndex()>=0&&combos->currentIndex()<found.pairs.size())return found.pairs[combos->currentIndex()];
    return std::nullopt;
}
