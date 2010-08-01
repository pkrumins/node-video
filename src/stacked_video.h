#ifndef STACKED_VIDEO_H
#define STACKED_VIDEO_H

#include <vector>
#include <node.h>
#include "video_encoder.h"

class StackedVideo : public node::ObjectWrap {
    int width, height;

    VideoEncoder videoEncoder;
    unsigned char *lastFrame;
    unsigned long lastTimeStamp;

    struct Update {
        int x, y, w, h;
        typedef std::vector<unsigned char> Rect;
        Rect rect;
        Update(unsigned char *rrect, int xx, int yy, int ww, int hh) :
            rect(rrect, rrect+(ww*hh*3)), x(xx), y(yy), w(ww), h(hh) {}
    };

    typedef std::vector<Update> VectorUpdate;
    typedef VectorUpdate::iterator VectorUpdateIterator;
    VectorUpdate updates;

public:
    StackedVideo(int wwidth, int hheight);
    ~StackedVideo();
    static void Initialize(v8::Handle<v8::Object> target);
    v8::Handle<v8::Value> NewFrame(const unsigned char *data, unsigned long timeStamp=0);
    v8::Handle<v8::Value> Push(unsigned char *rect, int x, int y, int w, int h);
    v8::Handle<v8::Value> EndPush(unsigned long timeStamp=0);
    void SetOutputFile(const char *fileName);
    void SetQuality(int quality);
    void SetFrameRate(int frameRate);
    void SetKeyFrameInterval(int keyFrameInterval);
    void End();

protected:
    static v8::Handle<v8::Value> New(const v8::Arguments &args);
    static v8::Handle<v8::Value> NewFrame(const v8::Arguments &args);
    static v8::Handle<v8::Value> Push(const v8::Arguments &args);
    static v8::Handle<v8::Value> EndPush(const v8::Arguments &args);
    static v8::Handle<v8::Value> SetOutputFile(const v8::Arguments &args);
    static v8::Handle<v8::Value> SetQuality(const v8::Arguments &args);
    static v8::Handle<v8::Value> SetFrameRate(const v8::Arguments &args);
    static v8::Handle<v8::Value> SetKeyFrameInterval(const v8::Arguments &args);
    static v8::Handle<v8::Value> End(const v8::Arguments &args);
};

#endif

