from pathlib import Path
import subprocess,json
r=Path('/archive');b=Path('/install/drone_tracker/lib/drone_tracker/rk3576_diagnostic_inspect');out=r/'native-exports';out.mkdir();result=[]
for p in sorted((r/'raw/window01-complete').rglob('*.dtd')):
 destination=out/str(len(result));call=subprocess.run([str(b),str(p),'--output',str(destination)],capture_output=True,text=True,timeout=3);result.append({'input':str(p.relative_to(r)),'output':str(destination.relative_to(r)),'rc':call.returncode,'stdout':call.stdout,'stderr':call.stderr});assert call.returncode==0
(r/'native-inspect-result.json').write_text(json.dumps(result,indent=2));print('native scanner/export passed',len(result),'files')
