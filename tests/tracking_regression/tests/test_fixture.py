import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common
from prepare_fixture import select_frames, extract_attitude, CdrImu


class FixtureTests(unittest.TestCase):
    def test_half_open_and_nonzero_first(self):
        rows = [{'pts_us': x, 'timestamp_us': 1_000_000_000 + x, 'sequence': n}
                for n,x in enumerate([54_999_999,55_015_721,74_992_663,75_000_000])]
        selected, epoch = select_frames(rows)
        self.assertEqual([r['source_frame_id'] for r in selected], [1,2])
        self.assertEqual(selected[0]['relative_us'], 15721)
        self.assertEqual(selected[-1]['relative_us'], 19992663)
        self.assertEqual(epoch, 1_000_000_000)

    def test_time_repeated_backwards_and_epoch(self):
        for pts in ([1,1], [2,1]):
            with self.assertRaisesRegex(ValueError, 'repeated or backwards'):
                select_frames([{'pts_us': t,'timestamp_us':t+100,'sequence': i} for i,t in enumerate(pts)], 0,10)
        with self.assertRaisesRegex(ValueError, 'source epoch'):
            select_frames([{'pts_us':1,'timestamp_us':101,'sequence':1}, {'pts_us':2,'timestamp_us':103,'sequence':2}])

    def test_changed_pixels(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'pixels'
            path.write_bytes(bytes([0,127,255]))
            expected = common.sha256(path)
            common.verify_hash(path, expected)
            path.write_bytes(bytes([0,126,255]))
            with self.assertRaisesRegex(ValueError, 'sha256 mismatch'):
                common.verify_hash(path, expected)

    def test_missing_schema_identity(self):
        with self.assertRaisesRegex(ValueError, 'fixture_frame_id'):
            common.validate_record({'schema_version':1}, 'frame')
        common.validate_record({'schema_version':1,'fixture_frame_id':0,'source_frame_id':3302,
            'driver_sequence':9,'source_timestamp_us':100,'pts_us':55015721,'relative_us':15721,
            'pixel_sha256':'a'*64}, 'frame')

    def test_initial_attitude_missing(self):
        import sqlite3
        # CDR origin follows the four-byte encapsulation, alignment is relative to it.
        raw = struct.pack('<iII', 100,0,1) + b'\0'
        raw += b'\0' * ((-len(raw)) % 8)
        raw += struct.pack('<37d', *([0,0,0,1] + [0]*33))
        data = b'\0\1\0\0' + raw
        self.assertEqual(CdrImu(data).decode()[0], 100_000_000)
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp)/'test.db3'
            with sqlite3.connect(file) as db:
                db.execute('CREATE TABLE topics(id INTEGER,name TEXT,type TEXT,serialization_format TEXT)')
                db.execute('CREATE TABLE messages(id INTEGER,topic_id INTEGER,timestamp INTEGER,data BLOB)')
                db.execute('INSERT INTO topics VALUES(1,"/fpv/gimbal","sensor_msgs/msg/Imu","cdr")')
                db.execute('INSERT INTO messages VALUES(1,1,100,?)',(data,))
            with self.assertRaisesRegex(ValueError,'initial attitude'):
                extract_attitude([file], 45_000_000)

class RunConfigurationTests(unittest.TestCase):
    def test_predecode_configuration_and_legacy_refusal(self):
        config = {'schema_version': 2, 'input_mode': 'predecode_all', 'max_supply_lateness_us': 50000,
                  'queue_capacity': 1200, 'clock_jump_tolerance_us': 5000, 'ros_domain_id': 83}
        common.validate_record(config, 'run_config')
        for changed in ({**config, 'schema_version': 1}, {**config, 'input_mode': 'stream'},
                        {**config, 'prefetch_frames': 24}):
            with self.assertRaises(ValueError):
                common.validate_record(changed, 'run_config')

if __name__ == '__main__':
    unittest.main()
