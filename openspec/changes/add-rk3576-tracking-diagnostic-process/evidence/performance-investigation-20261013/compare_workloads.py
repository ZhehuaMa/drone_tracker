from pathlib import Path
import json,statistics,math,csv,re,sys
out=[]
def pct(xs,p=.99):return sorted(xs)[math.ceil(len(xs)*p)-1] if xs else None
for summary_path in Path(sys.argv[1]).glob('window*/evidence/investigation-summary.json'):
 data=json.loads(summary_path.read_text())
 for case in data:
  modes=case['modes'];detail=[];rows=[]
  for i,m in enumerate(modes):
   calls=json.loads(Path(m['all_calls_file']).read_text());init=[c for c in calls if c['initializations']==1];x=[c['steps']for c in init];y=[c['duration_ns']/1e6 for c in init];mx=statistics.mean(x);my=statistics.mean(y);slope=sum((a-mx)*(b-my)for a,b in zip(x,y))/sum((a-mx)**2 for a in x);r2=sum((slope*(a-mx))**2 for a in x)/sum((a-my)**2 for a in y)
   q=min(m['slow_calls'],key=lambda c:c['duration_ns']);root=summary_path.parent/Path(case['case']).name;log=(root/f'{i}-{m["window"]["label"]}.log').read_text();tid=re.search(r'tid=(\d+) role=tracking',log).group(1)
   raw=json.loads((root/'case.json').read_text());name=f'{i}-{m["window"]["label"]}';rs=[r for r in raw['resources']if m['window']['measurement_start_ns']<=r['mono_ns']<m['window']['measurement_end_ns'] and name in r['processes']];a,b=rs[0],rs[-1];ta=[int(v) for v in a['processes'][name]['task_stats'][tid]['schedstat'].split()];tb=[int(v) for v in b['processes'][name]['task_stats'][tid]['schedstat'].split()];elapsed=(b['mono_ns']-a['mono_ns'])/1e9;nr=sum(a['mono_ns']<=c['start_ns']<b['mono_ns']for c in calls);runnable_wait=(tb[1]-ta[1])/1e9;cpu=(tb[0]-ta[0])/1e9
   trace=[{k:int(v) for k,v in r.items()}for r in csv.DictReader((root/name/'investigation.csv').open())];selected=[r for r in trace if r['producer']==0 and r['type']==6 and m['window']['measurement_start_ns']<=r['mono_ns']<m['window']['measurement_end_ns']];skipped=[r for r in trace if r['producer']==0 and r['type']==5 and m['window']['measurement_start_ns']<=r['mono_ns']<m['window']['measurement_end_ns']]
   stages={}
   for t in trace:
    if t['producer']==1 and t['type']==15:stages.setdefault(t['request_id'],[]).append(t)
   r={'label':m['window']['label'],'init_call_count':len(init),'slope_ms_per_step':slope,'r2':r2,'p99_call':q,'schedstat_sample_seconds':elapsed,'calls_in_schedstat_interval':nr,'tracking_cpu_seconds_per_second':cpu/elapsed,'tracking_runnable_wait_ms_per_second':runnable_wait*1000/elapsed,'tracking_runnable_wait_ms_per_call':runnable_wait*1000/nr,'selected_count_window':len(selected),'skipped_count_window':len(skipped),'p99_request_stages':[(s['extra'],s['duration_ns'])for yolo in q['results'] for s in stages.get(yolo['request_id'],[])]};detail.append(r);rows.append(calls)
  left={n:[x['duration_ns']for x in rows[0]if x['initializations']==1 and x['steps']==n] for n in set(x['steps']for x in rows[0]if x['initializations']==1)};right={n:[x['duration_ns']for x in rows[1]if x['initializations']==1 and x['steps']==n]for n in set(x['steps']for x in rows[1]if x['initializations']==1)}
  matched=[]
  for steps in sorted(left.keys()&right.keys()):
   a,b=left[steps],right[steps];matched.append({'steps':steps,'left_n':len(a),'right_n':len(b),'left_median_ns':pct(a,.5),'right_median_ns':pct(b,.5),'median_delta_ns':pct(b,.5)-pct(a,.5),'left_p99_ns':pct(a),'right_p99_ns':pct(b),'p99_delta_ns':pct(b)-pct(a)})
  qa,qb=[x['p99_call'] for x in detail];result={'case':str(summary_path.parent/Path(case['case']).name),'modes':detail,'matched_step_counts':matched,'p99_difference_ns':qb['duration_ns']-qa['duration_ns'],'p99_work_difference_ns':qb['init_ns']+qb['step_ns']-qa['init_ns']-qa['step_ns'],'p99_remaining_difference_ns':(qb['duration_ns']-qb['init_ns']-qb['step_ns'])-(qa['duration_ns']-qa['init_ns']-qa['step_ns'])};out.append(result)
  print(Path(case['case']).name,[x['label']for x in detail],'delta',result['p99_difference_ns']/1e6,'workdelta',result['p99_work_difference_ns']/1e6,'otherdelta',result['p99_remaining_difference_ns']/1e6)
Path(sys.argv[1]).parent.joinpath('workload-comparison.json').write_text(json.dumps(out,indent=2)+'\n')
