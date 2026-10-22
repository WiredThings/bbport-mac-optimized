import copy
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('chain',Path(__file__).parents[1]/'tools/macos/analyze_resource_chain.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


def generation(builder=1, setter=1, position=10, snapshot=1000, packet=4120):
    return [dict(op='cpu_driver_table_setter',setter_sequence=setter,host_thread=7,context=5000,table=8000,covered=True,changed=False),
            dict(op='sdk_write_builder',builder_sequence=builder,host_thread=7,context=5000,table=8000,shadow=5416,packet=packet,bytes=192,dirty=1,covered=True),
            dict(op='command_snapshot',sequence=builder,snapshot=snapshot,bytes=232,guest=packet-24,valid=True),
            dict(op='submitted_packet',decoder_packet=snapshot+24,bytes=208,copied=True,submission=builder,mapping_valid=True,guest_packet=packet,snapshot_begin=snapshot,range_bytes=232),
            dict(op='ordered_task_copy',queue=1,position=position,source=snapshot+24,payload=10000,bytes=208),
            dict(op='ordered_task_execute',queue=1,position=position,source=None,payload=10000,bytes=208),
            dict(op='command_write_origin',packet='graphics_write_data',address=8000,packet_address=10000),
            dict(op='command_write',address=8000,version=builder,payload_address=10016)]


class SdkProvenance(unittest.TestCase):
    def status(self,rows):return m.sdk_upload_provenance(rows)['writes'][0]['status']

    def test_complete_envelope(self):
        r=m.sdk_upload_provenance(generation());self.assertEqual(r['counts'],{'matched':1})
        self.assertEqual(r['writes'][0]['setter_sequence'],1)
        self.assertFalse(r['writes'][0]['setter_changed'])

    def test_rebuild_after_publication_does_not_change_frozen_identity(self):
        rows=generation();new=copy.deepcopy(rows[1]);new['builder_sequence']=2
        rows.insert(3,new)
        self.assertEqual(m.sdk_upload_provenance(rows)['writes'][0]['builder_sequence'],1)

    def test_snapshot_and_ring_addresses_reused_after_consumption(self):
        r=m.sdk_upload_provenance(generation()+generation(2,2,11))
        self.assertEqual(r['counts'],{'matched':2})
        self.assertEqual([w['snapshot_sequence'] for w in r['writes']],[1,2])

    def test_new_overlapping_builder_invalidates_old_packet(self):
        rows=generation();new=copy.deepcopy(rows[1]);new['builder_sequence']=2;new['packet']+=4
        rows.insert(2,new)
        self.assertEqual(self.status(rows),'missing_observed_sdk_builder')

    def test_missing_boundary_is_not_zero_activity(self):
        for index,status in [(0,'missing_same_thread_setter'),(2,'missing_publication_snapshot'),(3,'missing_submitted_packet')]:
            rows=generation();rows.pop(index)
            self.assertEqual(self.status(rows),status)
        self.assertIsNone(m.sdk_upload_provenance(generation()[2:]))
        rows=generation();rows.pop(1);self.assertIsNone(m.sdk_upload_provenance(rows))

    def test_cross_thread_setter_is_not_associated(self):
        rows=generation();rows[0]['host_thread']=8
        self.assertEqual(self.status(rows),'missing_same_thread_setter')
        rows=generation();rows[0].pop('host_thread');rows[1].pop('host_thread')
        self.assertEqual(self.status(rows),'missing_same_thread_setter')

    def test_uncovered_and_changed_backing_fail_closed(self):
        for index,field,value,status in [(0,'covered',False,'invalid_setter_state'),(0,'table',8001,'invalid_setter_state'),
                (1,'covered',False,'invalid_builder_state'),(1,'dirty',0,'invalid_builder_state'),(1,'shadow',5417,'invalid_builder_state')]:
            rows=generation();rows[index][field]=value;self.assertEqual(self.status(rows),status)

    def test_exact_last_byte_and_outside_range(self):
        self.assertEqual(self.status(generation()),'matched')
        rows=generation();rows[2]['bytes']=231;rows[3]['range_bytes']=231
        self.assertEqual(self.status(rows),'invalid_publication_mapping')

    def test_pointer_overflow_and_invalid_snapshot(self):
        for field,value in [('snapshot',(1<<64)-16),('guest',(1<<64)-16),('snapshot',None),('guest',None),('valid',False)]:
            rows=generation();rows[2][field]=value
            if field=='snapshot':rows[3]['snapshot_begin']=value
            self.assertNotEqual(self.status(rows),'matched')

    def test_duplicate_capture_identity_and_overflow_fail_closed(self):
        for extra in [generation()[0],generation()[1],generation()[2],dict(op='sdk_builder_overflow'),dict(op='frame',truncated=True)]:
            rows=generation()+[extra]
            self.assertEqual(self.status(rows),'incomplete_or_duplicate_observation')

    def test_frame_gaps_or_new_series_are_not_continuous_observation(self):
        for tail in [dict(op='frame',frame=12,series_count=3,series_member=3,truncated=False),
                     dict(op='frame',frame=11,series_count=3,series_member=1,truncated=False)]:
            rows=[dict(op='frame',frame=10,series_count=3,series_member=1,truncated=False)]+generation()+[tail]
            self.assertEqual(self.status(rows),'incomplete_or_duplicate_observation')

    def test_packet_offset_and_destination_must_match(self):
        rows=generation();rows[3]['guest_packet']+=1
        self.assertEqual(self.status(rows),'invalid_publication_mapping')
        rows=generation();rows[0]['table']=rows[1]['table']=8001
        self.assertEqual(self.status(rows),'sdk_destination_mismatch')

    def test_ordered_identity_and_direct_input_never_upgraded(self):
        rows=generation();rows[4]['payload']=9999
        self.assertEqual(self.status(rows),'missing_ordered_identity')
        rows=generation();rows[3]['copied']=False
        self.assertEqual(self.status(rows),'direct_input_not_snapshot')


if __name__=='__main__':unittest.main()
