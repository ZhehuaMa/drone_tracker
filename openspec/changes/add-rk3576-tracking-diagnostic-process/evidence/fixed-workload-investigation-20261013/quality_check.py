"""核对固定/随机工作量、保护指纹、完整原件及恢复记录。"""
from pathlib import Path
import csv,json,hashlib,math
import numpy as np
ROOT=Path(__file__).parent
RAW=ROOT/'raw'
result={'windows':{},'fixed_behavior_differences':{},'mixed':{},'localization':{}}
def rows(p):return list(csv.DictReader(p.open()))
for name,folder in [('window01','window01'),('window02','window02-complete'),('window03','window03-complete'),('window04','window04-complete')]:
 p=RAW/folder;before=json.loads((p/'protected-before.json').read_text());after=json.loads((p/'protected-after.json').read_text());restore=json.loads((p/'restore.json').read_text());windows=json.loads((p/'window-results.json').read_text())
 result['windows'][name]={'protected_equal':before==after,'maintenance_seconds':restore['elapsed_seconds'],'formal_restore':restore['formal_service'],'all_groups_within150_and_recovered':all(x['elapsed_seconds']<=150 and x['resources_recovered'] for x in windows),'failed_groups':[x['unit'] for x in windows if x['systemd_state'].get('ExecMainStatus')!='0'],'groups':len(windows)}
 if (p/'final-state.json').exists():
  state=json.loads((p/'final-state.json').read_text());result['windows'][name].update(final_sha_equal=state['protected_sha_equal'],final_pid=state['formal']['MainPID'],hdmi_formal=state['formal_hdmi_found'],remaining=state['remaining'])
 assert before==after and restore['elapsed_seconds']<1800
 assert all(x['elapsed_seconds']<=150 and x['resources_recovered'] for x in windows)
for folder in ['window02-complete','window03-complete']:
 analysis=json.loads((RAW/folder/'analysis.json').read_text())
 for a in analysis:
  result['fixed_behavior_differences'][folder+'/'+a['pair']]=len(a.get('behavior_differences',[]));assert not a.get('behavior_differences',[])
# 新旧两次audit分别保留和核对，只有修正后的亮度读取进入正式归因表。
result['audits']={}
for folder,pair in [('window01','pair7'),('window02-complete','pair7')]:
 p=RAW/folder/pair;equal=(p/'0-off/model-audit.csv').read_bytes()==(p/'1-basic/model-audit.csv').read_bytes();assert equal;result['audits'][folder]={'model_input_and_final_output_bytes_equal':equal,'api_rows_each':len(rows(p/'0-off/model-audit.csv'))}
for case in ['pair1','pair2']:
 p=RAW/'window04-complete'/case/'0-mixed';calls=rows(p/'fixed-calls.csv');partial=rows(p/'partial-calls.csv');blocks=rows(p/'blocks.csv');assert len(calls)==3200 and len(partial)==3200 and len(blocks)==80
 assert all(x['templates']=='1' and x['searches']=='39' and x['behavior_same']=='1' and x['drain_complete']=='1' and int(x['drain_wait_ns'])<=250000000 for x in blocks)
 assert sum(x['mode']=='1' for x in blocks)==40
 keys=['index','block','position','mode','start_ns','duration_ns','cpu_ns','state','tracking','consumed','score','x','y','w','h']
 assert all(all(x[k]==y[k] for k in keys) for x,y in zip(calls,partial))
 assert not (p/'submissions.csv').exists()
 metadata=(p/'metadata.txt').read_text();assert all('dropped_'+str(i)+'=0' in metadata for i in range(4))
 result['mixed'][case]={'calls':len(calls),'blocks':len(blocks),'each_mode_blocks':40,'each_mode_calls':1600,'each_mode_long_calls':40,'partial_exact_equal':True,'model_calls_templates':80,'model_calls_searches':3120,'drain_max_ns':max(int(x['drain_wait_ns']) for x in blocks)}
 events=[json.loads(x) for f in p.glob('export-*/events.jsonl') for x in f.read_text().splitlines()];by={}
 for event in events:
  if event['type'] in ['初始化结束','补跟踪步骤']:
   value=by.setdefault(event['update_id'],{'ns':0,'init':0,'steps':0});value['ns']+=event['duration_ns'];value['init']+=event['type']=='初始化结束';value['steps']+=event['type']=='补跟踪步骤'
 fractions=[];residual=[]
 for i,call in enumerate(calls):
  if call['mode']=='1' and call['position']=='20':
   value=by[i+81];assert value['init']==1 and value['steps']==19;fractions.append(value['ns']/int(call['duration_ns']));residual.append((int(call['duration_ns'])-value['ns'])/1e6)
 long_wall=[];long_cpu=[];ordinary_wall=[];ordinary_cpu=[]
 for pair in range(40):
  a=2*pair if blocks[2*pair]['mode']=='0' else 2*pair+1;b=2*pair+1 if a==2*pair else 2*pair
  for position in range(19,40):
   off=calls[a*40+position];basic=calls[b*40+position];w=(int(basic['duration_ns'])-int(off['duration_ns']))/1e6;c=(int(basic['cpu_ns'])-int(off['cpu_ns']))/1e6
   (long_wall if position==19 else ordinary_wall).append(w);(long_cpu if position==19 else ordinary_cpu).append(c)
 result['localization'][case]={'nano_intervals_coverage_min':min(fractions),'nano_intervals_coverage_median':float(np.median(fractions)),'outside_intervals_ms_median':float(np.median(residual)),'outside_intervals_ms_max':max(residual),'long_paired_wall_ms_median':float(np.median(long_wall)),'long_paired_cpu_ms_median':float(np.median(long_cpu)),'long_paired_non_cpu_ms_median':float(np.median(np.array(long_wall)-np.array(long_cpu))),'ordinary_paired_wall_ms_median':float(np.median(ordinary_wall)),'ordinary_paired_cpu_ms_median':float(np.median(ordinary_cpu)),'ordinary_paired_non_cpu_ms_median':float(np.median(np.array(ordinary_wall)-np.array(ordinary_cpu)))}
(ROOT/'quality-check.json').write_text(json.dumps(result,ensure_ascii=False,indent=2));print(json.dumps(result,ensure_ascii=False,indent=2))
