"""One-call scheduler records. Missing format-4 evidence stays unavailable."""


def compute(events, start_ns, end_ns, complete=True):
    rows = [row for row in events if start_ns <= row['mono_ns'] < end_ns]
    modern = [row for row in rows if row.get('version', 1) >= 4]
    names = lambda kind: [row for row in modern if row['type'] == kind]
    dispositions = names('StageDisposition')
    def decode(row):
        values, counts = row.get('values', []), row.get('counts', [])
        if len(values) != 7 or len(counts) != 4 or any(type(x) is not int for x in values + counts):
            raise ValueError('scheduler record requires seven integer values and four counts')
        return values, counts
    seeds = []
    seen = set()
    duplicates = []
    for row in dispositions:
        values, counts = decode(row)
        identity = (row.get('run_id'), row.get('round_id'), values[0])
        if identity in seen:
            duplicates.append(identity)
        seen.add(identity)
        seeds.append(dict(job_id=values[0], plan_id=values[1], fallback_plan_id=values[2],
                          request_attempt=values[3], seed_attempt=values[4], applicable_attempt=values[5],
                          original_revision=values[6], seed_eligible=bool(counts[0]),
                          confirmation_eligible=bool(counts[1]), seed_used=bool(counts[2]),
                          complete_coverage=bool(counts[3]), reason=row.get('reason'),
                          source_frame=row.get('source_tracker_frame_id')))
    starts, ends = names('DetectStart'), names('DetectEnd')
    def ids(group):
        return {(row.get('run_id'), row.get('round_id'), decode(row)[0][0]) for row in group}
    outcomes = names('PlanOutcome')
    policy = names('CleanupPolicy')
    timing = []
    credits = []
    waits = []
    for row in names('SchedulerTiming'):
        values, counts = decode(row)
        if counts[0] == 1:
            credits.append(dict(job_id=values[0], take_ns=values[2]))
        elif counts[0] in (2, 3, 4):
            waits.append(dict(job_id=values[0], plan_id=values[1], start_ns=values[2], end_ns=values[3],
                              duration_ns=values[3]-values[2], cause={2:'capacity',3:'seed_disposition',4:'idle'}[counts[0]]))
        else:
            timing.append(dict(job_id=values[0], preprocessing_ns=values[2] or None,
                               inference_ns=values[3] or None, postprocessing_ns=values[4] or None,
                               capacity_wait_ns=None, seed_disposition_wait_ns=None))
    return dict(metric_version=1, available=bool(modern) and complete,
                policy='one-call-2-targeted-1-discovery-v1' if modern else None,
                no_result_frame_cleanup='inactive' if policy else 'unavailable',
                historical_watchdog_events=sum(row.get('reason') == 'NoResultTimeout' for row in rows),
                executed_jobs=len(ids(starts)) if modern else None,
                completed_jobs=len(ids(ends)) if modern else None,
                parent_verdicts=len(ids(outcomes)) if modern else None,
                dispositions=seeds, duplicate_dispositions=duplicates,
                service_order=[dict(job_id=decode(row)[0][0], plan_id=decode(row)[0][1],
                                    targeted=bool(decode(row)[0][5]),
                                    source_frame=row.get('source_tracker_frame_id')) for row in starts],
                timing=timing, credits=credits, waits=waits,
                peak_ownership='unavailable: inspect controlled bounds tests and request occupancy records',
                performance_criteria='unset')
