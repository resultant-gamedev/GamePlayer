#!/usr/bin/env node
const chokidar = require('chokidar');
const fs = require('fs');
const path = require('path');
const argv = require('optimist').argv;
const process = require('process');
const exec = require('child_process').exec;
const log = console.log.bind(console);

let projectDir = argv.project?argv.project:"";
if(!path.isAbsolute(projectDir))
  projectDir = path.join(process.cwd(), projectDir)

let config = null
const configpath = path.join(projectDir, "config.json")
const gdconfigpath = path.join(projectDir, "engine.cfg")
if(fs.existsSync(configpath) && fs.existsSync(gdconfigpath) && fs.statSync(configpath).isFile() && fs.statSync(gdconfigpath).isFile()) {
  const content = fs.readFileSync(configpath, "utf-8");
  if(content) {
    config = JSON.parse(content)
  }
}
else {
  log("Error: config.json and engine.cfg are both required for godot proejct!");
}

if(config) {

  log("Start watching project: ", projectDir)

  var watcher = chokidar.watch(`${projectDir}/**`, {
    ignored: /someFilesToIgnoreWithReg/, persistent: true
  });

  watcher
    .on('add', function(path) { pushFile(path); })
    .on('addDir', function(path) { makedirs(path); })
    .on('change', function(path) { pushFile(path); })
    .on('unlink', function(path) { removeFile(path); })
    .on('unlinkDir', function(path) { removeDir(path); })
    .on('ready', function() { log('Initial scan complete. Ready for changes.'); })
    .on('raw', function(event, path, details) {})
    .on('error', function(error) { log('Error: ', error); })
}
else {
  log("Error load configurations from ", configpath)
}

function cmd(command){
  exec(command, (error, stdout, stderr) => {
    log(command)
    if (error) {
      console.error(`cmd error: ${error}`);
      return;
    }
    if(stdout)
      log(stdout);
    if(stderr)
      log(stderr);
  });
}

function pushFile(src) {
  const relpath = path.relative(projectDir, src);
  let androidDst = config.DebugResDir["Android"];
  if(androidDst) {
    androidDst = path.join(androidDst, relpath).replace(/\\/g, '/');
    cmd(`adb push ${src} ${androidDst}`);
  }
}

function removeFile(src) {
  const relpath = path.relative(projectDir, src);
  let androidDst = config.DebugResDir["Android"];
  if(androidDst) {
    androidDst = path.join(androidDst, relpath).replace(/\\/g, '/');
    cmd(`adb shell rm ${androidDst}`);
  }
}

function removeDir(src) {
  const relpath = path.relative(projectDir, src);
  let androidDst = config.DebugResDir["Android"];
  log(androidDst)
  if(androidDst) {
    androidDst = path.join(androidDst, relpath).replace(/\\/g, '/');
    cmd(`adb shell rm -rf ${androidDst}`);
  }
}

function makedirs(src) {
  const relpath = path.relative(projectDir, src);
  let androidDst = config.DebugResDir["Android"];
  if(androidDst) {
    androidDst = path.join(androidDst, relpath).replace(/\\/g, '/');
    cmd(`adb shell mkdir -p ${androidDst}`);
  }
}
