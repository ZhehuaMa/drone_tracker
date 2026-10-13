"""核对固定工作量、序列、双同步提交、事件负载、维护与资源。"""
import csv,json,struct,zlib,hashlib,sys
from pathlib import Path
ROOT=Path(sys.argv[1]);report={};allvalid=True
H=struct.Struct('<IHHIII')
def rows(p):return list(csv.DictReader(p.open()))
def scan(p):
 data=p.read_bytes();offset=0;events=[];commits=[];end=False
 while offset<len(data):
  raw=data[offset:offset+20]; magic,version,kind,length,crc,endian=H.unpack(raw);assert magic==0x44544731 and version==1 and endian==0x01020304
  payload=data[offset+20:offset+20+length];assert len(payload)==length and crc==(zlib.crc32(payload)^zlib.crc32(H.pack(magic,version,kind,length,0,endian)))
  if kind==1:
   assert length%256==0;events += [payload[i:i+256] for i in range(0,length,256)]
  elif kind==2:
   assert length==80;c=struct.unpack('<10Q',payload);assert c[0]==offset;commits.append(c)
  elif kind==3:assert payload==struct.pack('<I',1);end=True;end_boundary=offset+20+length
  else:assert False
  offset+=20+length
 assert end and commits and commits[-1][0]>=end_boundary
 return events,commits,len(data)
def q(e,offset):return struct.unpack_from('<Q',e,offset)[0]
def i(e,offset):return struct.unpack_from('<I',e,offset)[0]
def t(e):return struct.unpack_from('<H',e,4)[0]
for case in sorted(ROOT.glob('pair*')):
 p=case/'0-mixed'
 if not p.exists():continue
 calls=rows(p/'fixed-calls.csv');partial=rows(p/'partial-calls.csv');blocks=rows(p/'blocks.csv');positions=rows(p/'positions.csv');n=len(blocks);assert len(calls)==len(partial)==40*n
 assert all(b['templates']=='1' and b['searches']=='39' and b['drain_complete']=='1' and b['behavior_same']=='1' for b in blocks)
 for x,y in zip(calls,partial):assert all(x[k]==y[k] for k in ['index','block','position','mode','start_ns','duration_ns','cpu_ns','state','tracking','consumed','score','x','y','w','h'])
 ref=[{k:v for k,v in x.items() if k in ['position','state','tracking','consumed','score','x','y','w','h','dcx','dcy','dw','dh','dvalid','rcx','rcy','rw','rh','rvalid']} for x in calls[:40]]
 assert all([{k:v for k,v in x.items() if k in ref[0]} for x in calls[b*40:b*40+40]]==ref for b in range(n))
 for pos in positions:
  assert int(pos['dropped'])==0
  if pos['boundary'] in ['drained-old','opened-new','final-drained']:
   assert pos['generated']==pos['written']==pos['synced'];assert pos['normal_write']==pos['normal_read'] and pos['critical_write']==pos['critical_read']
 events=[];fileinfo=[];blockevents={};blockcommits={};health=[]
 files=list((p/'emmc').rglob('*.dtd'))+list((p/'tmpfs-originals/tmpfs').rglob('*.dtd'))
 for f in files:
  es,cs,size=scan(f);events+=es
  block=next((int(v[6:]) for v in f.parts if v.startswith('block-')), -1)
  fileinfo.append({'path':str(f.relative_to(ROOT)),'block':block,'events':len(es),'health':sum(t(e)==33 for e in es),'commits':len(cs),'bytes':size,'last_written':list(cs[-1][2:6]),'dropped':list(cs[-1][6:10])});assert not any(cs[-1][6:10]);health+=[e for e in es if t(e)==33]
  if block>=0:blockevents[block]=es;blockcommits[block]=cs
 for producer in range(4):
  seq=sorted(q(e,32) for e in events if i(e,12)==producer and q(e,32));assert seq==list(range(1,len(seq)+1))
 for b in range(n):
  start=next(x for x in positions if int(x['block'])==b and x['boundary']=='opened-new' and x['producer']=='0');end=next(x for x in positions if int(x['block'])==b+1 and x['boundary'] in ['drained-old','final-drained'] and x['producer']=='0')
  assert blockcommits[b][0][2]==int(start['synced']) and blockcommits[b][-1][2]==int(end['synced'])
 normalized=[];businesscounts=[]
 for b in range(n):
  es=[e for e in blockevents[b] if q(e,32) and i(e,12)==0];reqs=[q(e,88) for e in es if q(e,88)];base=min(reqs)-1 if reqs else 0;out=[]
  for e in es:
   value=bytearray(e)
   for off in [16,24,32,40,128]:struct.pack_into('<Q',value,off,0)
   for off in [72,96]:
    v=q(e,off)
    if v:struct.pack_into('<Q',value,off,(v-1)%40+1)
   if q(e,88):struct.pack_into('<Q',value,88,q(e,88)-base)
   if t(e)==13:
    for off in [184+3*8,184+4*8,184+5*8]:
     if q(e,off):struct.pack_into('<Q',value,off,q(e,off)-base)
   out.append(bytes(value))
  normalized.append(b''.join(out));businesscounts.append(len(es))
 different=[b for b in range(n) if normalized[b]!=normalized[0]]
 # Metadata records are asynchronous; report their differences without suppressing them.
 assert all(i(e,8)&128 for e in health)
 assert not any(t(e) in [30,31] for e in events)
 # actual flags Complete constant resolved below; errors and lag are checked independently.
 assert all(q(e,184+3*8)==0 and q(e,184+6*8)==0 and q(e,184+4*8)<=500000000 for e in health)
 resources=json.loads((case/'resources.json').read_text());worker_tasks=[];business_main=[]
 for r in resources:
  for tid,data in r.get('worker_tasks',{}).items():assert data['affinity']==[4,5,6,7] and data['scheduler']==0;worker_tasks.append(tid)
  pid=str(r['business']);task=r.get('business_tasks',{}).get(pid)
  if task and int(calls[0]['start_ns']) <= r['mono_ns'] <= int(calls[-1]['start_ns']) + int(calls[-1]['duration_ns']):assert task['affinity']==[4] and task['scheduler']==0;business_main.append(pid)
 metadata=dict(v.split('=',1) for v in (p/'metadata.txt').read_text().splitlines() if '=' in v)
 assert metadata['pixels_sha256']=='ad27883488764165ad633e74a354d9e02e0f29794e55b33d835c28e6d3146115' and metadata['cv_threads']=='1'
 record={'blocks':n,'calls':len(calls),'each_storage_calls':len(calls)//2,'templates':n,'searches':n*39,'partial_exact_equal':True,'all_behavior_same':True,'business_events_each_block':businesscounts,'normalized_business_sha256':sorted(set(hashlib.sha256(x).hexdigest() for x in normalized)),'normalized_different_blocks':different,'serialization_equivalent':not different,'native_file_crc_end_commit_valid':True,'cumulative_sequences_no_gaps':True,'all_drains_complete':True,'all_health_no_error_or_drops_or_late_sync':True,'worker_single_tid':sorted(set(worker_tasks)),'tracking_main_pid':sorted(set(business_main)),'drain_max_ns':max(int(b['drain_wait_ns']) for b in blocks),'switch_and_settling_max_ns':max(int(v) for k,v in metadata.items() if k.startswith('switch_')),'files':fileinfo}
 if different:allvalid=False
 if case.name=='pair1':
  audit=rows(p/'model-audit.csv');groups=[audit[b*40:(b+1)*40] for b in range(6)];canon=[{k:v for k,v in x.items() if k!='update'} for x in groups[0]];record['audit_rows']=len(audit);record['all_model_input_and_final_head_sha_same']=all([{k:v for k,v in x.items() if k!='update'} for x in g]==canon for g in groups);assert record['all_model_input_and_final_head_sha_same']
 report[case.name]=record
before=json.loads((ROOT/'protected-before.json').read_text());after=json.loads((ROOT/'protected-after.json').read_text());final=json.loads((ROOT/'final-state.json').read_text());restore=json.loads((ROOT/'restore.json').read_text());windows=json.loads((ROOT/'window-results.json').read_text());assert before==after and final['protected_sha_equal'] and not final['remaining'] and final['formal_hdmi_found'];assert restore['elapsed_seconds']<=1800;assert all(w['elapsed_seconds']<=150 and not w['timeout'] and w['resources_recovered'] and w['systemd_state']['ExecMainStatus']=='0' for w in windows)
report['maintenance']={'seconds':restore['elapsed_seconds'],'formal_pid':final['formal']['MainPID'],'protected_equal':True,'hdmi_formal':True,'remaining':[],'groups_seconds':[w['elapsed_seconds'] for w in windows]};report['all_serialization_equivalent']=allvalid
(ROOT/'quality-check.json').write_text(json.dumps(report,indent=2));print(json.dumps({k:{kk:vv for kk,vv in v.items() if kk not in ['files','business_events_each_block']} if isinstance(v,dict) else v for k,v in report.items()},indent=2))
