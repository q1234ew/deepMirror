package com.deepmirror.usbcheck;
import java.io.ByteArrayOutputStream;
import java.util.Arrays;
public final class DescriptorParserTest {
    static void check(boolean value,String message){if(!value)throw new AssertionError(message);}
    static byte[] config(byte[]... descriptors){
        ByteArrayOutputStream out=new ByteArrayOutputStream();out.writeBytes(new byte[]{9,2,0,0,2,1,0,(byte)128,50});
        for(byte[] d:descriptors)out.writeBytes(d);byte[] result=out.toByteArray();result[2]=(byte)result.length;result[3]=(byte)(result.length>>8);return result;
    }
    public static void main(String[] args){
        byte[] control={9,4,2,0,0,14,1,0,0};
        byte[] xu=new byte[28];xu[0]=28;xu[1]=0x24;xu[2]=6;xu[3]=3;
        for(int i=0;i<16;i++)xu[4+i]=(byte)(i+1);
        xu[20]=24;xu[21]=1;xu[22]=2;xu[23]=3;xu[24]=(byte)255;xu[25]=(byte)255;xu[26]=(byte)255;
        byte[] streaming={9,4,3,0,1,14,2,0,0};
        byte[] mjpg={11,0x24,6,1,1,0,1,0,0,0,0};
        byte[] frame=new byte[30];frame[0]=30;frame[1]=0x24;frame[2]=7;frame[3]=1;
        frame[5]=0x40;frame[6]=4;frame[7]=(byte)0x80;frame[8]=7;
        int interval=333333;for(int i=0;i<4;i++)frame[21+i]=(byte)(interval>>(8*i));frame[25]=1;
        byte[] endpoint={7,5,(byte)0x81,1,0,4,1};
        byte[] full=config(control,xu,streaming,mjpg,frame,endpoint);
        DescriptorParser.Result r=DescriptorParser.parse(full,0x300);
        check(r.complete,"valid configuration");check(r.extensions.size()==1,"XU discovery");
        DescriptorParser.Extension e=r.extensions.get(0);check(e.controlInterface==2&&e.unit==3&&e.controls==24,"XU addressing");
        check(e.guid.equals("04030201-0605-0807-090a-0b0c0d0e0f10"),"GUID byte order");
        check(r.text.contains("1088×1920")&&r.text.contains("30.00 FPS")&&r.text.contains("Isochronous"),"native format and endpoint");
        check(r.text.contains("400 mA"),"USB 3 power units");check(DescriptorParser.parse(full,0x200).text.contains("100 mA"),"USB 2 power units");
        check(!DescriptorParser.parse(Arrays.copyOf(full,full.length-1),0x300).complete,"truncated transfer");
        byte[] zero=config(new byte[]{0,4});check(!DescriptorParser.parse(zero,0x300).complete,"zero length terminates");
        byte[] shortXu=xu.clone();shortXu[21]=100;check(!DescriptorParser.parse(config(control,shortXu),0x300).complete,"pin overrun rejected");
        shortXu=xu.clone();shortXu[23]=100;check(!DescriptorParser.parse(config(control,shortXu),0x300).complete,"bitmap overrun rejected");
        check(!DescriptorParser.parse(new byte[]{9,2},0x300).complete,"short header");check(!DescriptorParser.parse(null,0x300).complete,"null input");
        check(DescriptorParser.parse(config(new byte[]{2,99}),0x300).complete,"unknown descriptors can be skipped");
        byte[] ignored=xu.clone();check(DescriptorParser.parse(config(streaming,ignored),0x300).extensions.isEmpty(),"XU only on VideoControl interface");
        System.out.println("PASS: UVC descriptor bounds, XU identity/address, frame dimensions, endpoint and power units");
    }
}
