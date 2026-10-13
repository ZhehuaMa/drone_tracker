from pathlib import Path
import csv,json,sys,re
import numpy as np
r=Path(sys.argv[1]);summary={}
for case in ['pair2','pair3']:
 v=json.loads((r/case/'resources.json').read_text());calls=list(csv.DictReader((r/case/'0-mixed/fixed-calls.csv').open()));blocks=list(csv.DictReader((r/case/'0-mixed/blocks.csv').open()));spans=[(int(calls[b*40]['start_ns']),int(calls[(b+1)*40-1]['start_ns'])+int(calls[(b+1)*40-1]['duration_ns']),int(blocks[b]['mode'])) for b in range(len(blocks))]; out={'frequency_values':{},'temperature_millidegrees':{},'fully_inside_block_interval_scheduling':{},'worker_last_cpu_counts':{},'sample_count':len(v)}
 for name in ['cpu','devfreq']:
  paths=set(k for x in v for k in x[name]);out['frequency_values'][name]={p:sorted(set(x[name][p] for x in v if p in x[name])) for p in paths}
 paths=set(k for x in v for k in x['thermal']);out['temperature_millidegrees']={p:{'minimum':min(int(x['thermal'][p]) for x in v if p in x['thermal']),'maximum':max(int(x['thermal'][p]) for x in v if p in x['thermal'])} for p in paths}
 for x in v:
  if 'worker_stat' in x:
   fields=x['worker_stat'].split(') ',1)[1].split();cpu=fields[36];out['worker_last_cpu_counts'][cpu]=out['worker_last_cpu_counts'].get(cpu,0)+1
 for mode,name in [(0,'emmc'),(1,'tmpfs')]:
  pieces=[]
  for a,b in zip(v,v[1:]):
   if any(start<=a['mono_ns']<b['mono_ns']<=end and m==mode for start,end,m in spans):
    delta={'seconds':(b['mono_ns']-a['mono_ns'])/1e9}
    for actor in ['business','worker']:
     if actor+'_schedstat' in a and actor+'_schedstat' in b:
      av=list(map(int,a[actor+'_schedstat'].split()));bv=list(map(int,b[actor+'_schedstat'].split()));delta[actor+'_run_ns']=bv[0]-av[0];delta[actor+'_runnable_wait_ns']=bv[1]-av[1]
    pieces.append(delta)
  duration=sum(x['seconds'] for x in pieces);out['fully_inside_block_interval_scheduling'][name]={'intervals':len(pieces),'observed_seconds':duration,'run_and_runnable_wait_ms_per_second':{key:sum(x.get(key,0) for x in pieces)/1e6/duration for key in ['business_run_ns','business_runnable_wait_ns','worker_run_ns','worker_runnable_wait_ns']} if duration else {}}
 measured=[x for x in v if spans[0][0]<=x['mono_ns']<=spans[-1][1]]
 if len(measured)>1:
  a,b=measured[0],measured[-1];seconds=(b['mono_ns']-a['mono_ns'])/1e9;out['combined_measured_schedule_including_boundaries']={'seconds':seconds,'scope':'两介质混合，包含段边界；1 Hz 不能逐段归因','run_and_runnable_wait_ms_per_second':{}}
  for actor in ['business','worker']:
   av=list(map(int,a[actor+'_schedstat'].split()));bv=list(map(int,b[actor+'_schedstat'].split()))
   out['combined_measured_schedule_including_boundaries']['run_and_runnable_wait_ms_per_second'][actor+'_run']=(bv[0]-av[0])/1e6/seconds
   out['combined_measured_schedule_including_boundaries']['run_and_runnable_wait_ms_per_second'][actor+'_runnable_wait']=(bv[1]-av[1])/1e6/seconds
  out['minimum_mem_available_kib']=min(int(re.search(r'MemAvailable:\s+(\d+)',x['meminfo'])[1]) for x in measured)
  out['worker_fd_counts']=sorted(set(x['worker_fd_count'] for x in measured if 'worker_fd_count' in x))
  out['business_fd_counts']=sorted(set(x['business_fd_count'] for x in measured if 'business_fd_count' in x))
 summary[case]=out
(r/'resource-summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
