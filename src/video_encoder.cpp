#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <cmath>

#include "loki/ScopeGuard.h"

#include "common.h"
#include "video_encoder.h"

using namespace v8;
using namespace node;

static int chroma_format = TH_PF_420;

static inline unsigned char
yuv_clamp(double d)
{
    http://catonmat.net/ftp/clamps.jpg
    if(d < 0) return 0;
    if(d > 255) return 255;
    return d;
}

static unsigned char *
rgb_to_yuv(const unsigned char *rgb, size_t size)
{
    unsigned char r, g, b;
    unsigned char *yuv = (unsigned char *)malloc(size);
    if (!yuv) return NULL;

    for (size_t i=0; i<size; i+=3) {
        r = rgb[i];
        g = rgb[i+1];
        b = rgb[i+2];

        yuv[i] = yuv_clamp(0.299 * r + 0.587 * g + 0.114 * b);
        yuv[i+1] = yuv_clamp((0.436 * 255 - 0.14713 * r - 0.28886 * g + 0.436 * b) / 0.872);
        yuv[i+2] = yuv_clamp((0.615 * 255 + 0.615 * r - 0.51499 * g - 0.10001 * b) / 1.230);
    }

    return yuv;
}

VideoEncoder::VideoEncoder(int wwidth, int hheight) :
    width(wwidth), height(hheight), quality(31), frameRate(25),
    keyFrameInterval(64),
    ogg_fp(NULL), td(NULL), ogg_os(NULL),
    frameCount(0) {}

VideoEncoder::~VideoEncoder() {
    end();
}

void
VideoEncoder::newFrame(const unsigned char *data)
{
    if (!frameCount) {
        if (outputFileName.empty())
            throw "No output means was set. Use setOutputFile to set it.";

        ogg_fp = fopen(outputFileName.c_str(), "w+");
        if (!ogg_fp) {
            char error_msg[256];
            snprintf(error_msg, 256, "Could not open %s. Error: %s.",
                outputFileName.c_str(), strerror(errno));
            throw error_msg;
        }

        InitTheora();
        WriteHeaders();
    }
    WriteFrame(data);
    frameCount++;
}

void
VideoEncoder::dupFrame(const unsigned char *data, int time)
{
    int frames = ceil((float)time*frameRate/1000);
    int repetitions = floor((float)frames/(keyFrameInterval-1));
    int i;

    if (repetitions == 0)
        WriteFrame(data, frames);

    for (i = 1; i<=repetitions; i++) {
        WriteFrame(data, keyFrameInterval-1);
    }
    
    int mod = frames%(keyFrameInterval-1);
    if (mod) WriteFrame(data, mod);
}

void
VideoEncoder::setOutputFile(const char *fileName)
{
    outputFileName = fileName;
}

void
VideoEncoder::setQuality(int qquality)
{
    quality = qquality;
}

void
VideoEncoder::setFrameRate(int fframeRate)
{
    frameRate = fframeRate;
}

void
VideoEncoder::setKeyFrameInterval(int kkeyFrameInterval)
{
    keyFrameInterval = kkeyFrameInterval;
}

void
VideoEncoder::end()
{
    if (ogg_fp) fclose(ogg_fp);
    if (td) th_encode_free(td);
    if (ogg_os) ogg_stream_clear(ogg_os);
    ogg_fp = NULL;
    td = NULL;
    ogg_os = NULL;
}

void
VideoEncoder::InitTheora()
{
    th_info_init(&ti);
    ti.frame_width = ((width + 15) >> 4) << 4; // make sure width%16==0
    ti.frame_height = ((height + 15) >> 4) << 4;
    ti.pic_width = width;
    ti.pic_height = height;
    ti.pic_x = 0;
    ti.pic_y = 0;
    ti.fps_numerator = frameRate;
    ti.fps_denominator = 1;
    ti.aspect_numerator = 0;
    ti.aspect_denominator = 0;
    ti.colorspace = TH_CS_UNSPECIFIED;
    ti.pixel_fmt = TH_PF_420;
    ti.target_bitrate = 0;
    ti.quality = quality;
    ti.keyframe_granule_shift = (int)log2(keyFrameInterval);

    td = th_encode_alloc(&ti);
    th_info_clear(&ti);

    int comp=1;
    th_encode_ctl(td,TH_ENCCTL_SET_VP3_COMPATIBLE,&comp,sizeof(comp));

    ogg_os = (ogg_stream_state *)malloc(sizeof(ogg_stream_state));
    if (!ogg_os)
        throw "malloc failed in InitTheora for ogg_stream_state";

    if (ogg_stream_init(ogg_os, rand()))
        throw "ogg_stream_init failed in InitTheora";
}

void
VideoEncoder::WriteHeaders()
{
    th_comment_init(&tc);
    if (th_encode_flushheader(td, &tc, &op) <= 0)
        throw "th_encode_flushheader failed in WriteHeaders";
    th_comment_clear(&tc);

    ogg_stream_packetin(ogg_os, &op);
    if (ogg_stream_pageout(ogg_os, &og)!=1)
        throw "ogg_stream_pageout failed in WriteHeaders";

    fwrite(og.header,1,og.header_len,ogg_fp);
    fwrite(og.body,1,og.body_len,ogg_fp);

    for (;;) {
        int ret = th_encode_flushheader(td, &tc, &op);
        if (ret<0)
            throw "th_encode_flushheader failed in WriteHeaders";
        else if (ret == 0)
            break;
        ogg_stream_packetin(ogg_os, &op);
    }

    for (;;) {
        int ret = ogg_stream_flush(ogg_os, &og);
        if (ret < 0)
            throw "ogg_stream_flush failed in WriteHeaders";
        else if (ret == 0)
            break;
        fwrite(og.header, 1, og.header_len, ogg_fp);
        fwrite(og.body, 1, og.body_len, ogg_fp);
    }
}

void
VideoEncoder::WriteFrame(const unsigned char *rgb, int dupCount)
{
    th_ycbcr_buffer ycbcr;
    ogg_packet op;
    ogg_page og;
    unsigned char *yuv;

    unsigned long yuv_w;
    unsigned long yuv_h;

    unsigned char *yuv_y;
    unsigned char *yuv_u;
    unsigned char *yuv_v;

    unsigned int x;
    unsigned int y;

    yuv = rgb_to_yuv(rgb, width*height*3);
    LOKI_ON_BLOCK_EXIT(free, yuv);
    if (!yuv)
        throw "malloc failed in rgb_to_yuv";

    yuv_w = (width + 15) & ~15;
    yuv_h = (height + 15) & ~15;

    ycbcr[0].width = yuv_w;
    ycbcr[0].height = yuv_h;
    ycbcr[0].stride = yuv_w;
    ycbcr[1].width = (chroma_format == TH_PF_444) ? yuv_w : (yuv_w >> 1);
    ycbcr[1].stride = ycbcr[1].width;
    ycbcr[1].height = (chroma_format == TH_PF_420) ? (yuv_h >> 1) : yuv_h;
    ycbcr[2].width = ycbcr[1].width;
    ycbcr[2].stride = ycbcr[1].stride;
    ycbcr[2].height = ycbcr[1].height;

    yuv_y = (unsigned char*)malloc(ycbcr[0].stride * ycbcr[0].height);
    LOKI_ON_BLOCK_EXIT(free, yuv_y);
    if (!yuv_y)
        throw "malloc failed in WriteFrame for yuv_y";

    yuv_u = (unsigned char*)malloc(ycbcr[1].stride * ycbcr[1].height);
    LOKI_ON_BLOCK_EXIT(free, yuv_u);
    if (!yuv_u)
        throw "malloc failed in WriteFrame for yuv_u";

    yuv_v = (unsigned char*)malloc(ycbcr[2].stride * ycbcr[2].height);
    LOKI_ON_BLOCK_EXIT(free, yuv_v);
    if (!yuv_u)
        throw "malloc failed in WriteFrame for yuv_v";

    ycbcr[0].data = yuv_y;
    ycbcr[1].data = yuv_u;
    ycbcr[2].data = yuv_v;

    for(y = 0; y < height; y++) {
        for(x = 0; x < width; x++) {
            yuv_y[x + y * yuv_w] = yuv[3 * (x + y * width) + 0];
        }
    }

    if (chroma_format == TH_PF_420) {
        for(y = 0; y < height; y += 2) {
            for(x = 0; x < width; x += 2) {
                yuv_u[(x >> 1) + (y >> 1) * (yuv_w >> 1)] =
                    yuv[3 * (x + y * width) + 1];
                yuv_v[(x >> 1) + (y >> 1) * (yuv_w >> 1)] =
                    yuv[3 * (x + y * width) + 2];
            }
        }
    } else if (chroma_format == TH_PF_444) {
        for(y = 0; y < height; y++) {
            for(x = 0; x < width; x++) {
                yuv_u[x + y * ycbcr[1].stride] = yuv[3 * (x + y * width) + 1];
                yuv_v[x + y * ycbcr[2].stride] = yuv[3 * (x + y * width) + 2];
            }
        }
    } else {  // TH_PF_422 
        for(y = 0; y < height; y += 1) {
            for(x = 0; x < width; x += 2) {
                yuv_u[(x >> 1) + y * ycbcr[1].stride] =
                    yuv[3 * (x + y * width) + 1];
                yuv_v[(x >> 1) + y * ycbcr[2].stride] =
                    yuv[3 * (x + y * width) + 2];
            }
        }    
    }

    if (dupCount > 0) {
        int ret = th_encode_ctl(td, TH_ENCCTL_SET_DUP_COUNT, &dupCount, sizeof(int));
        if (ret)
            throw "th_encode_ctl failed for dupCount>0";
    }

    if(th_encode_ycbcr_in(td, ycbcr))
        throw "th_encode_ycbcr_in failed in WriteFrame";

    while (int ret = th_encode_packetout(td, 0, &op)) {
        if (ret < 0)
            throw "th_encode_packetout failed in WriteFrame";
        ogg_stream_packetin(ogg_os, &op);
        while(ogg_stream_pageout(ogg_os, &og)) {
            fwrite(og.header, og.header_len, 1, ogg_fp);
            fwrite(og.body, og.body_len, 1, ogg_fp);
        }
    }

    ogg_stream_flush(ogg_os, &og);
    fwrite(og.header, og.header_len, 1, ogg_fp);
    fwrite(og.body, og.body_len, 1, ogg_fp);
}

