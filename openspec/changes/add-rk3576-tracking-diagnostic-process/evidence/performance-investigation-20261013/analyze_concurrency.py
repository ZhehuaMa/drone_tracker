from pathlib import Path
import csv,statistics,json,sys
output=[]
for root in sorted(Path(sys.argv[1]).glob('window*/evidence/pair*')):
 if not root.is_dir():continue
 case=json.loads((root/'case.json').read_text())
 for index,window in enumerate(case['rounds']):
  f=root/(str(index)+'-'+window['label'])/'investigation.csv';trace=[{k:int(v)for k,v in r.items()}for r in csv.DictReader(f.open())];starts={r['request_id']:r['mono_ns']for r in trace if r['type']==14};ends={r['request_id']:r['mono_ns']for r in trace if r['type']==16};groups=[[],[]];details=[]
  for r in trace:
   if r['type']!=22 or not r['flags']&128 or not window['measurement_start_ns']<=r['mono_ns']<window['measurement_end_ns']:continue
   end=r['mono_ns'];start=end-r['duration_ns'];hits=[k for k in starts.keys()&ends.keys()if start<ends[k]and end>starts[k]];groups[int(bool(hits))].append(r['duration_ns'])
   details.append({'update_id':r['update_id'],'init_ns':r['duration_ns'],'concurrent_yolo_requests':hits})
  row={'case':str(root),'label':window['label'],'idle_n':len(groups[0]),'idle_median_ns':statistics.median(groups[0])if groups[0]else None,'yolo_overlap_n':len(groups[1]),'yolo_overlap_median_ns':statistics.median(groups[1])if groups[1]else None,'initializations':details};output.append(row)
  print(root.name,row['label'],'idle',row['idle_n'],row['idle_median_ns'],'overlap',row['yolo_overlap_n'],row['yolo_overlap_median_ns'])
Path(sys.argv[1]).parent.joinpath('concurrency-analysis.json').write_text(json.dumps(output,indent=2)+'\n')
