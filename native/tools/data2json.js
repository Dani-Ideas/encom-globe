#!/usr/bin/env node
// Converts data.js (var data = [{lat, lng, label}, ...]) to plain JSON.

var vm = require('vm'),
    fs = require('fs');

var input = process.argv[2], output = process.argv[3];
if (!input || !output) {
    console.error('usage: data2json.js <data.js> <data.json>');
    process.exit(1);
}

var data = vm.runInNewContext(fs.readFileSync(input, 'utf8') + ';data', {});
fs.writeFileSync(output, JSON.stringify(data));
console.log('data2json: ' + data.length + ' pins -> ' + output);
