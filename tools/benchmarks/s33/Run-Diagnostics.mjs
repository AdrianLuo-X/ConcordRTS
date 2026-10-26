import fs from 'node:fs/promises';
import path from 'node:path';
import {root, record, localOutput} from './record.mjs';

const [outputArgument, buildArgument] = process.argv.slice(2);
if(!outputArgument||!buildArgument||!path.isAbsolute(outputArgument)||!path.isAbsolute(buildArgument))
  throw new Error('Pass absolute output and isolated build directories');
const output=localOutput(outputArgument),build=localOutput(buildArgument);
if(build.toLowerCase()===path.join(root,'build/clang-cl-x64-release').toLowerCase())
  throw new Error('The diagnostic must not configure the production build');
await fs.mkdir(output);
const cacheText=await fs.readFile(path.join(root,'build/clang-cl-x64-release/CMakeCache.txt'),'utf8');
const cache=Object.fromEntries(cacheText.split(/\r?\n/).filter(s=>/^[^#/][^:]*:[^=]+=/.test(s))
  .map(s=>[s.slice(0,s.indexOf(':')),s.slice(s.indexOf('=')+1)]));
const args=[cache.CMAKE_COMMAND,'-S',path.join(root,'tools/diagnostics/s33'),'-B',build,'-G','Ninja',
  '-DCMAKE_TOOLCHAIN_FILE='+path.join(root,'cmake/toolchains/windows-clang-cl.cmake'),'-DCMAKE_BUILD_TYPE=Release',
  ...['CONCORD_LLVM_ROOT','CONCORD_MSVC_ROOT','CONCORD_WINDOWS_SDK_ROOT','CONCORD_WINDOWS_SDK_VERSION',
    'CONCORD_NINJA_EXECUTABLE','CONCORD_CPU_ARCH','CONCORD_MSVC_COMPATIBILITY_VERSION'].map(k=>'-D'+k+'='+cache[k])];
for(const [name,argv] of [
  ['configure',args],
  ['build',[cache.CMAKE_COMMAND,'--build',build,'--parallel','1','--verbose']],
  ['timing-contract',[path.join(path.dirname(cache.CMAKE_COMMAND),'ctest.exe'),'--test-dir',build,'--no-tests=error','-V']],
  ['capabilities',[path.join(build,'concord_s33_capabilities.exe')]]]){
  const receipt=await record(path.join(output,name),argv);
  if(receipt.exitCode!==0)throw new Error('Diagnostic failed at '+name);
}
