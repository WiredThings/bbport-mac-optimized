#!/usr/bin/env python3
"""Inspect bounded requested-access/lifetime metadata; never authorizes deleting a write."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path

STAGES = {0: 'fragment', 1: 'tess_control', 2: 'tess_eval', 3: 'vertex',
          4: 'geometry', 5: 'compute', 6: 'index', 7: 'runtime_unknown'}
KINDS = ('buffers', 'staging', 'sampled', 'targets', 'proxies', 'velocity', 'upscaler', 'occlusion')


def ordered_write_provenance(events):
    """Join observed queue copies/execution, never ring addresses across lifetimes."""
    copies = defaultdict(list)
    executions = defaultdict(list)
    active = {}
    origins = {}
    submitted = defaultdict(list)
    submitted_for_copy = {}
    rows = []
    observed = any(e['op'] in ('ordered_task_copy', 'ordered_task_execute') or
                   e['op'] == 'command_write' and 'payload_address' in e for e in events)
    if not observed:
        return None
    for index, e in enumerate(events):
        op = e['op']
        if op == 'submitted_packet':
            submitted[e['decoder_packet']].append((index, e))
        elif op in ('ordered_task_copy', 'ordered_task_execute'):
            key = (e['queue'], e['position'])
            if op == 'ordered_task_copy':
                copies[key].append((index, e))
                submitted_for_copy[(key, index)] = submitted.pop(e['source'], [])
            else:
                executions[key].append((index, e))
                active[e['payload']] = (key, index, e)
        elif op == 'command_write_origin' and e['packet'] == 'graphics_write_data':
            packet = e.get('packet_address')
            if packet is not None:
                origins[(e['address'], packet + 16)] = (index, e, active.pop(packet, None))
        elif op == 'command_write' and 'payload_address' in e:
            row = dict(address=e['address'], version=e['version'], status='missing_graphics_origin')
            origin = origins.pop((e['address'], e['payload_address']), None)
            if origin:
                origin_index, source, execution = origin
                row['packet_address'] = source['packet_address']
                row['status'] = 'no_observed_queue_execution'
                if execution:
                    key, execute_index, execute = execution
                    row.update(queue=key[0], position=key[1])
                    matches = copies[key]
                    row['status'] = 'missing_queue_copy'
                    if len(matches) > 1 or len(executions[key]) > 1:
                        row['status'] = 'duplicate_queue_identity'
                    elif matches:
                        copy_index, copy = matches[0]
                        if not copy.get('source') or copy['payload'] != execute['payload'] or copy['bytes'] != 208 or execute['bytes'] != 208:
                            row['status'] = 'queue_payload_mismatch'
                        elif not copy_index < execute_index < origin_index < index:
                            row['status'] = 'invalid_observed_order'
                        else:
                            row.update(status='matched', decoder_packet=copy['source'])
                            mappings = submitted_for_copy[(key, copy_index)]
                            row['submission_source'] = dict(status='not_observed')
                            if len(mappings) > 1:
                                row['submission_source'] = dict(status='ambiguous')
                            elif mappings:
                                mapping_index, mapping = mappings[0]
                                if not mapping['mapping_valid'] or mapping.get('guest_packet') is None or mapping['bytes'] != 208:
                                    row['submission_source'] = dict(status='invalid_range')
                                elif mapping_index >= copy_index:
                                    row['submission_source'] = dict(status='invalid_order')
                                else:
                                    row['submission_source'] = dict(status='matched',
                                        kind='submitted_snapshot' if mapping['copied'] else 'direct_decoder_input',
                                        submission=mapping['submission'], guest_packet=mapping['guest_packet'])
            rows.append(row)
    # A duplicate identity later in the same bounded window invalidates earlier joins too.
    for row in rows:
        if 'queue' in row:
            key = (row['queue'], row['position'])
            if len(copies[key]) > 1 or len(executions[key]) > 1:
                row.pop('decoder_packet', None)
                row.pop('submission_source', None)
                row['status'] = 'duplicate_queue_identity'
    return dict(writes=rows, counts=dict(Counter(e['status'] for e in rows)),
        notes=['A match proves only observed decoder-input packet to queued-task to native-write provenance.',
               'Queue position disambiguates a reused ring pointer; missing boundary records stay unmatched.',
               'Submission source mappings are consumed at copy time; they do not establish an SDK builder caller or unchanged original contents.',
               'A match does not prove GPU completion, SDK caller identity, absence of other reads/writes or safe deletion.'])


def sdk_upload_provenance(events):
    """Observed SDK -> publication snapshot -> queued write. Never a write-elision proof."""
    if not any(e['op'] == 'sdk_write_builder' for e in events):
        return None
    limit = (1 << 64) - 1
    def span(begin, size):
        return type(begin) is int and type(size) is int and 0 < begin <= limit and 0 < size <= limit - begin
    setters, builders, snapshots, pending, queued = {}, {}, {}, {}, defaultdict(list)
    invalid_capture = any(e['op'] == 'frame' and e.get('truncated') or
                          e['op'] in ('sdk_builder_overflow', 'sdk_builder_failure',
                                      'driver_table_setter_overflow', 'driver_table_setter_failure')
                          for e in events)
    frames = [e for e in events if e['op'] == 'frame']
    if len(frames) > 1:
        invalid_capture |= len(frames) > 3 or any(
            type(a.get('frame')) is not int or type(b.get('frame')) is not int or
            b['frame'] != a['frame'] + 1 or
            b.get('series_count') != a.get('series_count') or
            type(a.get('series_member')) is not int or
            b.get('series_member') != a['series_member'] + 1
            for a, b in zip(frames, frames[1:]))
    seen_builders, seen_setters, seen_snapshots = set(), set(), set()
    duplicate = False
    for index, e in enumerate(events):
        op = e['op']
        if op == 'cpu_driver_table_setter':
            identity = e.get('setter_sequence')
            if identity is None:
                continue
            duplicate |= identity in seen_setters
            seen_setters.add(identity)
            setters[(e['context'], e.get('host_thread'))] = (index, e)
        elif op == 'sdk_write_builder':
            identity = e.get('builder_sequence')
            duplicate |= identity is None or identity in seen_builders
            seen_builders.add(identity)
            packet = e.get('packet')
            if not span(packet, 208):
                continue
            # A new construction overlapping an old packet invalidates that old identity.
            builders = {p: b for p, b in builders.items() if not (p < packet + 208 and packet < p + 208)}
            row = dict(status='missing_same_thread_setter', builder_sequence=identity,
                       context=e['context'], table=e['table'], builder_packet=packet)
            thread = e.get('host_thread')
            prior = setters.get((e['context'], thread)) if type(thread) is int and thread > 0 else None
            if not e.get('covered') or e.get('bytes') != 192 or e.get('dirty') != 1 or e.get('shadow') != e['context'] + 0x1a0:
                row['status'] = 'invalid_builder_state'
            elif prior:
                si, setter = prior
                if not setter.get('covered') or setter['table'] != e['table'] or si >= index:
                    row['status'] = 'invalid_setter_state'
                else:
                    row.update(status='matched', setter_sequence=setter['setter_sequence'],
                               host_thread=e.get('host_thread'), setter_changed=setter['changed'])
            builders[packet] = row
        elif op == 'command_snapshot':
            identity = e.get('sequence')
            duplicate |= identity is None or identity in seen_snapshots
            seen_snapshots.add(identity)
            begin, size, guest = e.get('snapshot'), e.get('bytes'), e.get('guest')
            valid = e.get('valid') and span(begin, size) and span(guest, size)
            # Freeze identities NOW, not when the decoder later consumes reused addresses.
            frozen = {p: dict(b) for p, b in builders.items() if valid and guest <= p and p + 208 <= guest + size}
            snapshots[(begin, size)] = (index, e, frozen, bool(valid))
        elif op == 'submitted_packet':
            row = dict(status='missing_publication_snapshot')
            snapshot = snapshots.get((e.get('snapshot_begin'), e.get('range_bytes')))
            if not e.get('copied'):
                row['status'] = 'direct_input_not_snapshot'
            elif not e.get('mapping_valid') or e.get('bytes') != 208:
                row['status'] = 'invalid_packet_mapping'
            elif snapshot:
                si, source, frozen, valid = snapshot
                packet = e.get('guest_packet')
                offset = e['decoder_packet'] - source['snapshot'] if valid and type(e.get('decoder_packet')) is int else -1
                if not valid or si >= index or offset < 0 or offset + 208 > source['bytes'] or packet != source['guest'] + offset:
                    row['status'] = 'invalid_publication_mapping'
                elif packet not in frozen:
                    row['status'] = 'missing_observed_sdk_builder'
                else:
                    row = dict(frozen[packet], snapshot_sequence=source['sequence'])
            pending[e['decoder_packet']] = row
        elif op == 'ordered_task_copy':
            queued[(e['queue'], e['position'])].append(pending.pop(e['source'], dict(status='missing_submitted_packet')))
    base = ordered_write_provenance(events)
    rows = []
    for write in base['writes'] if base else []:
        key = (write.get('queue'), write.get('position'))
        candidates = queued.get(key, [])
        row = dict(address=write['address'], version=write['version'], status='missing_ordered_identity')
        if write['status'] == 'matched' and len(candidates) == 1:
            row.update(candidates[0])
            if row['status'] == 'matched' and row['table'] != write['address']:
                row['status'] = 'sdk_destination_mismatch'
        if invalid_capture or duplicate:
            row['status'] = 'incomplete_or_duplicate_observation'
        rows.append(row)
    return dict(writes=rows, counts=dict(Counter(r['status'] for r in rows)),
        notes=['Matches associate observed same-thread SDK calls with a publication-time snapshot generation.',
               'An SDK hook runs before original packet stores; the subsequent existing snapshot copies their completed output.',
               'Unobserved CPU writers/builders, cross-thread mutation, aliases and GPU reads are not excluded.',
               'Identity/content observations do not authorize skipping dirty flags, uploads or synchronization.'])


def driver_table_observations(events):
    """Summarize private CPU hooks without promoting entry counts to payload coverage."""
    names = {'cpu_driver_table_prepare', 'cpu_driver_table_case', 'cpu_driver_table_setter'}
    records = [e for e in events if e['op'] in names]
    if not records:
        return None  # Legacy/uninstrumented capture, not zero CPU activity.
    tables = []
    for address in sorted({e['table'] for e in records}):
        group = [e for e in records if e['table'] == address]
        prepare = [e for e in group if e['op'] == 'cpu_driver_table_prepare']
        cases = [e for e in group if e['op'] == 'cpu_driver_table_case']
        setters = [e for e in group if e['op'] == 'cpu_driver_table_setter']
        valid_setters = [e for e in setters if e['covered']]
        tables.append(dict(address=address, contexts=sorted({e['context'] for e in group}),
            prepare_entries=len(prepare), case_entry_observations=len(cases),
            prepare_usage_1a=sum(e['usage_1a'] for e in prepare if e['covered']),
            uncovered_entries=sum(not e['covered'] for e in group), setter_entries=len(setters),
            unchanged_setters=sum(not e['changed'] for e in valid_setters),
            zero_slot_setters=sum(e['zero_words'] == 4 for e in valid_setters),
            dirty_clear_setters=sum(e['dirty_before'] == 0 for e in valid_setters),
            setter_slots=dict(Counter(str(e['slot']) for e in valid_setters)),
            setter_callers=sorted({e['caller'] for e in valid_setters})))
    return dict(tables=tables, raw_cpu_payload_coverage='not_established',
        observer_failures=[e for e in events if e['op'].startswith('driver_table_')
                           and ('overflow' in e['op'] or 'failure' in e['op'])],
        notes=['CPU windows and native write encoding are different timelines; equal counts are not packet identities.',
               'No case-entry observations alone cannot prove that a hook was installed.',
               'Unchanged CPU shadow slots do not establish GPU content ownership after rebinding or another writer.',
               'These entry hooks do not cover arbitrary CPU payload loads, physical aliases or other windows.'])


def fragment_update_observations(events):
    rows=[e for e in events if e['op']=='inline_update_fragment_guard']
    if not rows:return None
    counts=Counter()
    for e in rows:
        if not e['render_pass']:key='not_rendering'
        elif e['reasons'] or e['draws']!=e['known_draws']:key='unknown_fragment_access'
        elif e['overlap']:key='fragment_conflict'
        else:key='no_observed_fragment_conflict'
        counts[key]+=1
    return dict(counts=dict(counts), observations=rows,
        notes=['Diagnostic only; no write or synchronization was changed.',
               'Native-buffer identity is used across cache wrappers; interval unions may over-report overlap.',
               'No observed conflict does not establish all alias, attachment, replay, CPU or future-consumer safety requirements.'])


def texture_overlaps(textures):
    fields=('recorded_state','tag_covered','overlap_valid','range_live_overlaps',
            'range_live_bytes','heap_live_overlaps','heap_live_bytes','overlap_truncated','history_dropped')
    observed=[e for e in textures if all(k in e for k in fields)]
    groups={}
    for e in observed:
        if not e['overlap_valid'] or not e['tag_covered'] or e['overlap_truncated'] or e['history_dropped']:
            key='invalid_or_incomplete_evidence'
        elif e['range_live_overlaps'] or e['heap_live_overlaps']:
            key='live_overlap_with_covering_owner' if e.get('current_owner') else 'live_overlap_without_covering_owner'
        elif e['recorded_state']==2 and e.get('tag_state')==3:
            key='released_tag_reuse_outside_image_interval'
        elif e['recorded_state']==3:
            key='replacement_without_observed_original_free'
        else:key='other'
        group=groups.setdefault(key,dict(backings=0,capacity_bytes=0))
        group['backings']+=1;group['capacity_bytes']+=e['bytes']
    return dict(observed_backings=len(observed),missing_backings=len(textures)-len(observed),groups=groups,
        notes=['A covering-owner lookup does not exclude partial live overlaps.',
               'Per-domain overlapping byte counts are not a cross-domain physical-memory union.',
               'No category authorizes deletion; GPU-only contents and role/lifetime checks still apply.'])


def render_pass_transitions(events):
    """Pair physical encoders per scheduler; equality is an investigation hint, not permission."""
    active, previous = {}, {}
    counts, boundaries = Counter(), Counter()
    examples = []
    fields = ('id', 'parent', 'w', 'h', 'format', 'level', 'slice', 'load', 'store')
    for index, e in enumerate(events):
        op = e['op']
        if op == 'open':
            q = e['queue']
            invalid = q in active
            if invalid:
                boundaries['overlapping_open'] += 1
                previous.pop(q, None)
            active[q] = dict(e, attachments={}, index=index, invalid=invalid)
        elif op == 'attachment':
            a = active.get(e['queue'])
            if a is None:
                boundaries['unpaired_attachment'] += 1
                previous.pop(e['queue'], None)
            elif a['kind'] != 1 or e['slot'] in a['attachments'] or any(f not in e for f in fields):
                a['invalid'] = True
                boundaries['invalid_attachment'] += 1
            else:
                a['attachments'][e['slot']] = e
        elif op == 'close':
            q = e['queue']
            a = active.pop(q, None)
            if a is None or a['kind'] != e['kind'] or a['invalid']:
                boundaries['unpaired_or_invalid_close'] += 1
                previous.pop(q, None)
                continue
            p = previous.get(q)
            previous[q] = a  # compute/blit is a real intervening encoder, too
            if not p or p['kind'] != 1 or a['kind'] != 1:
                continue
            old, new = p['attachments'], a['attachments']
            if 'tick' not in p or 'tick' not in a or 'guest' not in p or 'guest' not in a:
                reason = 'missing_pass_metadata'
            elif p['tick'] != a['tick']:
                reason = 'submission_tick_change'
            elif p['guest'] != a['guest']:
                reason = 'host_guest_boundary'
            elif not old or not new:
                reason = 'no_attachment_metadata'
            elif old.keys() != new.keys():
                reason = 'attachment_slots_change'
            elif any(old[s]['id'] != new[s]['id'] for s in old):
                reason = 'attachment_view_change'
            elif any(any(old[s][f] != new[s][f] for f in fields[:7]) for s in old):
                reason = 'attachment_properties_change'
            elif any(new[s]['load'] != 1 for s in new):  # MTLLoadActionLoad
                reason = 'clear_or_discard'
            elif any(old[s]['store'] != 1 for s in old):  # MTLStoreActionStore
                reason = 'nonstore_previous_action'
            else:
                reason = 'same_attachments_loading'
                if len(examples) < 16:
                    examples.append(dict(queue=q, previous_index=p['index'], next_index=a['index'],
                                         tick=a['tick'], slots=sorted(new)))
            counts[reason] += 1
    boundaries['unclosed_encoder'] += len(active)
    requests = [e for e in events if e['op'] == 'render_pass_state_split']
    return dict(adjacent_render_pairs=sum(counts.values()), reasons=dict(counts),
                boundaries=dict(boundaries), same_attachment_examples=examples,
                state_requests=dict(count=len(requests), change_masks=dict(Counter(str(e['changes']) for e in requests)),
                                    consumed_clear_matches=sum(e['old_to_load_matches'] for e in requests)) if requests else None,
                notes=['Physical adjacency is per scheduler and is not exclusive GPU execution or a merge authorization.',
                       'Texture view identity is exact; common parents do not establish matching subresources.',
                       'Visibility queries, sample count, feedback, command-buffer ownership and logical dependencies require separate checks.',
                       'State-split requests occur on the recording thread; they are not physical pass counts or GPU timings.'])


def analyze(path):
    path = Path(path)
    if path.stat().st_size > 8 * 1024 * 1024:
        raise ValueError('Trace exceeds 8 MiB file bound')
    events = [json.loads(line[7:]) for line in path.read_text().splitlines() if line.startswith('NATIVE ')]
    frames = [e for e in events if e['op'] == 'frame']
    if len(frames) != 1 or len(events) > 8193:
        raise ValueError('Expected one bounded native frame')
    overflow = [e for e in events if 'overflow' in e['op']]
    writes = defaultdict(list)
    uses = defaultdict(list)
    cpu = defaultdict(list)
    origins = defaultdict(list)
    allocations = defaultdict(list)
    for e in events:
        if e['op'] == 'command_write': writes[e['address']].append(e)
        if e['op'] == 'watch_access': uses[e['watch']].append(e)
        if e['op'] == 'watch_cpu': cpu[e['watch']].append(e)
        if e['op'] == 'command_write_origin': origins[e['address']].append(e)
        if e['op'] == 'command_allocation': allocations[e['address']].append(e)
    targets = []
    for address, group in writes.items():
        accesses = uses[address]
        stage_reads = Counter(STAGES[e['logical_stage']] for e in accesses
                              if e['source'] == 'shader' and not e['write'] and not e['unknown'])
        by_version = Counter(e['version'] for e in accesses if not e['write'])
        targets.append(dict(address=address, writes=len(group),
            equal_payload=sum(e['previous'] and e['same_payload'] for e in group),
            equal_payload_known_same_generation=sum(e['previous'] and e['same_payload'] and e['same_generation'] for e in group),
            known_generations=sorted(set(e['generation'] for e in group if e['generation'])),
            unknown_generation=sum(not e['generation'] for e in group),
            shader_reads=dict(stage_reads), shader_ids=sorted(set(e['shader'] for e in accesses if e['source']=='shader')),
            runtime_reads=sum(e['source']=='runtime' and not e['write'] for e in accesses),
            srt_cpu_reads=sum(e['source']=='srt_cpu' and not e['write'] for e in accesses),
            srt_cpu_shader_ids=sorted(set(e['shader'] for e in accesses if e['source']=='srt_cpu')),
            unknown_accesses=sum(e['unknown'] for e in accesses),
            versions_with_any_requested_read=len(by_version),
            pre_first_write_requested_reads=sum(not e['write'] and e['version']==0 for e in accesses),
            # Historical field names: graphics origins occur in ordered-task execution;
            # compute/CE origins are on their decode paths. Counts alone cannot join packets.
            decoded_packet_sources=dict(Counter(e['packet'] for e in origins[address])) if origins[address] else None,
            decoded_constant_offsets=dict(Counter(str(e['constant_offset']) for e in origins[address]
                if e['packet']=='dump_const_ram')) if origins[address] else None,
            observed_allocations=list({tuple(sorted(e.items())):e for e in allocations[address]}.values()) if allocations[address] else None,
            cpu_reads=sum(not e['write'] for e in cpu[address]), cpu_writes=sum(e['write'] for e in cpu[address])))
    memory = [e for e in events if e['op'] == 'memory_live']
    snapshots = [e for e in events if e['op'] == 'memory_lifetimes']
    staging = [e for e in events if e['op'] == 'staging_lifetime']
    textures = [e for e in events if e['op'] == 'texture_lifetime']
    # Native pointer reuse cannot merge lifetimes: UID disambiguates allocations.
    destroyed = {e['uid']: e for e in events if e['op'] == 'memory_destroyed'}
    reader_counts=[e for e in events if e['op']=='command_read_source_counts']
    srt_counts=[e for e in events if e['op']=='srt_walk_coverage']
    gpu_copy=[e for e in events if e['op']=='gpu_copy_pointer_watch']
    gpu_missing=[e for e in events if e['op']=='gpu_copy_pointer_uncovered']
    # A command observation is positive provenance only. Storage bindings may never
    # write, and missing fields in old captures must never turn into zero writes.
    content_keys=('uploads','clears','copies','draws','storage_binds','unknown_writes')
    content=[e for e in memory if all(k in e for k in content_keys)]
    content_summary=dict(observed_objects=len(content), missing_objects=len(memory)-len(content),
        encoded_nonupload_write_objects=sum(any(e[k] for k in ('clears','copies','draws','unknown_writes')) for e in content),
        storage_binding_objects=sum(bool(e['storage_binds']) for e in content),
        operation_counts={k:sum(e[k] for e in content) for k in content_keys})
    category = {}
    now = snapshots[0]['now_us'] if snapshots else 0
    for i, name in enumerate(KINDS):
        live = [e for e in memory if e['kind']==i]
        dead = [e for e in destroyed.values() if e['kind']==i]
        category[name] = dict(bytes=sum(e['bytes'] for e in live), objects=len(live),
            retired_bytes=sum(e['bytes'] for e in live if e['retired']),
            oldest_pending_retirement_ms=max([0]+[(now-e['retired_us'])/1000 for e in live if e['retired_us']]),
            recent_destroyed_bytes=sum(e['bytes'] for e in dead), recent_destroyed_objects=len(dead),
            max_recorded_retirement_to_destroy_ms=max([0]+[(e['destroyed_us']-e['retired_us'])/1000 for e in dead if e['retired_us']]))
    return dict(frame=frames[0]['frame'], truncated=frames[0]['truncated'], overflows=overflow,
        targets=targets, memory_snapshots=len(snapshots), categories=category,
        cpu_driver_table=driver_table_observations(events),
        ordered_write_provenance=ordered_write_provenance(events),
        fragment_update_observations=fragment_update_observations(events),
        command_reader_coverage={k:v for k,v in reader_counts[0].items() if k!='op'} if len(reader_counts)==1 else None,
        srt_walker_coverage={k:v for k,v in srt_counts[0].items() if k!='op'} if len(srt_counts)==1 else None,
        gpu_copy_pointer_watch=dict(dispatches=len(gpu_copy),uncovered=len(gpu_missing),
            mapped_dispatches=sum(bool(e['mapping']) for e in gpu_copy),
            read_hit_dispatches=sum(bool(e['flags']&1) for e in gpu_copy),
            write_hit_dispatches=sum(bool(e['flags']&2) for e in gpu_copy),
            counter_overflows=sum(bool(e['overflow']) for e in gpu_copy),
            observations=gpu_copy,uncovered_observations=gpu_missing) if gpu_copy or gpu_missing else None,
        staging=dict(objects=len(staging), bytes=sum(e['bytes'] for e in staging),
            held_bytes=sum(e['bytes'] for e in staging if e['held']),
            unfinished_bytes=sum(e['bytes'] for e in staging if not e['gpu_complete']),
            completed_idle600_bytes=sum(e['bytes'] for e in staging if e['gpu_complete'] and not e['held'] and e.get('age_ticks',e.get('age_frames',0))>=600),
            completed_idle3000_bytes=sum(e['bytes'] for e in staging if e['gpu_complete'] and not e['held'] and e.get('age_ticks',e.get('age_frames',0))>=3000),
            max_idle_ticks=max([0]+[e.get('age_ticks',e.get('age_frames',0)) for e in staging])),
        texture_backings=len(textures), memory_history=snapshots, content_provenance=content_summary,
        texture_overlap_evidence=texture_overlaps(textures),
        render_transitions=render_pass_transitions(events),
        sdk_upload_provenance=sdk_upload_provenance(events),
        texture_states=dict(Counter(str(e.get('tag_state','not_recorded')) for e in textures)),
        cold_texture_backings=sum(e['lru_age']>=600 for e in textures),
        cold_gpu_modified_backings=sum(e['lru_age']>=600 and bool(e['flags']&8) for e in textures),
        existing_safe_invalid_predicate=sum(e.get('safe_invalid_release',False) for e in textures),
        notes=['Requested logical stages are not GPU timings or actual encoder execution order.',
               'Runtime and shader observations overlap; do not add their counts.',
               'CPU trap coverage and raw/paged aliases are incomplete. No deletion is authorized by absent reads.',
               'Version zero means before the first captured write, not before all writes in previous frames; decoded packet provenance is not GPU order.',
               'Command-reader counters count observed packet/encode entries, not repeated wait-loop loads; missing legacy totals are not zeros.',
               'SRT CPU observations overlap CPU-watch records; encoded read sites are static per-invocation coverage, not successful loads. Uncovered walkers prevent absence claims.',
               'GPU pointer-watch counters describe evaluated instrumented expressions; overflow, unmapped watch pages or uncovered dispatches prevent absence claims. Diagnostic waits invalidate FPS scoring.',
               'Equal payload and generation alone do not prove a write redundant; GPU/CPU writes may intervene.',
               'Lifetime records are bounded metadata; ring overwrites, missing snapshot or truncation invalidate absence claims.',
               'Capacity and actual process footprint are different; recent destruction history is not an interval delta.',
               'Content counters record encoded commands or potential storage writes, not pixels or full overwrite coverage; absent writes do not permit reclamation.'])


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace',type=Path)
    print(json.dumps(analyze(parser.parse_args().trace),indent=2))
