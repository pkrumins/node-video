//var Video = require('video');
var http = require('http');
var fs = require('fs');
var sys = require('sys');
var Buffer = require('buffer').Buffer;

var frameFiles = fs.readdirSync('./frames-full').sort();

var currFrame = 0;
function nextFrame () {
    if (currFrame >= frameFiles.length)
        currFrame = 0;
    return frameFiles[currFrame++];
}

//var video = new Video(720, 400);
// video.setEncoding('video/mp4');

function getRange (rangeString) {
    if (rangeString) {
        var m = r.match(/bytes=(\d+)-(\d+)?/);
        if (m) return { lo: m[1], hi: m[2] };
    }
}

http.createServer(function (req, res) {
    sys.log(req.url);
    if (req.url == '/') {
        res.writeHead(200, {
            'Content-Type': 'text/html',
        });
        res.write(fs.readFileSync('./index-full-frames.html'));
        res.end();
    }
    else if (req.url == '/video.ogv') {
        sys.log(req.headers.range);
        res.writeHead(200, {
            'Content-Type': 'video/ogg'
        });
        var contents = fs.readFileSync('./video2.ogv', 'binary');
        res.write(contents, 'binary');
        res.end();
    }
    else if (req.url == '/video.mp4') {
        var range = getRange(req.headers.range);

        if (range) {
            if (range.hi) {
                var len = range.hi-range.lo+1;
                var fd = fs.openSync('./video.mp4', 'r');

                var contents = fs.readFileSync('./video.mp4', 'binary');

                /*
                res.writeHead(206, {
                    'Content-Type': 'video/mp4', // video.getEncoding();
                    'Content-Length': len,
                    'Content-Range': 'bytes ' + range.lo + '-' + range.hi + '/18317',
                    'Accept-Ranges': 'bytes'
                });
                */
                res.writeHead(206, {
                    'Content-Type': 'video/mp4', // video.getEncoding();
                    'Content-Length': len,
                    'Content-Range': 'bytes ' + range.lo + '-' + range.hi + '/18317',
                    'Accept-Ranges': 'bytes'
                });

                var slice = contents.slice(range.lo, parseInt(range.hi)+1);
                res.write(slice, 'binary');
            }
            else {
                var contents = fs.readFileSync('./video.mp4', 'binary');
                res.writeHead(206, {
                    'Content-Type': 'video/mp4', // video.getEncoding();
                    'Content-Length': contents.length-range.lo,
                    'Accept-Ranges': 'bytes'
                });
                res.write(contents.slice(range.lo), 'binary');
            }
        }
        else {
            var vid = fs.readFileSync('./video.mp4', 'binary');
            res.writeHead(200, {
                'Content-Type': 'video/mp4', // video.getEncoding();
            });
            res.write(vid, 'binary');
        }

        //for (var i=0; i<60; i++) {
        //    res.write(video.newFrame(nextFrame()), 'binary');
        //    setTimeout(function(){},1000);
        //}

        res.end();
    }
}).listen(9000);

