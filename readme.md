
This is a node.js module, writen in C++, that produces Theora/Ogg videos from
the given RGB buffers.

It was written by Peteris Krumins (peter@catonmat.net).
His blog is at http://www.catonmat.net  --  good coders code, great reuse.

------------------------------------------------------------------------------

This module exports several objects that you can work with:

    * FixedVideo - to create videos from fixed size frames
    * StackedVideo - to create videos from fragmented frames (stack them together)
    * AsyncStackedVideo - same as StackedVideo but asynchronous

    // these are not there yet, still hacking them in right now.
    // * StreamingVideo - to create streamable videos (works with HTML5 <video>)

##FixedVideo

FixedVideo object is for creating videos from fixed size frames. That is,
each frame is exactly the same size, for example, each frame is 720x400 pixels.

Here is how to use FixedVideo. First you need to create a new instance of this
object. The constructor takes two arguments `width` and `height` of the video:

    var video = new FixedVideo(width, height);

Next, you need to set the output file this video will be written to. This is
done via `setOutputFile` method, it can be relative or absolute path. If nodejs
doesn't have the necessary permissions to write the file, it will throw an
exception as soon as you submit the first frame. Here is how you use setOutputFile:

    video.setOutputFile('./cool_video.ogv');

The .ogv extension stands for ogg-video.

Then you can also change the quality of the video via `setQuality` method. The
quality must be between 0-63, where 0 is the worst quality and 63 is the best.
The default quality is 31.

    video.setQuality(63);   // best video quality

You can also change the frame rate with `setFrameRate`. The default is 25fps,
to change it do this:

    video.setFrameRate(50);  // frame rate is now 50 fps

The keyframe interval can also be controlled. Use `setKeyFrameInterval` to set it.
It must be a power of two:

    video.setKeyFrameInterval(128);  // keyframe every 128 frames

Important: All of the above options should be set before submitting the first
frame.

Now, to start writing video, call `newFrame` method with frames sequentially.
Frames must be RGB nodejs Buffer objects.

    video.newFrame(rgb_frame);

FixedVideo is lazy by itself and will write headers of the video only after
receiving the first frame, so the first frame may take longer to encode than
subsequent, because there is a lot of initialization going on.

If at any time you're done writing video, call the `end` method,

    video.end();

This will close all open files and free resources. But you can also leave it
to garbage collector. If `video` goes out of scope, it also closes the video
file and frees all resources.


##StackedVideo

StackedVideo object is for stacking many small frame updates together and then
encoding the frame as a whole. Here is how it works. The first frame sent to
StackedVideo must be a full frame (the width and height must match video's
width and height). Next, you can either send another full frame for encoding
or update parts of the last frame. It's useful in a situation like doing a
screen recording, when only one smart part of the screen updates, you redraw
just that portion and nothing else.

Must of the usage is just like you'd use FixedVideo object.

First create a StackedVideo object:

    var stackedVideo = new StackedVideo(width, height);

Then set the output file:

    stackedVideo.setOutputFile('./screencast.ogv');

Then set the quality, framerate, keyframe interval, via `setQuality`,
`setFrameRate`, `setKeyFrameInterval` methods.

Now you have to submit a full frame to StackedVideo, do it via regular
`newFrame` method:

    stackedVideo.newFrame(rgb_frame);

This will encode this frame, and remember it. Now you can use `push` method
to push an update to the frame. The usage is as following:

    stackedVideo.push(rgb_rectangle, x, y, width, height);

This will put the rectangle of width x height at position (x, y). Make sure
dimensions don't overflow or you'll get an exception. You can also push the
first full frame with this method instead of using newFrame, make sure that
(x,y) = (0,0) and width, height are video's width, height.

After you're done pushing all the updates you wanted, call `endPush`. This
will encode the frame (and keep the previous frame in memory, so you can `push`
more stuff):

    stackedVideo.endPush();

Stacked videos can also duplicate previous frames cheaply to imitate VFR (variable
frame rate). Pass millisecond argument to `endPush` to make it duplicate the previous
for the right amount of time. Here is what I mean,

If you call,

    stackedVideo.endPush((new Date).getTime());

every time, then the previous frame will be duplicated the right number of times
so that video played at the right framerate.

When you're totally done with encoding, call the `end` method:

    stackedVideo.end();

That will close all the file handles and free memory. Alternatively you can let
the `stackedVideo` object go out of scope, which will have the same effect.


##AsyncStackedVideo

AsyncStackedVideo is the same as StackedVideo except it's asynchronous.

    var asyncVideo = new AsyncStackedVideo(width, height);
    asyncVideo.setOutputFile('./video.ogv');
    
To use it you must specify the temporary directory for fragments (it writes them
asynchronously to disk):

    asyncVideo.setTmpDir('/tmp/foo');

Next you .push fragments to it, and after you're done with one frame,
you call .endPush.

Then when you're totally done with all the frames, call .encode and pass it a
callback function, which will be called once the encoding is done:

    asyncVideo.encode(function (ok, error) {
        if (ok) {
            // video was written to the file you set by .setOutputFile
        }
        else {
            // failure, examine 'error'
        }
    });


##StreamingVideo

Also coming near you soon. This is the most awesome stuff!


##How to compile?

You need node.js installed to compile this module. When installed it comes with
node-waf tool, run it in this libs dir:

    node-waf configure build

This will produce video.node dll. After that, make sure NODE_PATH contains lib's
dir. 

## Installation

    npm install node-video [-g]

##Other stuff in this module

The discovery/ directory contains all the snippets I wrote to understand how
to get video working. It's a habit of effective hackers to try lots of small
things out until you get the whole picture of how things should work. I call
it "the hacker's approach," where you hack stuff up quickly without any
understanding, and then rewrite it to produce working modules.

I also tried libx264 but since it was only supported by Chrome, I went with
libtheora. Maybe I'll add libx264 later as it gets support from more browsers.

This library was written for my and SubStack's StackVM startup.

------------------------------------------------------------------------------

Happy videoing!


Sincerely,
Peteris Krumins
http://www.catonmat.net

## Contributors

* Node v0.3 buffers (James Halliday substack)
* Node v0.6 compatibility (Pascal Deschenes <pdeschen at gmail dot com>)
