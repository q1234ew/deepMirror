#include "capture.h"
#include "diagnostics.h"
#include "device_diagnostics.h"
#include "uvc_picker.h"
#include <QtWidgets>
#include <QOpenGLWidget>
#include <QtConcurrent>
#include <cmath>
class ImageView:public QWidget {
public:
 QImage image;
 ImageView(){setMinimumSize(250,180);setAccessibleName("图像预览");}
 void paintEvent(QPaintEvent*)override{QPainter p(this);p.fillRect(rect(),QColor("#101722"));if(image.isNull()){p.setPen(QColor("#b7c5d9"));p.drawText(rect(),Qt::AlignCenter,"启动采集以显示画面");return;}QSize z=image.size();z.scale(size()-QSize(20,20),Qt::KeepAspectRatio);p.drawImage(QRect(QPoint((width()-z.width())/2,(height()-z.height())/2),z),image);}
};
class CloudView:public QOpenGLWidget {
public:
 std::shared_ptr<Frame> frame; double yaw=0.15,pitch=-0.1,zoom=1;QPoint last;
 CloudView(){setMinimumSize(320,260);setAccessibleName("彩色点云，拖动旋转，滚轮缩放，双击复位");}
 void mousePressEvent(QMouseEvent*e)override{last=e->position().toPoint();}
 void mouseMoveEvent(QMouseEvent*e)override{auto d=e->position().toPoint()-last;last=e->position().toPoint();yaw+=d.x()*.008;pitch=qBound(-1.4,pitch+d.y()*.008,1.4);update();}
 void wheelEvent(QWheelEvent*e)override{zoom=qBound(.25,zoom*std::exp(e->angleDelta().y()*.001),4.0);update();}
 void mouseDoubleClickEvent(QMouseEvent*)override{yaw=.15;pitch=-.1;zoom=1;update();}
 void paintGL()override {
 QPainter p(this);p.fillRect(rect(),QColor("#101722"));p.setPen(QColor("#233044"));for(int i=0;i<width();i+=40)p.drawLine(i,0,i,height());for(int i=0;i<height();i+=40)p.drawLine(0,i,width(),i);
 if(frame){struct Dot{float z,x,y;QRgb c;};QVector<Dot> dots;dots.reserve(frame->points.size()/4);for(int i=0;i<frame->points.size();i+=4){auto a=frame->points[i];double z=a.z-1.8,x=a.x*std::cos(yaw)+z*std::sin(yaw);z=-a.x*std::sin(yaw)+z*std::cos(yaw);double y=a.y*std::cos(pitch)-z*std::sin(pitch);double zz=a.y*std::sin(pitch)+z*std::cos(pitch)+3.2;if(zz>.1)dots.push_back({float(zz),float(width()/2+x*qMin(width(),height())*1.35*zoom/zz),float(height()/2+y*qMin(width(),height())*1.35*zoom/zz),a.color});}std::sort(dots.begin(),dots.end(),[](auto a,auto b){return a.z>b.z;});for(auto d:dots){p.setPen(QPen(QColor(d.c),2));p.drawPoint(QPointF(d.x,d.y));}}
 p.setPen(QColor("#b7c5d9"));p.drawText(15,height()-15,"拖动旋转 · 滚轮缩放 · 双击复位 | 单位 m");
 }
};
class Window:public QMainWindow {
 std::optional<UvcPair> selectedUvc;
 QTimer timer,recordTimer;std::unique_ptr<FrameSource> source;QComboBox *mode;QLabel *badge;QGroupBox *rgbPanel;QElapsedTimer freshness;bool stopping=false;QString lastMessage;std::shared_ptr<Frame> frame;
 ImageView *rgb,*depth;CloudView *cloud;QPushButton *start,*snap,*record;QLabel *status,*stats;QLineEdit *path;QTextEdit *log;QSpinBox *fps,*interval;QFutureWatcher<QString> writer;bool recording=false;quint64 saved=0;bool pendingClose=false;
 void note(QString s){log->append(QTime::currentTime().toString("HH:mm:ss")+"  "+s);}
 void save(){if(!frame||writer.isRunning()||(timer.isActive()&&freshness.isValid()&&freshness.elapsed()>3000))return;auto copy=frame;QString target=path->text().trimmed();if(target.isEmpty()){note("请先选择保存目录");return;}snap->setEnabled(false);status->setText("正在保存…");writer.setFuture(QtConcurrent::run([copy,target]{return saveFrame(*copy,target);}));}
 void tick(){
  if(!source)return;
  if(stopping){if(source->finished()){source.reset();timer.stop();stopping=false;start->setEnabled(true);start->setText("启动采集");mode->setEnabled(true);status->setText("已暂停 · 可保存最后有效帧");snap->setEnabled(bool(frame)&&!writer.isRunning());}return;}
  const auto message=source->status();status->setText(message);
  if(message!=lastMessage){lastMessage=message;note(message);}
  auto incoming=source->next();
  if(!incoming){
   if(freshness.isValid()&&freshness.elapsed()>3000){snap->setEnabled(false);if(recording){stopRecord();note("数据已中断，连续保存停止");}}
   if(source->finished()&&mode->currentData().toBool()){note("设备后端已结束");source.reset();timer.stop();stopRecord();record->setEnabled(false);mode->setEnabled(true);start->setText("启动采集");status->setText(message);}
   return;
  }
  frame=std::move(incoming);freshness.restart();snap->setEnabled(!writer.isRunning());
  rgb->image=frame->rgb;depth->image=frame->depthPreview;cloud->frame=frame;
  rgbPanel->setTitle(QString("RGB · %1 × %2").arg(frame->rgb.width()).arg(frame->rgb.height()));
  rgb->update();depth->update();cloud->update();
  stats->setText(QString("帧 %1 · 预览上限 %2 FPS · 配对差 %3 μs（%4） · %5 点 · 已保存 %6 组")
   .arg(frame->sequence).arg(fps->value()).arg(frame->pairDeltaUs).arg(frame->source=="simulation"?"模拟":"主机到达时间").arg(frame->points.size()).arg(saved));
 }
 void requestPause(){
  stopRecord();record->setEnabled(false);
  if(source){source->requestStop();stopping=true;start->setEnabled(false);status->setText("正在停止设备…");}
 }
 void stopRecord(){recording=false;recordTimer.stop();record->setText("开始连续保存");}
public:
 Window(){
 setWindowTitle("DeepMirror · RGB-D Capture");resize(1360,860);setMinimumSize(1040,720);
 auto root=new QWidget;setCentralWidget(root);
 auto outer=new QVBoxLayout(root);outer->setContentsMargins(18,14,18,12);outer->setSpacing(10);
 auto header=new QHBoxLayout;
 auto title=new QLabel("DeepMirror  /  采集工作台");title->setStyleSheet("font-size:20px;font-weight:600");header->addWidget(title);
 badge=new QLabel("模拟设备");badge->setStyleSheet("background:#dce9fb;color:#24568e;border-radius:4px;padding:4px 8px");header->addWidget(badge);header->addStretch();
 status=new QLabel("就绪 · 模拟模式");header->addWidget(status);outer->addLayout(header);
 auto bar=new QHBoxLayout;bar->setSpacing(8);
 start=new QPushButton("启动采集");start->setShortcut(QKeySequence("Space"));start->setStyleSheet("QPushButton{background:#2466ce;color:white;border-color:#2466ce}QPushButton:hover{background:#1955b5}");
 snap=new QPushButton("保存当前组");snap->setShortcut(QKeySequence::Save);snap->setEnabled(false);
 record=new QPushButton("开始连续保存");record->setEnabled(false);
 bar->addWidget(start);bar->addWidget(snap);bar->addWidget(record);bar->addStretch();
 auto shortcuts=new QLabel("空格 启停  ·  Ctrl/Cmd+S 保存");shortcuts->setStyleSheet("color:#52647b");bar->addWidget(shortcuts);outer->addLayout(bar);
 rgb=new ImageView;depth=new ImageView;cloud=new CloudView;
 auto panel=[](QString title,QWidget*w){auto box=new QGroupBox(title);auto l=new QVBoxLayout(box);l->setContentsMargins(8,10,8,8);l->addWidget(w);return box;};
 auto images=new QSplitter(Qt::Vertical);images->setChildrenCollapsible(false);images->setHandleWidth(7);
 rgbPanel=panel("RGB · 320 × 240",rgb);images->addWidget(rgbPanel);images->addWidget(panel("深度 · 16 位 / 伪彩色预览",depth));images->setSizes({300,300});
 auto previews=new QSplitter(Qt::Horizontal);previews->setChildrenCollapsible(false);previews->setHandleWidth(8);
 previews->addWidget(images);previews->addWidget(panel("彩色点云 · 相机坐标系",cloud));previews->setStretchFactor(0,2);previews->setStretchFactor(1,3);previews->setSizes({400,620});
 auto body=new QHBoxLayout;body->setSpacing(12);body->addWidget(previews,1);
 auto side=new QGroupBox("采集设置");side->setFixedWidth(248);auto form=new QVBoxLayout(side);form->setContentsMargins(12,14,12,12);form->setSpacing(10);
 mode=new QComboBox;mode->addItem("模拟设备",false);
#ifdef DEEPMIRROR_WINDOWS_SDK
 mode->addItem("Insight 相机 / Windows SDK",true);
#endif
form->addWidget(new QLabel("数据源"));form->addWidget(mode);
 auto diagnose=new QPushButton("设备诊断…");form->addWidget(diagnose);
 connect(diagnose,&QPushButton::clicked,this,[this]{showDeviceDiagnostics(this);});
 auto discover=new QPushButton("发现并匹配 UVC 源…");form->addWidget(discover);
 connect(discover,&QPushButton::clicked,this,[this]{
  if(source&&!source->finished()){note("请先暂停采集，再发现设备");return;}
  auto selected=chooseUvcPair(this);if(selected){selectedUvc=selected;note("已选择 UVC 组合："+describeUvcPair(*selected));}
 });
 auto hint=new QLabel("当前预览与保存均为模拟数据");
 connect(mode,&QComboBox::currentIndexChanged,this,[this,hint](int){bool real=mode->currentData().toBool();badge->setText(real?"真实设备":"模拟设备");hint->setText(real?"UVC 模式 · 需开启深度输出\n程序不会修改设备配置":"当前预览与保存均为模拟数据");});hint->setWordWrap(true);hint->setStyleSheet("color:#52647b;font-size:12px");form->addWidget(hint);
 auto settings=new QFormLayout;settings->setHorizontalSpacing(8);settings->setVerticalSpacing(10);
 fps=new QSpinBox;fps->setRange(1,30);fps->setValue(15);fps->setSuffix(" FPS");fps->setAccessibleName("预览目标帧率");settings->addRow("目标帧率",fps);
 interval=new QSpinBox;interval->setRange(200,60000);interval->setValue(1000);interval->setSuffix(" ms");interval->setAccessibleName("连续保存间隔");settings->addRow("保存间隔",interval);form->addLayout(settings);
 auto line=new QFrame;line->setFrameShape(QFrame::HLine);line->setStyleSheet("color:#d5dfe9");form->addWidget(line);
 form->addWidget(new QLabel("保存目录"));path=new QLineEdit(QDir::homePath()+"/Pictures/DeepMirror");path->setAccessibleName("保存目录");path->setToolTip(path->text());form->addWidget(path);
 connect(path,&QLineEdit::textChanged,path,&QWidget::setToolTip);
 auto choose=new QPushButton("选择目录…");form->addWidget(choose);
 auto outputTitle=new QLabel("每组保存");outputTitle->setStyleSheet("font-weight:600;margin-top:6px");form->addWidget(outputTitle);
 auto desc=new QLabel("RGB PNG  ·  16 位深度 PNG\n彩色点云 PLY  ·  元数据 JSON");desc->setWordWrap(true);desc->setStyleSheet("font-size:12px;color:#52647b");form->addWidget(desc);form->addStretch();
 auto tips=new QLabel("深度单位 mm · 点云单位 m\n连续保存繁忙时跳过当前请求");tips->setWordWrap(true);tips->setStyleSheet("font-size:12px;color:#52647b");form->addWidget(tips);body->addWidget(side);outer->addLayout(body,1);
 auto footer=new QHBoxLayout;stats=new QLabel("尚无数据");stats->setStyleSheet("font-size:12px;color:#40546c");footer->addWidget(stats,1);
 auto logToggle=new QToolButton;logToggle->setText("日志");logToggle->setCheckable(true);logToggle->setArrowType(Qt::RightArrow);logToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);footer->addWidget(logToggle);
 if(!sdkDiagnosticPath().isEmpty()){
  auto openLogs=new QToolButton;openLogs->setText("打开 SDK 日志目录");footer->addWidget(openLogs);
  connect(openLogs,&QToolButton::clicked,this,[]{QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(sdkDiagnosticPath()).absolutePath()));});
 }
 outer->addLayout(footer);
 log=new QTextEdit;log->setReadOnly(true);log->document()->setMaximumBlockCount(1000);log->setFixedHeight(88);log->setAccessibleName("采集日志");log->hide();outer->addWidget(log);
 connect(logToggle,&QToolButton::toggled,this,[this,logToggle](bool on){log->setVisible(on);logToggle->setArrowType(on?Qt::DownArrow:Qt::RightArrow);});
 setStyleSheet("QSplitter::handle{background:#dce3ec;border-radius:3px}QSplitter::handle:hover{background:#9ab9df}QMainWindow{background:#eef2f6;color:#182638}QGroupBox{background:white;border:1px solid #d5dfe9;border-radius:8px;margin-top:12px;padding-top:12px;font-weight:600}QGroupBox::title{subcontrol-origin:margin;left:12px}QPushButton{padding:9px 14px;border:1px solid #b9c8d8;border-radius:5px;background:white;color:#182638}QPushButton:hover{background:#e1edff}QPushButton:disabled{color:#8492a3;background:#edf0f4}QPushButton:focus{border:2px solid #246be0}QSpinBox,QComboBox,QLineEdit{padding:7px;border:1px solid #b9c8d8;border-radius:4px;background:white;color:#182638}QLabel{color:#24364d}QTextEdit{background:white;color:#40546c;border:1px solid #d5dfe9;border-radius:5px}");
 connect(&timer,&QTimer::timeout,this,[this]{tick();});connect(&recordTimer,&QTimer::timeout,this,[this]{save();});connect(start,&QPushButton::clicked,this,[this]{
  if(timer.isActive()){requestPause();return;}
  if(mode->currentData().toBool()&&!selectedUvc){selectedUvc=chooseUvcPair(this,true);if(!selectedUvc)return;}
  frame.reset();cloud->frame.reset();rgb->image={};depth->image={};rgb->update();depth->update();cloud->update();snap->setEnabled(false);freshness.invalidate();lastMessage.clear();
  source=mode->currentData().toBool()?createWindowsSource(*selectedUvc):std::make_unique<SimulatedSource>();
  if(!source){note("此构建不包含 Windows SDK 后端");return;}
  mode->setEnabled(false);timer.start(1000/fps->value());start->setText("暂停采集");record->setEnabled(true);tick();
 });
 connect(snap,&QPushButton::clicked,this,[this]{save();});connect(record,&QPushButton::clicked,this,[this]{if(recording){stopRecord();note("连续保存已停止");}else{recording=true;record->setText("停止连续保存");recordTimer.start(interval->value());save();note("连续保存已开启");}});
 connect(fps,&QSpinBox::valueChanged,this,[this](int n){if(timer.isActive())timer.setInterval(1000/n);});connect(interval,&QSpinBox::valueChanged,this,[this](int n){recordTimer.setInterval(n);});connect(choose,&QPushButton::clicked,this,[this]{auto d=QFileDialog::getExistingDirectory(this,"选择保存目录",path->text());if(!d.isEmpty())path->setText(d);});
 connect(&writer,&QFutureWatcher<QString>::finished,this,[this]{auto err=writer.result();snap->setEnabled(bool(frame)&&!stopping&&(!timer.isActive()||!freshness.isValid()||freshness.elapsed()<=3000));if(err.isEmpty()){++saved;note(QString("已保存第 %1 组 RGB / 深度 / 点云").arg(saved));}else{stopRecord();note("保存失败："+err);}status->setText(err.isEmpty()?(timer.isActive()&&source?source->status():"已暂停"):"保存失败，请查看日志");if(pendingClose)close();});note("模拟后端就绪。空格启动 / 暂停，Ctrl/Cmd+S 保存。");
 if(!sdkDiagnosticPath().isEmpty())note("底层 SDK 诊断日志："+sdkDiagnosticPath());
 }
 void closeEvent(QCloseEvent*e)override{
  stopRecord();
  if(source&&!source->finished()){source->requestStop();stopping=true;pendingClose=true;status->setText("正在停止采集并完成保存…");start->setEnabled(false);timer.start(50);e->ignore();QTimer::singleShot(100,this,[this]{close();});return;}
  timer.stop();source.reset();
  if(writer.isRunning()){pendingClose=true;status->setText("正在完成保存，随后关闭…");e->ignore();}else e->accept();
 }
 void demo(QString screenshot){start->click();QTimer::singleShot(1200,this,[this,screenshot]{if(!screenshot.isEmpty())grab().save(screenshot);});}
};
int main(int argc,char**argv){
 for(int i=1;i<argc;++i)if(QString::fromLocal8Bit(argv[i])=="--diagnostics-self-test"){
  QCoreApplication app(argc,argv);app.setApplicationName("DeepMirror Capture Log Test");initializeSdkDiagnostics();
  if(sdkDiagnosticPath().isEmpty())return 1;
  fprintf(stdout,"stdout diagnostic probe\n");fprintf(stderr,"stderr diagnostic probe\n");fflush(stdout);fflush(stderr);
  QFile logFile(sdkDiagnosticPath());if(!logFile.open(QIODevice::ReadOnly))return 1;
  const auto content=logFile.readAll();const bool ok=content.contains("stdout diagnostic probe")&&content.contains("stderr diagnostic probe");
  logFile.close();QFile::remove(sdkDiagnosticPath());return ok?0:1;
 }
 // Data self-tests do not construct a GUI or require a platform/OpenGL plugin.
 for(int i=1;i<argc;++i)if(QString::fromLocal8Bit(argv[i])=="--self-test"){
  QCoreApplication app(argc,argv);
  return selfTest(app.arguments().value(i+1,QDir::tempPath()+"/deepmirror-test"));
 }
 for(int i=1;i<argc;++i)if(QString::fromLocal8Bit(argv[i])=="--device-diagnostics"){
  QCoreApplication app(argc,argv);app.setApplicationName("DeepMirror Capture");
  QFile output(app.arguments().value(i+1));
  if(!output.open(QIODevice::WriteOnly|QIODevice::Truncate))return 1;
  const auto report=(collectDeviceDiagnostics()+"\n"+discoverUvcSources().report).toUtf8();
  return output.write(report)==report.size()?0:1;
 }
 QApplication app(argc,argv);app.setApplicationName("DeepMirror Capture");initializeSdkDiagnostics();
 auto args=app.arguments();Window w;w.show();
 if(args.contains("--screenshot"))w.demo(args.value(args.indexOf("--screenshot")+1));
 return app.exec();
}
