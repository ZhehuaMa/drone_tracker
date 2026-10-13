"""同进程随机存储对照；保留原始 P99，不减模型或校准常数。"""
from pathlib import Path
import csv,json,math,sys
import numpy as np
ROOT=Path(sys.argv[1]);rng=np.random.default_rng(20261016)
def rows(p):return list(csv.DictReader(p.open()))
def p99(a):a=np.sort(np.asarray(a));return float(a[math.ceil(.99*len(a))-1])
def stats(a):return {'n':len(a),'median_ms':float(np.median(a)),'p99_ms':p99(a),'max_ms':float(np.max(a))}
results=[]
for case in sorted(ROOT.glob('pair*')):
 p=case/'0-mixed'
 if case.name=='pair1' or not (p/'fixed-calls.csv').exists():continue
 calls=rows(p/'fixed-calls.csv');blocks=rows(p/'blocks.csv');n=len(blocks)//2
 wall=np.array([int(x['duration_ns'])/1e6 for x in calls]).reshape(2*n,40);cpu=np.array([int(x['cpu_ns'])/1e6 for x in calls]).reshape(2*n,40)
 modes=np.array([int(x['mode']) for x in blocks]); a=np.array([wall[2*i+int(modes[2*i])] for i in range(n)]); b=np.array([wall[2*i+int(not modes[2*i])] for i in range(n)]);ac=np.array([cpu[2*i+int(modes[2*i])] for i in range(n)]);bc=np.array([cpu[2*i+int(not modes[2*i])] for i in range(n)])
 record={'case':case.name,'n_pairs':n,'metadata':(p/'metadata.txt').read_text(),'orders':['tmpfs/emmc' if x else 'emmc/tmpfs' for x in modes[::2]],'categories':{}}
 for category,cols in [('all',np.arange(40)),('catchup',[19]),('ordinary',np.arange(20,40)),('waiting',np.arange(19))]:
  av=a[:,cols];bv=b[:,cols];actual=p99(bv.ravel())-p99(av.ravel());iid=[];moving=[];perms=[];medwall=[];medcpu=[];mednon=[]
  for _ in range(4000):
   ix=rng.integers(0,n,n);iid.append(p99(bv[ix].ravel())-p99(av[ix].ravel()))
   starts=rng.integers(0,n,math.ceil(n/5));ix=np.concatenate([(s+np.arange(5))%n for s in starts])[:n];moving.append(p99(bv[ix].ravel())-p99(av[ix].ravel()));medwall.append(np.median((bv-av)[ix]));medcpu.append(np.median((bc[:,cols]-ac[:,cols])[ix]));mednon.append(np.median(((b-bc)[:,cols]-(a-ac)[:,cols])[ix]))
   swap=rng.integers(0,2,n).astype(bool);pa=np.where(swap[:,None],bv,av);pb=np.where(swap[:,None],av,bv);perms.append(p99(pb.ravel())-p99(pa.ravel()))
  record['categories'][category]={'emmc':stats(av.ravel()),'tmpfs':stats(bv.ravel()),'tmpfs_minus_emmc_p99_ms':actual,'paired_bootstrap95_ms':np.quantile(iid,[.025,.975]).tolist(),'moving_five_pairs_bootstrap95_ms':np.quantile(moving,[.025,.975]).tolist(),'sharp_null_randomization_two_sided_p':(1+sum(abs(v)>=abs(actual) for v in perms))/4001,'paired_wall_delta':stats((bv-av).ravel()),'paired_cpu_delta':stats((bc[:,cols]-ac[:,cols]).ravel()),'paired_non_cpu_delta':stats(((b-bc)[:,cols]-(a-ac)[:,cols]).ravel()),'paired_median_moving5_ci95':{'wall':np.quantile(medwall,[.025,.975]).tolist(),'cpu':np.quantile(medcpu,[.025,.975]).tolist(),'non_cpu':np.quantile(mednon,[.025,.975]).tolist()},'emmc_cpu':stats(ac[:,cols].ravel()),'tmpfs_cpu':stats(bc[:,cols].ravel())}
 sync=rows(p/'tmpfs-originals/sync.csv');record['sync']={}
 for mode,name in [(0,'emmc'),(1,'tmpfs')]:
  values=[(int(v['end_ns'])-int(v['start_ns']))/1e6 for v in sync if int(v['block'])>=0 and modes[int(v['block'])]==mode];record['sync'][name]=stats(values)
 results.append(record)
(ROOT/'storage-analysis.json').write_text(json.dumps(results,indent=2))
for r in results:
 print(r['case'],r['n_pairs'])
 for k,v in r['categories'].items():print(k,'P99 delta',v['tmpfs_minus_emmc_p99_ms'],'CI',v['moving_five_pairs_bootstrap95_ms'],'p',v['sharp_null_randomization_two_sided_p'],'wall/cpu/non med',v['paired_wall_delta']['median_ms'],v['paired_cpu_delta']['median_ms'],v['paired_non_cpu_delta']['median_ms'])
 print('sync',r['sync'])
