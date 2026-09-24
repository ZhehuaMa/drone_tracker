"""将验收原始数据转为独立可阅读的统计表，不修改原始证据。"""
import json,statistics,sys
from pathlib import Path
E=Path(sys.argv[1]);out=Path(sys.argv[2]);rows=json.loads((E/'comparison-analysis.json').read_text())
lines=['# 生产板性能与资源统计','', '[原始证据包](production-evidence.tar.gz) 包含命令、参数、消息、资源曲线和原始日志。','', '统计排除开始 10 秒，并只纳入完全位于稳定区间内的计时窗口。均值按样本数加权。P95 优先将各窗口的 28 个直方图桶逐项相加后计算，数值为桶上界；旧日志只能计算保守上界，两者在表中分别标记。跟踪 fps 使用 `/fpv/target_bbox` 消息接收数除以稳定时长，消息每次消费一帧发布一次。','', '| 运行 | 稳定秒数 | 跟踪 fps | HDMI fps | render 均值 ms | P95 上界 ms | 提交至显示均值 ms | P95 方法 |','|---|---:|---:|---:|---:|---:|---:|---|']
for r in rows:
 s=r['stages'];rt=s.get('render_total',{});lat=s.get('submit_to_display',{})
 fmt=lambda v:'—' if v is None else f'{v:.3f}'
 lines.append('| '+' | '.join([r['run'],fmt(r['stable_seconds']),fmt(r['tracking_fps']),fmt(r['hdmi_fps']),fmt(rt.get('mean_ms')),fmt(rt.get('p95_upper_ms')),fmt(lat.get('mean_ms')),{'merged_pow2_histogram':'合并原始桶','window_summary_upper_bound':'窗口摘要保守界'}.get(rt.get('p95_method'),'—')])+' |')
for r in rows:
 lines+=['', '## '+r['run'],'',f"新视频样本 {r['displayed']}；计时覆盖 {r['window_seconds']:.3f} 秒；失败样本 {r['failures']}；退出码 {r['exit_code']}。",'', '| 阶段 | 样本 | 均值 ms | 最大值 ms | P95 上界 ms |','|---|---:|---:|---:|---:|']
 for name,s in r['stages'].items():lines.append(f"| {name} | {s['count']} | {s['mean_ms']:.3f} | {s['max_ms']:.3f} | {s['p95_upper_ms']:.3f} |")
 data=json.loads((E/(r['run']+'.json')).read_text());rs=[x for x in data['resources'] if x['mono']>=data['live']+10]
 if rs:
  temps=[int(v)/1000 for x in rs for v in x['temps'].values()]
  lines+=['',f"资源：RSS {min(x['rss_kb'] for x in rs)/1024:.1f}–{max(x['rss_kb'] for x in rs)/1024:.1f} MiB；线程 {min(x['threads'] for x in rs)}–{max(x['threads'] for x in rs)}；文件描述符 {min(x['fds'] for x in rs)}–{max(x['fds'] for x in rs)}；各温度传感器范围 {min(temps):.1f}–{max(temps):.1f} °C。",f"开始/结束 RSS：{rs[0]['rss_kb']/1024:.1f}/{rs[-1]['rss_kb']/1024:.1f} MiB。"]
  if r['run'].startswith('endurance'):
   lines+=['','每分钟 RSS 中位数（MiB）：'+', '.join(f"{statistics.median(x['rss_kb'] for x in rs if minute*60<=x['mono']-data['live']<(minute+1)*60)/1024:.1f}" for minute in range(10) if any(minute*60<=x['mono']-data['live']<(minute+1)*60 for x in rs))+ '。']
 lines+=['',f"命令、完整参数、每条消息和每秒资源见原始证据包解压后的 `evidence/{r['run']}.json/.yaml/.log`。"]
out.write_text('\n'.join(lines)+'\n')
