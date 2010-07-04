//var Video = require('video');
var http = require('http');
var fs = require('fs');
var sys = require('sys');
var Buffer = require('buffer').Buffer;

var frameFiles = fs.readdirSync('./theora-frames-full').sort();

var currFrame = 0;
function nextFrame () {
    if (currFrame >= frameFiles.length)
        currFrame = 0;
    return frameFiles[currFrame++];
}

//var video = new Video(720, 400);
// video.setEncoding('video/ogg');

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
        res.write(fs.readFileSync('./theora-frames-full.html'));
        res.end();
    }
    else if (req.url == '/video.ogv') {
        res.writeHead(200, {
            'Content-Type': 'video/ogg'
        });

        for (var i = 0; i < frameFiles.length; i++) {
            (function (j) {
                setTimeout(function () {
                    var file = frameFiles[j];
                    var fileContents = fs.readFileSync('./theora-frames-full/' + file, 'binary');
                    res.write(fileContents, 'binary');
                    sys.log(file);
                }, 100*j);
                if (j == frameFiles.length-1) {
                    setTimeout(function () {
                        res.end();
                    }, 100*(j+1));
                }
            })(i);
        }
    }
}).listen(8000);

