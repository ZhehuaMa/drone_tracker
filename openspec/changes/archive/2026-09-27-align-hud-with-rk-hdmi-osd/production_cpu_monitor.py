"""生产板持续负载的进程 CPU 百分比与资源趋势采样。"""
import json
import os
import sys
import time
from pathlib import Path


pid = int(sys.argv[1])
out = Path(sys.argv[2])
proc = Path('/proc') / str(pid)
ticks = os.sysconf(os.sysconf_names['SC_CLK_TCK'])
previous = None
with out.open('w') as dst:
    while proc.exists():
        try:
            now = time.monotonic()
            raw = (proc / 'stat').read_text()
            fields = raw[raw.rfind(')') + 2:].split()
            cpu_ticks = int(fields[11]) + int(fields[12])
            status = dict(line.split(':', 1) for line in (proc / 'status').read_text().splitlines()
                          if ':' in line)
            sample = dict(mono=now, rss_kb=int(status['VmRSS'].split()[0]),
                          threads=int(status['Threads']), fds=len(list((proc / 'fd').iterdir())))
        except FileNotFoundError:
            break
        if previous is not None:
            sample['cpu_percent_one_core_100'] = 100 * (cpu_ticks - previous[1]) / ticks / (now - previous[0])
        dst.write(json.dumps(sample) + '\n')
        dst.flush()
        previous = (now, cpu_ticks)
        time.sleep(5)
