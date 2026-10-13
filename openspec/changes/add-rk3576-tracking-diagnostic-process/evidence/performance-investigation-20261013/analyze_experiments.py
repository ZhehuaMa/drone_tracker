from pathlib import Path
import sys,json,csv,math,bisect,collections,statistics
root=Path(sys.argv[1]);out=[]
def pct(values,p=.99):
 return sorted(values)[math.ceil(len(values)*p)-1] if values else None
def rows(path): return [{k:int(v) for k,v in row.items()} for row in csv.DictReader(line for line in path.open() if not line.startswith('#'))]
for directory in sorted(root.glob('pair*')):
 if not (directory/'case.json').exists():continue
 case=json.loads((directory/'case.json').read_text());item={'case':str(directory),'execution_passed':case.get('execution_passed'),'modes':[]}
 for index,window in enumerate(case['rounds']):
  folder=directory/(str(index)+'-'+window.get('label',window['mode']));fullcalls=rows(folder/'calls.csv');calls=[dict(x,update_id=i+1) for i,x in enumerate(fullcalls) if window['measurement_start_ns']<=x['start_ns']<window['measurement_end_ns']];trace=rows(folder/'investigation.csv');node=sorted([x for x in trace if x['producer']==0],key=lambda x:x['mono_ns']);times=[x['mono_ns'] for x in node];requests={x['request_id']:x for x in node if x['type']==13 and x['flags']&128};starts={x['request_id']:x for x in trace if x['type']==14};ends={x['request_id']:x for x in trace if x['type']==16};sync=rows(directory/f'sync-{index}.csv');submits=rows(folder/'submissions.csv');st=[x['start_ns'] for x in submits if x['producer']==0];sn=[x for x in submits if x['producer']==0];other=[x for x in submits if window['measurement_start_ns']<=x['start_ns']<window['measurement_end_ns']]
  detailed=[]
  for call in calls:
   relevant=node[bisect.bisect_left(times,call['start_ns']):bisect.bisect_left(times,call['start_ns']+call['duration_ns'])]
   init=[x for x in relevant if x['type']==22];steps=[x for x in relevant if x['type']==23];read=[x for x in relevant if x['type']==17];submitted=sn[bisect.bisect_left(st,call['start_ns']):bisect.bisect_left(st,call['start_ns']+call['duration_ns'])]
   sync_overlap=sum(max(0,min(call['start_ns']+call['duration_ns'],x['start_ns']+x['duration_ns'])-max(call['start_ns'],x['start_ns'])) for x in sync)
   workloads=dict(call,initializations=len(init),steps=len(steps),init_ns=sum(x['duration_ns'] for x in init),step_ns=sum(x['duration_ns'] for x in steps),submit_ns=sum(x['duration_ns'] for x in submitted),sync_overlap_ns=sync_overlap,results=[])
   for x in read:
    req=x['request_id'];accept=requests.get(req);start=starts.get(req);end=ends.get(req)
    source=fullcalls[x['source_update_id']-1] if 0<x['source_update_id']<=len(fullcalls) else None
    workloads['results'].append({'request_id':req,'source_update_id':x['source_update_id'],'source_tracker_frame_id':x['source_tracker_frame_id'],'tracker_frame_id':x['tracker_frame_id'],'has_target':bool(x['flags']&512),'source_frame_age':x['tracker_frame_id']-x['source_tracker_frame_id'],'source_to_read_ns':x['mono_ns']-source['start_ns'] if source else None,'accepted_to_start_ns':start['mono_ns']-accept['mono_ns'] if start and accept else None,'detect_ns':end['mono_ns']-start['mono_ns'] if start and end else None,'end_to_read_ns':x['mono_ns']-end['mono_ns'] if end else None})
   detailed.append(workloads)
  p=pct([x['duration_ns'] for x in calls]);slow=[x for x in detailed if x['duration_ns']>=p];initcalls=[x for x in detailed if x['initializations']];normal=[x for x in detailed if not x['initializations']]
  period=(window['measurement_end_ns']-window['measurement_start_ns'])/1e9
  withintrace=[x for x in trace if window['measurement_start_ns']<=x['mono_ns']<window['measurement_end_ns']]
  readings=[r for x in detailed for r in x['results']]
  resources=[x for x in case['resources'] if window['measurement_start_ns']<=x['mono_ns']<window['measurement_end_ns']];worker_resource=[(x['mono_ns'],x['processes'].get('worker-'+str(index))) for x in resources];worker_resource=[(t,p) for t,p in worker_resource if p and 'utime_ticks'in p];worker_cpu=None
  if len(worker_resource)>1:
   a,b=worker_resource[0],worker_resource[-1];worker_cpu=((b[1]['utime_ticks']+b[1]['stime_ticks'])-(a[1]['utime_ticks']+a[1]['stime_ticks']))/b[1]['hz']/((b[0]-a[0])/1e9)*100
  record={'window':window,'calls':len(calls),'fps':sum(x['consumed']for x in calls)/period,'p99_ns':p,'normal_p99_ns':pct([x['duration_ns']for x in normal]),'initializations':len(initcalls),'total_steps':sum(x['steps'] for x in initcalls),'init_work_fraction_min_slow':min((x['init_ns']+x['step_ns'])/x['duration_ns']for x in slow),'step_median_ns':pct([x['duration_ns']for x in withintrace if x['type']==23],.5),'step_p99_ns':pct([x['duration_ns']for x in withintrace if x['type']==23]),'cpu_fraction_median_slow':pct([x['cpu_ns']/x['duration_ns'] for x in slow],.5),'submit_p99_ns':pct([x['duration_ns']for x in other]),'worker_cpu_percent':worker_cpu,'sync_calls':len(sync),'sync_p99_ns':pct([x['duration_ns']for x in sync]),'sync_max_ns':max([x['duration_ns']for x in sync],default=0),'sync_errors':sum(x['result']!=0 for x in sync),'source_age_histogram':dict(collections.Counter(str(x['source_frame_age'])for x in readings)),'workload_histogram':dict(collections.Counter(str(x['initializations'])+':'+str(x['steps'])for x in detailed)),'slow_calls':slow,'status':(folder/'sampling-status.txt').read_text(),'all_calls_file':str(folder/'classified-calls.json')}
  (folder/'classified-calls.json').write_text(json.dumps(detailed,indent=2)+'\n');item['modes'].append(record)
  print(directory.name,index,window.get('label'),len(calls),'p99',round(p/1e6,3),'init',len(initcalls),'steps',record['total_steps'],'normalP99',round(record['normal_p99_ns']/1e6,3),'cpuSlow',round(record['cpu_fraction_median_slow'],3),'workerCPU',worker_cpu,'syncP99',record['sync_p99_ns'])
 out.append(item)
(root/'investigation-summary.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n')
