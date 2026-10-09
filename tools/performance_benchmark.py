"""Repeatable local performance traces; use real images for decoder latency conclusions."""
import argparse, json, os, re, statistics, subprocess, pathlib, time, math
from performance_memory import ProcessMemoryProbe
parser=argparse.ArgumentParser()
parser.add_argument('--exe',type=pathlib.Path,required=True)
parser.add_argument('--images',type=pathlib.Path,required=True)
parser.add_argument('--output',type=pathlib.Path,required=True)
parser.add_argument('--runs',type=int,default=20)
parser.add_argument('--scenario',choices=['startup','scroll','animated-scroll','fullscreen','fullscreen-clear','compare','refresh','slot-update'],default='startup')
parser.add_argument('--capture-delay-ms',type=int,help='Override capture time for slow formats; 250..50000ms')
parser.add_argument('--mode',choices=['grid','list','gallery'],default='grid')
parser.add_argument('--cache-state',choices=['existing','cold','disk'],default='existing')
parser.add_argument('--pane-images',type=pathlib.Path,action='append',default=[],help='Directory for each additional pane; repeat for panes 2..4')
parser.add_argument('--panes',type=int,choices=[1,2,3,4],default=1)
args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
if args.capture_delay_ms is not None and not 250<=args.capture_delay_ms<=50000:
    raise SystemExit('--capture-delay-ms must be 250..50000')
if len(args.pane_images)>=args.panes: raise SystemExit('--pane-images requires one directory per additional pane at most')
for directory in [args.images]+args.pane_images:
    if not directory.is_dir(): raise SystemExit(f'Image directory not found: {directory}')
images=[]
if args.scenario in {'fullscreen','fullscreen-clear','compare','slot-update'}:
    images=sorted(p for p in args.images.iterdir() if p.suffix.lower() in {'.jpg','.jpeg','.png','.bmp','.dib','.raw','.yuv','.dng','.heic','.heif','.cr2','.cr3','.crw','.nef','.nrw','.arw','.sr2','.srf','.raf','.rw2','.orf','.pef','.srw','.x3f','.rwl'})
summary=[]
cold_generation=str(time.time_ns())
for run in range(args.runs):
    env=os.environ.copy(); env['MVPVIEW_PERF']='1'; env['QT_FORCE_STDERR_LOGGING']='1'
    env['MVPVIEW_PERF_SETTINGS_DIR']=str((args.output/f'settings-{cold_generation}-{run}').resolve())
    if args.cache_state!='existing':
        cache=args.output/('disk-cache' if args.cache_state=='disk' else f'cold-cache-{cold_generation}-{run}')
        env['MVPVIEW_THUMBNAIL_CACHE_DIR']=str(cache.resolve())
    capture=args.output/f'{run:02}.png'
    capture_delay=args.capture_delay_ms or (10000 if args.scenario in {'scroll','animated-scroll'} else 6000 if args.scenario=='refresh' else 5000 if args.scenario=='slot-update' else 2500)
    command=[str(args.exe.resolve()),str(args.images.resolve()),'--display-mode',args.mode,'--file-managers',str(args.panes),'--screenshot-native','--screenshot',str(capture.resolve()),'--screenshot-delay',str(capture_delay)]
    for directory in args.pane_images: command+=['--perf-pane-directory',str(directory.resolve())]
    if args.scenario in {'scroll','animated-scroll','fullscreen','fullscreen-clear','refresh','slot-update'}: command+=['--perf-scenario',args.scenario]
    if args.scenario in {'fullscreen','fullscreen-clear'}:
        if not images: raise SystemExit('fullscreen requires images')
        command+=['--select',str(images[0].resolve())]
    if args.scenario in {'compare','slot-update'}:
        if len(images)<2: raise SystemExit('compare requires at least two images')
        command+=['--qml-compare']+[str(p.resolve()) for p in images[:4]]
    start=time.perf_counter()
    process=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace',creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    memory_probe=ProcessMemoryProbe(process.pid)
    try:
        stdout,stderr=process.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        process.kill(); process.communicate()
        raise
    finally:
        process_memory=memory_probe.finish()
    log=stdout+stderr; (args.output/f'{run:02}.log').write_text(log,encoding='utf-8')
    events=[]
    for line in log.splitlines():
        match=re.search(r'MVPVIEW_PERF\s+(\{.*\})',line)
        if match:
            try: events.append(json.loads(match.group(1)))
            except json.JSONDecodeError: pass
    (args.output/f'{run:02}.json').write_text(json.dumps(events,ensure_ascii=False,indent=2),encoding='utf-8')
    first=[e['sinceStartMs'] for e in events if e['event']=='startup.first_frame']
    completed=[e for e in events if e['event']=='loader.completed' and not e.get('cancelled')]
    result_buffers=[e for e in events if e['event']=='loader.result_buffers']
    motion=[]; start_motion=None
    for e in events:
        if e['event']=='scenario.motion':
            if e['active']: start_motion=e['sinceStartMs']
            elif start_motion is not None:
                motion.append((start_motion,e['sinceStartMs'])); start_motion=None
    motion_frames=[e.get('intervalMs',e.get('gapMs')) for e in events if e['event']=='ui.frame' and any(a+100<e['sinceStartMs']<b for a,b in motion)]
    motion_frames=[v for v in motion_frames if v is not None]
    viewport_reports=[e for e in events if e['event']=='viewport.report']
    report_keys={(e['owner'],e['frame']) for e in viewport_reports}
    presented=[e for e in events if e['event']=='viewport.presented']
    demands=[e for e in events if e['event']=='viewport.demand']
    stopped=[e for e in events if e['event']=='viewport.stopped']
    settled=[e for e in events if e['event']=='viewport.settled']
    presented_keys={(e['owner'],e['generation']) for e in presented}
    stop_fills=[]; incomplete_stops=0
    for index,stop in enumerate(stopped):
        next_stop=next((e['sinceStartMs'] for e in stopped[index+1:] if e['owner']==stop['owner']),float('inf'))
        completion=next((e for e in settled if e['owner']==stop['owner'] and e['stopId']==stop['stopId'] and stop['sinceStartMs']<=e['sinceStartMs']<next_stop),None)
        if completion: stop_fills.append(completion['elapsedMs'])
        else: incomplete_stops+=1
    first_fill=[e['firstElapsedMs'] for e in presented if e.get('first')]
    fills=[e['elapsedMs'] for e in presented]
    slot_markers=[e['sinceStartMs'] for e in events if e['event']=='scenario.slot_update']
    view_markers=[e['sinceStartMs'] for e in events if e['event']=='scenario.view_change']
    slot_uploads=[e['slot'] for e in events if e['event']=='render.upload' and slot_markers and view_markers and slot_markers[0]<=e['sinceStartMs']<view_markers[0]]
    view_uploads=[e for e in events if e['event']=='render.upload' and view_markers and e['sinceStartMs']>=view_markers[0]]
    if args.scenario=='slot-update' and (slot_uploads!=[0] or view_uploads): raise SystemExit(f'GPU slot isolation failed: {slot_uploads}, view uploads {len(view_uploads)}; see trace')
    summary.append({'runtime':next((e for e in events if e['event']=='benchmark.runtime'),None),'settingsIsolated':next((e.get('isolated') for e in events if e['event']=='benchmark.settings'),None),'processMemory':process_memory,'maxResultBufferBytes':max([e['peakBytes'] for e in result_buffers],default=None),'maxResultBufferWaitMs':max([e['bufferWaitMs'] for e in completed if 'bufferWaitMs' in e],default=None),'maxPendingResults':max([e.get('pendingResults',0) for e in events if e['event']=='loader.dispatched'],default=0),'viewportReports':len(viewport_reports),'duplicateViewportReports':len(viewport_reports)-len(report_keys) if viewport_reports else None,'demandViewports':len(demands),'unpresentedViewports':sum((e['owner'],e['generation']) not in presented_keys for e in demands),'retargetedStops':sum(e['event']=='viewport.retargeted' for e in events),'stoppedViewports':len(stopped),'unfilledStoppedViewports':incomplete_stops,'stopFillP95':sorted(stop_fills)[max(0,math.ceil(len(stop_fills)*.95)-1)] if stop_fills and not incomplete_stops else None,'firstScreenFillMs':max(first_fill) if first_fill else None,'viewportFillP95':sorted(fills)[max(0,math.ceil(len(fills)*.95)-1)] if fills else None,'presentedViewports':len(presented),'slotUploads':slot_uploads,'viewUploads':len(view_uploads),'motionFrameP50' :statistics.median(motion_frames) if motion_frames else None,'motionFrameP95':sorted(motion_frames)[max(0,math.ceil(len(motion_frames)*.95)-1)] if motion_frames else None,'run':run,'exit':process.returncode,'processMs':round((time.perf_counter()-start)*1000),'firstFrameMs':first[0] if first else None,'completed':len(completed),'decodeFailures':sum(bool(e.get('failed')) for e in completed),'maxCachedBytes':max([e.get('cachedBytes',0) for e in events if e['event']=='loader.cache_state'],default=0)})
    working=[e for e in events if e['event']=='loader.decode_working']
    summary[-1]['previewDerivedThumbnails']=sum(bool(e.get('previewHit')) for e in completed)
    summary[-1]['sourceDecodeCalls']=sum(bool(e.get('sourceDecode')) for e in completed)
    summary[-1]['diskHits']=sum(bool(e.get('diskHit')) for e in completed)
    summary[-1]['memoryHits']=sum(e['event']=='loader.memory_hit' for e in events)
    retirement=[e for e in events if e['event']=='loader.resource_retirement']
    summary[-1]['resourceRetirementBatches']=len(retirement)
    summary[-1]['maxResourceRetirementGuiMs']=max([e['elapsedMs'] for e in retirement],default=None)
    cache_retirement=[e for e in events if e['event']=='loader.cache_retirement_queued']
    summary[-1]['cacheRetirementOwners']=len(cache_retirement)
    summary[-1]['maxCacheRetirementBytes']=max([e['pendingBytes'] for e in cache_retirement],default=0)
    summary[-1]['cacheRetirementSaturations']=sum(e['event']=='loader.cache_retirement_saturated' for e in events)
    summary[-1]['guiCacheRetirementReleases']=sum(e['event']=='loader.cache_retirement_released' and e.get('guiThread',False) for e in events)
    summary[-1]['maxCacheClearGuiMs']=max([e['elapsedMs'] for e in events if e['event']=='scenario.cache_clear'],default=None)
    summary[-1].update({'maxDecodeWorkingBytes':max([e['peakBytes'] for e in working],default=None),
                       'maxDecodeWorkingWaitMs':max([e['workingWaitMs'] for e in completed if 'workingWaitMs' in e],default=None),
                       'maxResidentPixelBytes':max([e.get('peakResidentPixelBytes',e['residentPixelBytes']) for e in events if 'residentPixelBytes' in e],default=None),
                       'maxNominalGpuPixelBytes':max([e.get('peakNominalGpuPixelBytes',e['nominalGpuPixelBytes']) for e in events if 'nominalGpuPixelBytes' in e],default=None)})
    if process.returncode or not capture.exists(): raise SystemExit(f'Run {run} failed; see {args.output}/{run:02}.log')
valid=[r['firstFrameMs'] for r in summary if r['firstFrameMs'] is not None]
def percentile(values,p):
    values=sorted(values); return values[min(len(values)-1,max(0,math.ceil(len(values)*p)-1))] if values else None
report={'settingsIsolated':all(r['settingsIsolated'] for r in summary) if all(r['settingsIsolated'] is not None for r in summary) else None,'paneDirectories':[str((args.pane_images[i-1] if i>0 and i-1<len(args.pane_images) else args.images).resolve()) for i in range(args.panes)],'cacheState':args.cache_state,'scenario':args.scenario,'mode':args.mode,'runs':summary,'firstFrameP50':statistics.median(valid) if valid else None,'firstFrameP95':percentile(valid,.95),'note':'cold uses a new application disk cache per run; disk shares the output disk-cache directory. Each process starts with empty decode memory caches; OS filesystem caches are not purged. Frame gaps include idle periods; inspect scenario traces before interpreting rendering FPS.'}
(args.output/'summary.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8'); print(json.dumps(report,ensure_ascii=False))
