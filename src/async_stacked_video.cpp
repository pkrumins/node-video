#include <cstdlib>
#include <node_buffer.h>
#include "common.h"
#include "utils.h"
#include "async_stacked_video.h"

#include "loki/ScopeGuard.h"

using namespace v8;
using namespace node;

AsyncStackedVideo::AsyncStackedVideo(int wwidth, int hheight) :
    width(wwidth), height(hheight), videoEncoder(wwidth, hheight),
    push_id(0), fragment_id(0) {}

#if NODE_VERSION_AT_LEAST(0,6,0)
void
#else
int
#endif
AsyncStackedVideo::Initialize(Handle<Object> target)
{
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);
    t->InstanceTemplate()->SetInternalFieldCount(1);
    NODE_SET_PROTOTYPE_METHOD(t, "push", Push);
    NODE_SET_PROTOTYPE_METHOD(t, "endPush", EndPush);
    NODE_SET_PROTOTYPE_METHOD(t, "setOutputFile", SetOutputFile);
    NODE_SET_PROTOTYPE_METHOD(t, "setQuality", SetQuality);
    NODE_SET_PROTOTYPE_METHOD(t, "setFrameRate", SetFrameRate);
    NODE_SET_PROTOTYPE_METHOD(t, "setKeyFrameInterval", SetKeyFrameInterval);
    NODE_SET_PROTOTYPE_METHOD(t, "setTmpDir", SetTmpDir);
    NODE_SET_PROTOTYPE_METHOD(t, "encode", Encode);
    target->Set(String::NewSymbol("AsyncStackedVideo"), t->GetFunction());
}

#if NODE_VERSION_AT_LEAST(0,6,0)
void
#else
int
#endif
AsyncStackedVideo::EIO_Push(eio_req *req)
{
    push_request *push_req = (push_request *)req->data;

    if (!is_dir(push_req->tmp_dir)) {
        if (mkdir(push_req->tmp_dir, 0775) == -1) {
            // there is no way to return this error to node as this call was
            // async with no callback
            fprintf(stderr, "Could not mkdir(%s) in AsyncStackedVideo::EIO_Push.\n",
                push_req->tmp_dir);
            #if NODE_VERSION_AT_LEAST(0,6,0)
            return;
            #else
            return 0;
            #endif
        }
    }

    char fragment_dir[512];
    snprintf(fragment_dir, 512, "%s/%d", push_req->tmp_dir, push_req->push_id);

    if (!is_dir(fragment_dir)) {
        if (mkdir(fragment_dir, 0775) == -1) {
            fprintf(stderr, "Could not mkdir(%s) in AsyncStackedVideo::EIO_Push.\n",
                fragment_dir);
            #if NODE_VERSION_AT_LEAST(0,6,0)
            return;
            #else
            return 0;
            #endif
        }
    }

    char filename[512];
    snprintf(filename, 512, "%s/%d/rect-%d-%d-%d-%d-%d.dat",
        push_req->tmp_dir, push_req->push_id, push_req->fragment_id,
        push_req->x, push_req->y, push_req->w, push_req->h);
    FILE *out = fopen(filename, "w+");
    LOKI_ON_BLOCK_EXIT(fclose, out);
    if (!out) {
        fprintf(stderr, "Failed to open %s in AsyncStackedVideo::EIO_Push.\n",
            filename);
        #if NODE_VERSION_AT_LEAST(0,6,0)
        return;
        #else
        return 0;
        #endif
    }
    int written = fwrite(push_req->data, sizeof(unsigned char), push_req->data_size, out);
    if (written != push_req->data_size) {
        fprintf(stderr, "Failed to write all data to %s. Wrote only %d of %d.\n",
            filename, written, push_req->data_size);
    }

    #if NODE_VERSION_AT_LEAST(0,6,0)
    return;
    #else
    return 0;
    #endif
}

int
AsyncStackedVideo::EIO_PushAfter(eio_req *req)
{
    ev_unref(EV_DEFAULT_UC);

    push_request *push_req = (push_request *)req->data;
    free(push_req->data);
    free(push_req);

    return 0;
}

Handle<Value>
AsyncStackedVideo::Push(unsigned char *rect, int x, int y, int w, int h)
{
    HandleScope scope;

    if (tmp_dir.empty())
        throw "Tmp dir is not set. Use .setTmpDir to set it before pushing.";

    push_request *push_req = (push_request *)malloc(sizeof(*push_req));
    if (!push_req)
        throw "malloc in AsyncStackedVideo::Push failed.";

    push_req->data = (unsigned char *)malloc(sizeof(*push_req->data)*w*h*3);
    if (!push_req->data) {
        free(push_req);
        throw "malloc in AsyncStackedVideo::Push failed.";
    }

    memcpy(push_req->data, rect, w*h*3);
    push_req->push_id = push_id;
    push_req->fragment_id = fragment_id++;
    push_req->tmp_dir = tmp_dir.c_str();
    push_req->data_size = w*h*3;
    push_req->x = x;
    push_req->y = y;
    push_req->w = w;
    push_req->h = h;

    eio_custom(EIO_Push, EIO_PRI_DEFAULT, EIO_PushAfter, push_req);
    ev_ref(EV_DEFAULT_UC);

    return Undefined();
}

void
AsyncStackedVideo::EndPush(unsigned long timeStamp)
{
    push_id++;
    fragment_id = 0;
}

void
AsyncStackedVideo::SetOutputFile(const char *fileName)
{
    videoEncoder.setOutputFile(fileName);
}

void
AsyncStackedVideo::SetQuality(int quality)
{
    videoEncoder.setQuality(quality);
}

void
AsyncStackedVideo::SetFrameRate(int frameRate)
{
    videoEncoder.setFrameRate(frameRate);
}

void
AsyncStackedVideo::SetKeyFrameInterval(int keyFrameInterval)
{
    videoEncoder.setKeyFrameInterval(keyFrameInterval);
}

Handle<Value>
AsyncStackedVideo::New(const Arguments &args)
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

    AsyncStackedVideo *video = new AsyncStackedVideo(w, h);
    video->Wrap(args.This());
    return args.This();
}

Handle<Value>
AsyncStackedVideo::Push(const Arguments &args)
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

    AsyncStackedVideo *video = ObjectWrap::Unwrap<AsyncStackedVideo>(args.This());
    v8::Handle<v8::Object> rgb = args[0]->ToObject();
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
    if (x >= video->width) 
        return VException("Coordinate x exceeds AsyncStackedVideo's dimensions.");
    if (y >= video->height) 
        return VException("Coordinate y exceeds AsyncStackedVideo's dimensions.");
    if (x+w > video->width) 
        return VException("Pushed buffer exceeds AsyncStackedVideo's width.");
    if (y+h > video->height) 
        return VException("Pushed buffer exceeds AsyncStackedVideo's height.");

    try {
        video->Push((unsigned char *) Buffer::Data(rgb), x, y, w, h);
    }
    catch (const char *err) {
        return VException(err);
    }

    return Undefined();
}

Handle<Value>
AsyncStackedVideo::EndPush(const Arguments &args)
{
    HandleScope scope;

    AsyncStackedVideo *video = ObjectWrap::Unwrap<AsyncStackedVideo>(args.This());
    video->EndPush();

    return Undefined();
}

Handle<Value>
AsyncStackedVideo::SetOutputFile(const Arguments &args)
{
    HandleScope scope;

    if (args.Length() != 1)
        return VException("One argument required - output file name.");

    String::AsciiValue fileName(args[0]->ToString());

    AsyncStackedVideo *video = ObjectWrap::Unwrap<AsyncStackedVideo>(args.This());
    video->SetOutputFile(*fileName);

    return Undefined();
}

Handle<Value>
AsyncStackedVideo::SetQuality(const Arguments &args)
{
    HandleScope scope;

    if (args.Length() != 1) 
        return VException("One argument required - video quality.");

    if (!args[0]->IsInt32())
        return VException("Quality must be integer.");

    int q = args[0]->Int32Value();

    if (q < 0) return VException("Quality smaller than 0.");
    if (q > 63) return VException("Quality greater than 63.");

    AsyncStackedVideo *video = ObjectWrap::Unwrap<AsyncStackedVideo>(args.This());
    video->SetQuality(q);

    return Undefined();
}

Handle<Value>
AsyncStackedVideo::SetFrameRate(const Arguments &args)
{
    HandleScope scope;

    if (args.Length() != 1) 
        return VException("Two argument required - frame rate.");

    if (!args[0]->IsInt32())
        return VException("Frame rate must be integer.");

    int rate = args[0]->Int32Value();

    AsyncStackedVideo *video = ObjectWrap::Unwrap<AsyncStackedVideo>(args.This());
    video->SetFrameRate(rate);

    return Undefined();
}

Handle<Value>
AsyncStackedVideo::SetKeyFrameInterval(const Arguments &args)
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

    AsyncStackedVideo *video = ObjectWrap::Unwrap<AsyncStackedVideo>(args.This());
    video->SetKeyFrameInterval(interval);

    return Undefined();
}

Handle<Value>
AsyncStackedVideo::SetTmpDir(const Arguments &args)
{
    HandleScope scope;

    if (args.Length() != 1)
        return VException("One argument required - path to tmp dir.");

    if (!args[0]->IsString())
        return VException("First argument must be string.");

    String::AsciiValue tmp_dir(args[0]->ToString());

    AsyncStackedVideo *video = ObjectWrap::Unwrap<AsyncStackedVideo>(args.This());
    video->tmp_dir = *tmp_dir;

    return Undefined();
}

int
fragment_sort(const void *aa, const void *bb)
{
    const char *a = *(const char **)aa;
    const char *b = *(const char **)bb;
    int na, nb;
    sscanf(a, "rect-%d", &na);
    sscanf(b, "rect-%d", &nb);
    return na > nb;
}

void
AsyncStackedVideo::push_fragment(unsigned char *frame, int width, int height,
    unsigned char *fragment, int x, int y, int w, int h)
{
    int start = y*width*3 + x*3;
    unsigned char *fragmentp = fragment;

    for (int i = 0; i < h; i++) {
        unsigned char *framep = &frame[start + i*width*3];
        for (int j = 0; j < w; j++) {
            *framep++ = *fragment++;
            *framep++ = *fragment++;
            *framep++ = *fragment++;
        }
    }
}

Rect
AsyncStackedVideo::rect_dims(const char *fragment_name)
{
    int moo, x, y, w, h;
    sscanf(fragment_name, "rect-%d-%d-%d-%d-%d", &moo, &x, &y, &w, &h);
    return Rect(x, y, w, h);
}


#if NODE_VERSION_AT_LEAST(0,6,0)
void
#else
int
#endif
AsyncStackedVideo::EIO_Encode(eio_req *req)
{
    async_encode_request *enc_req = (async_encode_request *)req->data;
    AsyncStackedVideo *video = (AsyncStackedVideo *)enc_req->video_obj;

    unsigned char *frame = (unsigned char *)malloc(sizeof(*frame)*video->width*video->height*3);
    LOKI_ON_BLOCK_EXIT(free, frame);

    for (size_t push_id = 0; push_id < video->push_id; push_id++) {
        char fragment_path[512];
        snprintf(fragment_path, 512, "%s/%d", video->tmp_dir.c_str(), push_id);
        if (!is_dir(fragment_path)) {
            char error[600];
            snprintf(error, 600, "Error in AsyncStackedVideo::EIO_Encode %s is not a dir.",
                fragment_path);
            enc_req->error = strdup(error);
            #if NODE_VERSION_AT_LEAST(0,6,0)
            return;
            #else
            return 0;
            #endif
        }

        char **fragments = find_files(fragment_path);
        LOKI_ON_BLOCK_EXIT(free_file_list, fragments);
        int nfragments = file_list_length(fragments);

        qsort(fragments, nfragments, sizeof(char *), fragment_sort);

        if (!frame) {
            enc_req->error = strdup("malloc failed in AsyncStackedVideo::EIO_Encode.");
            #if NODE_VERSION_AT_LEAST(0,6,0)
            return;
            #else
            return 0;
            #endif
        }

        for (int i = 0; i < nfragments; i++) {
            snprintf(fragment_path, 512, "%s/%d/%s",
                video->tmp_dir.c_str(), push_id, fragments[i]);
            FILE *in = fopen(fragment_path, "r");
            if (!in) {
                char error[600];
                snprintf(error, 600, "Failed opening %s in AsyncStackedVideo::EIO_Encode.",
                    fragment_path);
                enc_req->error = strdup(error);
                #if NODE_VERSION_AT_LEAST(0,6,0)
                return;
                #else
                return 0;
                #endif
            }
            LOKI_ON_BLOCK_EXIT(fclose, in);
            int size = file_size(fragment_path);
            unsigned char *data = (unsigned char *)malloc(sizeof(*data)*size);
            LOKI_ON_BLOCK_EXIT(free, data);
            int read = fread(data, sizeof *data, size, in);
            if (read != size) {
                char error[600];
                snprintf(error, 600, "Error - should have read %d but read only %d from %s in AsyncStackedVideo::EIO_Encode", size, read, fragment_path);
                enc_req->error = strdup(error);
                #if NODE_VERSION_AT_LEAST(0,6,0)
                return;
                #else
                return 0;
                #endif
            }
            Rect dims = rect_dims(fragments[i]);
            push_fragment(frame, video->width, video->height,
                data, dims.x, dims.y, dims.w, dims.h);
        }
        video->videoEncoder.newFrame(frame);
    }
    video->videoEncoder.end();

    #if NODE_VERSION_AT_LEAST(0,6,0)
    return;
    #else
    return 0;
    #endif
}

int
AsyncStackedVideo::EIO_EncodeAfter(eio_req *req)
{
    HandleScope scope;

    ev_unref(EV_DEFAULT_UC);
    async_encode_request *enc_req = (async_encode_request *)req->data;

    Handle<Value> argv[2];

    if (enc_req->error) {
        argv[0] = False();
        argv[1] = ErrorException(enc_req->error);
    }
    else {
        argv[0] = True();
        argv[1] = Undefined();
    }

    TryCatch try_catch; // don't quite see the necessity of this

    enc_req->callback->Call(Context::GetCurrent()->Global(), 2, argv);

    if (try_catch.HasCaught())
        FatalException(try_catch);

    enc_req->callback.Dispose();

    enc_req->video_obj->Unref();
    free(enc_req);

    return 0;
}


Handle<Value>
AsyncStackedVideo::Encode(const Arguments &args)
{
    HandleScope scope;

    if (args.Length() != 1)
        return VException("One argument required - callback function.");

    if (!args[0]->IsFunction())
        return VException("First argument must be a function.");

    Local<Function> callback = Local<Function>::Cast(args[0]);
    AsyncStackedVideo *video = ObjectWrap::Unwrap<AsyncStackedVideo>(args.This());

    async_encode_request *enc_req = (async_encode_request *)malloc(sizeof(*enc_req));
    if (!enc_req)
        return VException("malloc in AsyncStackedVideo::Encode failed.");

    enc_req->callback = Persistent<Function>::New(callback);
    enc_req->video_obj = video;
    enc_req->error = NULL;

    eio_custom(EIO_Encode, EIO_PRI_DEFAULT, EIO_EncodeAfter, enc_req);

    ev_ref(EV_DEFAULT_UC);
    video->Ref();

    return Undefined();
}

