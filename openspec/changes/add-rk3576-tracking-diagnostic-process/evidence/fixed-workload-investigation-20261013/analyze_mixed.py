"""同进程相邻随机对复算；不扣模型或观察成本。"""
from pathlib import Path
import csv,json,math,sys
import numpy as np
ROOT=Path(sys.argv[1]);rng=np.random.default_rng(20261015)
def rows(p):return list(csv.DictReader(p.open()))
def p99(a):return float(np.sort(np.asarray(a))[math.ceil(.99*len(a))-1])
def stats(a):return {'n':len(a),'median_ms':float(np.median(a)),'p99_ms':p99(a),'max_ms':float(np.max(a))}
results=[]
for case in sorted(ROOT.glob('pair*')):
 p=case/'0-mixed'
 if not (p/'fixed-calls.csv').exists():continue
 calls=rows(p/'fixed-calls.csv');blocks=rows(p/'blocks.csv');n=len(blocks)//2
 ordered=np.array([int(x['duration_ns'])/1e6 for x in calls]).reshape(2*n,40);cpu=np.array([int(x['cpu_ns'])/1e6 for x in calls]).reshape(2*n,40)
 modes=np.array([int(x['mode']) for x in blocks]);off=[];basic=[];offcpu=[];basiccpu=[];orders=[]
 for pair in range(n):
  ia=2*pair;ib=ia+1
  if modes[ia]:orders.append('basic/off');basic.append(ordered[ia]);off.append(ordered[ib]);basiccpu.append(cpu[ia]);offcpu.append(cpu[ib])
  else:orders.append('off/basic');off.append(ordered[ia]);basic.append(ordered[ib]);offcpu.append(cpu[ia]);basiccpu.append(cpu[ib])
 off=np.array(off);basic=np.array(basic);offcpu=np.array(offcpu);basiccpu=np.array(basiccpu)
 record={'case':case.name,'metadata':(p/'metadata.txt').read_text(),'n_pairs':n,'order_sequence':orders,'all_model_workload_and_behavior_verified':all(x['templates']=='1' and x['searches']=='39' and x['behavior_same']=='1' and x['drain_complete']=='1' for x in blocks),'drain_wait_ns':{'min':min(int(x['drain_wait_ns']) for x in blocks),'max':max(int(x['drain_wait_ns']) for x in blocks)},'categories':{}}
 for category,columns in [('all',np.arange(40)),('catchup',[19]),('ordinary',np.arange(20,40)),('waiting',np.arange(19))]:
  a=off[:,columns];b=basic[:,columns];actual=p99(b.ravel())-p99(a.ravel());iid=[];moving=[];permutations=[]
  for _ in range(4000):
   ix=rng.integers(0,n,n);iid.append(p99(b[ix].ravel())-p99(a[ix].ravel()))
   starts=rng.integers(0,n,math.ceil(n/5));ix=np.concatenate([(start+np.arange(5))%n for start in starts])[:n];moving.append(p99(b[ix].ravel())-p99(a[ix].ravel()))
   swap=rng.integers(0,2,n).astype(bool);pa=np.where(swap[:,None],b,a);pb=np.where(swap[:,None],a,b);permutations.append(p99(pb.ravel())-p99(pa.ravel()))
  record['categories'][category]={'off':stats(a.ravel()),'basic':stats(b.ravel()),'basic_minus_off_p99_ms':actual,'paired_bootstrap95_ms':np.quantile(iid,[.025,.975]).tolist(),'moving_five_pairs_bootstrap95_ms':np.quantile(moving,[.025,.975]).tolist(),'actual_assignment_permutation_sharp_null_two_sided_p':(1+sum(abs(x)>=abs(actual) for x in permutations))/4001,'paired_call_delta':stats((b-a).ravel()),'off_cpu':stats(offcpu[:,columns].ravel()),'basic_cpu':stats(basiccpu[:,columns].ravel())}
 record['long_pair_deltas_ms']=(basic[:,19]-off[:,19]).tolist();record['block_order']=blocks
 results.append(record)
(ROOT/'mixed-analysis.json').write_text(json.dumps(results,indent=2))
for r in results:
 print(r['case'],r['n_pairs'],r['all_model_workload_and_behavior_verified'])
 for k,v in r['categories'].items():print(k,v['off']['p99_ms'],v['basic']['p99_ms'],v['basic_minus_off_p99_ms'],v['moving_five_pairs_bootstrap95_ms'],v['actual_assignment_permutation_sharp_null_two_sided_p'])
