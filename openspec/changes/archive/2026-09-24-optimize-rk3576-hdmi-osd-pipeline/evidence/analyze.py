"""从完整计时窗口汇总；P95 使用各窗口统计可证明的保守上界。"""
import json, re, sys, math
from pathlib import Path
root=Path(sys.argv[1])
report=[]
for result_file in sorted(root.glob('*.json')):
 if not re.fullmatch(r'(comparison|endurance)-\d+-(off|legacy|cached-overlay)\.json', result_file.name):continue
 result=json.loads(result_file.read_text())
 live=result['live']; end=result['end']; stable_seconds=end-live-10
 tracking=sum(live+10<=t<=end for t,_ in result['boxes'])/stable_seconds
 row={'run':result_file.stem,'tracking_fps':tracking,'valid_boxes':result['valid_boxes'],'exit_code':result['exit_code'],
      'stable_seconds':stable_seconds,'stages':{},'window_seconds':0,'displayed':0,'failures':0}
 windows=[]; current=None
 for line in result_file.with_suffix('.log').read_text().splitlines():
  if 'OSD timing ' in line:
   stamp=re.search(r'\[(\d{10}\.\d+)\]',line); duration=re.search(r'window_us=(\d+)',line)
   current=None
   if stamp and duration:
    stop=float(stamp[1]); seconds=int(duration[1])/1e6
    if stop-seconds>=result['live_wall']+10 and stop<=result['end_wall']:
     current={'seconds':seconds};windows.append(current)
  elif current is not None and 'new_video result=success' in line:
   current['count']=int(re.search(r'count=(\d+)',line)[1])
   current['stages']={name:dict(count=int(count),mean=float(mean),max=int(maximum),p95=int(p95))
     for name,count,mean,maximum,p95 in re.findall(r'(\w+)\{count=(\d+),mean=([\d.e+-]+),max=(\d+),p95_approx=(\d+)',line)}
  elif current is not None and 'result=failure' in line:
   row['failures']+=int(re.search(r'count=(\d+)',line)[1])
  if current is not None and 'new_video result=success' in line and 'stages' in current:
   for name,raw in re.findall(r'(\w+)\{[^}]*,buckets=([\d:]+)',line):
    current['stages'][name]['buckets']=[int(v) for v in raw.split(':')]
 for window in windows:
  row['window_seconds']+=window['seconds'];row['displayed']+=window.get('count',0)
  for name,values in window.get('stages',{}).items():
   stage=row['stages'].setdefault(name,{'count':0,'total_us':0,'max_us':0,'upper_samples':[],'buckets':[0]*28,'histogram_count':0})
   n=values['count'];stage['count']+=n;stage['total_us']+=n*values['mean'];stage['max_us']=max(stage['max_us'],values['max'])
   if 'buckets' in values:
    assert len(values['buckets'])==28 and sum(values['buckets'])==n
    stage['buckets']=[a+b for a,b in zip(stage['buckets'],values['buckets'])];stage['histogram_count']+=n
   rank=(n*95+99)//100
   stage['upper_samples'] += [(min(values['p95'],values['max']),rank),(values['max'],n-rank)]
 for name,stage in row['stages'].items():
  rank=(stage['count']*95+99)//100; count=0;upper=0
  for value,n in sorted(stage.pop('upper_samples')):
   count+=n
   if count>=rank:upper=value;break
  if stage['histogram_count']==stage['count']:
   cumulative=0
   for bucket,n in enumerate(stage['buckets']):
    cumulative+=n
    if cumulative>=rank:upper=(1<<bucket) if bucket<27 else stage['max_us'];break
   stage['p95_method']='merged_pow2_histogram'
  else:stage['p95_method']='window_summary_upper_bound'
  stage['mean_ms']=stage.pop('total_us')/stage['count']/1000
  stage['p95_upper_ms']=upper/1000;stage['max_ms']=stage.pop('max_us')/1000
 row['hdmi_fps']=row['displayed']/row['window_seconds'] if row['window_seconds'] else None
 report.append(row)
base=next((r['tracking_fps'] for r in report if r['run'].endswith('-off')),None)
for row in report:
 if 'cached-overlay' in row['run']:
  stages=row['stages'];row['tracking_drop_percent']=100*(1-row['tracking_fps']/base) if base else None
  row['passes']=bool(stages and stages['render_total']['mean_ms']<=40 and stages['render_total']['p95_upper_ms']<=65.536
   and row['hdmi_fps']>=25 and stages['submit_to_display']['mean_ms']<=60 and row['tracking_drop_percent']<=5
   and row['failures']==0 and row['exit_code']==0)
print(json.dumps(report,indent=2))
