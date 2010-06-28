This is a node.js module, writen in C++, that produces theora video from the
given RGB/RGBA buffers. The module may get more video formats in the future.

It was written by Peteris Krumins (peter@catonmat.net).
His blog is at http://www.catonmat.net  --  good coders code, great reuse.

------------------------------------------------------------------------------

This module exports several objects that you can work with:

    * FixedVideo - to create videos from fixed size frames

    // these are not there yet, still hacking them in right now.
    // * StackedVideo - to create videos from fragmented frames
    // * StreamingVideo - to create streamable videos (works with HTML5 <video>)


FixedVideo
----------

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

Now, to start writing video, call `newFrame` method with frames sequentially.
Frames must be RGBA nodejs Buffer objects.

    video.newFrame(rgba_frame);

FixedVideo is lazy by itself and will write headers of the video only after
receiving the first frame, so the first frame may take longer to encode than
subsequent, because there is a lot of initialization going on.

If at any time you're done writing video, call the `end` method,

    video.end();

This will close all open files and free resources. But you can also leave it
to garbage collector. If `video` goes out of scope, it also closes the video
file and frees all resources.


StackedVideo
------------

Coming near you soon.


StreamingVideo
--------------

Also coming near you soon. This is the most awesome stuff!


Other stuff in this module
--------------------------

The discovery/ directory contains all the snippets I wrote to understand how
to get video working. It's a habit of effective hackers to try lots of small
things out until you get the whole picture of how things should work. I call
it "the hacker's approach," where you hack stuff up quickly without any
understanding, and then rewrite it to produce working modules.

I also tried libx264 but since it was only supported by Chrome, I went with
libtheora. Maybe I'll add libx264 later as it gets support from more browsers.

This library was written for my and substack's StackVM startup.

------------------------------------------------------------------------------

Happy videoing!


Sincerely,
Peteris Krumins
http://www.catonmat.net

