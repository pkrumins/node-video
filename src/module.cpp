#include <node.h>

#include "fixed_video.h"
#include "stacked_video.h"
#include "async_stacked_video.h"

extern "C" void
init(v8::Handle<v8::Object> target)
{
    v8::HandleScope scope;

    FixedVideo::Initialize(target);
    StackedVideo::Initialize(target);
    AsyncStackedVideo::Initialize(target);
}

