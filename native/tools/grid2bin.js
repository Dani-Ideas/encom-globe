#!/usr/bin/env node
// Converts grid.js (var grid = {tiles: [...]}) into a compact little-endian binary:
//   uint32 tileCount
//   per tile: float32 lat, float32 lon, uint32 vertexCount, float32 b[6][3] (zero-padded)

var vm = require('vm'),
    fs = require('fs');

var input = process.argv[2], output = process.argv[3];
if (!input || !output) {
    console.error('usage: grid2bin.js <grid.js> <grid.bin>');
    process.exit(1);
}

var grid = vm.runInNewContext(fs.readFileSync(input, 'utf8') + ';grid', {});
var tiles = grid.tiles;
var stride = 4 * (2 + 1 + 18);
var buf = Buffer.alloc(4 + tiles.length * stride);

buf.writeUInt32LE(tiles.length, 0);
tiles.forEach(function(t, i){
    var o = 4 + i * stride;
    // grid.js stores latitude with the opposite sign (hexasphere's convention); flip
    // it here so the native side can use real latitudes (the highlight relies on it).
    buf.writeFloatLE(-t.lat, o);
    buf.writeFloatLE(t.lon, o + 4);
    buf.writeUInt32LE(t.b.length, o + 8);
    for (var j = 0; j < Math.min(t.b.length, 6); j++) {
        buf.writeFloatLE(t.b[j].x, o + 12 + j * 12);
        buf.writeFloatLE(t.b[j].y, o + 16 + j * 12);
        buf.writeFloatLE(t.b[j].z, o + 20 + j * 12);
    }
});

fs.writeFileSync(output, buf);
console.log('grid2bin: ' + tiles.length + ' tiles -> ' + output);
