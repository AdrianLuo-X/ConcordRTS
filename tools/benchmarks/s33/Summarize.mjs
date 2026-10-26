import fs from 'node:fs/promises';
import path from 'node:path';
import { json, writeJson, identity } from './record.mjs';

const directory = path.resolve(process.argv[2]);
const originalReceipts = await json(path.join(directory,'receipts.json'));
let receipts = originalReceipts;
const input = await json(path.join(directory,'input.json'));
if(process.argv[3]){
  const replacementDirectory=path.resolve(process.argv[3]);
  const replacements=await json(path.join(replacementDirectory,'receipts.json'));
  if(replacements.some(x=>x.scenario!=='visible'))throw new Error('Replacement must contain only visible runs');
  const replacementInput=await json(path.join(replacementDirectory,'input.json'));
  if(replacementInput.executable.sha256!==input.executable.sha256
      ||replacementInput.config.sha256!==input.config.sha256
      ||replacementInput.sourceMap.sha256!==input.sourceMap.sha256)throw new Error('Replacement product or input differs');
  input.visibleReplacement={directory:replacementDirectory,input:await identity(path.join(replacementDirectory,'input.json')),
    excludedFirstSeries:originalReceipts.filter(x=>x.scenario==='visible').map(x=>({repetition:x.repetition,
      receipt:x.receipt.stdout.path,reason:'exploratory window occlusion was not controlled; preserved and excluded from formal visible baseline'}))};
  receipts=[...originalReceipts.filter(x=>x.scenario!=='visible'),...replacements.map(x=>({...x,sourceDirectory:replacementDirectory}))];
}
const median = values => { const sorted = [...values].sort((a,b)=>a-b); return (sorted[Math.floor((sorted.length-1)/2)] + sorted[Math.floor(sorted.length/2)])/2; };
const quantile = (values, p) => [...values].sort((a,b)=>a-b)[Math.ceil(values.length*p)-1];
const stats = values => values.length ? {count:values.length,median:median(values),p95:quantile(values,0.95),p99:quantile(values,0.99),
  min:Math.min(...values),max:Math.max(...values),mean:values.reduce((sum,x)=>sum+x,0)/values.length,
  mad:median(values.map(x=>Math.abs(x-median(values))))} : null;
const fields = (text,prefix) => {
  const line = text.split(/\r?\n/).find(line=>line.startsWith(prefix));
  if (!line) throw new Error('Missing '+prefix);
  return Object.fromEntries([...line.matchAll(/(?:^|\s)([^\s=]+)=([^\s]+)/g)].map(x=>[x[1],x[2]]));
};
const runs=[];
for (const entry of receipts) {
  const {scenario,repetition,receipt}=entry;
  const sourceDirectory=entry.sourceDirectory??directory;
  if (receipt.exitCode!==0) throw new Error('Failed receipt '+scenario);
  const label=`${scenario}-r${repetition}`;
  let file=receipt.stdout.path;
  if (['small','movement','combat','stationary'].includes(scenario)) {
    const logs=(await fs.readdir(path.join(sourceDirectory,label))).filter(x=>x.endsWith('.log')&&!/\.(stdout|stderr)\.log$/.test(x));
    if (logs.length!==1) throw new Error('Ambiguous native log '+label);
    file=path.join(sourceDirectory,label,logs[0]);
  }
  const text=await fs.readFile(file,'utf8');
  const frame=fields(text,'Live frame benchmark:');
  const presentation=fields(text,'Live presentation baseline:');
  const context=fields(text,'Live benchmark context:');
  const raw=Object.fromEntries([...text.matchAll(/^Live baseline samples: metric=(\S+) values=([^\r\n]*)/gm)]
    .map(x=>[x[1],x[2]?x[2].split(',').map(Number):[]]));
  if(frame.passed!=='yes'||frame.sequentialTicks!=='yes'||frame.deadlineExceeded!=='no'
      ||frame.requestedTicks!==frame.measuredTicks
      ||context.client!=='1280x720'||context.native!=='1280x720'
      ||frame.displayFpsMeasured!=='no'||presentation.gpuGameTime!=='not-applicable')
    throw new Error('Measured contract failed '+label);
  for (const key of ['simulationMs','paintMs','frameGapMs','submissionCpuMs','cpuPreparationMs','visibleSubmissionGapMs'])
    if(!raw[key]||raw[key].some(x=>!Number.isFinite(x)||x<0))throw new Error('Invalid raw samples '+label+' '+key);
  if(raw.simulationMs.length!==Math.min(Number(frame.measuredTicks),Number(frame.timeSampleLimit))
      ||raw.paintMs.length!==Math.min(Number(frame.paintCount),Number(frame.timeSampleLimit)))
    throw new Error('Raw observation count mismatch '+label);
  if(scenario==='visible'&&(Number(presentation.rejectedVisibleSubmissions)!==0
      ||Number(presentation.visibleSubmissions)!==Number(frame.paintCount)))
    throw new Error('Visible sample contains rejected submissions '+label);
  const metricStats=Object.fromEntries(Object.entries(raw).map(([key,values])=>[key,stats(values)]));
  const comparisons=[['simulationMs','simMedianMs','median'],['simulationMs','simP95Ms','p95'],['simulationMs','simP99Ms','p99'],
    ['paintMs','paintMedianMs','median'],['paintMs','paintP95Ms','p95'],['paintMs','paintP99Ms','p99']];
  for(const [metric,field,quantile] of comparisons)
    if(Math.abs(metricStats[metric][quantile]-Number(frame[field]))>0.0011)
      throw new Error('Recomputed quantile mismatch '+label+' '+field);
  const row={scenario,repetition,frame,presentation,context,metrics:metricStats,
    nativeLog:await identity(file),receipt:await identity(path.join(sourceDirectory,'commands',label,'command.json'))};
  if(['small','movement','combat','visible'].includes(scenario)){
    row.generatedMap=await identity(path.join(sourceDirectory,scenario==='visible'?'commands':'',label,'generated.map'));
  }else if(scenario==='stationary'){
    row.generatedMap=await identity(path.join(sourceDirectory,label,'population-1024-256.map'));
  }
  if(receipt.observer){
    const observer=await json(receipt.observer.stdout.path);
    const refreshMs=[],vblankPollAdvanceMs=[];
    for(const sample of observer.samples){
      if(sample.dwm.hresult==='0x00000000'&&sample.dwm.qpcRefreshPeriod>0)
        refreshMs.push(sample.dwm.qpcRefreshPeriod/observer.qpcFrequency*1000);
    }
    for(let i=1;i<observer.samples.length;++i){
      const a=observer.samples[i-1].dwm,b=observer.samples[i].dwm;
      const delta=b.qpcVBlank-a.qpcVBlank;
      if(a.hresult==='0x00000000'&&b.hresult==='0x00000000'&&delta>0)
        vblankPollAdvanceMs.push(delta/observer.qpcFrequency*1000);
    }
    const visible=observer.samples.filter(s=>s.windows.some(w=>w.visible&&!w.minimized&&w.cloaked===0));
    if(!visible.length||!refreshMs.length)throw new Error('No visible/compositor samples '+label);
    const offset=Date.parse(receipt.observer.startedUtc)-Date.parse(receipt.startedUtc);
    const measuredStart=Number(frame.sessionToReadyMs)+(Number(frame.wallSeconds)-Number(frame.measuredSeconds))*1000;
    const measuredEnd=Number(frame.sessionToReadyMs)+Number(frame.wallSeconds)*1000;
    const interior=observer.samples.filter(s=>s.elapsedMs+offset>=measuredStart+200&&s.elapsedMs+offset<=measuredEnd-200);
    if(!observer.topmostPolicy||!observer.observerPhysicalDpiAware||!interior.length
        ||interior.some(s=>!s.windows.some(w=>w.visible&&!w.minimized&&w.cloaked===0&&w.topmost
          &&w.threeClientPointsOwned===3&&w.clientWidth===1280&&w.clientHeight===720)))
      throw new Error('Visible measurement interior is occluded or not physically resolved '+label);
    row.observer={samples:observer.samples.length,visibleSamples:visible.length,
      threePointsOwnedSamples:observer.samples.filter(s=>s.windows.some(w=>w.visible&&!w.minimized&&w.threeClientPointsOwned===3)).length,
      foregroundSamples:observer.samples.filter(s=>s.windows.some(w=>w.foreground)).length,
      acLineStatuses:[...new Set(observer.samples.map(s=>s.power.acLineStatus))],
      observedPrivatePeakBytes:Math.max(...observer.samples.map(s=>s.privateBytes)),
      reportedPeakWorkingSetBytes:Math.max(...observer.samples.map(s=>s.peakWorkingSet)),
      measuredInteriorSamples:interior.length,measuredInteriorThreePointsOwned:true,
      globalDwmReportedRefreshPeriodMs:stats(refreshMs),vblankTimestampAdvanceBetweenPollsMs:stats(vblankPollAdvanceMs),
      refreshCounterConvertedToFps:false,perApplicationDisplayFpsMeasured:false,
      raw:await identity(receipt.observer.stdout.path)};
  }
  runs.push(row);
}
const scenarios={};
for(const name of [...new Set(runs.map(x=>x.scenario))]){
  const selected=runs.filter(x=>x.scenario===name);
  const metrics={};
  for(const metric of ['simulationMs','paintMs','frameGapMs','submissionCpuMs','cpuPreparationMs','visibleSubmissionGapMs']){
    const selectedStats=selected.map(x=>x.metrics[metric]).filter(Boolean);
    if(selectedStats.length){
      const between=stats(selectedStats.map(x=>x.median));
      metrics[metric]={runMedians:between,runP95s:stats(selectedStats.map(x=>x.p95)),runP99s:stats(selectedStats.map(x=>x.p99)),
        noiseRecheckAbove:between.max+3*between.mad,thresholdMeaning:'empirical recheck trigger, not a confidence bound or promised speedup'};
    }
  }
  scenarios[name]={repeats:selected.length,metrics,sessionToReadyMs:stats(selected.map(x=>Number(x.frame.sessionToReadyMs))),
    worldHashes:[...new Set(selected.map(x=>x.frame.worldHash))],seeds:[...new Set(selected.map(x=>x.context.seed))],
    initialWorldHashes:[...new Set(selected.map(x=>x.context.initialWorldHash))],
    generatedMapHashes:[...new Set(selected.map(x=>x.generatedMap?.sha256).filter(Boolean))],
    cameraOffsets:[...new Set(selected.map(x=>x.context.cameraOffsetX+','+x.context.cameraOffsetY))],
    mapIds:[...new Set(selected.map(x=>x.context.map))]};
  if(scenarios[name].worldHashes.length!==1||scenarios[name].initialWorldHashes.length!==1
      ||scenarios[name].generatedMapHashes.length>1||scenarios[name].seeds.length!==1||scenarios[name].cameraOffsets.length!==1)
    throw new Error('Fixed scenario identity diverged '+name);
}
const summary={schema:1,input,quantileMethod:'median and nearest-rank P95/P99 of bounded retained samples; rounded milliseconds; see per-run quantileSampled and timeSampleLimit',
  displayFpsMeasured:false,gpuGameMetrics:'not-applicable',scenarios,runs};
await writeJson(path.join(directory,'summary.json'),summary);
console.log(JSON.stringify(Object.fromEntries(Object.entries(scenarios).map(([name,x])=>[name,{repeats:x.repeats,
  sim:x.metrics.simulationMs.runMedians.median,paint:x.metrics.paintMs.runMedians.median,load:x.sessionToReadyMs.median,
  worldHash:x.worldHashes[0]}])),null,2));
