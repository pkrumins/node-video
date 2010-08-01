#ifndef VIDEO_ENCODER_H
#define VIDEO_ENCODER_H

#include <string>
#include <node.h>
#include <theora/theoraenc.h>

class VideoEncoder {
    int width, height, quality, frameRate, keyFrameInterval;
    std::string outputFileName;

    FILE *ogg_fp;
    th_info ti;
    th_enc_ctx *td;
    th_comment tc;
    ogg_packet op;
    ogg_page og;
    ogg_stream_state *ogg_os;

    unsigned long frameCount;

public:
    VideoEncoder(int wwidth, int hheight);
    ~VideoEncoder();

    v8::Handle<v8::Value> newFrame(const unsigned char *data);
    v8::Handle<v8::Value> dupFrame(const unsigned char *data, int time);
    void setOutputFile(const char *fileName);
    void setQuality(int qquality);
    void setFrameRate(int fframeRate);
    void setKeyFrameInterval(int kkeyFrameInterval);
    void end();

private:
    v8::Handle<v8::Value> InitTheora();
    v8::Handle<v8::Value> WriteHeaders();
    v8::Handle<v8::Value> WriteFrame(const unsigned char *rgb, int dupCount=0);
};

#endif
