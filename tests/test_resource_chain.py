import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('resource_chain',Path(__file__).resolve().parents[1]/'tools/macos/analyze_resource_chain.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class ResourceChainTests(unittest.TestCase):
    def test_fragment_guard_never_promotes_missing_or_inconsistent_metadata(self):
        self.assertIsNone(m.fragment_update_observations([]))
        base=dict(op='inline_update_fragment_guard',render_pass=True,reasons=0,
                  draws=2,known_draws=2,overlap=False)
        rows=[base,dict(base,render_pass=False),dict(base,reasons=2),
              dict(base,known_draws=1),dict(base,overlap=True)]
        result=m.fragment_update_observations(rows)
        self.assertEqual(result['counts'],dict(no_observed_fragment_conflict=1,
            not_rendering=1,unknown_fragment_access=2,fragment_conflict=1))
        self.assertNotIn('safe_to_remove',result)
    def ordered_packet(self, position, source=1000):
        return [dict(op='ordered_task_copy', queue=7, position=position,
                     source=source, payload=2000, bytes=208),
                dict(op='ordered_task_execute', queue=7, position=position,
                     source=None, payload=2000, bytes=208),
                dict(op='command_write_origin', address=4096, packet='graphics_write_data',
                     packet_address=2000),
                dict(op='command_write', address=4096, version=position, payload_address=2016)]

    def test_queue_identity_separates_reused_payload_pointer(self):
        rows = self.ordered_packet(8) + self.ordered_packet(8192, source=3000)
        result = m.ordered_write_provenance(rows)
        self.assertEqual(result['counts'], {'matched': 2})
        self.assertEqual([e['decoder_packet'] for e in result['writes']], [1000, 3000])

    def test_missing_copy_and_late_identity_collision_stay_unmatched(self):
        self.assertEqual(m.ordered_write_provenance(self.ordered_packet(8)[1:])['counts'],
                         {'missing_queue_copy': 1})
        rows = self.ordered_packet(8)
        rows.append(dict(op='ordered_task_copy', queue=7, position=8, source=3000,
                         payload=2000, bytes=208))
        result = m.ordered_write_provenance(rows)
        self.assertEqual(result['counts'], {'duplicate_queue_identity': 1})
        self.assertNotIn('decoder_packet', result['writes'][0])

    def test_legacy_and_mismatched_packet_observations_cannot_prove_a_join(self):
        self.assertIsNone(m.ordered_write_provenance([dict(op='command_write', address=4096)]))
        rows = self.ordered_packet(8)
        rows[0]['payload'] = 999
        self.assertEqual(m.ordered_write_provenance(rows)['counts'], {'queue_payload_mismatch': 1})
        rows = self.ordered_packet(8)
        rows[3]['payload_address'] = 999
        self.assertEqual(m.ordered_write_provenance(rows)['counts'], {'missing_graphics_origin': 1})

    def test_execution_identity_cannot_be_reused_without_another_execute(self):
        rows = self.ordered_packet(8)
        rows += self.ordered_packet(16)[2:]
        self.assertEqual(m.ordered_write_provenance(rows)['counts'],
                         {'matched': 1, 'no_observed_queue_execution': 1})
        rows = self.ordered_packet(8)
        rows[0], rows[1] = rows[1], rows[0]
        self.assertEqual(m.ordered_write_provenance(rows)['counts'], {'invalid_observed_order': 1})

    def test_submission_mapping_is_consumed_before_decoder_address_reuse(self):
        first = dict(op='submitted_packet', decoder_packet=1000, bytes=208, copied=True,
                     submission=2, mapping_valid=True, guest_packet=4096)
        second = dict(first, submission=3, guest_packet=8192)
        # Decoder storage may already be reused while the old task waits for execution.
        rows = [first, self.ordered_packet(8)[0], second, self.ordered_packet(16)[0]]
        rows += self.ordered_packet(8)[1:] + self.ordered_packet(16)[1:]
        result = m.ordered_write_provenance(rows)['writes']
        self.assertEqual([e['submission_source']['guest_packet'] for e in result], [4096, 8192])

    def test_submission_mapping_missing_invalid_or_ambiguous_is_not_a_match(self):
        self.assertEqual(m.ordered_write_provenance(self.ordered_packet(8))['writes'][0]
                         ['submission_source']['status'], 'not_observed')
        source = dict(op='submitted_packet', decoder_packet=1000, bytes=208, copied=True,
                      submission=2, mapping_valid=False, guest_packet=None)
        self.assertEqual(m.ordered_write_provenance([source]+self.ordered_packet(8))['writes'][0]
                         ['submission_source']['status'], 'invalid_range')
        self.assertEqual(m.ordered_write_provenance([source, source]+self.ordered_packet(8))['writes'][0]
                         ['submission_source']['status'], 'ambiguous')

    def test_direct_decoder_input_is_not_labelled_a_submission_snapshot(self):
        source = dict(op='submitted_packet', decoder_packet=1000, bytes=208, copied=False,
                      submission=2, mapping_valid=True, guest_packet=1000)
        result = m.ordered_write_provenance([source]+self.ordered_packet(8))['writes'][0]
        self.assertEqual(result['submission_source']['kind'], 'direct_decoder_input')

    def test_driver_hooks_do_not_imply_raw_payload_coverage_or_packet_pairing(self):
        rows = [dict(op='frame', frame=1, truncated=False),
                dict(op='cpu_driver_table_prepare', context=100, table=4096,
                     covered=True, usage_1a=0),
                dict(op='cpu_driver_table_case', context=100, table=4096, covered=True),
                dict(op='cpu_driver_table_setter', context=100, table=4096,
                     covered=True, changed=False, zero_words=4, dirty_before=0,
                     slot=0, caller=200)]
        result = self.analyze(rows)['cpu_driver_table']
        table = result['tables'][0]
        self.assertEqual((table['prepare_entries'], table['case_entry_observations'],
                          table['unchanged_setters'], table['dirty_clear_setters']), (1, 1, 1, 1))
        self.assertEqual(table['setter_callers'], [200])
        self.assertEqual(result['raw_cpu_payload_coverage'], 'not_established')
        self.assertNotIn('safe_to_remove', result)
        self.assertNotIn('matched_writes', result)

    def test_missing_driver_hooks_are_not_zero_activity(self):
        self.assertIsNone(self.analyze([dict(op='frame', frame=1, truncated=False)])['cpu_driver_table'])

    def test_uncovered_driver_records_cannot_count_as_equal_valid_setters(self):
        rows = [dict(op='frame', frame=1, truncated=True),
                dict(op='cpu_driver_table_setter', context=100, table=0,
                     covered=False, changed=False, zero_words=4, dirty_before=0,
                     slot=0, caller=200),
                dict(op='driver_table_setter_overflow')]
        result = self.analyze(rows)
        table = result['cpu_driver_table']['tables'][0]
        self.assertTrue(result['truncated'])
        self.assertEqual((table['uncovered_entries'], table['unchanged_setters'],
                          table['zero_slot_setters'], table['dirty_clear_setters']), (1, 0, 0, 0))
        self.assertEqual(len(result['cpu_driver_table']['observer_failures']), 1)

    def test_gpu_watch_requires_mapping_and_coverage_for_absence(self):
        base=[dict(op='frame',frame=1,truncated=False)]
        self.assertIsNone(self.analyze(base)['gpu_copy_pointer_watch'])
        observations=[dict(op='gpu_copy_pointer_watch',mapping=3,flags=2,overflow=0),
                      dict(op='gpu_copy_pointer_watch',mapping=0,flags=0,overflow=0),
                      dict(op='gpu_copy_pointer_uncovered',kind='indirect')]
        r=self.analyze(base+observations)['gpu_copy_pointer_watch']
        self.assertEqual((r['dispatches'],r['mapped_dispatches'],r['write_hit_dispatches'],r['uncovered']),(2,1,1,1))
        self.assertNotIn('safe_to_remove',r)
    def analyze(self,events):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'capture.txt';p.write_text('\n'.join('NATIVE '+json.dumps(e) for e in events))
            return m.analyze(p)
    def test_equal_payload_is_not_a_safe_deletion(self):
        events=[dict(op='frame',frame=1,truncated=False)]
        for v in (1,2): events.append(dict(op='command_write',address=4096,previous=v>1,same_payload=v>1,same_generation=False,generation=0,version=v))
        events += [dict(op='watch_access',watch=4096,version=1,logical_stage=0,shader=5,write=False,unknown=False,source='shader'),
                   dict(op='watch_access',watch=4096,version=1,logical_stage=7,shader=0,write=False,unknown=True,source='global'),
                   dict(op='watch_cpu',watch=4096,version=1,write=True)]
        r=self.analyze(events);t=r['targets'][0]
        self.assertEqual(t['equal_payload'],1);self.assertEqual(t['equal_payload_known_same_generation'],0)
        self.assertEqual(t['shader_reads'],{'fragment':1});self.assertEqual(t['unknown_accesses'],1)
        self.assertEqual(t['cpu_writes'],1);self.assertNotIn('safe_to_remove',r)
    def test_uid_separates_reused_native_pointer(self):
        events=[dict(op='frame',frame=1,truncated=False),dict(op='memory_lifetimes',now_us=10000),
                dict(op='memory_live',uid=2,native=99,kind=1,bytes=2048,retired=True,retired_us=9000),
                dict(op='memory_destroyed',uid=1,native=99,kind=1,bytes=1024,retired_us=1000,destroyed_us=2000)]
        r=self.analyze(events)['categories']['staging']
        self.assertEqual((r['bytes'],r['recent_destroyed_bytes'],r['oldest_pending_retirement_ms']),(2048,1024,1))
    def test_truncation_and_overflow_are_explicit(self):
        r=self.analyze([dict(op='frame',frame=1,truncated=True),dict(op='dependency_audit_overflow',kind='watch_slots')])
        self.assertTrue(r['truncated']);self.assertEqual(len(r['overflows']),1)
    def test_staging_age_is_internal_ticks_with_legacy_field_compatibility(self):
        for field in ('age_ticks','age_frames'):
            entry=dict(op='staging_lifetime',bytes=4096,held=False,gpu_complete=True)
            entry[field]=900
            r=self.analyze([dict(op='frame',frame=1,truncated=False),entry])['staging']
            self.assertEqual(r['max_idle_ticks'],900)
            self.assertEqual(r['completed_idle600_bytes'],4096)
    def test_content_provenance_does_not_treat_legacy_fields_as_zero(self):
        base=dict(op='memory_live',uid=1,native=1,kind=3,bytes=1024,retired=False,retired_us=0)
        legacy=dict(base)
        sampled=dict(base,uid=2,native=2,uploads=1,clears=0,copies=0,draws=0,storage_binds=3,unknown_writes=0)
        drawn=dict(sampled,uid=3,native=3,draws=1,storage_binds=0)
        r=self.analyze([dict(op='frame',frame=1,truncated=False),legacy,sampled,drawn])['content_provenance']
        self.assertEqual(r['missing_objects'],1);self.assertEqual(r['observed_objects'],2)
        self.assertEqual(r['encoded_nonupload_write_objects'],1)
        self.assertEqual(r['storage_binding_objects'],1)
        self.assertEqual(r['operation_counts']['uploads'],2)
    def test_rejects_multiple_frames(self):
        with self.assertRaises(ValueError):self.analyze([dict(op='frame'),dict(op='frame')])
    def test_decode_origins_are_provenance_and_legacy_is_missing(self):
        write=dict(op='command_write',address=4096,previous=False,same_payload=False,
                   same_generation=False,generation=7,version=1)
        events=[dict(op='frame',frame=1,truncated=False),write,
                dict(op='watch_access',watch=4096,version=0,logical_stage=5,shader=8,
                     write=False,unknown=True,source='hoisted_shader')]
        legacy=self.analyze(events)['targets'][0]
        self.assertIsNone(legacy['decoded_packet_sources'])
        self.assertIsNone(legacy['decoded_constant_offsets'])
        self.assertEqual(legacy['pre_first_write_requested_reads'],1)
        events += [dict(op='command_write_origin',address=4096,packet='dump_const_ram',constant_offset=768),
                   dict(op='command_write_origin',address=4096,packet='dump_const_ram',constant_offset=768),
                   dict(op='command_write_origin',address=4096,packet='graphics_write_data',constant_offset=None)]
        current=self.analyze(events)['targets'][0]
        self.assertEqual(current['decoded_packet_sources'],{'dump_const_ram':2,'graphics_write_data':1})
        self.assertEqual(current['decoded_constant_offsets'],{'768':2})
        self.assertEqual(current['writes'],1) # decode count never replaces actual write count
    def test_partial_owner_and_edge_reuse_are_distinct_from_missing_free(self):
        base=dict(bytes=4096,recorded_state=2,tag_covered=True,overlap_valid=True,
                  range_live_overlaps=0,range_live_bytes=0,heap_live_overlaps=0,heap_live_bytes=0,
                  overlap_truncated=False,history_dropped=False,current_owner=0,tag_state=3)
        rows=[dict(base,heap_live_overlaps=1,heap_live_bytes=128),dict(base),
              dict(base,recorded_state=3),dict(base,overlap_truncated=True),dict(bytes=1)]
        result=m.texture_overlaps(rows)
        self.assertEqual(result['missing_backings'],1)
        for key in ('live_overlap_without_covering_owner','released_tag_reuse_outside_image_interval',
                    'replacement_without_observed_original_free','invalid_or_incomplete_evidence'):
            self.assertEqual(result['groups'][key],dict(backings=1,capacity_bytes=4096))
        self.assertNotIn('safe_to_remove',result)
    def test_reader_coverage_preserves_missing_vs_zero(self):
        rows=[dict(op='frame',frame=1,truncated=False)]
        self.assertIsNone(self.analyze(rows)['command_reader_coverage'])
        rows.append(dict(op='command_read_source_counts',cp_graphics_wait=0,indirect_dispatch_args=1))
        self.assertEqual(self.analyze(rows)['command_reader_coverage'],dict(cp_graphics_wait=0,indirect_dispatch_args=1))
    def test_srt_cpu_reads_are_not_gpu_execution_evidence(self):
        rows=[dict(op='frame',frame=1,truncated=False),
              dict(op='command_write',address=4096,previous=False,same_payload=False,
                   same_generation=False,generation=7,version=1)]
        self.assertIsNone(self.analyze(rows)['srt_walker_coverage'])
        rows.extend([dict(op='srt_walk_coverage',calls=3,covered=2,uncovered=1,encoded_read_sites=12),
                     dict(op='watch_access',watch=4096,version=1,logical_stage=0,shader=123,
                          write=False,unknown=False,source='srt_cpu'),
                     dict(op='watch_cpu',watch=4096,version=1,write=False)])
        r=self.analyze(rows);t=r['targets'][0]
        self.assertEqual(t['srt_cpu_reads'],1);self.assertEqual(t['srt_cpu_shader_ids'],[123])
        self.assertEqual(t['cpu_reads'],1);self.assertEqual(t['shader_reads'],{})
        self.assertEqual(r['srt_walker_coverage']['uncovered'],1)
        self.assertNotIn('safe_to_remove',r)
