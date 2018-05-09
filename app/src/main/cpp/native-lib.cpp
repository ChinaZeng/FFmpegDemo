#include "lang.h"
#include <string>
#include "log.h"

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

};


static void video_decode_example(const char *outfilename, const char *filename) {
    //1.注册
    av_register_all();

    AVFormatContext *pFormatCtx = NULL;
    //2. 打开文件，从文件头获取格式信息，数据封装在AVFormatContext里面
    if (avformat_open_input(&pFormatCtx, filename, NULL, NULL) != 0) {
        LOGE ("从文件头获取格式信息失败");
        return;
    }
    //3. 获取流信息，数据封装在AVFormatContext里面
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE ("获取流信息失败");
        return;
    }
    //只输出输入文件的格式信息
    av_dump_format(pFormatCtx, 0, filename, 0);
    int video_index = -1;
    //4. 从流中遍历获取video的index
    for (int i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = i;
            LOGE ("video_index = %d", video_index);
            break;
        }
    }
    if (video_index == -1) {
        LOGE ("遍历获取video_index失败");
        return;
    }
    AVCodecContext *pCodecCtxOrg = NULL;
    AVCodecContext *pCodecCtx = NULL;

    AVCodec *pCodec = NULL;
    //5. 解码器获取
    //5.1 根据video_index获取解码器上下文AVCodecContext
    pCodecCtxOrg = pFormatCtx->streams[video_index]->codec; // codec context
    //5.1 根据AVCodecContext获取解码器
    pCodec = avcodec_find_decoder(pCodecCtxOrg->codec_id);

    if (!pCodec) {
        LOGE ("解码器获取失败");
        return;
    }

    //6.获取一个AVCodecContext实例，并将第五步获取的AVCodecContext数据copy过来，解码的时候需要用这个
    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (avcodec_copy_context(pCodecCtx, pCodecCtxOrg) != 0) {
        LOGE ("解码器上下文数据copy失败");
        return;
    }
    //7. 打开解码器
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        LOGE ("打开解码器失败");
        return;
    }
    //原始数据帧
    AVFrame *pFrame = NULL;
    //yuv数据帧
    AVFrame *pFrameYUV = NULL;
    //内存开辟 不要忘记free
    pFrame = av_frame_alloc();
    pFrameYUV = av_frame_alloc();
    int numBytes = 0;
    uint8_t *buffer = NULL;
    //根据需要解码的类型，获取需要的buffer，不要忘记free
    numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);
    buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));

    //根据指定的图像参数和提供的数组设置数据指针和行数 ，数据填充到对应的pFrameYUV里面
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, buffer, AV_PIX_FMT_YUV420P,
                         pCodecCtx->width,
                         pCodecCtx->height, 1);

    //获取SwsContext
    struct SwsContext *sws_ctx = NULL;
    sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
                             pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC,
                             NULL, NULL, NULL);

    FILE *pFile = fopen(outfilename, "wb+");
    int ret;
    AVPacket packet;
    int frameFinished = 0;
    //8. 根据AVFormatContext 读取帧数据，读取的编码数据存储到AVPacket里面
    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        if (packet.stream_index == video_index) {
            //9. 将读取到的AVPacket，转换为AVFrame
            ret = avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
            if (ret < 0) {
                LOGE("解码失败");
                return;
            }
            if (frameFinished) {
                //10. 将原始的AVFrame数据转换为自己需要的YUV AVFrame数据
                sws_scale(sws_ctx, (uint8_t const *const *) pFrame->data, pFrame->linesize, 0,
                          pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
                //11. 根据YUV AVFrame数据保存文件
                if (pFile == NULL)
                    return;
                int y_size = pCodecCtx->width * pCodecCtx->height;

                fwrite(pFrame->data[0], 1, static_cast<size_t>(y_size), pFile); //y
                fwrite(pFrame->data[1], 1, static_cast<size_t>(y_size / 4), pFile);//u
                fwrite(pFrame->data[2], 1, static_cast<size_t>(y_size / 4), pFile);//v
            }
        }
        av_packet_unref(&packet);
    }

    //flush decoder
    //FIX: Flush Frames remained in Codec
    //12. 刷新解码器
    while (1) {
        ret = avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
        if (ret < 0)
            break;
        if (!frameFinished)
            break;
        sws_scale(sws_ctx, (const unsigned char *const *) pFrame->data, pFrame->linesize, 0,
                  pCodecCtx->height,
                  pFrameYUV->data, pFrameYUV->linesize);

        int y_size = pCodecCtx->width * pCodecCtx->height;
        fwrite(pFrameYUV->data[0], 1, static_cast<size_t>(y_size), pFile);    //Y
        fwrite(pFrameYUV->data[1], 1, static_cast<size_t>(y_size / 4), pFile);  //U
        fwrite(pFrameYUV->data[2], 1, static_cast<size_t>(y_size / 4), pFile);  //V
        LOGE("Flush Decoder: Succeed to decode 1 frame!\n");
    }
    //release resource
    sws_freeContext(sws_ctx);
    fclose(pFile);
    av_free(buffer);
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avcodec_close(pCodecCtxOrg);
    avformat_close_input(&pFormatCtx);

}


int audio_flush_encoder(AVFormatContext *fmt_ctx, unsigned int stream_index){
    int ret;
    int got_frame;
    AVPacket enc_pkt;
    if (!(fmt_ctx->streams[stream_index]->codec->codec->capabilities &
          CODEC_CAP_DELAY))
        return 0;
    while (1) {
        enc_pkt.data = NULL;
        enc_pkt.size = 0;
        av_init_packet(&enc_pkt);
        ret = avcodec_encode_audio2 (fmt_ctx->streams[stream_index]->codec, &enc_pkt,
                                     NULL, &got_frame);
        av_frame_free(NULL);
        if (ret < 0)
            break;
        if (!got_frame){
            ret=0;
            break;
        }
        LOGE("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n",enc_pkt.size);
        /* mux encoded frame */
        ret = av_write_frame(fmt_ctx, &enc_pkt);
        if (ret < 0)
            break;
    }
    return ret;
}

int video_flush_encoder(AVFormatContext *fmt_ctx, unsigned int stream_index) {
    int ret;
    int got_frame;
    AVPacket enc_pkt;
    if (!(fmt_ctx->streams[stream_index]->codec->codec->capabilities &
          CODEC_CAP_DELAY))
        return 0;
    while (1) {
        enc_pkt.data = NULL;
        enc_pkt.size = 0;
        av_init_packet(&enc_pkt);
        ret = avcodec_encode_video2(fmt_ctx->streams[stream_index]->codec, &enc_pkt,
                                    NULL, &got_frame);
        av_frame_free(NULL);
        if (ret < 0)
            break;
        if (!got_frame) {
            ret = 0;
            break;
        }
        printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);
        /* mux encoded frame */
        ret = av_write_frame(fmt_ctx, &enc_pkt);
        if (ret < 0)
            break;
    }
    return ret;
}


static void video_encode_example(const char *yuvFilePath, const char *h264FilePath) {
    AVFormatContext *pFormatCtx;
    AVOutputFormat *poutFmt;
    AVStream *video_st;
    AVCodecContext *pCodecCtx;
    AVCodec *pCodec;
    AVFrame *pFrame;
    AVPacket pkt;

    int framecnt = 0;
    const int framenum = 100;

    //1. 注册所有编解码器
    av_register_all();

    //2. AVFormatContext获取
    pFormatCtx = avformat_alloc_context();

    //3. AVOutputFormat 获取
//    poutFmt = av_guess_format(NULL, h264FilePath, NULL);
//    pFormatCtx->oformat = poutFmt;

    avformat_alloc_output_context2(&pFormatCtx, NULL, NULL, h264FilePath);
    poutFmt = pFormatCtx->oformat;

    //4. 打开outFile
    if (avio_open(&pFormatCtx->pb, h264FilePath, AVIO_FLAG_READ_WRITE) < 0) {
        LOGE("打开文件失败! %s", h264FilePath);
        return;
    }

    //5. 获取到流
    video_st = avformat_new_stream(pFormatCtx, 0);
    if (!video_st) {
        return;
    }

    //6. 根据流获取到AVCodecContext,并设置下相关参数
    pCodecCtx = video_st->codec;
    pCodecCtx->codec_id = poutFmt->video_codec;
//    pCodecCtx->codec_id = AV_CODEC_ID_H264;
    pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    pCodecCtx->width = 640;
    pCodecCtx->height = 320;
    pCodecCtx->bit_rate = 400000;
    pCodecCtx->gop_size = 250;

    pCodecCtx->time_base.num = 1;
    pCodecCtx->time_base.den = 25;


    pCodecCtx->qmin = 10;
    pCodecCtx->qmax = 51;

    pCodecCtx->max_b_frames = 3;

    //7. 设置选项
    AVDictionary *param = 0;
    //H.264
    if (pCodecCtx->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&param, "preset", "slow", 0);
        av_dict_set(&param, "tune", "zerolatency", 0);
    }
    //H.265
    if (pCodecCtx->codec_id == AV_CODEC_ID_H265) {
        av_dict_set(&param, "preset", "ultrafast", 0);
        av_dict_set(&param, "tune", "zero-latency", 0);
    }

    //Show some Information
//    av_dump_format(pFormatCtx, 0, h264FilePath, 1);

    //8. 找到编码器
    pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
    if (!pCodec) {
        LOGE("未找到编码器");
        return;
    }
    //9. 打开编码器
    if (avcodec_open2(pCodecCtx, pCodec, &param) < 0) {
        LOGE("打开编码器失败");
        return;
    }

    //10.写入数据
    //10.1. 准备pFrame
    pFrame = av_frame_alloc();
    uint8_t *buffer = NULL;


    int numBytes = av_image_get_buffer_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
                                            1);
    buffer = (uint8_t *) av_malloc(numBytes);

    //根据指定的图像参数和提供的数组设置数据指针和行数 ，数据填充到对应的pFrame里面
    av_image_fill_arrays(pFrame->data, pFrame->linesize, buffer, pCodecCtx->pix_fmt,
                         pCodecCtx->width,
                         pCodecCtx->height, 1);
    //10.2 写入header
    avformat_write_header(pFormatCtx, NULL);
    //10.3 初始化packet
    av_new_packet(&pkt, numBytes);

    int y_size = pCodecCtx->width * pCodecCtx->height;
    //10.4 写
    int i = 0;
    FILE *in_file = fopen(yuvFilePath, "rb");
    for (; i < framenum; i++) {
        if (fread(buffer, 1, y_size * 3 / 2, in_file) <= 0) {
            LOGE("读取文件raw data%s失败", yuvFilePath);
            return;
        } else if (feof(in_file)) {
            break;
        }

        pFrame->data[0] = buffer;//Y
        pFrame->data[1] = buffer + y_size;//U
        pFrame->data[2] = buffer + y_size * 5 / 4;//V

        //PTS
        pFrame->pts = i * (video_st->time_base.den) / ((video_st->time_base.num) * 25);
        int got_picture = 0;
        if (avcodec_encode_video2(pCodecCtx, &pkt, pFrame, &got_picture) < 0) {
            LOGE("编码失败");
            return;
        }
        if (got_picture == 1) {
            LOGE("Succeed to encode frame: %5d\tsize:%5d\n", framecnt, pkt.size);
            framecnt++;
            pkt.stream_index = video_st->index;
            av_write_frame(pFormatCtx, &pkt);
            av_packet_unref(&pkt);
        }
    }
    //flush encoder
    if (video_flush_encoder(pFormatCtx, 0) < 0) {
        LOGE("刷新编码器失败");
        return;
    }

    //Write file trailer
    av_write_trailer(pFormatCtx);

    //Clean
    avcodec_close(video_st->codec);
    av_free(pFrame);
    av_free(buffer);
    avio_close(pFormatCtx->pb);
    avformat_free_context(pFormatCtx);

    fclose(in_file);

}



#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio
static int audio_decode_example(const char *input, const char *output) {
    AVCodec *pCodec;
    AVCodecContext *pCodecContext;
    AVFormatContext *pFormatContext;
    struct SwrContext *au_convert_ctx;


    uint8_t *out_buffer;

    //1. 注册
    av_register_all();
    //2.打开解码器 <-- 拿到解码器  <-- 拿到id <-- 拿到stream和拿到AVCodecContext <-- 拿到AVFormatContext

    //2.1 拿到AVFormatContext
    pFormatContext = avformat_alloc_context();
    //2.1.1 拿到文件封装格式数据
    if (avformat_open_input(&pFormatContext, input, NULL, NULL) != 0) {
        LOGE("AVFormatContext获取封装格式信息失败!");
        return -1;
    }
    //2.2 拿到AVCodecContext
    //2.2.1 拿到流信息
    if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
        LOGE("AVFormatContext获取流信息失败!");
        return -1;
    }
    //打印信息
//    av_dump_format(pFormatContext, 0, input, false);

    //2.2.2 通过streams找到audio的索引下标 也就获取到了stream
    int audioStream = -1;
    int i = 0;
    for (; i < pFormatContext->nb_streams; i++)
        if (pFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStream = i;
            break;
        }

    if (audioStream == -1) {
        LOGE("AVMEDIA_TYPE_AUDIO索引没找到!");
        return -1;
    }
    //2.2.3 获取到AVCodecContext
    pCodecContext = pFormatContext->streams[audioStream]->codec;

    //2.2.4 通过AVCodecContext拿到id ，拿到解码器
    pCodec = avcodec_find_decoder(pCodecContext->codec_id);
    if (pCodec == NULL) {
        LOGE("AVCodec获取失败!");
        return -1;
    }
    //2.2.5 打开解码器
    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) {
        LOGE("打开解码器失败!");
        return -1;
    }

    //3. 解码  将解码数据封装在AVFrame <-- 拿到编码的数据AVPacket  <-- 读取数据源 <-- 解码文件参数设置

    //3.1 AVPacket初始化
    AVPacket *packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    av_init_packet(packet);

    //3.2 解码文件参数设置
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;

    //nb_samples: AAC-1024 MP3-1152
    //音频帧中每个声道的采样数
    int out_nb_samples = pCodecContext->frame_size;

    //音频采样格式 量化精度
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    //采样率
    int out_sample_rate = 44100;
    //声道
    int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);

    //获取到 缓冲大小
    int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples,
                                                     out_sample_fmt, 1);
    out_buffer = (uint8_t *) av_malloc(MAX_AUDIO_FRAME_SIZE * 2);

    //3.3 初始化AVFrame
    AVFrame *pFrame = av_frame_alloc();


    //3.4 获取到编码文件的参数信息
    //声道
    int64_t in_channel_layout = av_get_default_channel_layout(pCodecContext->channels);

    //3.5 参数设置
    au_convert_ctx = swr_alloc();
    au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt,
                                        out_sample_rate,
                                        in_channel_layout, pCodecContext->sample_fmt,
                                        pCodecContext->sample_rate, 0, NULL);
    swr_init(au_convert_ctx);

    //4. 读取编码数据到AVPacket 然后将数据解码存储到AVFrame  转换存储数据
    //4.1 读取编码数据到AVPacket
    int got_picture;
    int index = 0;
    FILE *outputFile = fopen(output, "wb");
    while (av_read_frame(pFormatContext, packet) >= 0) {
        if (packet->stream_index == audioStream) {
            //4.2 将数据解码存储到AVFrame
            if (avcodec_decode_audio4(pCodecContext, pFrame, &got_picture, packet) < 0) {
                LOGE("解码失败");
                return -1;
            }

            if (got_picture > 0) {
                //4.3 转换音频数据
                swr_convert(au_convert_ctx, &out_buffer, MAX_AUDIO_FRAME_SIZE,
                            (const uint8_t **) pFrame->data, pFrame->nb_samples);
                LOGE("index:%5d\t pts:%lld\t packet size:%d\n", index, packet->pts, packet->size);
                //4.4 存储数据
                fwrite(out_buffer, 1, static_cast<size_t>(out_buffer_size), outputFile);
                index++;
            }
        }
        //5. 释放相关资源
        av_packet_unref(packet);
    }

    swr_free(&au_convert_ctx);
    fclose(outputFile);
    av_free(out_buffer);
    // Close the codec
    avcodec_close(pCodecContext);
    // Close the video file
    avformat_close_input(&pFormatContext);

    return 0;
}


int audio_encode_example(const char *input, const char *output) {

    AVFormatContext *pFormatContext;
    AVOutputFormat *pOutputFormat;
    AVCodecContext *pCodecContext;
    AVStream   *pStream;
    AVCodec* pCodec;
    FILE* inputFile=NULL;

    int framenum=1000;

    //1. 注册
    av_register_all();

    /**
     * 2.打开编码器<--获取编码器 <--AVCodec获取 <--AVCodecContext获取 <-- AVStream获取 <-- AVFormatContext获取
     *  获取AVCodec需要id <-- 通过AVOutputFormat获取 <-- 通过AVFormatContext获取
     */

    //2.1 获取AVFormatContext
    pFormatContext = avformat_alloc_context();
    //2.2 获取AVOutputFormat
//    pOutputFormat = av_guess_format(NULL, output, NULL);
//    pFormatContext->oformat = oformat;

    avformat_alloc_output_context2(&pFormatContext,NULL,NULL,output);
    pOutputFormat = pFormatContext->oformat;

    //2.3 打开文件
    if(avio_open(&pFormatContext->pb,output,AVIO_FLAG_READ_WRITE)<0){
        LOGE("打开文件失败%s",output);
        return -1;
    }

    //2.4 获取到流
    pStream = avformat_new_stream(pFormatContext,0);
    if(!pStream){
        LOGE("获取流失败");
        return -1;
    }

    //2.5 获取AVCodecContext
    pCodecContext = pStream->codec;

    //2.6 设置参数
    pCodecContext -> codec_id = pOutputFormat->video_codec;
    //类型
    pCodecContext -> codec_type = AVMEDIA_TYPE_AUDIO;

    //音频采样格式 量化精度
    pCodecContext -> sample_fmt = AV_SAMPLE_FMT_S16;
    //采样率
    pCodecContext->sample_rate= 44100;

    pCodecContext->channel_layout=AV_CH_LAYOUT_STEREO;
    // 声道
    pCodecContext->channels = av_get_channel_layout_nb_channels(pCodecContext->channel_layout);
    //平均比特率
    pCodecContext->bit_rate = 64000;


    //打印信息
//    av_dump_format(pFormatContext, 0, output, 1);

    //2.7 获取编码器
    pCodec = avcodec_find_encoder(pCodecContext->codec_id);
    if(!pCodec){
        LOGE("编码器未找到!");
        return -1;
    }

    //2.8 打开编码器
    if(avcodec_open2(pCodecContext,pCodec,NULL)<0){
        LOGE("打开编码器失败");
        return -1;
    }

    //3. 编码写入数据  获取解码数据存储在AVFrame里面  --> 编码为Packet数据  --> 将Packet数据写入AVFormatContext --> 写入文件

    //3.1 编码准备
    //3.1.1 AVFrame初始化
    AVFrame* pFrame= av_frame_alloc();
    pFrame->nb_samples=pCodecContext->frame_size;
    pFrame ->format = pCodecContext->sample_fmt;
    //3.1.2 计算缓冲大小
    int size =av_samples_get_buffer_size(NULL,pCodecContext->channels,pCodecContext->frame_size,pCodecContext->sample_fmt,1);
    uint8_t *frame_buffer = static_cast<uint8_t *>(malloc(static_cast<size_t>(size)));
    //3.1.3 填充AVFrame
    avcodec_fill_audio_frame(pFrame, pCodecContext->channels, pCodecContext->sample_fmt,(const uint8_t*)frame_buffer, size, 1);

    //3.2 编码数据
    //3.2.1 写入头部信息
    avformat_write_header(pFormatContext,NULL);

    //3.2.2 初始化Packet
    AVPacket  pkt;
    av_new_packet(&pkt,size);
    inputFile  = fopen(input,"rb");
    int i=0;
    int got_frame=0;
    int ret;

    for(; i<framenum; i++){
        //3.2.3 读取pcm数据
        //Read PCM
        if(fread(frame_buffer,1,size,inputFile)<=0){
            LOGE("读取raw data失败");
            return -1;
        }else if(feof(inputFile)){
            break;
        }
        //3.2.4 为AVFrame赋值
        pFrame->data[0] = frame_buffer;//pcm data
        pFrame->pts =i*100;

        //3.2.5 编码数据存储到pkt
        got_frame =0;
        ret = avcodec_encode_video2(pCodecContext,&pkt,pFrame,&got_frame);
        if(ret<0){
            LOGE("编码失败");
            return -1;
        }
        if(got_frame==1){
            LOGE("Succeed to encode 1 frame! \tsize:%5d\n",pkt.size);
            pkt.stream_index = pStream->index;
            //3.2.6 编码数据写入AVFormatContext
            ret = av_write_frame(pFormatContext,&pkt);
            av_packet_unref(&pkt);
        }
    }
    //3.2.7  刷新AVFormatContext
    ret = audio_flush_encoder(pFormatContext,0);
    if (ret < 0) {
        LOGE("Flushing encoder failed\n");
        return -1;
    }
    //3.3 写入数据
    av_write_trailer(pFormatContext);

    //4. 释放资源
    avcodec_close(pStream->codec);
    av_free(pFrame);
    av_free(frame_buffer);
    avio_close(pFormatContext->pb);
    avformat_free_context(pFormatContext);
    fclose(inputFile);
    return 0;
}


extern "C"
JNIEXPORT jint JNICALL
Java_zzw_com_ffmpegdemo_VideoUtils_audio_1encode(JNIEnv *env, jclass type, jstring input_,
                                                 jstring output_) {
    const char *input = env->GetStringUTFChars(input_, 0);
    const char *output = env->GetStringUTFChars(output_, 0);


    int flog = audio_encode_example(input, output);

    env->ReleaseStringUTFChars(input_, input);
    env->ReleaseStringUTFChars(output_, output);

    return flog;
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
    video_decode_example(output_file_name, input_file_name);
//    video_decode_example(output_file_name, input_file_name);

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


    video_encode_example(input, output);

    env->ReleaseStringUTFChars(input_, input);
    env->ReleaseStringUTFChars(output_, output);
    return 1;
}
extern "C"
JNIEXPORT jint JNICALL
Java_zzw_com_ffmpegdemo_VideoUtils_audio_1decode(JNIEnv *env, jclass type, jstring input_,
                                                 jstring output_) {
    const char *input = env->GetStringUTFChars(input_, 0);
    const char *output = env->GetStringUTFChars(output_, 0);
    int flog = audio_decode_example(input, output);

    env->ReleaseStringUTFChars(input_, input);
    env->ReleaseStringUTFChars(output_, output);

    return flog;
}

