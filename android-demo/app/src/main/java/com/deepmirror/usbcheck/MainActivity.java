package com.deepmirror.usbcheck;

import android.Manifest;
import androidx.core.content.ContextCompat;
import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.hardware.usb.*;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.View;
import android.widget.*;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public final class MainActivity extends Activity {
    private static final String USB_PERMISSION="com.deepmirror.usbcheck.USB_PERMISSION";
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final AtomicBoolean cancelled=new AtomicBoolean();
    private final List<UsbDevice> devices=new ArrayList<>();
    private UsbManager usb;
    private Spinner selector;
    private TextView state,report;
    private Button refresh,connect,stable,stop;
    private String pendingName,activeName;
    private boolean pendingStable,busy,destroyed;
    private int generation;

    private final BroadcastReceiver receiver=new BroadcastReceiver() {
        @Override public void onReceive(Context context,Intent intent) {
            String action=intent.getAction();
            if(USB_PERMISSION.equals(action)) {
                String requested=intent.getStringExtra("requestedDevice");
                if(pendingName==null||!pendingName.equals(requested))return;
                // Query the authoritative permission state; do not trust broadcast extras.
                UsbDevice device=usb.getDeviceList().get(pendingName);
                boolean longer=pendingStable;pendingName=null;
                if(device!=null&&usb.hasPermission(device))runProbe(device,longer);
                else {setBusy(false);setState("USB 授权未完成",false);append("USB 权限被拒绝，或设备已断开。可点击连接重试。");}
            } else if(UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
                UsbDevice removed=deviceExtra(intent);
                if(removed!=null) {
                    append("USB 拔出："+id(removed));
                    if(removed.getDeviceName().equals(pendingName)){pendingName=null;setBusy(false);}
                    if(busy&&removed.getDeviceName().equals(activeName)){cancelled.set(true);setState("设备变化，正在停止检测…",false);}
                }
                refreshDevices();
            } else if(UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action)) {
                UsbDevice added=deviceExtra(intent);if(added!=null)append("USB 接入："+id(added));refreshDevices();
            }
        }
    };
    @SuppressWarnings("deprecation") private static UsbDevice deviceExtra(Intent intent) {
        return Build.VERSION.SDK_INT>=33?intent.getParcelableExtra(UsbManager.EXTRA_DEVICE,UsbDevice.class):intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
    }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);usb=(UsbManager)getSystemService(USB_SERVICE);buildUi();
        IntentFilter filter=new IntentFilter();filter.addAction(USB_PERMISSION);filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        ContextCompat.registerReceiver(this,receiver,filter,ContextCompat.RECEIVER_NOT_EXPORTED);
        report.setText(saved==null?intro():saved.getString("report",intro()));refreshDevices();
    }
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private TextView label(String value,int size) {TextView v=new TextView(this);v.setText(value);v.setTextSize(size);v.setTextColor(Color.rgb(28,45,65));return v;}
    private void buildUi() {
        LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(dp(16),dp(16),dp(16),dp(10));root.setBackgroundColor(Color.rgb(238,242,246));setContentView(root);
        root.addView(label("DeepMirror · USB 连通性",22));
        TextView scope=label("第一阶段：识别、授权、控制读取。尚不采集 RGB、深度或点云。",13);scope.setPadding(0,dp(8),0,dp(8));root.addView(scope);
        state=label("等待检测",16);state.setPadding(dp(12),dp(10),dp(12),dp(10));root.addView(state);
        selector=new Spinner(this);selector.setContentDescription("选择 USB 设备");root.addView(selector,new LinearLayout.LayoutParams(-1,dp(56)));
        LinearLayout actions=new LinearLayout(this);refresh=button("刷新设备",actions);connect=button("连接并检测",actions);root.addView(actions);
        LinearLayout longer=new LinearLayout(this);stable=button("连续检测 30 秒",longer);stop=button("停止检测",longer);stop.setEnabled(false);root.addView(longer);
        LinearLayout export=new LinearLayout(this);Button copy=button("复制诊断报告",export);root.addView(export);
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        report=label("",12);report.setTypeface(Typeface.MONOSPACE);report.setTextIsSelectable(true);report.setPadding(dp(10),dp(10),dp(10),dp(10));report.setBackgroundColor(Color.WHITE);scroll.addView(report);
        refresh.setOnClickListener(v->{if(!busy){append("\n刷新设备列表");refreshDevices();append(inventory());}});
        connect.setOnClickListener(v->requestConnect(false));stable.setOnClickListener(v->requestConnect(true));
        stop.setOnClickListener(v->{cancelled.set(true);stop.setEnabled(false);setState("正在停止，等待当前读取结束…",false);});
        copy.setOnClickListener(v->{((ClipboardManager)getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("DeepMirror USB report",report.getText()));Toast.makeText(this,"诊断报告已复制",Toast.LENGTH_SHORT).show();});
        selector.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener(){
            @Override public void onItemSelected(android.widget.AdapterView<?> parent,View view,int position,long itemId){if(!busy)showSelectedState();}
            @Override public void onNothingSelected(android.widget.AdapterView<?> parent){}
        });
    }
    private Button button(String text,LinearLayout row){Button b=new Button(this);b.setText(text);b.setTextSize(13);b.setAllCaps(false);b.setMinHeight(dp(48));row.addView(b,new LinearLayout.LayoutParams(0,-2,1));return b;}
    private static boolean target(UsbDevice d){return d.getVendorId()==0x3652&&d.getProductId()==0x0104;}
    private static String id(UsbDevice d){return String.format(Locale.ROOT,"VID_%04X / PID_%04X",d.getVendorId(),d.getProductId());}
    private static String name(UsbDevice d){String value=d.getProductName();return value==null?"USB 设备":value;}
    private String intro(){return "DeepMirror Android USB Demo "+BuildConfig.VERSION_NAME+"\n手机："+Build.MANUFACTURER+" "+Build.MODEL+"\nAndroid "+Build.VERSION.RELEASE+" / API "+Build.VERSION.SDK_INT+"\nUSB Host："+getPackageManager().hasSystemFeature(PackageManager.FEATURE_USB_HOST)+"\n目标相机：VID_3652 / PID_0104\n\n连接并检测：打开设备并读取标准设备/配置描述符，探测 XU 的 GET_INFO / GET_LEN。\n连续检测：每秒读取标准设备描述符，持续 30 秒。\n不修改配置、驱动、曝光或深度开关，不抢占视频接口。\n此测试不代表视频带宽、相机满负载供电、标定或点云已通过。\n\n";}
    private void refreshDevices() {
        String selected=selected()==null?null:selected().getDeviceName();devices.clear();devices.addAll(usb.getDeviceList().values());
        devices.sort(Comparator.comparing((UsbDevice d)->!target(d)).thenComparing(UsbDevice::getDeviceName));
        List<String> labels=new ArrayList<>();int chosen=0;
        for(int i=0;i<devices.size();i++){UsbDevice d=devices.get(i);labels.add((target(d)?"目标相机 · ":"")+name(d)+" · "+id(d));if(d.getDeviceName().equals(selected))chosen=i;}
        if(labels.isEmpty())labels.add("未发现 USB 设备");
        ArrayAdapter<String> adapter=new ArrayAdapter<>(this,android.R.layout.simple_spinner_item,labels);adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);selector.setAdapter(adapter);selector.setSelection(chosen);
        if(!busy)showSelectedState();
        connect.setEnabled(!busy&&!devices.isEmpty());stable.setEnabled(!busy&&!devices.isEmpty());
    }
    private UsbDevice selected(){int i=selector==null?-1:selector.getSelectedItemPosition();return i>=0&&i<devices.size()?devices.get(i):null;}
    private void showSelectedState(){if(devices.isEmpty())setState("未发现设备 · 检查 OTG、线缆与供电",false);else setState("已发现 "+devices.size()+" 个 USB 设备 · 等待授权检测",false);}
    private String inventory(){StringBuilder b=new StringBuilder("当前 USB 设备：\n");for(UsbDevice d:devices)b.append(name(d)).append(" · ").append(id(d)).append(" · ").append(d.getDeviceName()).append(" · 权限=").append(usb.hasPermission(d)).append('\n');return b.toString();}
    private void setState(String message,boolean passed){state.setText(message);state.setBackgroundColor(passed?Color.rgb(217,239,225):Color.rgb(222,232,248));}
    private void setBusy(boolean value){busy=value;refresh.setEnabled(!value);selector.setEnabled(!value);connect.setEnabled(!value&&!devices.isEmpty());stable.setEnabled(!value&&!devices.isEmpty());stop.setEnabled(value&&pendingName==null);}
    private void append(String value){String text=report.getText()+value+"\n";report.setText(text.length()>120000?"[较早记录已截断]\n"+text.substring(text.length()-110000):text);}
    private void requestConnect(boolean longer) {
        UsbDevice device=selected();if(device==null||busy)return;
        pendingName=device.getDeviceName();pendingStable=longer;setBusy(true);
        if(hasVideo(device)&&checkSelfPermission(Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED){append("申请相机权限：Android 对 USB 视频设备访问可能要求 CAMERA 权限；Demo 不打开手机内置摄像头。");requestPermissions(new String[]{Manifest.permission.CAMERA},10);return;}
        requestUsb();
    }
    private static boolean hasVideo(UsbDevice device){for(int i=0;i<device.getInterfaceCount();i++)if(device.getInterface(i).getInterfaceClass()==14)return true;return device.getDeviceClass()==14;}
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants) {
        super.onRequestPermissionsResult(request,permissions,grants);
        if(request==10&&pendingName!=null){if(grants.length>0&&grants[0]==PackageManager.PERMISSION_GRANTED)requestUsb();else{pendingName=null;setBusy(false);setState("相机权限未授予",false);append("请允许相机权限后重试 USB 视频设备连接。");}}
    }
    private void requestUsb() {
        UsbDevice device=usb.getDeviceList().get(pendingName);
        if(device==null){pendingName=null;setBusy(false);refreshDevices();return;}
        if(usb.hasPermission(device)){boolean longer=pendingStable;pendingName=null;runProbe(device,longer);return;}
        setState("等待系统 USB 授权…",false);
        Intent callback=new Intent(USB_PERMISSION).setPackage(getPackageName()).putExtra("requestedDevice",device.getDeviceName());
        PendingIntent permission=PendingIntent.getBroadcast(this,device.getDeviceId(),callback,PendingIntent.FLAG_CANCEL_CURRENT|PendingIntent.FLAG_IMMUTABLE);
        try{usb.requestPermission(device,permission);}catch(RuntimeException e){pendingName=null;setBusy(false);setState("申请 USB 权限失败",false);append(e.toString());}
    }
    private void runProbe(UsbDevice device,boolean longer) {
        cancelled.set(false);setBusy(true);stop.setEnabled(true);int token=++generation;
        setState(longer?"连续检测中（30 秒）…":"正在打开设备并读取描述符…",false);
        append("\n===== "+new SimpleDateFormat("yyyy-MM-dd HH:mm:ss",Locale.ROOT).format(new Date())+" =====\n"+inventory());
        worker.execute(()->{
            StringBuilder out=new StringBuilder();boolean passed=false;UsbDeviceConnection connection=null;
            try {
                connection=usb.openDevice(device);
                if(connection==null)throw new IllegalStateException("openDevice 返回 null：检查权限、占用与连接");
                out.append("设备打开成功；原生 FD 有效：").append(connection.getFileDescriptor()>=0).append('\n');
                out.append(describe(device));
                byte[] descriptor=readDevice(connection,device);int version=DescriptorParser.le16(descriptor,2);
                out.append(String.format(Locale.ROOT,"GET_DESCRIPTOR(Device) 通过；bcdUSB=%04x（设备声明版本，不是实际协商速率）\n",version));
                int configurations=descriptor[17]&255;
                if(configurations==0)throw new IllegalStateException("设备报告 0 个配置");
                for(int index=0;index<Math.min(configurations,8)&&!cancelled.get();index++) {
                    byte[] head=new byte[9];int n=connection.controlTransfer(0x80,6,0x0200|index,0,head,9,1500);
                    if(n!=9||head[1]!=2)throw new IllegalStateException("读取配置描述符头失败，返回 "+n);
                    int total=DescriptorParser.le16(head,2);if(total<9)throw new IllegalStateException("配置总长度异常："+total);
                    byte[] bytes=new byte[total];n=connection.controlTransfer(0x80,6,0x0200|index,0,bytes,total,1500);
                    if(n<9)throw new IllegalStateException("读取配置描述符失败，返回 "+n);
                    DescriptorParser.Result parsed=DescriptorParser.parse(Arrays.copyOf(bytes,n),version);out.append(parsed.text);
                    if(!parsed.complete)throw new IllegalStateException("配置描述符校验失败");
                    for(DescriptorParser.Extension xu:parsed.extensions) {
                        for(int selector:new int[]{0x14,0x16})if(selector<=xu.controls&&!cancelled.get()) {
                            byte[] info=new byte[1],len=new byte[2];
                            int a=connection.controlTransfer(0xa1,0x86,selector<<8,(xu.unit<<8)|xu.controlInterface,info,1,500);
                            int b=connection.controlTransfer(0xa1,0x85,selector<<8,(xu.unit<<8)|xu.controlInterface,len,2,500);
                            out.append(String.format(Locale.ROOT,"  XU IF=%d unit=%d selector=0x%02x：GET_INFO=%s；GET_LEN=%s\n",xu.controlInterface,xu.unit,selector,a==1?String.format(Locale.ROOT,"0x%02x",info[0]&255):"失败("+a+")",b==2?String.valueOf(DescriptorParser.le16(len,0)):"失败("+b+")"));
                        }
                    }
                }
                if(configurations>8)out.append("配置数量超过 8，仅检测前 8 个。\n");
                if(longer&&!cancelled.get()) {
                    long until=SystemClock.elapsedRealtime()+30000;int count=0;
                    while(SystemClock.elapsedRealtime()<until&&!cancelled.get()) {
                        readDevice(connection,device);count++;
                        final int done=count;runOnUiThread(()->{if(!destroyed&&token==generation&&!cancelled.get())setState("连续检测：已完成 "+done+" 次读取",false);});
                        Thread.sleep(1000);
                    }
                    out.append("连续读取成功 ").append(count).append(" 次；目标时长 30 秒。\n");
                }
                passed=!cancelled.get();out.append(passed?"\n标准 USB 控制读取通过。XU 的探测结果单独列出，不影响此基础结论。\n":"\n检测已停止，未完成本轮验收。\n");
            } catch(Exception e){out.append("\n检测失败：").append(e).append('\n');}
            finally {if(connection!=null)connection.close();}
            out.append("设备句柄已释放。尚未验证视频传输、标定、点云或手机最大供电能力。\n");
            final boolean ok=passed;final String text=out.toString();
            runOnUiThread(()->{if(destroyed||token!=generation)return;setBusy(false);append(text);boolean attached=usb.getDeviceList().containsKey(device.getDeviceName());setState(ok&&attached?"控制读取通过 · 视频采集尚未验证":(cancelled.get()?"检测已停止 / 设备已断开":"检测失败 · 查看报告"),ok&&attached);});
        });
    }
    private static byte[] readDevice(UsbDeviceConnection connection,UsbDevice expected) {
        byte[] data=new byte[18];int n=connection.controlTransfer(0x80,6,0x0100,0,data,18,1500);
        if(n!=18||data[1]!=1||DescriptorParser.le16(data,8)!=expected.getVendorId()||DescriptorParser.le16(data,10)!=expected.getProductId())throw new IllegalStateException("标准设备描述符读取或身份校验失败，返回 "+n);
        return data;
    }
    private static String describe(UsbDevice device) {
        StringBuilder b=new StringBuilder(name(device)+" · "+id(device)+"\n路径："+device.getDeviceName()+"\n");
        for(int i=0;i<device.getInterfaceCount();i++) {
            UsbInterface f=device.getInterface(i);b.append(String.format(Locale.ROOT,"接口 %d / alt %d：class=%02x sub=%02x protocol=%02x\n",f.getId(),f.getAlternateSetting(),f.getInterfaceClass(),f.getInterfaceSubclass(),f.getInterfaceProtocol()));
            for(int j=0;j<f.getEndpointCount();j++){UsbEndpoint e=f.getEndpoint(j);b.append(String.format(Locale.ROOT,"  端点 %02x %s，%s，maxPacket=%d\n",e.getAddress(),e.getDirection()==UsbConstants.USB_DIR_IN?"IN":"OUT",DescriptorParser.endpointType(e.getType()),e.getMaxPacketSize()));}
        }
        return b.toString();
    }
    @Override protected void onSaveInstanceState(Bundle state){super.onSaveInstanceState(state);state.putString("report",report.getText().toString());}
    @Override protected void onStop(){super.onStop();if(busy&&pendingName==null){cancelled.set(true);stop.setEnabled(false);}}
    @Override protected void onDestroy(){destroyed=true;generation++;cancelled.set(true);worker.shutdownNow();unregisterReceiver(receiver);super.onDestroy();}
}
