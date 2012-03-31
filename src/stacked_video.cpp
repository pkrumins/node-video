#include <cstdlib>
#include <node_buffer.h>
#include <node_version.h>
#include "common.h"
#include "stacked_video.h"

using namespace v8;
using namespace node;

StackedVideo::StackedVideo(int wwidth, int hheight) :
    width(wwidth), height(hheight), videoEncoder(wwidth, hheight),
    lastFrame(NULL), lastTimeStamp(0) {}

StackedVideo::~StackedVideo()
{
    free(lastFrame);
}

void
StackedVideo::Initialize(Handle<Object> target)
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
StackedVideo::NewFrame(const unsigned char *data, unsigned long timeStamp)
{
    HandleScope scope;

    if (!lastFrame) {
        lastFrame = (unsigned char *)malloc(width*height*3);
        if (!lastFrame)
            return VException("malloc failed in StackedVideo::NewFrame.");
    }

    if (lastTimeStamp != 0 && timeStamp > 0)
        videoEncoder.dupFrame(lastFrame, timeStamp-lastTimeStamp);

    videoEncoder.newFrame(data);

    memcpy(lastFrame, data, width*height*3);
    lastTimeStamp = timeStamp;

    return Undefined();
}

Handle<Value>
StackedVideo::Push(unsigned char *rect, int x, int y, int w, int h)
{
    HandleScope scope;

    if (!lastFrame) {
       if (x==0 && y==0 && w==width && h==height) {
           lastFrame = (unsigned char *)malloc(width*height*3);
           if (!lastFrame) 
               return VException("malloc failed in StackedVideo::Push.");
           memcpy(lastFrame, rect, width*height*3);
           return Undefined();
        }
        return VException("The first full frame was not pushed.");
    }

    updates.push_back(Update(rect, x, y, w, h));

    return Undefined();
}

Handle<Value>
StackedVideo::EndPush(unsigned long timeStamp)
{
    HandleScope scope;

    if (!lastFrame)
        return VException("The first full frame was not pushed.");

    if (lastTimeStamp != 0 && timeStamp > 0)
        videoEncoder.dupFrame(lastFrame, timeStamp-lastTimeStamp);

    for (VectorUpdateIterator it = updates.begin();
        it != updates.end();
        ++it)
    {
        const Update &update = *it;

        int start = (update.y)*width*3 + (update.x)*3;
        const unsigned char *updatep = &(update.rect[0]);
        for (int i = 0; i < update.h; i++) {
            unsigned char *framep = lastFrame + start + i*width*3;
            for (int j = 0; j < update.w; j++) {
                *framep++ = *updatep++;
                *framep++ = *updatep++;
                *framep++ = *updatep++;
            }
        }
    }
    updates.clear();

    videoEncoder.newFrame(lastFrame);

    lastTimeStamp = timeStamp;

    return Undefined();
}

void
StackedVideo::SetOutputFile(const char *fileName)
{
    videoEncoder.setOutputFile(fileName);
}

void
StackedVideo::SetQuality(int quality)
{
    videoEncoder.setQuality(quality);
}

void
StackedVideo::SetFrameRate(int frameRate)
{
    videoEncoder.setFrameRate(frameRate);
}

void
StackedVideo::SetKeyFrameInterval(int keyFrameInterval)
{
    videoEncoder.setKeyFrameInterval(keyFrameInterval);
}

void
StackedVideo::End()
{
    videoEncoder.end();
}

Handle<Value>
StackedVideo::New(const Arguments &args)
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

Handle<Value>
StackedVideo::NewFrame(const Arguments &args)
{
    HandleScope scope;

    if (args.Length() < 1)
        return VException("One argument required - Buffer with full frame data.");

    if (!Buffer::HasInstance(args[0])) 
        return VException("First argument must be Buffer.");

    unsigned long timeStamp = 0;

    if (args.Length() == 2) {
        if (!args[1]->IsNumber())
            return VException("Second argument (if present) must be int64 timestamp (measured in milliseconds).");

        timeStamp = args[1]->IntegerValue();
        if (timeStamp < 0)
            return VException("Timestamp can't be negative.");
    }

#if NODE_VERSION_AT_LEAST(0,3,0)
    v8::Handle<v8::Object> rgb = args[0]->ToObject();
#else
    Buffer *rgb = ObjectWrap::Unwrap<Buffer>(args[0]->ToObject());
#endif

    StackedVideo *sv = ObjectWrap::Unwrap<StackedVideo>(args.This());

#if NODE_VERSION_AT_LEAST(0,3,0)
    sv->NewFrame((unsigned char *) Buffer::Data(rgb), timeStamp);
#else
    sv->NewFrame((unsigned char *)rgb->data(), timeStamp);
#endif

    return Undefined();
}

Handle<Value>
StackedVideo::Push(const Arguments &args)
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
#if NODE_VERSION_AT_LEAST(0,3,0)
    v8::Handle<v8::Object> rgb = args[0]->ToObject();
#else
    Buffer *rgb = ObjectWrap::Unwrap<Buffer>(args[0]->ToObject());
#endif
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

#if NODE_VERSION_AT_LEAST(0,3,0)
    sv->Push((unsigned char *) Buffer::Data(rgb), x, y, w, h);
#else
    sv->Push((unsigned char *)rgb->data(), x, y, w, h);
#endif

    return Undefined();
}

Handle<Value>
StackedVideo::EndPush(const Arguments &args)
{
    HandleScope scope;

    unsigned long timeStamp = 0;

    if (args.Length() == 1) {
        if (!args[0]->IsNumber())
            return VException("First argument (if present) must be int64 timestamp (measured in milliseconds).");

        timeStamp = args[0]->IntegerValue();

        if (timeStamp < 0)
            return VException("Timestamp can't be negative.");
    }

    StackedVideo *sv = ObjectWrap::Unwrap<StackedVideo>(args.This());
    sv->EndPush(timeStamp);

    return Undefined();
}

Handle<Value>
StackedVideo::SetOutputFile(const Arguments &args)
{
    HandleScope scope;

    if (args.Length() != 1)
        return VException("One argument required - output file name.");

    String::AsciiValue fileName(args[0]->ToString());

    StackedVideo *sv = ObjectWrap::Unwrap<StackedVideo>(args.This());
    sv->SetOutputFile(*fileName);

    return Undefined();
}

Handle<Value>
StackedVideo::SetQuality(const Arguments &args)
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

Handle<Value>
StackedVideo::SetFrameRate(const Arguments &args)
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

Handle<Value>
StackedVideo::SetKeyFrameInterval(const Arguments &args)
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

Handle<Value>
StackedVideo::End(const Arguments &args)
{
    HandleScope scope;

    StackedVideo *sv = ObjectWrap::Unwrap<StackedVideo>(args.This());
    sv->End();

    return Undefined();
}

