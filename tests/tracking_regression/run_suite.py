#!/usr/bin/env python3
"""Execute one explicit multi-clip operation from one frozen YAML/JSON file."""
import csv
import json
from pathlib import Path
import signal
import subprocess
import sys
import time

import baseline
import common
import compare
from configuration import load_config, operation_directory, snapshot
import run_batch


def prepare_case(clip, output, budget):
    """Preparation, including reread verification, has its own process-tree budget."""
    run_batch.enable_subreaper()
    request_path = output.parent / (clip['case_id'] + '-prepare-request.json')
    common.write(request_path, {'clip':clip, 'output':str(output)})
    begin = time.monotonic()
    with (output.parent / (clip['case_id'] + '-prepare.log')).open('xb') as log:
        process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '--prepare', str(request_path)],
                                   stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        until = begin + budget
        reserve = min(.5, budget / 4)
        while process.poll() is None and time.monotonic() < until - reserve:
            time.sleep(.01)
        timed_out = process.poll() is None
        recovered = run_batch.stop_children([process], time.monotonic(), until - reserve / 4)
    success = not timed_out and recovered and process.returncode == 0
    if not success:
        (output / 'fixture.json').unlink(missing_ok=True)
    return {'operation_success':success, 'resources_recovered':recovered,
            'elapsed_seconds':time.monotonic()-begin, 'timed_out':timed_out,
            'exit_code':process.returncode, 'reason':None if success else 'fixture_preparation_failed'}


def write_summary(output, operation, suite_id, cases):
    success = all(case['operation_success'] for case in cases)
    performance = 'passed' if operation == 'compare' and cases and all(
        case.get('decision') == 'passed' and case.get('status') == 'comparable' for case in cases) else (
        'not_evaluated' if operation != 'compare' else 'not_passed')
    report = {'schema_version':1, 'suite_id':suite_id, 'operation':operation,
              'operation_success':success, 'performance_status':performance, 'cases':cases}
    common.write(output / 'suite-report.json', report)
    with (output / 'suite-report.csv').open('x', newline='', encoding='utf-8') as stream:
        fields = ['case_id','operation_success','status','decision','reason','resources_recovered','path','rounds']
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction='ignore')
        writer.writeheader()
        for case in cases:
            writer.writerow({key:json.dumps(value,ensure_ascii=False) if isinstance(value,(list,dict)) else value
                             for key,value in case.items()})
    lines = [f'# 多用例回归报告：{suite_id}', '', f'操作：`{operation}`。', '',
             f'操作执行成功：`{success}`；性能判定：`{performance}`。', '',
             '操作成功表示请求完成，不代表性能通过。未设置门槛、缺失或不完整用例均不能产生套件通过结论。', '',
             '| 用例 | 操作成功 | 状态 | 门槛判定 | 原因 |', '|---|---|---|---|---|']
    for case in cases:
        lines.append(f"| {case['case_id']} | {case['operation_success']} | {case.get('status','')} | "
                     f"{case.get('decision','未设置判定规则')} | {case.get('reason') or ''} |")
    lines.extend(['', '各用例保留独立结果；不跨录像平均指标、拼接样本或抵消失败。', ''])
    (output / 'report.md').write_text('\n'.join(lines), encoding='utf-8')
    return report


def preflight_inputs(config):
    """Check every required locator before the first case or output directory."""
    if config['operation'] == 'prepare':
        for index, clip in enumerate(config['clips']):
            for key, value in clip['source'].items():
                for path in value if isinstance(value,list) else [value]:
                    if not Path(path).is_file():
                        raise ValueError(f'$.clips[{index}].source.{key}: missing file {path}')
    elif config['operation'] == 'run':
        from verify_fixture import verify
        for key in ('binary','worker'):
            if not Path(config['run'][key]).is_file():
                raise ValueError(f'$.run.{key}: missing executable')
        if not Path(config['run']['source_repo']).is_dir():
            raise ValueError('$.run.source_repo: missing source tree')
        for index,clip in enumerate(config['clips']):
            manifest=verify(Path(clip['fixture']),verify_pixels=False)
            if manifest['schema_version'] != 2 or manifest['case_id'] != clip['case_id']:
                raise ValueError(f'$.clips[{index}].fixture: prepare this case with the new format')
            requested = clip.get('frame_selection_interval')
            start = requested['start_us'] if requested else manifest['source_start_us']
            end = requested['end_us'] if requested else manifest['source_end_us']
            if end == 'end': end = manifest['source_end_us']
            windows = {'full':{'start_us':0,'end_us':end-start},**clip['analysis_windows']}
            if (start,end,windows) != (manifest['start_us'],manifest['end_us'],manifest['analysis_windows']):
                raise ValueError(f'$.clips[{index}].fixture: selection or windows differ; prepare again')


def execute(record):
    config = record['parsed']
    operation = config['operation']
    output = operation_directory(config)
    preflight_inputs(config)
    # This atomic creation also rejects reruns that could overwrite frozen evidence.
    output.mkdir(parents=True, exist_ok=False)
    snapshot(record, output)
    cases, stopped = [], False
    clips = config['clips']
    if operation in ('freeze','compare'):
        declared = {clip['case_id'] for clip in clips}
        for case in sorted(set(config[operation]['cases']) - declared):
            cases.append({'case_id':case,'operation_success':False,'status':'unexpected_case',
                          'reason':'case not declared in clips'})
    for clip in clips:
        case_id = clip['case_id']
        row = {'case_id':case_id,'operation_success':False,'resources_recovered':True}
        if operation == 'run':
            row['rounds']=[{'round':index+1,'status':'not_executed'} for index in range(config['rounds'])]
        cases.append(row)
        if stopped:
            row.update(status='not_executed',reason='previous process tree was not recovered')
            if operation == 'run':
                row['rounds'] = [{'round':index+1,'status':'not_executed'} for index in range(config['rounds'])]
            continue
        try:
            if operation == 'prepare':
                row.update(prepare_case(clip, output / case_id, config['deadlines']['preparation_seconds']))
                row['path'] = str(output / case_id)
                stopped = not row['resources_recovered']
            elif operation == 'run':
                manifest = common.load(Path(clip['fixture']) / 'fixture.json')
                common.validate_record(manifest, 'fixture')
                if manifest['schema_version'] != 2 or manifest['case_id'] != case_id:
                    raise ValueError('run requires this case in fixture version 2; prepare a new fixture')
                # Match the declared selection/window request against prepared content.
                from prepare_fixture import resolve_selection
                frame_rows = [{'pts_us':item['pts_us'], 'timestamp_us':item['source_timestamp_us'],
                               'sequence':item['driver_sequence']} for item in
                              common.lines(Path(clip['fixture']) / 'frames.jsonl')]
                start, end, _, _, windows = resolve_selection(clip, manifest, frame_rows)
                if (start,end,windows) != (manifest['start_us'],manifest['end_us'],manifest['analysis_windows']):
                    raise ValueError('fixture selection/windows differ from suite configuration; prepare again')
                settings = {'schema_version':3, **config['run']['settings'], 'case_id':case_id,
                            'deadlines':config['deadlines'], 'analysis_windows':manifest['analysis_windows']}
                common.validate_record(settings,'run_config')
                directory = output / case_id
                directory.mkdir()
                row['path'] = str(directory)
                run = config['run']
                for index in range(config['rounds']):
                    if stopped:
                        continue
                    destination = directory / f'run-{index+1:03d}'
                    request = {'fixture':clip['fixture'],'configuration':settings,'repo':run['source_repo'],
                               'binary':run['binary'],'worker':run['worker'],'output':str(destination),
                               'duration_us':manifest['duration_us'],'deadlines':config['deadlines'],
                               'suite_context':{'suite_id':config['suite_id'],'case_position':len(cases)-1,'round':index+1}}
                    qualified = run_batch.supervise(request)
                    deadline = common.load(destination / 'deadline.json')
                    recovered = deadline['resources_recovered']
                    row['rounds'][index]={'round':index+1,'path':str(destination),'qualified':qualified,
                                         'resources_recovered':recovered,'status':'qualified' if qualified else 'incomplete'}
                    if not recovered:
                        stopped = True
                        row['resources_recovered'] = False
                row['operation_success'] = all(item.get('qualified') for item in row['rounds'])
            elif operation == 'freeze':
                item = config['freeze']['cases'].get(case_id)
                if item is None:
                    raise ValueError('missing reference run declaration')
                runs = baseline.validate_set(item['runs'])
                if runs[0]['identity']['schema_version'] != 2:
                    raise ValueError('historical runs are read-only; rerun this case with the new configuration')
                if runs[0]['identity']['case_id'] != case_id:
                    raise ValueError('declared case does not match reference runs')
                baseline.freeze(item['runs'], output / case_id)
                row.update(operation_success=True,path=str(output / case_id))
            else:
                item = config['compare']['cases'].get(case_id)
                if item is None:
                    row.update(status='missing_case',reason='missing baseline/candidates',decision='not_evaluated')
                    continue
                report = compare.compare(item['baseline'],item['candidates'],item.get('thresholds'),item.get('affinity_experiment'))
                if report.get('case_id') != case_id:
                    report['status'] = 'incomparable'
                    report['differences'].append({'field':'case_id','reason':'declared_case_mismatch'})
                    report['metrics'] = []
                    report['decision'] = {'status':'not_evaluated','reason':'case mismatch'}
                compare.write_report(report,output / case_id)
                row.update(operation_success=True,status=report['status'],decision=report['decision']['status'],
                           reason='; '.join(item['reason'] for item in report['differences']) or None,
                           path=str(output / case_id))
        except (OSError,ValueError,KeyError,TypeError,subprocess.SubprocessError) as error:
            row.update(status='failed',reason=str(error))
            if operation == 'run' and run_batch.descendants():
                row['resources_recovered'] = False
                stopped = True
        row.setdefault('status','completed' if row['operation_success'] else 'incomplete')
    return write_summary(output,operation,config['suite_id'],cases)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) == 2 and argv[0] == '--prepare':
        from prepare_fixture import prepare
        request = common.load(argv[1])
        prepare(request['clip'], Path(request['output']))
        return 0
    if len(argv) != 1:
        raise ValueError('Usage: run_suite.py CONFIG.yaml (all business settings belong in that file)')
    record = load_config(argv[0])
    report = execute(record)
    print(operation_directory(record['parsed']) / 'report.md')
    return 0 if report['operation_success'] else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError,ValueError,KeyError,TypeError,subprocess.SubprocessError) as error:
        print(error,file=sys.stderr)
        sys.exit(1)
