import fs from 'node:fs/promises';
import path from 'node:path';
import { root, record, identity, json, writeJson, localOutput } from './record.mjs';

const options = Object.fromEntries(process.argv.slice(2).map((value, index, all) =>
  value.startsWith('--') ? [value.slice(2), all[index + 1]] : null).filter(Boolean));
for (const required of ['output', 'executable', 'pwsh', 'diagnostic'])
  if (!options[required] || !path.isAbsolute(options[required]))
    throw new Error('Pass an absolute --' + required);
const output = localOutput(options.output);
const repeats = Number(options.repeats ?? 5);
if (!Number.isInteger(repeats) || repeats < 3 || repeats > 9) throw new Error('Use 3 through 9 repeats');
await fs.mkdir(output);
await fs.mkdir(path.join(output, 'commands'));
const provenance = await json(path.join(root, 'bin/build-provenance.json'));
const executable = await identity(options.executable);
const buildDirectory = path.join(root, 'build/clang-cl-x64-release');
const sourceExecutable = await identity(path.join(buildDirectory, 'concord_runtime.exe'));
if (provenance.toolchain !== 'clang-cl' || provenance.configuration !== 'Release'
    || path.resolve(provenance.buildDirectory).toLowerCase() !== buildDirectory.toLowerCase()
    || sourceExecutable.sha256 !== executable.sha256)
  throw new Error('Executable must match the current trusted Release product and provenance');
const config = path.join(output, 'runtime-baseline.ini');
await fs.writeFile(config, `[Runtime]\nResourceRoot=RedAlert2\nLocalDataRoot=local_data\nWindowWidth=1280\nWindowHeight=720\nFixedTickHz=30\nTargetFps=60\nShowMainMenu=no\n\n[Skirmish]\nShowSetup=no\nGameSpeed=4\nShortGame=no\nComputerAI=no\nUnitCount=10\n`, { flag: 'wx' });
const sourceMap = path.join(root, 'local_data/resources/mix_exports/multimd/2peaks.map');
await writeJson(path.join(output, 'input.json'), { executable, sourceExecutable,
  provenance: await identity(path.join(root, 'bin/build-provenance.json')),
  config: await identity(config), sourceMap: await identity(sourceMap),
  repetitions: repeats, order: 'round-robin by repetition', warmupTicks: 60,
  resolution: [1280, 720], targetFps: 60, fixedTickHz: 30, gameSpeed: 4,
  audio: 'existing benchmark forces music/sound/voice muted',
  generalWorkerSwitch: 'not-implemented', gpuGameDrawing: 'not-implemented',
  coldFileCacheClaimed: false, gpuPresentClaimed: false,
  observerBoundary: '100ms target visibility/memory/power and global DWM refresh; no app scanout proof' });

const scenarios = [
  { name: 'small', script: 'Run-MovementBenchmark.ps1', args: ['-Units','48','-Scenario','open','-Ticks','600'] },
  { name: 'movement', script: 'Run-MovementBenchmark.ps1', args: ['-Units','128','-Scenario','open','-Ticks','600'] },
  { name: 'combat', script: 'Run-MovementBenchmark.ps1', args: ['-Units','128','-Scenario','combat','-Ticks','1800'] },
  { name: 'stationary', script: 'Run-ScalabilityBenchmark.ps1', args: ['-Units','1024','-Buildings','256','-Ticks','600','-CameraCell','80,105'] },
  { name: 'loading', ticks: 60 },
  { name: 'visible', ticks: 600, movement: true, visible: true },
];
const receipts = [];
const selectedScenarios = options['only-visible'] === 'yes' ? scenarios.filter(x=>x.visible) : scenarios;
for (let repetition = 1; repetition <= repeats; ++repetition) {
  for (const scenario of selectedScenarios) {
    const label = `${scenario.name}-r${repetition}`;
    let argv, env = {}, observer = {};
    if (scenario.script) {
      argv = [options.pwsh, '-NoProfile', '-File', path.join(root, 'tools/benchmarks', scenario.script),
        '-Executable', options.executable, '-Config', config, '-Label', label, '-RawSamples',
        '-OutputDirectory', path.join(output, label), ...scenario.args];
      if (scenario.script === 'Run-MovementBenchmark.ps1') argv.push('-SaveGeneratedMap');
    } else {
      argv = [options.executable, '--config', config, '--skip-main-menu', '--skip-skirmish-menu',
        '--resource-root', path.join(root, 'RedAlert2'), '--local-data-root', path.join(root, 'local_data'),
        '--map', sourceMap];
      env = { CONCORD_LIVE_BENCHMARK: '1', CONCORD_LIVE_BENCHMARK_TICKS: String(scenario.ticks),
        CONCORD_LIVE_BENCHMARK_RAW_SAMPLES: '1', CONCORD_RENDER_DETAIL_STATS: '0' };
      if (scenario.movement) Object.assign(env, { CONCORD_LIVE_BENCHMARK_MOVEMENT: '128', CONCORD_LIVE_BENCHMARK_MIXED: '1' });
      if (scenario.movement) env.CONCORD_LIVE_BENCHMARK_GENERATED_MAP = path.join(output,'commands',label,'generated.map');
      if (scenario.visible) {
        env.CONCORD_LIVE_BENCHMARK_VISIBLE = '1';
        observer = { visible: true, observeExecutable: options.diagnostic, observeTopmost: true };
      }
    }
    const receipt = await record(path.join(output, 'commands', label), argv, env, observer);
    receipts.push({ scenario: scenario.name, repetition, receipt });
    if (receipt.exitCode !== 0) throw new Error('Baseline failed; preserve attempt: ' + label);
    const log = await fs.readFile(receipt.stdout.path, 'utf8');
    // Wrapper output is deliberately concise; the full native log remains in
    // the scenario's new private directory and is consumed by Summarize.mjs.
    if (!/Live frame benchmark: passed=yes/.test(log)) throw new Error('No completed measured fixture: ' + label);
    if (scenario.visible && (receipt.observer?.exitCode !== 0 || !/visibleRequested=yes/.test(log)))
      throw new Error('Visible observation was not completed: ' + label);
    if ((await identity(options.executable)).sha256 !== executable.sha256)
      throw new Error('Product changed during sampling');
  }
}
await writeJson(path.join(output, 'receipts.json'), receipts);
console.log('S33 baseline completed; summarize exact native logs before acceptance.');
