#include "lang.h"
#include <string>
//封装格式
//解码
#include "log.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
};
#define INBUF_SIZE 4096


static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
                     char *filename) {
    FILE *f;
    int i;

    f = fopen(filename, "w");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}

static int decode_write_frame(const char *outfilename, AVCodecContext *avctx,
                              AVFrame *frame, int *frame_count, AVPacket *pkt, int last) {
    int len, got_frame;
    char buf[1024];

    len = avcodec_decode_video2(avctx, frame, &got_frame, pkt);
    if (len < 0) {
        LOGE("Error while decoding frame %d\n", *frame_count);
        return len;
    }
    if (got_frame) {
        LOGE("Saving %sframe %3d\n", last ? "last " : "", *frame_count);
        fflush(stdout);

        /* the picture is allocated by the decoder, no need to free it */
        snprintf(buf, sizeof(buf), outfilename, *frame_count);
        pgm_save(frame->data[0], frame->linesize[0],
                 frame->width, frame->height, buf);
        (*frame_count)++;
    }
    if (pkt->data) {
        pkt->size -= len;
        pkt->data += len;
    }
    return 0;
}

static void video_decode_example(const char *outfilename, const char *filename) {

    AVCodec *codec;
    AVCodecContext *c = NULL;
    int frame_count;
    FILE *f;
    AVFrame *frame;
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    AVPacket avpkt;



    //1. 初始化编码数据承载实体
    av_init_packet(&avpkt);
    //将缓冲区的末尾设置为0（这可确保损坏的MPEG流不会发生过度读取）
    /* set end of buffer to 0 (this ensures that no overreading happens for damaged mpeg streams) */
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    LOGE("Decode video file %s to %s\n", filename, outfilename);

    //2.解码器相关
    /* find the mpeg4 video decoder */
    //2.1 获取解码器
    codec = avcodec_find_decoder(AV_CODEC_ID_MPEG1VIDEO);
    if (!codec) {
        LOGE("Codec not found\n");
        return;
    }
    //2.2 根据解码器获取解码上下文
    c = avcodec_alloc_context3(codec);
    if (!c) {
        LOGE("Could not allocate video codec context\n");
        return;
    }
    //TODO  ???
    if (codec->capabilities & AV_CODEC_CAP_TRUNCATED)
        c->flags |= AV_CODEC_FLAG_TRUNCATED; // we do not send complete frames

    //2.3 打开解码器
    if (avcodec_open2(c, codec, NULL) < 0) {
        LOGE("Could not open codec\n");
        return;
    }

    //3. 从文件获取编码数据，并且解码

    //3.1 打开文件
    f = fopen(filename, "rb");
    if (!f) {
        LOGE("Could not open %s\n", filename);
        return;
    }

    //3.2 开辟解码的数据承载实体
    frame = av_frame_alloc();
    if (!frame) {
        LOGE("Could not allocate video frame\n");
        return;
    }
    //3.3  找到video索引位置
    frame_count = 0;
    for (;;) {
        //
        avpkt.size = fread(inbuf, 1, INBUF_SIZE, f);
        if (avpkt.size == 0)
            break;
        avpkt.data = inbuf;
        while (avpkt.size > 0)
            if (decode_write_frame(outfilename, c, frame, &frame_count, &avpkt, 0) < 0)
                return;
    }

    /* some codecs, such as MPEG, transmit the I and P frame with a
      latency of one frame. You must do the following to have a
      chance to get the last frame of the video */
    avpkt.data = NULL;
    avpkt.size = 0;
    decode_write_frame(outfilename, c, frame, &frame_count, &avpkt, 1);

    fclose(f);

    avcodec_close(c);
    av_free(c);
    av_frame_free(&frame);

}

extern "C"
JNIEXPORT jint JNICALL
Java_zzw_com_ffmpegdemo_VideoUtils_fix(JNIEnv *env, jclass type, jstring path_) {
    const char *path = env->GetStringUTFChars(path_, 0);

    char *info = (char *) malloc(40000);
    memset(info, 0, 40000);

    av_register_all();

    AVCodec *c_temp = av_codec_next(NULL);

    while (c_temp != NULL) {
        if (c_temp->decode != NULL) {
            strcat(info, "[Decode]");
        } else {
            strcat(info, "[Encode]");
        }
        switch (c_temp->type) {
            case AVMEDIA_TYPE_VIDEO:
                strcat(info, "[Video]");
                break;
            case AVMEDIA_TYPE_AUDIO:
                strcat(info, "[Audeo]");
                break;
            default:
                strcat(info, "[Other]");
                break;
        }
        LOGE(info, "%s %10s\n", info, c_temp->name);
        c_temp = c_temp->next;
    }
    puts(info);
    free(info);


    env->ReleaseStringUTFChars(path_, path);

    return 1;
}
extern "C"
JNIEXPORT jint JNICALL
Java_zzw_com_ffmpegdemo_VideoUtils_filter(JNIEnv *env, jclass type, jstring input_,
                                          jstring output_) {
    const char *input = env->GetStringUTFChars(input_, 0);
    const char *output = env->GetStringUTFChars(output_, 0);


    env->ReleaseStringUTFChars(input_, input);
    env->ReleaseStringUTFChars(output_, output);
    return 1;
}
extern "C"
JNIEXPORT jint JNICALL
Java_zzw_com_ffmpegdemo_VideoUtils_decode(JNIEnv *env, jclass type, jstring input_,
                                          jstring output_) {
    const char *input_file_name = env->GetStringUTFChars(input_, 0);
    const char *output_file_name = env->GetStringUTFChars(output_, 0);
    video_decode_example(output_file_name, input_file_name);

    env->ReleaseStringUTFChars(input_, input_file_name);
    env->ReleaseStringUTFChars(output_, output_file_name);
    return 0;

}
extern "C"
JNIEXPORT jint JNICALL
Java_zzw_com_ffmpegdemo_VideoUtils_encode(JNIEnv *env, jclass type, jstring input_,
                                          jstring output_) {
    const char *input = env->GetStringUTFChars(input_, 0);
    const char *output = env->GetStringUTFChars(output_, 0);


    env->ReleaseStringUTFChars(input_, input);
    env->ReleaseStringUTFChars(output_, output);
    return 1;
}


