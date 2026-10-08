"""Repeatable local performance traces; use real images for decoder latency conclusions."""
import argparse, json, os, re, statistics, subprocess, pathlib, time, math
parser=argparse.ArgumentParser()
parser.add_argument('--exe',type=pathlib.Path,required=True)
parser.add_argument('--images',type=pathlib.Path,required=True)
parser.add_argument('--output',type=pathlib.Path,required=True)
parser.add_argument('--runs',type=int,default=20)
parser.add_argument('--scenario',choices=['startup','scroll','fullscreen','compare','refresh'],default='startup')
parser.add_argument('--mode',choices=['grid','list','gallery'],default='grid')
parser.add_argument('--cache-state',choices=['existing','cold','disk'],default='existing')
parser.add_argument('--panes',type=int,choices=[1,2,3,4],default=1)
args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
images=sorted(p for p in args.images.iterdir() if p.suffix.lower() in {'.jpg','.jpeg','.png','.raw','.yuv','.dng','.heic','.heif'})
summary=[]
cold_generation=str(time.time_ns())
for run in range(args.runs):
    env=os.environ.copy(); env['MVPVIEW_PERF']='1'; env['QT_FORCE_STDERR_LOGGING']='1'
    if args.cache_state!='existing':
        cache=args.output/('disk-cache' if args.cache_state=='disk' else f'cold-cache-{cold_generation}-{run}')
        env['MVPVIEW_THUMBNAIL_CACHE_DIR']=str(cache.resolve())
    capture=args.output/f'{run:02}.png'
    command=[str(args.exe.resolve()),str(args.images.resolve()),'--display-mode',args.mode,'--file-managers',str(args.panes),'--screenshot-native','--screenshot',str(capture.resolve()),'--screenshot-delay','10000' if args.scenario=='scroll' else '6000' if args.scenario=='refresh' else '2500']
    if args.scenario in {'scroll','fullscreen','refresh'}: command+=['--perf-scenario',args.scenario]
    if args.scenario=='fullscreen':
        if not images: raise SystemExit('fullscreen requires images')
        command+=['--select',str(images[0].resolve())]
    if args.scenario=='compare':
        if len(images)<2: raise SystemExit('compare requires at least two images')
        command+=['--qml-compare']+[str(p.resolve()) for p in images[:4]]
    start=time.perf_counter()
    result=subprocess.run(command,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=60,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    log=result.stdout+result.stderr; (args.output/f'{run:02}.log').write_text(log,encoding='utf-8')
    events=[]
    for line in log.splitlines():
        match=re.search(r'MVPVIEW_PERF\s+(\{.*\})',line)
        if match:
            try: events.append(json.loads(match.group(1)))
            except json.JSONDecodeError: pass
    (args.output/f'{run:02}.json').write_text(json.dumps(events,ensure_ascii=False,indent=2),encoding='utf-8')
    first=[e['sinceStartMs'] for e in events if e['event']=='startup.first_frame']
    completed=[e for e in events if e['event']=='loader.completed' and not e.get('cancelled')]
    motion=[]; start_motion=None
    for e in events:
        if e['event']=='scenario.motion':
            if e['active']: start_motion=e['sinceStartMs']
            elif start_motion is not None:
                motion.append((start_motion,e['sinceStartMs'])); start_motion=None
    motion_frames=[e.get('intervalMs',e.get('gapMs')) for e in events if e['event']=='ui.frame' and any(a+100<e['sinceStartMs']<b for a,b in motion)]
    motion_frames=[v for v in motion_frames if v is not None]
    summary.append({'motionFrameP50':statistics.median(motion_frames) if motion_frames else None,'motionFrameP95':sorted(motion_frames)[max(0,math.ceil(len(motion_frames)*.95)-1)] if motion_frames else None,'run':run,'exit':result.returncode,'processMs':round((time.perf_counter()-start)*1000),'firstFrameMs':first[0] if first else None,'completed':len(completed),'maxCachedBytes':max([e.get('cachedBytes',0) for e in completed],default=0)})
    if result.returncode or not capture.exists(): raise SystemExit(f'Run {run} failed; see {args.output}/{run:02}.log')
valid=[r['firstFrameMs'] for r in summary if r['firstFrameMs'] is not None]
def percentile(values,p):
    values=sorted(values); return values[min(len(values)-1,max(0,math.ceil(len(values)*p)-1))] if values else None
report={'cacheState':args.cache_state,'scenario':args.scenario,'mode':args.mode,'runs':summary,'firstFrameP50':statistics.median(valid) if valid else None,'firstFrameP95':percentile(valid,.95),'note':'First run and subsequent runs have different cache warmth. Frame gaps include idle periods; inspect scenario traces before interpreting rendering FPS.'}
(args.output/'summary.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8'); print(json.dumps(report,ensure_ascii=False))
