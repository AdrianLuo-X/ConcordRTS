import fs from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

export const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export async function identity(file) {
  const bytes = await fs.readFile(file);
  return { path: path.resolve(file), bytes: bytes.length,
    sha256: crypto.createHash('sha256').update(bytes).digest('hex').toUpperCase() };
}
export const json = async file => JSON.parse((await fs.readFile(file, 'utf8')).replace(/^\uFEFF/, ''));
export const writeJson = (file, value) => fs.writeFile(file, JSON.stringify(value, null, 2) + '\n', { flag: 'wx' });
export function localOutput(directory) {
  const resolved = path.resolve(directory);
  if (!resolved.toLowerCase().startsWith(root.toLowerCase() + path.sep))
    throw new Error('Output must be inside this project: ' + directory);
  return resolved;
}

// One child at a time. No shell expansion, global environment writes, name-based
// process termination, directory removal, or reuse of an earlier receipt.
export async function record(directory, argv, overrides = {}, options = {}) {
  directory = localOutput(directory);
  await fs.mkdir(directory); // Existing attempts must never be overwritten.
  await fs.mkdir(path.join(directory, 'temp'));
  const environment = { ...process.env };
  const cleared = [];
  for (const key of Object.keys(environment)) {
    if (/^CONCORD_/i.test(key) || /^(CL|_CL_|LINK|_LINK_|INCLUDE|LIB|LIBPATH)$/i.test(key)) {
      cleared.push(key); delete environment[key];
    }
  }
  Object.assign(environment, overrides, { TEMP: path.join(directory, 'temp'), TMP: path.join(directory, 'temp') });
  const receipt = { argv, cwd: root, executable: await identity(argv[0]),
    startedUtc: new Date().toISOString(), environmentOverrides: overrides, clearedInherited: cleared,
    TEMP: environment.TEMP, TMP: environment.TMP, exitCode: null, signal: null,
    launchError: null, timedOut: false, pid: null };
  const stdout = await fs.open(path.join(directory, 'stdout.log'), 'wx');
  const stderr = await fs.open(path.join(directory, 'stderr.log'), 'wx');
  const start = performance.now();
  const child = spawn(argv[0], argv.slice(1), { cwd: root, env: environment,
    windowsHide: options.visible !== true, stdio: ['ignore', stdout.fd, stderr.fd] });
  receipt.pid = child.pid ?? null;
  const observer = options.observeExecutable && child.pid
    ? record(path.join(directory, 'observer'), [options.observeExecutable,
      options.observeTopmost === true ? '--observe-visible-pid' : '--observe-pid',
      String(child.pid), '120000']) : null;
  await new Promise(resolve => {
    child.once('error', error => { receipt.launchError = String(error); });
    child.once('close', (code, signal) => { receipt.exitCode = code; receipt.signal = signal; resolve(); });
  });
  receipt.elapsedSeconds = (performance.now() - start) / 1000;
  receipt.finishedUtc = new Date().toISOString();
  if (observer) receipt.observer = await observer;
  await stdout.close(); await stderr.close();
  receipt.stdout = await identity(path.join(directory, 'stdout.log'));
  receipt.stderr = await identity(path.join(directory, 'stderr.log'));
  receipt.recorder = await identity(fileURLToPath(import.meta.url));
  await writeJson(path.join(directory, 'command.json'), receipt);
  console.log(JSON.stringify({ command: path.basename(directory), exitCode: receipt.exitCode,
    elapsedSeconds: receipt.elapsedSeconds, launchError: receipt.launchError }));
  return receipt;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const spec = await json(process.argv[2]);
  const result = await record(spec.directory, spec.argv, spec.environmentOverrides ?? {}, spec.options ?? {});
  process.exitCode = result.exitCode === 0 && !result.launchError ? 0 : 1;
}
