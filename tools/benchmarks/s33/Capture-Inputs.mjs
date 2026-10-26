import fs from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { root, identity, json, writeJson, localOutput } from './record.mjs';

const output = localOutput(process.argv[2]);
const hashFile = async file => {
  const hash = crypto.createHash('sha256');
  for await (const chunk of createReadStream(file)) hash.update(chunk);
  return hash.digest('hex').toUpperCase();
};
const roots = ['RedAlert2', 'local_data/resources', 'resources'];
const files = [], absentRoots = [];
async function visit(directory) {
  for (const entry of (await fs.readdir(directory, { withFileTypes: true })).sort((a,b) => a.name.localeCompare(b.name, 'en'))) {
    const full = path.join(directory, entry.name);
    if (entry.isSymbolicLink()) throw new Error('Linked input needs explicit ownership review: ' + full);
    if (entry.isDirectory()) await visit(full);
    else if (entry.isFile()) {
      const stat = await fs.stat(full);
      files.push({ path: path.relative(root, full).replaceAll('\\','/'), bytes: stat.size, sha256: await hashFile(full) });
      if (files.length % 1000 === 0) console.log('Hashed inputs: ' + files.length);
    }
  }
}
for (const relative of roots) {
  const full = path.join(root, relative);
  try { await visit(full); }
  catch (error) { if (error.code === 'ENOENT' && error.path === full) absentRoots.push(relative); else throw error; }
}
const provenance = await json(path.join(root, 'bin/build-provenance.json'));
const compiles = await json(path.join(provenance.buildDirectory, 'compile_commands.json'));
const runtimeCommands = compiles.filter(value => /concord_runtime\.dir/.test(value.command));
const compilerContract = await fs.readFile(path.join(provenance.buildDirectory, 'compiler-contract-Release.txt'), 'utf8');
const document = { capturedUtc: new Date().toISOString(), inputRoots: roots, absentRoots,
  files, totalBytes: files.reduce((sum,value) => sum + value.bytes, 0),
  fileListSha256: crypto.createHash('sha256').update(JSON.stringify(files)).digest('hex').toUpperCase(),
  product: await identity(path.join(root,'bin/concord_runtime.exe')),
  provenance: await identity(path.join(root,'bin/build-provenance.json')),
  sourceHashes: provenance.sourceHashes, actualRuntimeCompilerCommands: runtimeCommands,
  compilerContract, components: provenance.components, parallelBuild: provenance.parallel,
  rootCMake: await identity(path.join(root,'CMakeLists.txt')),
  fullAssetCopyMade: false };
await writeJson(output, document);
console.log(JSON.stringify({files:files.length,bytes:document.totalBytes,manifest:output}));
