#include <node.h>
#include <node_buffer.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <utility>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <theora/theoraenc.h>

using namespace v8;
using namespace node;

static int chroma_format = TH_PF_420;

static void VException(const char *msg) {
    ThrowException(Exception::Error(String::New(msg)));
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
    if (!yuv) VException("malloc failed in rgba_to_yuv");

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
    if (!yuv) VException("malloc failed in rgb_to_yuv");

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

// FixedVideo class, all frames are of fixed size (no stacking).
class FixedVideo : public ObjectWrap {
private:
    int width, height, quality;
    std::string outputFileName;

    bool hadFrame;
    FILE *ogg_fp;

    // TODO: factor it out to VideoEncoder class
    th_info ti;
    th_enc_ctx *td;
    th_comment tc;
    ogg_packet op;
    ogg_page og;
    ogg_stream_state *ogg_os;

public:
    FixedVideo(int wwidth, int hheight, int qquality) :
        width(wwidth), height(hheight), quality(qquality),
        hadFrame(false), ogg_fp(NULL), td(NULL), ogg_os(NULL) {}

    ~FixedVideo() {
        End();
    }

    static void
    Initialize(Handle<Object> target)
    {
        HandleScope scope;

        Local<FunctionTemplate> t = FunctionTemplate::New(New);
        t->InstanceTemplate()->SetInternalFieldCount(1);
        NODE_SET_PROTOTYPE_METHOD(t, "newFrame", NewFrame);
        NODE_SET_PROTOTYPE_METHOD(t, "setOutputFile", SetOutputFile);
        NODE_SET_PROTOTYPE_METHOD(t, "end", End);
        target->Set(String::NewSymbol("FixedVideo"), t->GetFunction());
    }

    void NewFrame(const unsigned char *data) {
        if (!hadFrame) {
            if (outputFileName.empty())
                VException("No output means was set. Use setOutputFile to set it.");

            ogg_fp = fopen(outputFileName.c_str(), "w+");
            if (!ogg_fp) {
                char error_msg[256];
                snprintf(error_msg, 256, "Could not open %s. Error: %s.",
                    outputFileName.c_str(), strerror(errno));
                VException(error_msg);
            }

            InitTheora();
            WriteHeaders();
        }
        WriteFrame(data);
        hadFrame = true;
    }

    void SetOutputFile(const char *fileName) {
        outputFileName = fileName;
    }

    void End() {
        if (ogg_fp) fclose(ogg_fp);
        if (td) th_encode_free(td);
        if (ogg_os) ogg_stream_clear(ogg_os);
    }

private:
    void InitTheora() {
        th_info_init(&ti);
        ti.frame_width = ((width + 15) >> 4) << 4; // make sure width%16==0
        ti.frame_height = ((height + 15) >> 4) << 4;
        ti.pic_width = width;
        ti.pic_height = height;
        ti.pic_x = 0;
        ti.pic_y = 0;
        ti.fps_numerator = 0;
        ti.fps_denominator = 0;
        ti.aspect_numerator = 0;
        ti.aspect_denominator = 0;
        ti.colorspace = TH_CS_UNSPECIFIED;
        ti.pixel_fmt = TH_PF_420;
        ti.target_bitrate = 0;
        ti.quality = quality;
        ti.keyframe_granule_shift=6; // keyframe every 64 frames

        td = th_encode_alloc(&ti);
        th_info_clear(&ti);

        ogg_os = (ogg_stream_state *)malloc(sizeof(ogg_stream_state));
        if (!ogg_os)
            VException("malloc failed in InitTheora for ogg_stream_state");

        if (ogg_stream_init(ogg_os, rand()))
            VException("ogg_stream_init failed in InitTheora");
    }

    void WriteHeaders() {
        th_comment_init(&tc);
        if (th_encode_flushheader(td, &tc, &op) <= 0)
            VException("th_encode_flushheader failed in WriteHeaders");
        th_comment_clear(&tc);

        ogg_stream_packetin(ogg_os, &op);
        if (ogg_stream_pageout(ogg_os, &og)!=1)
            VException("ogg_stream_pageout failed in WriteHeaders");

        fwrite(og.header,1,og.header_len,ogg_fp);
        fwrite(og.body,1,og.body_len,ogg_fp);

        for (;;) {
            int ret = th_encode_flushheader(td, &tc, &op);
            if (ret<0)
                VException("th_encode_flushheader failed in WriteHeaders");
            else if (ret == 0) break;
            ogg_stream_packetin(ogg_os, &op);
        }

        for (;;) {
            int ret = ogg_stream_flush(ogg_os, &og);
            if (ret < 0)
                VException("ogg_stream_flush failed in WriteHeaders");
            else if (ret == 0) break;
            fwrite(og.header, 1, og.header_len, ogg_fp);
            fwrite(og.body, 1, og.body_len, ogg_fp);
        }

    }

    void WriteFrame(const unsigned char *rgba) {
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
            VException("malloc failed in WriteFrame for yuv_y");
        }
        yuv_u = (unsigned char*)malloc(ycbcr[1].stride * ycbcr[1].height);
        if (!yuv_u) {
            free(yuv);
            free(yuv_y);
            VException("malloc failed in WriteFrame for yuv_u");
        }
        yuv_v = (unsigned char*)malloc(ycbcr[2].stride * ycbcr[2].height);
        if (!yuv_u) {
            free(yuv);
            free(yuv_y);
            free(yuv_u);
            VException("malloc failed in WriteFrame for yuv_v");
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
            VException("th_encode_ycbcr_in failed in WriteFrame");
        }

        if(!th_encode_packetout(td, 0, &op)) {
            free(yuv);
            free(yuv_y);
            free(yuv_u);
            free(yuv_v);
            VException("th_encode_packetout failed in WriteFrame");
        }

        ogg_stream_packetin(ogg_os, &op);
        while(ogg_stream_pageout(ogg_os, &og)) {
            fwrite(og.header, og.header_len, 1, ogg_fp);
            fwrite(og.body, og.body_len, 1, ogg_fp);
        }

        free(yuv_y);
        free(yuv_u);
        free(yuv_v);
        free(yuv);
    }

protected:
    static Handle<Value>
    New(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 3)
            VException("Three arguments required - width, height and quality");
        if (!args[0]->IsInt32())
            VException("First argument must be integer width.");
        if (!args[1]->IsInt32())
            VException("Second argument must be integer height.");
        if (!args[2]->IsInt32())
            VException("Third argument must be integer quality.");

        int w = args[0]->Int32Value();
        int h = args[1]->Int32Value();
        int q = args[2]->Int32Value();

        if (w < 0)
            VException("Width smaller than 0.");
        if (h < 0)
            VException("Height smaller than 0.");
        if (q < 0)
            VException("Quality smaller than 0.");
        if (q > 63)
            VException("Quality greater than 63.");

        FixedVideo *v = new FixedVideo(w, h, q);
        v->Wrap(args.This());
        return args.This();
    }

    static Handle<Value>
    NewFrame(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 1)
            VException("One argument required - Buffer with full frame data.");

        if (!Buffer::HasInstance(args[0])) 
            ThrowException(Exception::Error(String::New("First argument must be Buffer.")));

        Buffer *rgba = ObjectWrap::Unwrap<Buffer>(args[0]->ToObject());

        FixedVideo *fv = ObjectWrap::Unwrap<FixedVideo>(args.This());
        fv->NewFrame((unsigned char *)rgba->data());

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

    static Handle<Value>
    SetOutputFile(const Arguments &args)
    {
        HandleScope scope;

        if (args.Length() != 1)
            VException("One argument required - output file name.");

        String::AsciiValue fileName(args[0]->ToString());

        FixedVideo *fv = ObjectWrap::Unwrap<FixedVideo>(args.This());
        fv->SetOutputFile(*fileName);

        return Undefined();
    }
};

extern "C" void
init(Handle<Object> target)
{
    HandleScope scope;

    srand((getpid()<<16) ^ (getpid()<<8) ^ time(NULL));
    FixedVideo::Initialize(target);
}

