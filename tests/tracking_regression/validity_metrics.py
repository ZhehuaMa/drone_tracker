"""Versioned freshness protocol evidence; historical age records remain unavailable."""

MAPPINGS = {1: 'capture-monotonic-v1', 2: 'standalone-admission-v1'}
TYPES = ('ValidityState', 'EvidenceClock', 'CandidateMatch', 'TrustLoss', 'TrustLossDisposed', 'ValidAdmission')


def fields(event):
    """Decode exact integer value records without interpreting display strings."""
    if event.get('version', 1) < 3 or event.get('type') not in TYPES:
        return None
    values, counts = event.get('values', []), event.get('counts', [])
    if len(values) != 7 or len(counts) != 4 or any(type(x) is not int for x in values + counts):
        raise ValueError('validity record requires seven exact integers and four integer counts')
    kind = event['type']
    if kind == 'EvidenceClock':
        return dict(clock_mapping=MAPPINGS.get(values[0], 'unavailable'), anchor_ns=values[1],
                    decision_ns=values[2], deadline_ns=values[3], source_us=values[4],
                    attempt=values[5], revision=values[6], clock_valid=bool(counts[0]), units='ns except source_us')
    if kind == 'ValidityState':
        return dict(attempt=values[0], revision=values[1], seed_frame=values[2], seed_request=values[3],
                    attempt_deadline_ns=values[4], verification_ns=values[5], confirmation_frame=values[6],
                    phase=counts[0], allowed=bool(counts[1]), appearance_validator='not_evaluated',
                    motion_validator='not_evaluated', units='ns')
    if kind in ('TrustLoss', 'TrustLossDisposed'):
        return dict(attempt=values[0], revision=values[1], revocation_ns=values[2],
                    revocation_source_us=values[3], publication_ns=values[4], reason_id=counts[0], units='ns except source_us')
    if kind == 'ValidAdmission':
        return dict(attempt=values[0], revision=values[1], admission_ns=values[2], deadline_ns=values[3],
                    observation_id=values[4], admitted=bool(counts[0]), units='ns')
    return dict(disposition='stale_seed' if event.get('flags', 0) & 131072 else
                'fresh_seed' if event.get('flags', 0) & 65536 else 'verification_or_diagnostic',
                comparison_origin='replay' if values[0] else 'submitted', candidate_count=values[1],
                overlap_matches=values[2], anchor_ns=values[3], decision_ns=values[4], fresh=bool(values[5]),
                eligible_identity=bool(values[6]), counter_before=counts[0], counter_after=counts[1],
                counter_disposition='changed' if counts[0] != counts[1] else 'neutral_or_reset',
                stage_kind=counts[2], stage_index=counts[3], units='ns')


def compatible(left, right):
    """A mapping mismatch prevents affected same-condition timing comparisons."""
    return left in MAPPINGS.values() and left == right


def compute(events, start_ns, end_ns, complete):
    selected = [event for event in events if start_ns <= event['mono_ns'] < end_ns]
    records = [(event, fields(event)) for event in selected]
    modern = any(record for _, record in records)
    mapping = sorted({record['clock_mapping'] for _, record in records
                      if record and 'clock_mapping' in record and record['clock_valid']})
    states = [(event, record) for event, record in records if record and event['type'] == 'ValidityState']
    losses = [(event, record) for event, record in records if record and event['type'] == 'TrustLoss']
    admissions = [(event, record) for event, record in records
                  if record and event['type'] == 'ValidAdmission' and record['admitted']]
    episodes = []
    seen = set()
    for event, loss in losses:
        identity = (event.get('round_id'), loss['attempt'], loss['revision'])
        if identity in seen:
            continue
        seen.add(identity)
        verification = next((record for row, record in states if row.get('round_id') == event.get('round_id')
                             and record['verification_ns'] >= loss['revocation_ns']
                             and record['attempt'] > loss['attempt'] and record['verification_ns'] > 0), None)
        admission = next(((row, record) for row, record in admissions
                          if row.get('round_id') == event.get('round_id')
                          and record['admission_ns'] >= loss['revocation_ns']
                          and record['attempt'] > loss['attempt']), None)
        outcomes = {}
        if admission:
            row, record = admission
            for name in ('PublishEnd', 'LosPublishEnd'):
                call = next((item for item in selected if item.get('round_id') == row.get('round_id')
                             and item['type'] == name and item.get('values', [None])[0] == record['observation_id']), None)
                outcomes[name] = None if call is None else bool(call.get('flags', 0) & 128)
        episodes.append(dict(round=event.get('round_id'), attempt=loss['attempt'], revision=loss['revision'],
            revocation_ns=loss['revocation_ns'], verification_ns=None if verification is None else verification['verification_ns'],
            admission_ns=None if admission is None else admission[1]['admission_ns'],
            to_verification_ms=None if verification is None else (verification['verification_ns'] - loss['revocation_ns']) / 1e6,
            to_admission_ms=None if admission is None else (admission[1]['admission_ns'] - loss['revocation_ns']) / 1e6,
            complete=admission is not None, topic_outcomes=outcomes))
    outputs = [event for event in selected if event['type'] == 'UpdateEnd' and event.get('flags', 0) & 256]
    valid, invalid, suppressed, absent = 0, 0, 0, 0
    for output in outputs:
        same = [event for event in selected if event.get('run_id') == output.get('run_id')
                and event.get('round_id') == output.get('round_id') and event.get('update_id') == output.get('update_id')
                and not event.get('flags', 0) & 32768]
        pair = next((event for event in same if event['type'] == 'ObservationBbox'), None)
        if pair:
            if pair.get('flags', 0) & 16: valid += 1
            else: invalid += 1
        elif any(event['type'] == 'ObservationSuppressed' for event in same): suppressed += 1
        else: absent += 1
    return dict(metric_version=1, available=modern and complete, clock_mappings=mapping,
                historical_meaning='age_observation_only; no inferred rejection policy' if not modern else None,
                window=dict(start_ns=start_ns, end_ns=end_ns, boundary='[start,end)'),
                frame_output=dict(denominator='consumed UpdateEnd images; event pairs excluded', sample_count=len(outputs),
                                  valid=valid, invalid=invalid, suppressed=suppressed, absent=absent,
                                  valid_ratio=valid / len(outputs) if modern and outputs else None),
                recovery_episodes=episodes, recovery_sample_count=len(episodes),
                incomplete_episodes=sum(not episode['complete'] for episode in episodes),
                protocol_outcome='inspect_recorded_issues' if modern else 'unavailable',
                performance_criteria='unset', accuracy='unmeasured', subscriber_delivery='unmeasured')
