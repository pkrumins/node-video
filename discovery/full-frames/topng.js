var Png = require('png').Png;
var Buffer = require('buffer').Buffer;
var fs = require('fs');

var fileRx = new RegExp(/^terminal-(\d+).rgba/);

var files = fs.readdirSync('.').sort().filter(
    function (f) { return fileRx.test(f) }
);

function baseName(fileName) {
    return fileName.slice(0, fileName.indexOf('.'));
}

var buf = new Buffer(1152000);
files.forEach(function (file) {
    var terminal = fs.readFileSync(file, 'binary');
    buf.write(terminal, 'binary');
    var png = new Png(buf, 720, 400);
    fs.writeFileSync(baseName(file) + '.png', png.encode(), 'binary');
});

