package com.deepmirror.usbcheck;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** Pure parser: only describes USB bytes. No device access or configuration changes. */
public final class DescriptorParser {
    public static final class Extension {
        public final int controlInterface, unit, controls;
        public final String guid;
        Extension(int intf, int unit, int controls, String guid) {
            this.controlInterface=intf; this.unit=unit; this.controls=controls; this.guid=guid;
        }
    }
    public static final class Result {
        public final List<Extension> extensions=new ArrayList<>();
        public String text="";
        public boolean complete=true;
    }
    public static int u8(byte[] b,int p) { return b[p]&255; }
    public static int le16(byte[] b,int p) { return u8(b,p)|(u8(b,p+1)<<8); }
    private static long le32(byte[] b,int p) { return (long)le16(b,p)|((long)le16(b,p+2)<<16); }
    public static String guid(byte[] b,int p) {
        return String.format(Locale.ROOT,"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",le32(b,p),le16(b,p+4),le16(b,p+6),u8(b,p+8),u8(b,p+9),u8(b,p+10),u8(b,p+11),u8(b,p+12),u8(b,p+13),u8(b,p+14),u8(b,p+15));
    }
    public static Result parse(byte[] b,int usbVersion) {
        Result result=new Result();StringBuilder out=new StringBuilder();
        if(b==null||b.length<9||u8(b,0)<9||u8(b,1)!=2) {
            result.complete=false;result.text="配置描述符无效或不足 9 字节\n";return result;
        }
        int total=le16(b,2),limit=Math.min(total,b.length),intf=-1,klass=-1,sub=-1;
        String format="未识别格式";
        if(total<9||total>b.length){result.complete=false;out.append("配置描述符长度不完整：声明 ").append(total).append("，实际 ").append(b.length).append('\n');}
        out.append("配置 ").append(u8(b,5)).append("；声明最大总线取电 ").append(u8(b,8)*(usbVersion>=0x300?8:2)).append(" mA（描述符声明，非手机输出能力或实测电流）\n");
        for(int p=0;p<limit;) {
            if(p+2>limit){result.complete=false;out.append("描述符头被截断\n");break;}
            int len=u8(b,p),type=u8(b,p+1);
            if(len<2||p+len>limit){result.complete=false;out.append("描述符长度异常，偏移 ").append(p).append('\n');break;}
            if(type==4&&len>=9) {
                intf=u8(b,p+2);klass=u8(b,p+5);sub=u8(b,p+6);format="未识别格式";
                out.append(String.format(Locale.ROOT,"\n接口 %d / alt %d：class=%02x sub=%02x，端点=%d\n",intf,u8(b,p+3),klass,sub,u8(b,p+4)));
            } else if(type==5&&len>=7) {
                out.append(String.format(Locale.ROOT,"  端点 %02x %s，%s，wMaxPacketSize=0x%04x\n",u8(b,p+2),(u8(b,p+2)&128)!=0?"IN":"OUT",endpointType(u8(b,p+3)&3),le16(b,p+4)));
            } else if(type==0x24&&klass==14&&len>=3) {
                int subtype=u8(b,p+2);
                if(sub==1&&subtype==6) {
                    // VC_EXTENSION_UNIT: fixed prefix, source pins, control size, bitmap, iExtension.
                    if(len>=24) {
                        int pins=u8(b,p+21),controlSizeOffset=22+pins;
                        if(controlSizeOffset<len&&controlSizeOffset+2+u8(b,p+controlSizeOffset)<=len) {
                            Extension xu=new Extension(intf,u8(b,p+3),u8(b,p+20),guid(b,p+4));result.extensions.add(xu);
                            out.append("  XU unit=").append(xu.unit).append("，controls=").append(xu.controls).append("，GUID=").append(xu.guid).append('\n');
                        } else {result.complete=false;out.append("  XU 描述符的引脚或控制位图被截断\n");}
                    } else {result.complete=false;out.append("  XU 描述符过短\n");}
                } else if(sub==2&&subtype==6&&len>=11) {
                    format="MJPG";out.append("  格式 ").append(u8(b,p+3)).append(": MJPG\n");
                } else if(sub==2&&subtype==4&&len>=27) {
                    String raw="";for(int i=0;i<4;i++){int c=u8(b,p+5+i);raw+=(c>=32&&c<=126)?(char)c:'?';}
                    format=raw.trim();out.append("  格式 ").append(u8(b,p+3)).append(": ").append(format).append("，GUID=").append(guid(b,p+5)).append('\n');
                } else if(sub==2&&(subtype==5||subtype==7)&&len>=26) {
                    long interval=le32(b,p+21);String fps=interval>0?String.format(Locale.ROOT,"%.2f",10000000.0/interval):"未知";
                    out.append("    ").append(format).append(' ').append(le16(b,p+5)).append('×').append(le16(b,p+7)).append("，默认 ").append(fps).append(" FPS\n");
                }
            }
            p+=len;
        }
        result.text=out.toString();return result;
    }
    public static String endpointType(int type) { return switch(type){case 0->"Control";case 1->"Isochronous";case 2->"Bulk";case 3->"Interrupt";default->"Unknown";}; }
    private DescriptorParser() {}
}
