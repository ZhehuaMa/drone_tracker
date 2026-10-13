from pathlib import Path
import json,csv,math,bisect,collections
raw=Path('.docker-artifacts/diagnostic-warning-fixes-20261012/raw-complete/ten-final/evidence')
out=[]
def percentile(values,p=.99):
 return sorted(values)[math.ceil(len(values)*p)-1] if values else None
for n in range(1,11):
 d=raw/f'pair{n}'; case=json.loads((d/'case.json').read_text()); row={'pair':n,'modes':{}}
 for idx,window in enumerate(case['rounds']):
  mode=window['mode']; calls=[{k:int(v) for k,v in x.items()} for x in csv.DictReader((d/f'{idx}-{mode}'/'calls.csv').open()) if window['measurement_start_ns']<=int(x['start_ns'])<window['measurement_end_ns']]
  p=percentile([x['duration_ns'] for x in calls]); result={'calls':len(calls),'p99_ns':p,'slow_calls':sum(x['duration_ns']>=p for x in calls),'fps':sum(x['consumed'] for x in calls)*1e9/(window['measurement_end_ns']-window['measurement_start_ns'])}
  if mode=='basic':
   events=[json.loads(line) for path in d.glob('export-*/events.jsonl') for line in path.open()]
   node=sorted((e for e in events if e['producer_id']==0 and e['producer_seq'] and e['update_id']),key=lambda e:e['mono_ns']); times=[x['mono_ns'] for x in node]
   joins=[]
   for call in calls:
    relevant=node[bisect.bisect_left(times,call['start_ns']):bisect.bisect_left(times,call['start_ns']+call['duration_ns'])]
    init=[e for e in relevant if e['type']=='初始化结束'];steps=[e for e in relevant if e['type']=='补跟踪步骤'];
    joins.append(dict(call,initializations=len(init),steps=len(steps),work_ns=sum(e['duration_ns'] for e in init+steps)))
   result['workload_histogram']=dict(collections.Counter((str(x['initializations'])+':'+str(x['steps'])) for x in joins));result['slow_workload']= [x for x in joins if x['duration_ns']>=p]
   result['all_init_calls']=sum(x['initializations']>0 for x in joins);result['total_steps']=sum(x['steps'] for x in joins)
   result['no_init_p99_ns']=percentile([x['duration_ns'] for x in joins if not x['initializations']]);result['slow_min_work_fraction']=min(x['work_ns']/x['duration_ns'] for x in joins if x['duration_ns']>=p)
  log=(d/f'{idx}-{mode}.log').read_text(); import re
  result['stdout_whole_run_initial_catchups']=[int(x) for x in re.findall(r'and caught up (\d+) frames',log)]
  row['modes'][mode]=result
 row['increment_ns']=row['modes']['basic']['p99_ns']-row['modes']['off']['p99_ns'];out.append(row)
Path('.docker-artifacts/performance-investigation-20261013/history-recompute.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n')
for r in out:
 b=r['modes']['basic'];o=r['modes']['off'];print(r['pair'],round(r['increment_ns']/1e6,6),'inits',b['all_init_calls'],'steps',b['total_steps'],'slow',len(b['slow_workload']),'noinitP99',b['no_init_p99_ns'],'workfraction',b['slow_min_work_fraction'])
