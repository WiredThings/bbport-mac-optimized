/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include "../vendor/dev_world_warper/world_warper_state.h"
int main(void) {
    WWState s={0};
    assert(!ww_request(&s,1,9999,0,1,0,0x17000000));
    assert(!ww_request(&s,1,2302950,0,1,0,0x17000000));
    assert(!ww_request(&s,2,2102950,0,1,0,0x17000000));
    assert(!ww_request(&s,1,2102950,0,0,0,0x17000000));
    assert(!ww_request(&s,1,2102950,0,1,1,0x17000000));
    assert(ww_request(&s,1,2102950,0,1,0,0x17000000));
    assert(!ww_request(&s,2,2302950,1,1,0,0x15000000));
    /* Destination coordinates/map alone never prove that loading occurred. */
    for(int i=0;i<240;i++)ww_observe(&s,i*.1,1,0,0x15000000);
    assert(s.stage==WW_REQUESTED);
    ww_observe(&s,26,1,0,0x15000000);assert(s.stage==WW_FAILED);
    s=(WWState){0};assert(ww_request(&s,1,2102950,0,1,0,0x17000000));
    ww_observe(&s,1,0,1,0x17000000);assert(s.saw_loading && s.saw_world_down);
    for(int i=0;i<40;i++)ww_observe(&s,2+i*.1,1,0,0x17000000);
    assert(s.stage==WW_LOADING); /* Wrong map cannot pass. */
    ww_observe(&s,6,1,0,0x15000000);ww_observe(&s,10,1,0,0x15000000);
    assert(s.stage==WW_LOADING); /* Two sparse observations cannot pass. */
    for(int i=0;i<35;i++)ww_observe(&s,10.1+i*.1,1,0,0x15000000);
    assert(s.stage==WW_ARRIVED);
    assert(ww_request(&s,2,2302950,15,1,0,0x15000000));
    ww_observe(&s,16,0,1,0x15000000);ww_observe(&s,196,0,1,0x15000000);
    assert(s.stage==WW_FAILED);
    s=(WWState){.stage=WW_ARRIVED,.sequence=24};
    assert(!ww_request(&s,25,2102950,0,1,0,0x17000000));
    return 0;
}
