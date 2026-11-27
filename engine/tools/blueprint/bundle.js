#!/usr/bin/env node
'use strict';
const fs = require('node:fs'), path = require('node:path');
const at = n => fs.readFileSync(path.join(__dirname, n), 'utf8');
let html = at('index.html').replace('<link rel="stylesheet" href="studio.css">', '<style>' + at('studio.css') + '</style>');
html = html.replace(/<script src="([^"]+)"><\/script>/g, (_, name) => '<script>' + at(name).replace(/<\/script/gi, '<\\/script') + '</script>');
const target = process.argv[2] || path.join(__dirname, 'NexgenBlueprintStudio.html');
fs.writeFileSync(target, html);
console.log(target);
