#include "lang.h"
#include <string>
//封装格式
//解码
#include "log.h"
extern "C"{
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
};



static int video_stream_idx = -1, audio_stream_idx = -1;
static int refcount = 0;
static AVStream *video_stream = NULL, *audio_stream = NULL;
static FILE *video_dst_file = NULL, *audio_dst_file = NULL;
static int width, height;

int ret = 0, got_frame;
AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL, *audio_dec_ctx;
static enum AVPixelFormat pix_fmt;
static uint8_t *video_dst_data[4] = {NULL};
static int video_dst_linesize[4];
static int video_dst_bufsize;
static AVFrame *frame = NULL;
static AVPacket pkt;
static int audio_frame_count = 0, video_frame_count = 0;

/*
 * 1.找到type（音视频，字幕等）对应的AVStream索引位置
 * 2.找到解码器
 * 3.初始化解码器
 * 4.打开解码器
 */
static int open_codec_context(int *stream_idx,
                              AVFormatContext *fmt_ctx, enum AVMediaType type) {
    int ret, stream_index;
    AVStream *st;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    //找到type（音视频，字幕等）对应的AVStream索引位置
    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        LOGE("Could not find %s stream ", av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        return ret;
    }

    stream_index = ret;
    st = fmt_ctx->streams[stream_index];

    //找到该流的解码器上下文
    dec_ctx = st->codec;
    //通过解码器上下文id来获取解码器
    dec = avcodec_find_decoder(dec_ctx->codec_id);
    if (!dec) {
        LOGE("Failed to find %s codec",
             av_get_media_type_string(type));
        return AVERROR(EINVAL);
    }

    //初始化解码器，有或没有参考计数
    av_dict_set(&opts, "refcounted_frames", refcount ? "1" : "0", 0);
    //打开解码器
   if ((ret = avcodec_open2(dec_ctx, dec, &opts)) < 0) {
        LOGE("Failed to open %s code", av_get_media_type_string(type));
        return ret;
    }
    *stream_idx = stream_index;

    return 0;
}



static int decode_packet(int *got_frame, int cached) {
    int ret = 0;
    int decoded = pkt.size;

    *got_frame = 0;

    if (pkt.stream_index == video_stream_idx) {
        /* decode video frame */
        ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, &pkt);
        if (ret < 0) {
            LOGE("Error decoding video frame (%s)\n", av_err2str(ret));
            return ret;
        }

        if (*got_frame) {
            if (frame->width != width || frame->height != height ||
                frame->format != pix_fmt) {
                /* To handle this change, one could call av_image_alloc again and
                 * decode the following frames into another rawvideo file. */
                LOGE("Error: Width, height and pixel format have to be "
                                "constant in a rawvideo file, but the width, height or "
                                "pixel format of the input video changed:\n"
                                "old: width = %d, height = %d, format = %s\n"
                                "new: width = %d, height = %d, format = %s\n",
                        width, height, av_get_pix_fmt_name(pix_fmt),
                        frame->width, frame->height,
                        av_get_pix_fmt_name((AVPixelFormat) frame->format));
                return -1;
            }

            printf("video_frame%s n:%d coded_n:%d\n",
                   cached ? "(cached)" : "",
                   video_frame_count++, frame->coded_picture_number);

            /* copy decoded frame to destination buffer:
             * this is required since rawvideo expects non aligned data */
            av_image_copy(video_dst_data, video_dst_linesize,
                          (const uint8_t **) (frame->data), frame->linesize,
                          pix_fmt, width, height);

            /* write to rawvideo file */
            fwrite(video_dst_data[0], 1, video_dst_bufsize, video_dst_file);
        }
    } else if (pkt.stream_index == audio_stream_idx) {
        /* decode audio frame */
        ret = avcodec_decode_audio4(audio_dec_ctx, frame, got_frame, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding audio frame (%s)\n", av_err2str(ret));
            return ret;
        }
        /* Some audio decoders decode only part of the packet, and have to be
         * called again with the remainder of the packet data.
         * Sample: fate-suite/lossless-audio/luckynight-partial.shn
         * Also, some decoders might over-read the packet. */
        decoded = FFMIN(ret, pkt.size);

        if (*got_frame) {
            size_t unpadded_linesize =
                    frame->nb_samples * av_get_bytes_per_sample((AVSampleFormat) frame->format);
            LOGE("audio_frame%s n:%d nb_samples:%d pts:%s\n",
                   cached ? "(cached)" : "",
                   audio_frame_count++, frame->nb_samples,
                   av_ts2timestr(frame->pts, &audio_dec_ctx->time_base));

            /* Write the raw audio data samples of the first plane. This works
             * fine for packed formats (e.g. AV_SAMPLE_FMT_S16). However,
             * most audio decoders output planar audio, which uses a separate
             * plane of audio samples for each channel (e.g. AV_SAMPLE_FMT_S16P).
             * In other words, this code will write only the first audio channel
             * in these cases.
             * You should use libswresample or libavfilter to convert the frame
             * to packed data. */
            fwrite(frame->extended_data[0], 1, unpadded_linesize, audio_dst_file);
        }
    }

    /* If we use frame reference counting, we own the data and need
     * to de-reference it when we don't use it anymore */
    if (*got_frame && refcount)
        av_frame_unref(frame);

    return decoded;
}

extern "C"
JNIEXPORT jint JNICALL
Java_zzw_com_ffmpegdemo_VideoUtils_fix(JNIEnv *env, jclass type, jstring path_) {
    const char *path = env->GetStringUTFChars(path_, 0);


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




    //1.注册所有格式和编解码器
    av_register_all();

    //2.打开这个文件，并且分配编码上下文
    if (avformat_open_input(&fmt_ctx, input_file_name, NULL, NULL) < 0) {
        LOGE("Could not open source file %s", input_file_name);
        return -1;
    }

    //3.获取视频信息
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        LOGE("Could not find stream information");
        return -1;
    }
    //4. 视频解码
    //4.1 根据type获取AVStream索引位置，找到解码器并将初始化，打开解码器
    if (open_codec_context(&video_stream_idx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
        //4.2 根据索引位置读取数据
        video_stream = fmt_ctx->streams[video_stream_idx];
        video_dec_ctx = video_stream->codec;

        video_dst_file = fopen(output_file_name, "wb");
        if (!video_dst_file) {
            LOGE( "Could not open destination file %s\n", output_file_name);
            ret = 1;
            goto end;
        }
        LOGE( "video_stream_idx %d\n", video_stream_idx);
        //4.3 收集信息
        /* allocate image where the decoded image will be put */
        width = video_dec_ctx->width;
        height = video_dec_ctx->height;
        pix_fmt = video_dec_ctx->pix_fmt;
        ret = av_image_alloc(video_dst_data, video_dst_linesize,
                             width, height, pix_fmt, 1);
        if (ret < 0) {
            LOGE("Could not allocate raw video buffer\n");
            goto end;
        }
        video_dst_bufsize = ret;
    }

    //打印format信息
    av_dump_format(fmt_ctx, 0, input_file_name, 0);
    if (video_stream) {
        LOGE("Could not find  video stream in the input, aborting");
        ret = 1;
        goto end;
    }
    //解码数据 frame
    frame = av_frame_alloc();
    if (!frame) {
        LOGE("Could not allocate frame");
        ret = AVERROR(ENOMEM);
        goto end;
    }
    //编码数据 pkt
    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    if (video_stream)
        LOGE("Demuxing video from file '%s' into '%s'\n", input_file_name, output_file_name);


    /* read frames from the file */
    while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        AVPacket orig_pkt = pkt;
        do {
            ret = decode_packet(&got_frame, 0);
            if (ret < 0)
                break;
            pkt.data += ret;
            pkt.size -= ret;
        } while (pkt.size > 0);
        av_packet_unref(&orig_pkt);
    }
    /* flush cached frames */
    pkt.data = NULL;
    pkt.size = 0;
    do {
        decode_packet(&got_frame, 1);
    } while (got_frame);

    LOGE("Demuxing succeeded.\n");

    if (video_stream) {
        LOGE("Play the output video file with the command:\n"
                     "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
             av_get_pix_fmt_name(pix_fmt), width, height,
             output_file_name);
    }

    //5. 音频解码
//    if (open_codec_context(&audio_stream_idx, fmt_ctx, AVMEDIA_TYPE_AUDIO) >= 0) {
//        audio_stream = fmt_ctx->streams[audio_stream_idx];
//        audio_dec_ctx = audio_stream->codec;
//        audio_dst_file = fopen(audio_dst_filename, "wb");
//        if (!audio_dst_file) {
//            LOGE(stderr, "Could not open destination file %s\n", audio_dst_filename);
//            ret = 1;
//            goto end;
//        }
//    }


    end:
    avcodec_close(video_dec_ctx);
//    avcodec_close(audio_dec_ctx);
    avformat_close_input(&fmt_ctx);
    if (video_dst_file)
        fclose(video_dst_file);
//    if (audio_dst_file)
//        fclose(audio_dst_file);
    av_frame_free(&frame);
    av_free(video_dst_data[0]);
    env->ReleaseStringUTFChars(input_, input_file_name);
    env->ReleaseStringUTFChars(output_, output_file_name);
    return ret < 0;

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


