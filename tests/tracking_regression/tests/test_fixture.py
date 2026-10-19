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
        raw = struct.pack('<iII', 100,1000,1) + b'\0'
        raw += b'\0' * ((-len(raw)) % 8)
        raw += struct.pack('<37d', *([0,0,0,1] + [0]*33))
        data = b'\0\1\0\0' + raw
        self.assertEqual(CdrImu(data).decode()[0], 100_000_001)
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

class VariableFixtureTests(unittest.TestCase):
    def test_recording_tick_quantization_requires_exact_encoded_mapping(self):
        import json
        from unittest.mock import patch
        from prepare_fixture import probe_source
        rows = [{'pts_us':value,'pts_90k':tick} for value,tick in ((0,0),(16659,1499),(33320,2999))]
        stream = {'width':32,'height':24,'time_base':'1/90000','start_time':'0.000000'}
        frames = [{'width':32,'height':24,'best_effort_timestamp':tick,
                   'best_effort_timestamp_time':time,'pkt_duration_time':'0.016667'}
                  for tick,time in ((0,'0.000000'),(1499,'0.016656'),(2999,'0.033322'))]
        def probe():
            return patch('prepare_fixture.subprocess.check_output',return_value=json.dumps({'streams':[stream],'frames':frames}))
        with probe():
            self.assertEqual(probe_source('video.mp4',rows)['source_frame_count'],3)
        rows[1]['pts_us'] = 16670
        with probe(), self.assertRaisesRegex(ValueError,'does not match video timing'):
            probe_source('video.mp4',rows)
        rows[1]['pts_us'] = 16659
        frames[1]['best_effort_timestamp'] = 1500
        with probe(), self.assertRaisesRegex(ValueError,'does not match video timing'):
            probe_source('video.mp4',rows)
        frames[1]['best_effort_timestamp'] = 1499
        del rows[1]['pts_90k']
        with probe(), self.assertRaisesRegex(ValueError,'does not match video timing'):
            probe_source('video.mp4',rows)
        rows[1]['pts_90k'] = 1499
        stream['time_base'] = '1/1000'
        with probe(), self.assertRaisesRegex(ValueError,'time_base'):
            probe_source('video.mp4',rows)

    def test_resolved_selection_boundaries_whole_and_end(self):
        from prepare_fixture import resolve_selection
        rows=[{'pts_us':value,'timestamp_us':value+1_000_000_000,'sequence':index}
              for index,value in enumerate((0,54_999_999,55_000_000,74_999_999,75_000_000,165_000_000,179_999_999))]
        meta={'source_start_us':0,'source_end_us':180_000_000}
        clip={'analysis_windows':{},'frame_selection_interval':{'start_us':55_000_000,'end_us':75_000_000}}
        self.assertEqual([row['pts_us'] for row in resolve_selection(clip,meta,rows)[2]],[55_000_000,74_999_999])
        whole=resolve_selection({'analysis_windows':{}},meta,rows)
        self.assertEqual(len(whole[2]),len(rows));self.assertEqual(whole[2][-1]['source_frame_id'],6)
        clip['frame_selection_interval']={'start_us':165_000_000,'end_us':'end'}
        tail=resolve_selection(clip,meta,rows)
        self.assertEqual(tail[1],180_000_000);self.assertEqual(tail[2][-1]['source_frame_id'],6)
        clip['frame_selection_interval']['end_us']=180_000_000
        self.assertEqual(tail,resolve_selection(clip,meta,rows))
        clip['frame_selection_interval']={'start_us':179_999_999,'end_us':180_000_000}
        self.assertEqual(len(resolve_selection(clip,meta,rows)[2]),1)
        with self.assertRaisesRegex(ValueError,'empty'):
            resolve_selection({'analysis_windows':{},'frame_selection_interval':{'start_us':165_000_000,'end_us':'end'}},meta,rows[:5])
        clip['frame_selection_interval']={'start_us':180_000_000,'end_us':'end'}
        with self.assertRaisesRegex(ValueError,'outside'):resolve_selection(clip,meta,rows)
        with self.assertRaisesRegex(ValueError,'empty'):
            resolve_selection({'analysis_windows':{},'frame_selection_interval':{'start_us':76_000_000,'end_us':77_000_000}},meta,rows)

    def test_small_video_pixels_full_tail_and_replaced_content(self):
        import json, sqlite3, subprocess
        from prepare_fixture import prepare, raw_frames, probe_source
        from verify_fixture import verify
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            video=root/'source.mkv'
            subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','testsrc2=s=32x24:r=4',
                '-frames:v','4','-c:v','ffv1',str(video)],check=True)
            rows=[{'pts_us':index*250000,'timestamp_us':100000000+index*250000,'sequence':index} for index in range(4)]
            common.write_lines(root/'frames.jsonl',rows)
            bag=root/'imu.db3'
            with sqlite3.connect(bag) as db:
                db.execute('CREATE TABLE topics(id INTEGER,name TEXT,type TEXT,serialization_format TEXT)')
                db.execute('CREATE TABLE messages(id INTEGER,topic_id INTEGER,timestamp INTEGER,data BLOB)')
                db.execute('INSERT INTO topics VALUES(1,"/fpv/gimbal","sensor_msgs/msg/Imu","cdr")')
                for index,stamp in enumerate((100000000,100250000,100500000,100750000)):
                    sec,micro=divmod(stamp,1000000)
                    raw=struct.pack('<iII',sec,micro*1000,1)+b'\0'
                    raw+=b'\0'*((-len(raw))%8)
                    raw+=struct.pack('<37d',*([0,0,0,1]+[0]*33))
                    db.execute('INSERT INTO messages VALUES(?,1,?,?)',(index,stamp*1000,b'\0\1\0\0'+raw))
            identity='\n'.join(f'{key} path=/models/{key} sha256='+('a'*64) for key in
                ('tracker.yolo_engine','tracker.nano_template_backbone','tracker.nano_search_backbone','tracker.nano_head'))
            identity+='\ntracker.yolo_interval=1\ntracker.lost_max_count=20\ntracker.frame_buffer_size=30\ncamera.fx=10\ncamera.fy=10'
            common.write(root/'session.json',{'identity':identity})
            clip={'case_id':'tiny','source':{'video':str(video),'frame_logs':[str(root/'frames.jsonl')],
                'attitude_bags':[str(bag)],'session':str(root/'session.json')},'analysis_windows':{}}
            manifest=prepare(clip,root/'whole')
            self.assertEqual((manifest['width'],manifest['height'],manifest['frame_count']),(32,24,4))
            self.assertEqual(manifest['analysis_windows'],{'full':{'start_us':0,'end_us':1000000}})
            source=list(raw_frames(video,32,24));self.assertEqual(list(raw_frames(root/'whole/clip.mkv',32,24)),source)
            clip['case_id']='tail';clip['frame_selection_interval']={'start_us':500000,'end_us':'end'}
            tail=prepare(clip,root/'tail')
            self.assertEqual(tail['frame_count'],2)
            self.assertEqual(list(raw_frames(root/'tail/clip.mkv',32,24)),source[2:])
            (root/'tail/clip.mkv').write_bytes(b'replaced')
            with self.assertRaises((ValueError,subprocess.CalledProcessError)):verify(root/'tail')
            from unittest.mock import patch
            with patch('prepare_fixture.subprocess.check_output',return_value=json.dumps({'streams':[{'width':32,'height':24,'start_time':'0'}],
                'frames':[{'width':32,'height':24,'best_effort_timestamp_time':str(index/4)} for index in range(4)]})):
                with self.assertRaisesRegex(ValueError,'tail duration'):probe_source(video,rows)
            def replaced_during_decode(path,width,height):
                yield from raw_frames(path,width,height)
                video.write_bytes(b'replaced source after decode')
            with patch('prepare_fixture.raw_frames',side_effect=replaced_during_decode):
                with self.assertRaisesRegex(ValueError,'sha256 mismatch'):prepare(clip,root/'changed-source')
            self.assertFalse((root/'changed-source/fixture.json').exists())

if __name__ == '__main__':
    unittest.main()
