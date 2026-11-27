#!/usr/bin/env node
'use strict';
const fs = require('node:fs'), path = require('node:path'), B = require('./blueprint'), P=require('./project');
function help() { console.log(`Nexgen Blueprint CLI\n  node cli.js check GRAPH.bp.json\n  node cli.js compile GRAPH.bp.json --out OUTPUT.as [--force]\n  node cli.js screen GRAPH.bp.json --out MEDIA/showcase/SCREEN_ID [--force] [--debug]\n  node cli.js project GAME.bp.project.json --out GAME.zip [--force] [--debug]\n\ncheck    Validate the graph without writing any files.\ncompile  Write ordinary AngelScript, including imported Behaviour components.\nscreen   Write main.as and (if missing) screen.json. Use a new screen folder.\n--force  Allow replacing an existing main.as or output script. Existing screen.json is preserved.\nproject  Compile all screens and package assets and editable graphs.\n--debug  Add node trace instrumentation and debugger source maps; use release for gameplay.\n\nNode.js 18+; no packages or network access. Graph checks are not a substitute for\ncompiling and running the generated script inside Nexgen.`); }
function fail(message) { throw Error(message); }
function write(file, text, force) { if (fs.existsSync(file) && !force)
    fail('Refusing to replace ' + file + ' (use --force)'); fs.mkdirSync(path.dirname(file), { recursive: true }); const temp = file + '.bp-tmp-' + process.pid; try {
    fs.writeFileSync(temp, text, { flag: 'wx' });
    fs.renameSync(temp, file);
}
finally {
    if (fs.existsSync(temp))
        fs.unlinkSync(temp);
} }
function main(args) {
    if (!args.length || args.includes('--help') || args.includes('-h')) {
        help();
        return 0;
    }
    const [command, input, ...flags] = args;
    if (!['check', 'compile', 'screen','project'].includes(command) || !input)
        fail('Invalid command. Use --help.');
    let out = null, force = false, debug=false;
    for (let i = 0; i < flags.length; i++) {
        if (flags[i] === '--out' && !out) {
            out = flags[++i];
            if (!out)
                fail('Missing output path');
        }
        else if(flags[i]==='--debug')debug=true;
        else if (flags[i] === '--force')
            force = true;
        else
            fail('Unknown argument ' + flags[i]);
    }
    if (command === 'check' && (out || force || debug))
        fail('check takes no output flags');
    if (command !== 'check' && !out)
        fail('Specify --out');
    const size = fs.statSync(input).size;
    if (size > (command==='project'?100:2) * 1024 * 1024)
        fail('Graph exceeds 2 MB');
    const doc = JSON.parse(fs.readFileSync(input, 'utf8'));
    if(command==='project'){if(!out.toLowerCase().endsWith('.zip'))fail('Project output must end in .zip');const r=P.pack(doc,{debug});write(out,r.bytes,force);console.log('Wrote '+out+' ('+doc.screens.length+' screens)');return 0;}
    const result = debug?B.traceCompile(doc):B.compile(doc);
    for (const d of result.diagnostics)
        console.error(`${d.severity || 'error'}: ${[d.graph, d.node].filter(Boolean).join('/')}: ${d.message}`);
    if (!result.ok)
        return 1;
    if (command === 'compile') {
        if (!out.toLowerCase().endsWith('.as'))
            fail('Output must end in .as');
        write(out, result.source, force);
        console.log('Wrote ' + out);
    }
    else if (command === 'screen') {
        if (doc.kind !== 'Screen')
            fail('Only Screen graphs can be installed as screens');
        const file = path.join(out, 'main.as');
        write(file, result.source, force);
        const meta = path.join(out, 'screen.json');
        if (!fs.existsSync(meta))
            write(meta, JSON.stringify({ title: doc.name, description: 'Generated with Nexgen Blueprint Studio', category: 'Blueprints', order: 1000 }, null, 2) + '\n', false);
        console.log('Wrote ' + file + ' (screen metadata preserved or created)');
    }
    else
        console.log('Graph valid: ' + doc.name);
    return 0;
}
try {
    process.exitCode = main(process.argv.slice(2));
}
catch (e) {
    console.error(e.message);
    process.exitCode = 1;
}
