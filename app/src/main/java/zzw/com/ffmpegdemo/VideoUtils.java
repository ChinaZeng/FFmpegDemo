package zzw.com.ffmpegdemo;

public class VideoUtils {

    static {
        System.loadLibrary("avutil-55");
        System.loadLibrary("swscale-4");
        System.loadLibrary("swresample-2");
        System.loadLibrary("avcodec-57");
        System.loadLibrary("avformat-57");
        System.loadLibrary("avfilter-6");
        //自己实现逻辑编译的库
        System.loadLibrary("native-lib");
    }

    public native static int fix(String path);

    public native static int filter(String input, String output);

    public native static int decode(String input, String output);

    public native static int encode(String input, String output);



    public native static int audio_decode(String input, String output);
    public native static int audio_encode(String input, String output);


}
