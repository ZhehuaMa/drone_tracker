import json,math,pathlib,re,statistics,sys
root=pathlib.Path(sys.argv[1]); rows=[]
for meta in sorted(root.glob('comparison-[0-9]*.json')):
    data=json.loads(meta.read_text()); label=meta.stem; path=label.split('-',2)[2]
    lo=data['live']+10;hi=data['end'];elapsed=hi-lo
    boxes=[box for box in data['boxes'] if lo<=box[0]<=hi]
    samples=[s for s in data['resources'] if lo<=s['mono']<=hi]
    row=dict(label=label,path=path,duration_sec=elapsed,tracking_fps=len(boxes)/elapsed,valid_boxes=sum(bool(x[1]) for x in boxes),exit_code=data['exit_code'],display_failure=data['display_failure'])
    if len(samples)>1:row['cpu_percent']=(samples[-1]['cpu_ticks']-samples[0]['cpu_ticks'])/(samples[-1]['mono']-samples[0]['mono'])
    for name in ['rss_kb','threads','fds']:
        row[name+'_range']=[min(s[name] for s in samples),max(s[name] for s in samples)]
    text=meta.with_suffix('.log').read_text();windows=[]
    row['tracker_main_affinities']=sorted(set(s['affinity'] for s in samples))
    row['tracker_thread_affinities']=sorted(set(a for s in samples for a in s['thread_affinities']))
    row['production_policy_loaded']='CPU policy loaded mode=role-pinned' in text
    row['matching_affinity']=row['production_policy_loaded'] and row['tracker_main_affinities']==['4'] and all(a in ['4','5','6','7','4-7'] for a in row['tracker_thread_affinities'])
    blocks=re.split(r'(?=\[INFO\].*OSD timing round=)',text)
    for block in blocks:
        if 'OSD timing round=' not in block:continue
        head=block.splitlines()[0]
        stamp=re.search(r'\[([0-9]+\.[0-9]+)\]',head);duration=re.search(r'window_us=(\d+)',head)
        if not stamp or not duration:continue
        end=float(stamp[1]);duration=float(duration[1])/1e6
        if end-duration<data['live_wall']+10 or end>data['end_wall']:continue
        match=re.search(r'^  new_video result=success[^\n]*',block,re.M)
        if not match:continue
        windows.append((duration,head,match[0]))
    if windows:
        row['hdmi_fps']=sum(int(re.search(r' displayed=(\d+)',h)[1]) for _,h,_ in windows)/sum(d for d,_,_ in windows)
        row['timing_windows']=len(windows);row['timing_seconds']=sum(d for d,_,_ in windows)
        for stage in ['render_total','submit_to_display']:
            total=0;count=0;buckets=[0]*28
            for _,_,line in windows:
                fields=dict(part.split('=',1) for part in re.search(stage+r'\{([^}]+)\}',line)[1].split(','))
                n=int(fields['count']);count+=n;total+=n*float(fields['mean'])
                for i,bucket in enumerate(fields['buckets'].split(':')):buckets[i]+=int(bucket)
            row[stage+'_mean_ms']=total/count/1000
            cumulative=0;rank=math.ceil(count*.95)
            for i,bucket in enumerate(buckets):
                cumulative+=bucket
                if cumulative>=rank:row[stage+'_p95_upper_ms']=2**i/1000;break
    rows.append(row)
off=[r for r in rows if r['path']=='off'];baseline=statistics.median(r['tracking_fps'] for r in off) if off else None
for row in rows:
    if row['path']=='cached-overlay':
        row['tracking_decline_percent']=100*(baseline-row['tracking_fps'])/baseline
        row['pass']=all([row['duration_sec']>=60,row['render_total_mean_ms']<=40,row['render_total_p95_upper_ms']<=65.536,row['hdmi_fps']>=25,row['submit_to_display_mean_ms']<=60,row['tracking_decline_percent']<=5,row['exit_code']==0,not row['display_failure']])
result=dict(baseline_median_fps=baseline,rows=rows,passed=len(rows)==9 and all(r.get('pass',True) and r['matching_affinity'] and r['exit_code']==0 and not r['display_failure'] for r in rows))
checkpoints=root/'comparison-checkpoints.jsonl'
if checkpoints.exists():
    # An aborted rerun must not pass because nine stale case files still exist.
    try:
        events=[json.loads(line) for line in checkpoints.read_text().splitlines()]
        start=max(i for i,event in enumerate(events) if event['event']=='identity')
        current=events[start:]
        complete=current[-1]['event']=='completed' and not any(event['event'] in ['failed','thermal_abort','thermal_monitor_failed','signal_abort'] or event.get('errors') for event in current)
    except (ValueError,KeyError,IndexError):
        complete=False
    result['diagnostics_completed']=complete
    result['passed']=result['passed'] and complete
print(json.dumps(result,indent=2));(root/'comparison-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
