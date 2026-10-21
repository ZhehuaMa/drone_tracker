import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import collect
import metrics


def packed_event(kind=0):
    scalar=[1,256,kind,0,0,0,7,1,1,123,0,0,0,0,1,1,1,0,0,100,200,300]
    return collect.EVENT.pack(*scalar,*([0.0]*12),*range(7),*range(4))


def record(kind,data):
    header=collect.HEADER.pack(0x44544731,1,kind,len(data),0,0x01020304)
    crc=zlib.crc32(data)^zlib.crc32(header)
    return collect.HEADER.pack(0x44544731,1,kind,len(data),crc,0x01020304)+data


class CollectorTests(unittest.TestCase):
    def test_all_native_fields_and_arrays_preserved(self):
        row=collect.decode_event(packed_event())
        self.assertEqual(row['byte_length'],256)
        self.assertEqual(row['frame_round_id'],0)
        self.assertEqual(row['values'],list(range(7)))
        self.assertEqual(row['counts'],list(range(4)))
        self.assertEqual(row['source_tracker_frame_id'],0)

    def test_complete_storage_and_corrupt_tail(self):
        batch=record(1,packed_event())
        end=record(3,struct.pack('<I',1))
        commit=record(2,collect.COMMIT.pack(len(batch)+len(end),123,*([1]*4),*([0]*4)))
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'events.dtd'
            path.write_bytes(batch+end+commit)
            events,errors=collect.scan(path)
            self.assertEqual(len(events),1);self.assertEqual(errors,[])
            for data in (batch, batch+end, batch+end+commit[:-1], batch[:-1]+b'\0'):
                path.write_bytes(data)
                self.assertTrue(collect.scan(path)[1])

    def test_supersession_records_require_successor_and_no_execution(self):
        def row(kind, request, reason='None', successor=0):
            return dict(type=kind, reason=reason, request_id=request, run_id=7, round_id=1,
                        producer_id=0, producer_seq=0, frame_time_us=0, update_id=0, flags=128, values=[successor,0,0,0,0,0,0])
        rows = [row('RequestDecision', 1), row('RequestDecision', 2), row('RequestDecision', 3),
                row('RequestCancel',1,'Superseded',2), row('RequestCancel',2,'Superseded',3),
                row('RequestCancel',3,'Cleaning')]
        def reasons(events):
            return collect.qualify(events, [], {'m0_ns':0}, {}, {'max_supply_lateness_us':50000})
        self.assertFalse(any('terminal outcome' in s or 'supersession' in s for s in reasons(rows)))
        self.assertTrue(any('terminal outcome' in s for s in reasons(rows[:-1])))
        self.assertTrue(any('supersession' in s for s in reasons(rows + [row('DetectStart',1)])))
        bad = [dict(item) for item in rows]; bad[3]['values'] = [99]
        self.assertTrue(any('supersession' in s for s in reasons(bad)))

    def test_pair_missing_end_keeps_unknown(self):
        events=[dict(run_id=7,round_id=1,update_id=3,type='UpdateStart')]
        self.assertIn('unpaired identity',collect.pairs(events,'UpdateStart','UpdateEnd')[0])

    def test_crc_and_unknown_version_rejected(self):
        raw=bytearray(record(1,packed_event()));raw[-1]^=1
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'events.dtd';path.write_bytes(raw)
            self.assertIn('CRC',collect.scan(path)[1][0])
            raw=bytearray(record(1,packed_event()));raw[4]=99;path.write_bytes(raw)
            self.assertIn('unsupported header',collect.scan(path)[1][0])

class PreparationQualificationTests(unittest.TestCase):
    def test_complete_cache_and_clock_order_required(self):
        prepared = {'ready': True, 'input_mode': 'predecode_all', 'verified_frame_count': 1200,
                    'cached_pixel_bytes': 2764800000, 'begin_mono_ns': 10, 'end_mono_ns': 20,
                    'process_rss_kib': 2700000, 'process_peak_rss_kib': 2800000,
                    'system_total_kib': 8100000, 'system_available_kib': 4000000, 'reason': ''}
        def reasons(preparation, origin=30):
            result = collect.qualify([], [], {'m0_ns': origin}, {}, {'max_supply_lateness_us': 50000}, preparation)
            return [row for row in result if row.startswith('preparation:')]
        self.assertEqual(reasons(prepared), [])
        for changed in (None, {**prepared, 'ready': False}, {**prepared, 'verified_frame_count': 1199},
                        {**prepared, 'cached_pixel_bytes': 0}, {**prepared, 'end_mono_ns': 0},
                        {**prepared, 'process_rss_kib': 0}, {**prepared, 'reason': 'allocation failed'}):
            self.assertTrue(reasons(changed))
        self.assertTrue(reasons(prepared, origin=20))

class VariablePreparationTests(unittest.TestCase):
    def test_actual_count_and_bytes_replace_reference_constants(self):
        manifest={'frame_count':4,'width':32,'height':24}
        prepared={'ready':True,'input_mode':'predecode_all','verified_frame_count':4,'cached_pixel_bytes':3072,
                  'begin_mono_ns':10,'end_mono_ns':20,'process_rss_kib':1,'process_peak_rss_kib':1,
                  'system_total_kib':1,'system_available_kib':1,'reason':''}
        reasons=collect.qualify([],[],{'m0_ns':30},{},{'max_supply_lateness_us':50000},prepared,manifest)
        self.assertFalse(any('preparation:' in item for item in reasons))
        prepared['cached_pixel_bytes']-=1
        reasons=collect.qualify([],[],{'m0_ns':30},{},{'max_supply_lateness_us':50000},prepared,manifest)
        self.assertTrue(any('cached frame count or bytes' in item for item in reasons))

if __name__=='__main__':unittest.main()
