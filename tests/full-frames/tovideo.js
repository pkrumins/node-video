var VideoLib = require('video');
var Buffer = require('buffer').Buffer;
var fs = require('fs');
var sys = require('sys');

var fileRx = new RegExp(/^terminal-(\d+).rgba/);

var files = fs.readdirSync('.').sort().filter(
    function (f) { return fileRx.test(f) }
);

function baseName(fileName) {
    return fileName.slice(0, fileName.indexOf('.'));
}

var fixedVideo = new VideoLib.FixedVideo(720, 400); // width, height, quality
fixedVideo.setOutputFile('video.ogv');

var buf = new Buffer(1152000);
files.forEach(function (file) {
    var terminal = fs.readFileSync(file, 'binary');
    buf.write(terminal, 'binary');
    fixedVideo.newFrame(buf);
});

fixedVideo.end();

