#include <node.h>
#include <node_buffer.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <cmath>
#include <theora/theoraenc.h>

using namespace v8;
using namespace node;

static int chroma_format = TH_PF_420;

static Handle<Value>
VException(const char *msg) {
    HandleScope scope;
    return ThrowException(Exception::Error(String::New(msg)));
}

static inline unsigned char
yuv_clamp(double d)
{
    http://ochemonline.files.wordpress.com/2010/03/clamps.jpg
    if(d < 0) return 0;
    if(d > 255) return 255;
    return d;
}

static unsigned char *
rgba_to_yuv(const unsigned char *rgba, size_t size)
{
    unsigned char r, g, b;
    unsigned char *yuv = (unsigned char *)malloc(size*3/4);
    if (!yuv) return NULL;

    for (size_t i=0, j=0; i<size; i+=4, j+=3) {
        r = rgba[i];
        g = rgba[i+1];
        b = rgba[i+2];

        yuv[j] = yuv_clamp(0.299 * r + 0.587 * g + 0.114 * b);
        yuv[j+1] = yuv_clamp((0.436 * 255 - 0.14713 * r - 0.28886 * g + 0.436 * b) / 0.872);
        yuv[j+2] = yuv_clamp((0.615 * 255 + 0.615 * r - 0.51499 * g - 0.10001 * b) / 1.230);
    }

    return yuv;
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

class VideoEncoder {
private:
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
    VideoEncoder(int wwidth, int hheight) :
        width(wwidth), height(hheight), quality(31), frameRate(25),
        keyFrameInterval(64),
        ogg_fp(NULL), td(NULL), ogg_os(NULL),
        frameCount(0) {}

    ~VideoEncoder() { end(); }

    Handle<Value>
    newFrame(const unsigned char *data)
    {
        HandleScope scope;
        Handle<Value> ret;

        if (!frameCount) {
            if (outputFileName.empty())
                return VException("No output means was set. Use setOutputFile to set it.");

            ogg_fp = fopen(outputFileName.c_str(), "w+");
            if (!ogg_fp) {
                char error_msg[256];
                snprintf(error_msg, 256, "Could not open %s. Error: %s.",
                    outputFileName.c_str(), strerror(errno));
                return VException(error_msg);
            }

            ret = InitTheora();
            if (!ret->IsUndefined()) return ret;

            ret = WriteHeaders();
            if (!ret->IsUndefined()) return ret;
        }
        ret = WriteFrame(data);
        if (!ret->IsUndefined()) return ret;
        frameCount++;

        return Undefined();
    }

    void setOutputFile(const char *fileName) {
        outputFileName = fileName;
    }

    void setQuality(int qquality) {
        quality = qquality;
    }

    void setFrameRate(int fframeRate) {
        frameRate = fframeRate;
    }

    void setKeyFrameInterval(int kkeyFrameInterval) {
        keyFrameInterval = kkeyFrameInterval;
    }

    void end() {
        if (ogg_fp) {
            fclose(ogg_fp);
            ogg_fp = NULL;
        }
        if (td) {
            th_encode_free(td);
            td = NULL;
        }
        if (ogg_os) {
            ogg_stream_clear(ogg_os);
            ogg_os = NULL;
        }
    }

private:
    Handle<Value>
    InitTheora()
    {
        HandleScope scope;

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

        ogg_os = (ogg_stream_state *)malloc(sizeof(ogg_stream_state));
        if (!ogg_os)
            return VException("malloc failed in InitTheora for ogg_stream_state");

        if (ogg_stream_init(ogg_os, rand()))
            return VException("ogg_stream_init failed in InitTheora");

        return Undefined();
    }

    Handle<Value>
    WriteHeaders()
    {
        HandleScope scope;

        th_comment_init(&tc);
        if (th_encode_flushheader(td, &tc, &op) <= 0)
            return VException("th_encode_flushheader failed in WriteHeaders");
        th_comment_clear(&tc);

        ogg_stream_packetin(ogg_os, &op);
        if (ogg_stream_pageout(ogg_os, &og)!=1)
            return VException("ogg_stream_pageout failed in WriteHeaders");

        fwrite(og.header,1,og.header_len,ogg_fp);
        fwrite(og.body,1,og.body_len,ogg_fp);

        for (;;) {
            int ret = th_encode_flushheader(td, &tc, &op);
            if (ret<0)
                return VException("th_encode_flushheader failed in WriteHeaders");
            else if (ret == 0) break;
            ogg_stream_packetin(ogg_os, &op);
        }

        for (;;) {
            int ret = ogg_stream_flush(ogg_os, &og);
            if (ret < 0)
                return VException("ogg_stream_flush failed in WriteHeaders");
            else if (ret == 0) break;
            fwrite(og.header, 1, og.header_len, ogg_fp);
            fwrite(og.body, 1, og.body_len, ogg_fp);
        }

        return Undefined();
    }

    Handle<Value>
    WriteFrame(const unsigned char *rgba)
    {
        HandleScope scope;

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

        yuv = rgba_to_yuv(rgba, width*height*4);
        if (!yuv)
            return VException("malloc failed in rgba_to_yuv");

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
        if (!yuv_y) {
            free(yuv);
            return VException("malloc failed in WriteFrame for yuv_y");
        }
        yuv_u = (unsigned char*)malloc(ycbcr[1].stride * ycbcr[1].height);
        if (!yuv_u) {
            free(yuv);
            free(yuv_y);
            return VException("malloc failed in WriteFrame for yuv_u");
        }
        yuv_v = (unsigned char*)malloc(ycbcr[2].stride * ycbcr[2].height);
        if (!yuv_u) {
            free(yuv);
            free(yuv_y);
            free(yuv_u);
            return VException("malloc failed in WriteFrame for yuv_v");
        }

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

        if(th_encode_ycbcr_in(td, ycbcr)) {
            free(yuv);
            free(yuv_y);
            free(yuv_u);
            free(yuv_v);
            return VException("th_encode_ycbcr_in failed in WriteFrame");
        }

        if(!th_encode_packetout(td, 0, &op)) {
            free(yuv);
            free(yuv_y);
            free(yuv_u);
            free(yuv_v);
            return VException("th_encode_packetout failed in WriteFrame");
        }

        ogg_stream_packetin(ogg_os, &op);
        while(ogg_stream_pageout(ogg_os, &og)) {
            fwrite(og.header, og.header_len, 1, ogg_fp);
            fwrite(og.body, og.body_len, 1, ogg_fp);
        }

        ogg_stream_flush(ogg_os, &og);
        fwrite(og.header, og.header_len, 1, ogg_fp);
        fwrite(og.body, og.body_len, 1, ogg_fp);

        free(yuv_y);
        free(yuv_u);
        free(yuv_v);
        free(yuv);

        return Undefined();
    }
};

// FixedVideo class, all frames are of fixed size (no stacking).
class FixedVideo : public ObjectWrap {
private:
    VideoEncoder videoEncoder;

public:
    FixedVideo(int width, int height) : videoEncoder(width, height) {}

    static void
    Initialize(Handle<Object> target)
    {
        HandleScope scope;

        Local<FunctionTemplate> t = FunctionTemplate::New(New);
        t->InstanceTemplate()->SetInternalFieldCount(1);
        NODE_SET_PROTOTYPE_METHOD(t, "newFrame", NewFrame);
        NODE_SET_PROTOTYPE_METHOD(t, "setOutputFile", SetOutputFile);
        NODE_SET_PROTOTYPE_METHOD(t, "setQuality", SetQuality);
        NODE_SET_PROTOTYPE_METHOD(t, "setFrameRate", SetFrameRate);
        NODE_SET_PROTOTYPE_METHOD(t, "setKeyFrameInterval", SetKeyFrameInterval);
        NODE_SET_PROTOTYPE_METHOD(t, "end", End);
        target->Set(String::NewSymbol("FixedVideo"), t->GetFunction());
    }

    void NewFrame(const unsigned char *data) {
        videoEncoder.newFrame(data);
    }

    void SetOutputFile(const char *fileName) {
        videoEncoder.setOutputFile(fileName);
    }

    void SetQuality(int quality) {
        videoEncoder.setQuality(quality);
    }

    void SetFrameRate(int frameRate) {
        videoEncoder.setFrameRate(frameRate);
    }

    void SetKeyFrameInterval(int keyFrameInterval) {
        videoEncoder.setKeyFrameInterval(keyFrameInterval);
    }

    void End() {
        videoEncoder.end();
    }

protected:
    static Handle<Value>
    New(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 2)
            return VException("Two arguments required - width and height.");
        if (!args[0]->IsInt32())
            return VException("First argument must be integer width.");
        if (!args[1]->IsInt32())
            return VException("Second argument must be integer height.");

        int w = args[0]->Int32Value();
        int h = args[1]->Int32Value();

        if (w < 0)
            return VException("Width smaller than 0.");
        if (h < 0)
            return VException("Height smaller than 0.");

        FixedVideo *fv = new FixedVideo(w, h);
        fv->Wrap(args.This());
        return args.This();
    }

    static Handle<Value>
    NewFrame(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 1)
            return VException("One argument required - Buffer with full frame data.");

        if (!Buffer::HasInstance(args[0])) 
            return VException("First argument must be Buffer.");

        Buffer *rgba = ObjectWrap::Unwrap<Buffer>(args[0]->ToObject());

        FixedVideo *fv = ObjectWrap::Unwrap<FixedVideo>(args.This());
        fv->NewFrame((unsigned char *)rgba->data());

        return Undefined();
    }

    static Handle<Value>
    SetOutputFile(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 1)
            return VException("One argument required - output file name.");

        String::AsciiValue fileName(args[0]->ToString());

        FixedVideo *fv = ObjectWrap::Unwrap<FixedVideo>(args.This());
        fv->SetOutputFile(*fileName);

        return Undefined();
    }

    static Handle<Value>
    SetQuality(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 1) 
            return VException("One argument required - video quality.");

        if (!args[0]->IsInt32())
            return VException("Quality must be integer.");

        int q = args[0]->Int32Value();

        if (q < 0) return VException("Quality smaller than 0.");
        if (q > 63) return VException("Quality greater than 63.");

        FixedVideo *fv = ObjectWrap::Unwrap<FixedVideo>(args.This());
        fv->SetQuality(q);

        return Undefined();
    }

    static Handle<Value>
    SetFrameRate(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 1) 
            return VException("One argument required - frame rate.");

        if (!args[0]->IsInt32())
            return VException("Frame rate must be integer.");

        int rate = args[0]->Int32Value();
        
        if (rate < 0)
            return VException("Frame rate must be positive.");

        FixedVideo *fv = ObjectWrap::Unwrap<FixedVideo>(args.This());
        fv->SetFrameRate(rate);

        return Undefined();
    }

    static Handle<Value>
    SetKeyFrameInterval(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 1) 
            return VException("One argument required - keyframe interval.");

        if (!args[0]->IsInt32())
            return VException("Keyframe interval must be integer.");

        int interval = args[0]->Int32Value();

        if (interval < 0)
            return VException("Keyframe interval must be positive.");

        if ((interval & (interval - 1)) != 0)
            return VException("Keyframe interval must be a power of two.");

        FixedVideo *fv = ObjectWrap::Unwrap<FixedVideo>(args.This());
        fv->SetKeyFrameInterval(interval);

        return Undefined();
    }

    static Handle<Value>
    End(const Arguments &args)
    {
        HandleScope scope;

        FixedVideo *fv = ObjectWrap::Unwrap<FixedVideo>(args.This());
        fv->End();

        return Undefined();
    }
};

// I have no idea how to inherit correctly with all this object wrapping,
// so I just copy/pasted everything from FixedVideo for great good.
class StackedVideo : public ObjectWrap {
private:
    int width, height;

    VideoEncoder videoEncoder;
    unsigned char *lastFrame;

public:
    StackedVideo(int wwidth, int hheight) :
        width(wwidth), height(hheight), videoEncoder(wwidth, hheight),
        lastFrame(NULL) {}

    ~StackedVideo() { free(lastFrame); }

    static void
    Initialize(Handle<Object> target)
    {
        HandleScope scope;

        Local<FunctionTemplate> t = FunctionTemplate::New(New);
        t->InstanceTemplate()->SetInternalFieldCount(1);
        NODE_SET_PROTOTYPE_METHOD(t, "newFrame", NewFrame);
        NODE_SET_PROTOTYPE_METHOD(t, "push", Push);
        NODE_SET_PROTOTYPE_METHOD(t, "endPush", EndPush);
        NODE_SET_PROTOTYPE_METHOD(t, "setOutputFile", SetOutputFile);
        NODE_SET_PROTOTYPE_METHOD(t, "setQuality", SetQuality);
        NODE_SET_PROTOTYPE_METHOD(t, "setFrameRate", SetFrameRate);
        NODE_SET_PROTOTYPE_METHOD(t, "setKeyFrameInterval", SetKeyFrameInterval);
        NODE_SET_PROTOTYPE_METHOD(t, "end", End);
        target->Set(String::NewSymbol("StackedVideo"), t->GetFunction());
    }

    Handle<Value>
    NewFrame(const unsigned char *data)
    {
        HandleScope scope;

        if (!lastFrame) {
            lastFrame = (unsigned char *)malloc(width*height*4);
            if (!lastFrame)
                return VException("malloc failed in StackedVideo::NewFrame.");
        }
        memcpy(lastFrame, data, width*height*4);

        videoEncoder.newFrame(data);
    }

    Handle<Value>
    Push(unsigned char *rect, int x, int y, int w, int h)
    {
        HandleScope scope;

        if (!lastFrame) {
           if (x==0 && y==0 && w==width && h==height) {
               lastFrame = (unsigned char *)malloc(width*height*4);
               if (!lastFrame) 
                   return VException("malloc failed in StackedVideo::Push.");
               memcpy(lastFrame, rect, width*height*4);
               videoEncoder.newFrame(rect);
               return Undefined();
            }
            return VException("The first full frame was not pushed.");
        }

        int start = y*width*4 + x*4;
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < 4*w; j+=4) {
                lastFrame[start + i*width*4 + j] = rect[i*w*4 + j];
                lastFrame[start + i*width*4 + j + 1] = rect[i*w*4 + j + 1];
                lastFrame[start + i*width*4 + j + 2] = rect[i*w*4 + j + 2];
                lastFrame[start + i*width*4 + j + 3] = rect[i*w*4 + j + 3];
            }
        }

        return Undefined();
    }

    Handle<Value>
    EndPush()
    {
        HandleScope scope;

        if (!lastFrame)
            return VException("The first full frame was not pushed.");
        videoEncoder.newFrame(lastFrame);

        return Undefined();
    }

    void SetOutputFile(const char *fileName) {
        videoEncoder.setOutputFile(fileName);
    }

    void SetQuality(int quality) {
        videoEncoder.setQuality(quality);
    }

    void SetFrameRate(int frameRate) {
        videoEncoder.setFrameRate(frameRate);
    }

    void SetKeyFrameInterval(int keyFrameInterval) {
        videoEncoder.setKeyFrameInterval(keyFrameInterval);
    }

    void End() {
        videoEncoder.end();
    }

protected:
    static Handle<Value>
    New(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 2)
            return VException("Two arguments required - width and height.");
        if (!args[0]->IsInt32())
            return VException("First argument must be integer width.");
        if (!args[1]->IsInt32())
            return VException("Second argument must be integer height.");

        int w = args[0]->Int32Value();
        int h = args[1]->Int32Value();

        if (w < 0)
            return VException("Width smaller than 0.");
        if (h < 0)
            return VException("Height smaller than 0.");

        StackedVideo *sv = new StackedVideo(w, h);
        sv->Wrap(args.This());
        return args.This();
    }

    static Handle<Value>
    NewFrame(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 1)
            return VException("One argument required - Buffer with full frame data.");

        if (!Buffer::HasInstance(args[0])) 
            return VException("First argument must be Buffer.");

        Buffer *rgba = ObjectWrap::Unwrap<Buffer>(args[0]->ToObject());

        StackedVideo *sv = ObjectWrap::Unwrap<StackedVideo>(args.This());
        sv->NewFrame((unsigned char *)rgba->data());

        return Undefined();
    }

    static Handle<Value>
    Push(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 5)
            return VException("Five arguments required - buffer, x, y, width, height.");

        if (!Buffer::HasInstance(args[0]))
            return VException("First argument must be Buffer.");
        if (!args[1]->IsInt32())
            return VException("Second argument must be integer x.");
        if (!args[2]->IsInt32())
            return VException("Third argument must be integer y.");
        if (!args[3]->IsInt32())
            return VException("Fourth argument must be integer width.");
        if (!args[4]->IsInt32())
            return VException("Fifth argument must be integer height.");

        StackedVideo *sv = ObjectWrap::Unwrap<StackedVideo>(args.This());
        Buffer *rgba = ObjectWrap::Unwrap<Buffer>(args[0]->ToObject());
        int x = args[1]->Int32Value();
        int y = args[2]->Int32Value();
        int w = args[3]->Int32Value();
        int h = args[4]->Int32Value();

        if (x < 0)
            return VException("Coordinate x smaller than 0.");
        if (y < 0)
            return VException("Coordinate y smaller than 0.");
        if (w < 0)
            return VException("Width smaller than 0.");
        if (h < 0)
            return VException("Height smaller than 0.");
        if (x >= sv->width) 
            return VException("Coordinate x exceeds StackedVideo's dimensions.");
        if (y >= sv->height) 
            return VException("Coordinate y exceeds StackedVideo's dimensions.");
        if (x+w > sv->width) 
            return VException("Pushed buffer exceeds StackedVideo's width.");
        if (y+h > sv->height) 
            return VException("Pushed buffer exceeds StackedVideo's height.");

        sv->Push((unsigned char *)rgba->data(), x, y, w, h);

        return Undefined();
    }

    static Handle<Value>
    EndPush(const Arguments &args) {
        HandleScope scope;

        StackedVideo *sv = ObjectWrap::Unwrap<StackedVideo>(args.This());
        sv->EndPush();

        return Undefined();
    }

    static Handle<Value>
    SetOutputFile(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 1)
            return VException("One argument required - output file name.");

        String::AsciiValue fileName(args[0]->ToString());

        StackedVideo *sv = ObjectWrap::Unwrap<StackedVideo>(args.This());
        sv->SetOutputFile(*fileName);

        return Undefined();
    }

    static Handle<Value>
    SetQuality(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 1) 
            return VException("One argument required - video quality.");

        if (!args[0]->IsInt32())
            return VException("Quality must be integer.");

        int q = args[0]->Int32Value();

        if (q < 0) return VException("Quality smaller than 0.");
        if (q > 63) return VException("Quality greater than 63.");

        StackedVideo *sv = ObjectWrap::Unwrap<StackedVideo>(args.This());
        sv->SetQuality(q);

        return Undefined();
    }

    static Handle<Value>
    SetFrameRate(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 1) 
            return VException("Two argument required - frame rate.");

        if (!args[0]->IsInt32())
            return VException("Frame rate must be integer.");

        int rate = args[0]->Int32Value();

        StackedVideo *sv = ObjectWrap::Unwrap<StackedVideo>(args.This());
        sv->SetFrameRate(rate);

        return Undefined();
    }

    static Handle<Value>
    SetKeyFrameInterval(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 1) 
            return VException("One argument required - keyframe interval.");

        if (!args[0]->IsInt32())
            return VException("Keyframe interval must be integer.");

        int interval = args[0]->Int32Value();

        if (interval < 0)
            return VException("Keyframe interval must be positive.");

        if ((interval & (interval - 1)) != 0)
            return VException("Keyframe interval must be a power of two.");

        StackedVideo *sv = ObjectWrap::Unwrap<StackedVideo>(args.This());
        sv->SetKeyFrameInterval(interval);

        return Undefined();
    }

    static Handle<Value>
    End(const Arguments &args)
    {
        HandleScope scope;

        StackedVideo *sv = ObjectWrap::Unwrap<StackedVideo>(args.This());
        sv->End();

        return Undefined();
    }
};

extern "C" void
init(Handle<Object> target)
{
    HandleScope scope;

    srand((getpid()<<16) ^ (getpid()<<8) ^ time(NULL));
    FixedVideo::Initialize(target);
    StackedVideo::Initialize(target);
}

