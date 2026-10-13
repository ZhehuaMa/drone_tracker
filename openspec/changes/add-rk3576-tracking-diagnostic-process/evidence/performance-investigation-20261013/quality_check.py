from pathlib import Path
import json,csv
root=Path('.docker-artifacts/performance-investigation-20261013/raw');out=[]
for window in sorted(root.glob('window*')):
 evidence=window/'evidence'
 for pair in sorted(evidence.glob('pair*')):
  if not pair.is_dir() or not (pair/'case.json').exists():continue
  case=json.loads((pair/'case.json').read_text());external=json.loads((evidence/(pair.name+'-window.json')).read_text());statuses={}
  for status in pair.glob('*/sampling-status.txt'):
   entries=dict(line.split('=',1)for line in status.read_text().splitlines());statuses[str(status.relative_to(pair))]=entries;assert all(int(v)==0 for k,v in entries.items()if k.endswith('_overflow'))
  gap=drop=error=0
  for path in pair.glob('export-*/events.jsonl'):
   for line in path.open():
    e=json.loads(line);gap+=e['type']=='缺口'
    if e['type']=='健康':drop=max(drop,e['values'][3]);error=max(error,e['values'][6])
  sync_overflow=[]
  for path in pair.glob('sync-*.csv'):
   for line in path.open():
    if line.startswith('#overflow='):sync_overflow.append(int(line.split('=')[1]))
  assert case['execution_passed'] and not case['cleanup_errors'] and all(v==0 for v in case['exit'].values())
  assert external['elapsed_seconds']<=150 and not external['timeout'] and external['resources_recovered'] and not external['remaining_pids']
  assert not gap and not drop and not error and not any(sync_overflow)
  out.append({'case':str(pair),'elapsed':external['elapsed_seconds'],'execution_passed':True,'all_exit_zero':True,'recovered':True,'sampling_status':statuses,'gaps':gap,'dropped':drop,'health_error':error,'sync_trace_overflow':sync_overflow})
(root.parent/'quality-check.json').write_text(json.dumps(out,indent=2)+'\n');print('7组分析均正常退出、<150秒、回收、健康无缺口、观测无溢出')
