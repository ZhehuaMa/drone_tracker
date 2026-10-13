"""复算全部固定工作量调用，原始值不扣观察成本。"""
from pathlib import Path
import csv,json,math,sys,bisect
import numpy as np
ROOT=Path(sys.argv[1]);rng=np.random.default_rng(20261013)
def rows(p):return list(csv.DictReader(p.open()))
def p99(v):return sorted(v)[math.ceil(.99*len(v))-1] if len(v) else None
def stats(v):
 v=list(v);return {'n':len(v),'median':float(np.median(v)) if v else None,'p99':p99(v),'max':max(v) if v else None}
ALL={};results=[]
fields=['state','tracking','consumed','score','x','y','w','h','dcx','dcy','dw','dh','dvalid','rcx','rcy','rw','rh','rvalid']
for pair in sorted(ROOT.glob('pair*')):
 if not (pair/'case.json').exists():continue
 case=json.loads((pair/'case.json').read_text());conditions=[]
 for record in case['records']:
  out=pair/Path(record['directory']).name
  if not (out/'fixed-calls.csv').exists():continue
  calls=rows(out/'fixed-calls.csv');ALL[str(out)]=calls
  submits=rows(out/'submissions.csv') if (out/'submissions.csv').exists() else []
  starts=[int(x['start_ns']) for x in calls];sums=[0]*len(calls)
  for sub in submits:
   idx=bisect.bisect_right(starts,int(sub['start_ns']))-1
   if idx>=0 and int(sub['start_ns'])<starts[idx]+int(calls[idx]['duration_ns']):sums[idx]+=int(sub['duration_ns'])
  summaries={}
  for cat,predicate in [('all',lambda p:True),('catchup',lambda p:p==20),('ordinary',lambda p:p>20),('waiting',lambda p:p<20)]:
   selected=[(i,x) for i,x in enumerate(calls) if predicate(int(x['position']))]
   summaries[cat]={'wall_ms':stats(int(x['duration_ns'])/1e6 for _,x in selected),'cpu_ms':stats(int(x['cpu_ns'])/1e6 for _,x in selected),
    'measured_submit_sum_us':stats(sums[i]/1e3 for i,_ in selected),'preparation_sum_us':stats(int(x['preparation_ns'])/1e3 for _,x in selected),
    'model_run_ms':stats(int(x['stage_12_ns'])/1e6 for _,x in selected),'init_ms':stats(int(x['init_ns'])/1e6 for _,x in selected),'step_sum_ms':stats(int(x['step_ns'])/1e6 for _,x in selected)}
  cal=rows(out/'calibration.csv');calibration={k:stats(int(x[k]) for x in cal) for k in cal[0]}
  conditions.append({'record':record,'summary':summaries,'submit_ns':stats(int(x['duration_ns']) for x in submits),'calibration_ns':calibration,
   'metadata':(out/'metadata.txt').read_text(),'sampling_status':(out/'sampling-status.txt').read_text() if (out/'sampling-status.txt').exists() else 'disabled'})
 pair_result={'pair':pair.name,'conditions':conditions,'audit_only':any(c['record']['audit'] for c in conditions)}
 if len(conditions)==2:
  a=ALL[str(pair/Path(case['records'][0]['directory']).name)];b=ALL[str(pair/Path(case['records'][1]['directory']).name)]
  compared=fields+(['event_hash','events','init','steps'] if all(x['record']['observer'] for x in conditions) else [])
  pair_result['behavior_differences']=[{'index':i,'fields':[k for k in compared if x[k]!=y[k]]} for i,(x,y) in enumerate(zip(a,b)) if any(x[k]!=y[k] for k in compared)]
  pair_result['quantile_differences_ms']={}
  for cat,predicate in [('all',lambda p:True),('catchup',lambda p:p==20),('ordinary',lambda p:p>20),('waiting',lambda p:p<20)]:
   aval=np.array([int(x['duration_ns'])/1e6 for x in a if predicate(int(x['position']))]);bval=np.array([int(x['duration_ns'])/1e6 for x in b if predicate(int(x['position']))])
   blockcount=int(case['records'][0]['cycles']);width=len(aval)//blockcount
   boots=[]
   for _ in range(0 if pair_result['audit_only'] else 2000):
    aa=aval.reshape(blockcount,width)[rng.integers(0,blockcount,blockcount)].ravel();bb=bval.reshape(blockcount,width)[rng.integers(0,blockcount,blockcount)].ravel()
    boots.append(p99(bb.tolist())-p99(aa.tolist()))
   pair_result['quantile_differences_ms'][cat]={'second_minus_first':p99(bval.tolist())-p99(aval.tolist()),'bootstrap_95_conditional':np.quantile(boots,[.025,.975]).tolist() if boots else None,
     'n_each':len(aval),'independent_blocks_each':blockcount,'matched_delta_ms':stats((bval-aval).tolist())}
 results.append(pair_result)
(ROOT/'analysis.json').write_text(json.dumps(results,indent=2))
for result in results:
 print(result['pair'],len(result.get('behavior_differences',[])),[(c['record']['mode'],c['record']['observer'],c['summary']['all']['wall_ms']['p99'],c['summary']['ordinary']['wall_ms']['p99']) for c in result['conditions']],result.get('quantile_differences_ms',{}).get('all'))
