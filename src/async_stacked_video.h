#ifndef ASYNC_STACKED_VIDEO_H
#define ASYNC_STACKED_VIDEO_H

#include <vector>
#include <string>
#include <node.h>
#include <node_version.h>
#include "video_encoder.h"

struct push_request {
    unsigned int push_id;
    unsigned int fragment_id;
    const char *tmp_dir;
    unsigned char *data;
    int data_size;
    int x, y, w, h;
};

class AsyncStackedVideo;

struct async_encode_request {
    AsyncStackedVideo *video_obj;
    v8::Persistent<v8::Function> callback;
    char *error;
};

struct Rect {
    int x, y, w, h;
    Rect() {}
    Rect(int xx, int yy, int ww, int hh) : x(xx), y(yy), w(ww), h(hh) {}
    bool isNull() { return x == 0 && y == 0 && w == 0 && h == 0; }
};

class AsyncStackedVideo : public node::ObjectWrap {
    int width, height;

    VideoEncoder videoEncoder;

    std::string tmp_dir;
    unsigned int push_id, fragment_id;

#if NODE_VERSION_AT_LEAST(0,6,0)
    static void EIO_Push(eio_req *req);
    static void EIO_Encode(eio_req *req);
#else
    static int EIO_Push(eio_req *req);
    static int EIO_Encode(eio_req *req);
#endif
    static int EIO_PushAfter(eio_req *req);
    static int EIO_EncodeAfter(eio_req *req);

    static void push_fragment(unsigned char *frame, int width, int height,
        unsigned char *fragment, int x, int y, int w, int h);
    static Rect rect_dims(const char *fragment_name);

public:
    AsyncStackedVideo(int wwidth, int hheight);
    static void Initialize(v8::Handle<v8::Object> target);
    v8::Handle<v8::Value> Push(unsigned char *rect, int x, int y, int w, int h);
    void EndPush(unsigned long timeStamp=0);
    void SetOutputFile(const char *fileName);
    void SetQuality(int quality);
    void SetFrameRate(int frameRate);
    void SetKeyFrameInterval(int keyFrameInterval);

protected:
    static v8::Handle<v8::Value> New(const v8::Arguments &args);
    static v8::Handle<v8::Value> Push(const v8::Arguments &args);
    static v8::Handle<v8::Value> EndPush(const v8::Arguments &args);
    static v8::Handle<v8::Value> SetOutputFile(const v8::Arguments &args);
    static v8::Handle<v8::Value> SetQuality(const v8::Arguments &args);
    static v8::Handle<v8::Value> SetFrameRate(const v8::Arguments &args);
    static v8::Handle<v8::Value> SetKeyFrameInterval(const v8::Arguments &args);
    static v8::Handle<v8::Value> SetTmpDir(const v8::Arguments &args);
    static v8::Handle<v8::Value> Encode(const v8::Arguments &args);
};

#endif

