import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('resource',Path(__file__).parents[1]/'tools/macos/analyze_resource_chain.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


def encoder(q=1,kind=1,tick=3,guest=True,texture=7,load=1,store=1,parent=0):
    rows=[dict(op='open',queue=q,kind=kind,tick=tick,guest=guest)]
    if kind==1:
        rows.append(dict(op='attachment',queue=q,slot=0,id=texture,parent=parent,w=8,h=8,format=70,level=0,slice=0,load=load,store=store))
    rows.append(dict(op='close',queue=q,kind=kind))
    return rows


class Transitions(unittest.TestCase):
    def test_store_clear_then_load_is_only_a_hint(self):
        r=m.render_pass_transitions(encoder(load=2)+encoder())
        self.assertEqual(r['reasons'],{'same_attachments_loading':1})
        self.assertEqual(r['same_attachment_examples'][0]['slots'],[0])
        self.assertIsNone(r['state_requests'])

    def test_clear_and_dontcare_never_look_like_continuation(self):
        for rows,reason in [(encoder()+encoder(load=2),'clear_or_discard'),
                            (encoder(store=0)+encoder(),'nonstore_previous_action')]:
            self.assertEqual(m.render_pass_transitions(rows)['reasons'],{reason:1})

    def test_intervening_compute_or_blit_breaks_chain(self):
        for kind in (2,3):
            r=m.render_pass_transitions(encoder()+encoder(kind=kind)+encoder())
            self.assertEqual(r['adjacent_render_pairs'],0)

    def test_same_parent_cannot_hide_view_or_subresource_change(self):
        self.assertEqual(m.render_pass_transitions(encoder(texture=7,parent=5)+encoder(texture=8,parent=5))['reasons'],{'attachment_view_change':1})
        b=encoder();b[1]['slice']=1
        self.assertEqual(m.render_pass_transitions(encoder()+b)['reasons'],{'attachment_properties_change':1})

    def test_tick_and_host_guest_are_boundaries(self):
        self.assertEqual(m.render_pass_transitions(encoder()+encoder(tick=4))['reasons'],{'submission_tick_change':1})
        self.assertEqual(m.render_pass_transitions(encoder()+encoder(guest=False))['reasons'],{'host_guest_boundary':1})

    def test_separate_queues_are_not_joined(self):
        r=m.render_pass_transitions(encoder(q=1)+encoder(q=2))
        self.assertEqual(r['adjacent_render_pairs'],0)

    def test_clipped_malformed_and_duplicate_metadata_fail_closed(self):
        candidates=[encoder()[1:],encoder()[:-1],encoder()+[dict(op='close',queue=1,kind=1)],
                    encoder()[:2]+encoder(),encoder()[:2]+[encoder()[1]]+[encoder()[-1]]]
        missing=encoder();del missing[1]['format'];candidates.append(missing)
        for rows in candidates:
            r=m.render_pass_transitions(rows+encoder())
            self.assertEqual(r['same_attachment_examples'],[])
            self.assertTrue(any(r['boundaries'].values()))

    def test_logical_state_events_are_not_physical_passes(self):
        r=m.render_pass_transitions([dict(op='render_pass_state_split',changes=32,old_to_load_matches=True)])
        self.assertEqual(r['adjacent_render_pairs'],0)
        self.assertEqual(r['state_requests'],dict(count=1,change_masks={'32':1},consumed_clear_matches=1))


if __name__=='__main__':unittest.main()
