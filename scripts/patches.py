"""Compile selected shadPS4/GoldHEN XML patches into out/patches.bin for the loader.

Patch addresses are PS4 virtual addresses (eboot base 0x400000); the loader's
image places eboot vaddr 0 at image offset 0. Only literal writes are supported
(bytes, bytes16/32/64, float32/64, utf8, utf16); pattern ("mask") patches are rejected.
"""
import argparse
import json
import os
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

EBOOT_BASE=0x400000
# BB_FPS presets: patch names from patches/Bloodborne.xml (app version 01.09).
# Above 60 FPS the sprint fix always goes with the frame rate patch: without it sprinting drops to
# half speed (the game's wall detector measured distance per frame, a 30 FPS rule).
FPS_PRESETS={'30':[],'60':['60 FPS++'],'90':['90 FPS++','Sprint Fix (High FPS)'],
             'uncap':['Uncap FPS++','Sprint Fix (High FPS)']}
# Upscaler presets (bbport.ini "preset", the in-game menu): output / render size ratio. The game
# then renders at 1920x1080 / ratio and the port's temporal upscaler restores the output size.
OUTPUT_SIZE=(1920,1080)
PRESET_SCALES=[1.0,1.5,1.7,2.0,3.0,1.25]  # 5: Ultra Quality (bbport-mac)
# The community patch changes two independent consumers: the game render/window setup
# and the UI movie viewport. Keep the latter at native size so glyph rasterisation and
# vector tessellation do not inherit the scene's FSR resolution.
RESOLUTION_TEMPLATE='Resolution Patch 1280x720 (16:9)'
ULTRAWIDE_UI=(3840,1080)
ULTRAWIDE_TEMPLATE='Resolution Patch 3840x1080 (32:9)'
# Effect switches (bbport.ini, in-game menu, launcher): key -> (patch when the key is 0, patch
# when it is 1). The game reads these at start; a change applies after a restart.
EFFECTS={
    'effect_chromatic_aberration':('Disable Chromatic Aberration',None),
    'effect_dof':('Disable DoF',None),
    'effect_motion_blur':('Disable Motion Blur (perf increase)',None),
    'effect_ssao':('Disable SSAO',None),
    'effect_game_aa':('Disable AA',None),
    'effect_dynamic_shadows':('Disable Dynamic Light Shadows (perf increase)',None),
    'effect_ssr':(None,'Enable Screen Space Reflections (READ NOTE)'),
    'skip_intro':(None,'Skip Intro'),
    'debug_camera':(None,'Restore Debug Camera'),
    'debug_menu':(None,'Restore Debug Menu (READ NOTES)'),
}
# Intel CPUs: the game's tone mapping turns black (DLC areas most; reported as darker than on the
# PS4): the game code runs natively, and Intel's approximate float instructions differ from the
# PS4's AMD CPU, so a block the game gates on a computed flag never runs. The community fix runs
# it always; on by default on Intel. BB_INTEL_TONEMAP_FIX=0/1 forces it off or on.
INTEL_TONEMAP='Intel Black Tonemap Fix'


def intel_cpu(cpuinfo='/proc/cpuinfo'):
    try:
        with open(cpuinfo) as f:
            return any(line.startswith('vendor_id') and 'GenuineIntel' in line for line in f)
    except OSError:
        return False


# The title's PLAY ONLINE / PLAY OFFLINE dialog: the port has no PSN, the game goes straight to the
# main menu offline. On by default (the launcher's switch); BB_SKIP_NETWORK_CHOICE=0 shows it.
SKIP_NETWORK_CHOICE='Skip Online/Offline Choice'
# Official Chinese requires matching dump assets AND the region-check bypass.
# Community GOTY retranslation instead replaces English slots and runs in English.
# Neither route modifies the source dump.
UNLOCK_GAME_REGION='Unlock Game Region'


def language_patches(env=os.environ):
    return [UNLOCK_GAME_REGION] if (env.get('BB_CHINESE_SOURCE') != 'community'
                                   and env.get('BB_LANGUAGE','1') in ('10','11')) else []


def skip_network_choice(env=os.environ):
    return env.get('BB_SKIP_NETWORK_CHOICE','1')!='0'


def intel_tonemap_fix(env=os.environ, cpuinfo='/proc/cpuinfo'):
    forced=env.get('BB_INTEL_TONEMAP_FIX')
    return forced=='1' if forced in ('0','1') else intel_cpu(cpuinfo)

# model_lod: -2 highest, 0 the game's, 1 lower, 2 lowest.
MODEL_LOD={'-2':'Model LOD -2 (Highest)','1':'Model LOD 1 (Lower)','2':'Model LOD 2 (Lowest)'}


DEBUG_MENU='Restore Debug Menu (READ NOTES)'
# What the patched game reads for the debug menu (BB_FILE_TRACE=1), under dvdroot_ps4. The game asks
# for lower-case names; the PS4's file system and the port's runtime ignore case.
DEBUG_MENU_FONTS=('adhoc/font/DbgFont14h.ccm','adhoc/font/DbgFont14h.tpf')
DEBUG_MENU_SHADERS=('adhoc/FontShader/debugFont_vs.vpo','adhoc/FontShader/debugFont_ps.ppo')


def find_ignoring_case(root, relative):
    """`root`/`relative`, each component matched ignoring case; None when absent."""
    path=Path(root)
    for part in relative.split('/'):
        exact=path/part
        if exact.exists():
            path=exact
            continue
        try:
            matches=[e for e in path.iterdir() if e.name.casefold()==part.casefold()]
        except OSError:
            return None
        if not matches:
            return None
        path=matches[0]
    return path


def debug_menu_problem(game):
    """Why the debug menu patch cannot work with this game folder, or None."""
    dvdroot=Path(game)/'dvdroot_ps4'
    missing=[]
    for relative in DEBUG_MENU_FONTS:
        found=find_ignoring_case(dvdroot,relative)
        if not found or not found.is_file() or found.stat().st_size==0:
            missing.append(relative)
    if not missing:
        return None
    hint=''
    if all(find_ignoring_case(dvdroot,'font/'+r.rsplit('/',1)[1]) for r in missing):
        hint=' (they are in dvdroot_ps4/font: the game reads them from dvdroot_ps4/adhoc/font)'
    return ('the debug menu needs the font files from https://www.nexusmods.com/bloodborne/mods/253 '
            f'in dvdroot_ps4 of the game folder or of a mod: missing {", ".join(missing)}{hint}')


def validate_patch_requirements(names, game):
    """The patches to apply: raises for conflicts; drops the debug menu (with a message) when its
    font files are missing, as the game crashes opening the menu without them."""
    if 'Restore Debug Camera' in names and 'Enemy Control' in names:
        raise ValueError('Restore Debug Camera conflicts with Enemy Control; enable only one')
    if DEBUG_MENU in names:
        problem=debug_menu_problem(game)
        if problem:
            print(f'Patches: debug menu off: {problem}',file=sys.stderr)
            return [n for n in names if n!=DEBUG_MENU]
        absent=[r for r in DEBUG_MENU_SHADERS if not find_ignoring_case(Path(game)/'dvdroot_ps4',r)]
        if absent:
            print(f'Patches: debug menu: {", ".join(absent)} not found (the mod\'s font shaders); '
                  'the menu may not draw',file=sys.stderr)
    return names


def metal_game_velocity(settings):
    return (os.environ.get('BB_RENDERER') == 'metal' and
            os.environ.get('BB_METAL_OBJECT_MOTION') == '1' and
            os.environ.get('BB_UPSCALER', settings.get('upscaler')) == 'metalfx')


def compatible_effect_names(names, settings):
    # The old perf patch destroys the velocity producer. Metal replaces the final blur passes
    # with scene copies instead, keeping the user's effect_motion_blur=0 setting unchanged.
    if metal_game_velocity(settings):
        names = [n for n in names if n != EFFECTS['effect_motion_blur'][0]]
    return names


def validate_velocity_writes(writes, settings):
    if not metal_game_velocity(settings): return
    constructor = 0x026C2549 - EBOOT_BASE
    for offset, data in writes:
        if offset <= constructor < offset + len(data) and data[constructor-offset] == 0:
            raise ValueError('External patch disables the motion-blur constructor and game velocity; '
                             'disable that patch or turn off Metal game object motion vectors')


def effect_patches(settings):
    names=[]
    for key,(off,on) in EFFECTS.items():
        if key not in settings: continue
        name=on if settings[key]=='1' else off
        if name: names.append(name)
    lod=MODEL_LOD.get(settings.get('model_lod','0'))
    if lod: names.append(lod)
    return compatible_effect_names(names, settings)
SCENE_WIDTH=0x02196A6B-EBOOT_BASE
SCENE_HEIGHT=0x02196A7A-EBOOT_BASE
UI_WIDTH=0x02358554-EBOOT_BASE
UI_HEIGHT=0x0235855D-EBOOT_BASE
# The community high-resolution patches enlarge two graphics heaps. They can be selected
# externally while output_res stays 1080p, so run.sh cannot infer their memory requirement
# from the output setting. These are the final patched bytes, regardless of patch names.
GRAPHICS_HEAP_WRITES={0x04B36DDA-EBOOT_BASE:b'\x00\xf7',
                      0x04B36DE2-EBOOT_BASE:b'\x00\xa0'}
DEFAULT_DMEM_MB=5056
LARGE_DMEM_MB=9152


def required_dmem_mb(writes):
    """Direct-memory budget for the final writes, including external overrides."""
    addresses={offset+i for offset,data in GRAPHICS_HEAP_WRITES.items() for i in range(len(data))}
    final={}
    for offset,data in writes:
        for address in addresses:
            if offset<=address<offset+len(data): final[address]=data[address-offset]
    return LARGE_DMEM_MB if any(all(final.get(offset+i)==byte for i,byte in enumerate(data))
                               for offset,data in GRAPHICS_HEAP_WRITES.items()) else DEFAULT_DMEM_MB


def read_settings(path):
    settings={}
    if path.exists():
        for line in path.read_text(encoding='utf-8').splitlines():
            key,sep,value=line.partition('=')
            if sep and not line.startswith('#'): settings[key.strip()]=value.strip()
    return settings


def render_size(settings,override=''):
    """Render resolution for the upscaler preset, or None for native."""
    if override:
        w,h=(int(v) for v in override.lower().split('x'))
        return (w,h)
    if settings.get('upscaler','fsr3')=='off': return None
    preset=int(settings.get('preset','0') or 0)
    scale=PRESET_SCALES[max(0,min(preset,len(PRESET_SCALES)-1))]
    if scale==1.0: return None
    # Even sizes (the game has half-resolution buffers).
    return tuple(max(2,round(v/scale/2)*2) for v in OUTPUT_SIZE)


def output_size(settings):
    """Output (UI) size from bbport.ini output_res, e.g. 3840x2160; 1920x1080 by default."""
    try:
        w,h=(int(v) for v in settings.get('output_res','').lower().split('x'))
        if w>0 and h>0: return (w,h)
    except ValueError:
        pass
    return OUTPUT_SIZE


def scaled_sizes(settings):
    """(render, output) for an output other than 1080p (above it, or 720p for the Steam Deck):
    the game renders at output / preset scale (or at the output size without upscaler) and the
    upscaler fills the output. None at 1080p and for TAA (native, live host targets only)."""
    out=output_size(settings)
    if out==OUTPUT_SIZE or settings.get('upscaler')=='taa': return None
    scale=1.0
    if settings.get('upscaler','fsr3')!='off':
        preset=int(settings.get('preset','0') or 0)
        scale=PRESET_SCALES[max(0,min(preset,len(PRESET_SCALES)-1))]
    render=tuple(max(2,round(v/scale/2)*2) for v in out)
    # A scene of exactly 1920x1080 (4K Performance) is indistinguishable from the game's UI
    # coordinate space, which the port's UI composition recognizes by that size.
    if render==OUTPUT_SIZE: render=(1916,1078)
    return render,out


def resolution_writes(xml,size,app_version,segments,ui=OUTPUT_SIZE):
    # The wide template also fixes camera aspect, lock-on markers and HUD coordinates.
    # Keep those at the full UI size while only the scene/window allocation is reduced.
    wide=ui==ULTRAWIDE_UI
    template=ULTRAWIDE_TEMPLATE if wide else RESOLUTION_TEMPLATE
    original=ULTRAWIDE_UI if wide else (1280,720)
    writes=compile_patches(xml,[template],app_version,segments)
    replacements={SCENE_WIDTH:(0xB8,original[0],size[0]),
                  SCENE_HEIGHT:(0xB8,original[1],size[1]),
                  UI_WIDTH:(0xB8,original[0],ui[0]),
                  UI_HEIGHT:(0xB9,original[1],ui[1])}
    out=[]
    seen=set()
    for offset,data in writes:
        if offset in replacements:
            opcode,old,value=replacements[offset]
            if data!=bytes([opcode])+old.to_bytes(3,'little') or offset in seen:
                raise ValueError(f'unexpected resolution patch at {offset+EBOOT_BASE:#x}')
            data=bytes([opcode])+value.to_bytes(3,'little')
            seen.add(offset)
        out.append((offset,data))
    if seen!=replacements.keys():
        raise ValueError('resolution patch is missing scene/UI viewport instructions')
    return out


def eboot_segments(elf):
    phoff,=struct.unpack_from('<Q',elf,0x20)
    phentsize,phnum=struct.unpack_from('<HH',elf,0x36)
    segments=[]
    for i in range(phnum):
        kind,_,_,vaddr,_,_,memsz,_=struct.unpack_from('<IIQQQQQQ',elf,phoff+i*phentsize)
        if kind==1: segments.append((vaddr,vaddr+memsz))
    return segments


def encode(line):
    kind,value=line.get('Type'),line.get('Value')
    if kind=='bytes': return bytes.fromhex(value.replace(' ',''))
    if kind in ('bytes16','bytes32','bytes64'):
        return int(value,0).to_bytes(int(kind[5:])//8,'little')
    if kind=='float32': return struct.pack('<f',float(value))
    if kind=='float64': return struct.pack('<d',float(value))
    if kind=='utf8': return value.encode()+b'\0'
    if kind=='utf16': return value.encode('utf-16-le')+b'\0\0'
    raise ValueError(f'unsupported patch type {kind!r}')


def compile_patches(xml, names, app_version, segments):
    found={}
    for meta in ET.parse(xml).getroot().iter('Metadata'):
        if meta.get('Name') in names and meta.get('AppVer')==app_version and meta.get('AppElf','eboot.bin')=='eboot.bin':
            found[meta.get('Name')]=meta
    missing=[n for n in names if n not in found]
    if missing: raise ValueError(f'patches not found for app version {app_version}: {missing}')
    writes=[]
    for name in names:
        for line in found[name].iter('Line'):
            offset=int(line.get('Address'),0)-EBOOT_BASE
            data=encode(line)
            if not any(start<=offset and offset+len(data)<=end for start,end in segments):
                raise ValueError(f'{name}: address {line.get("Address")} is outside the eboot')
            writes.append((offset,data))
    return writes


# Third-party patch files (shadPS4/GoldHEN XML) in the data directory's patches/ folder.
BLOODBORNE_IDS={'CUSA00207','CUSA00208','CUSA00900','CUSA01363','CUSA03173','CUSA03023'}


def external_patches(directory, app_version='01.09', exclude=Path(__file__).resolve().parent.parent/'patches/Bloodborne.xml'):
    """[(key, file, metadata)] of eboot patches for this version in directory/*.xml.
    key is "<file name>/<patch name>" (the launcher's selection, patches.json)."""
    found=[]
    directory=Path(directory)
    for path in sorted(directory.glob('*.xml')) if directory.is_dir() else []:
        if path.resolve()==Path(exclude).resolve(): continue  # the built-in file (patches/ in a checkout)
        try:
            root=ET.parse(path).getroot()
        except ET.ParseError as error:
            print(f'Patches: {path.name}: {error}',file=sys.stderr)
            continue
        ids={e.text.strip() for e in root.iter('ID') if e.text}
        if ids and not ids&BLOODBORNE_IDS: continue
        for meta in root.iter('Metadata'):
            if meta.get('AppVer')==app_version and meta.get('AppElf','eboot.bin')=='eboot.bin':
                found.append((f'{path.name}/{meta.get("Name")}',path,meta))
    return found


def external_selection(found, config):
    """Selected external patches: patches.json {"enabled": [...], "disabled": [...]} overrides
    each file's isEnabled."""
    settings={}
    if config and Path(config).is_file():
        settings=json.loads(Path(config).read_text(encoding='utf-8'))
    enabled,disabled=set(settings.get('enabled',[])),set(settings.get('disabled',[]))
    return [(key,path,meta) for key,path,meta in found
            if key in enabled or (key not in disabled and meta.get('isEnabled','false').lower()=='true')]


def compile_external(selected, segments):
    """Writes of the selected external patches; a patch with unsupported lines is skipped whole."""
    writes=[]
    for key,_,meta in selected:
        try:
            ours=[]
            for line in meta.iter('Line'):
                offset=int(line.get('Address') or '',0)-EBOOT_BASE
                data=encode(line)
                if not any(start<=offset and offset+len(data)<=end for start,end in segments):
                    raise ValueError(f'address {line.get("Address")} is outside the eboot')
                ours.append((offset,data))
        except ValueError as error:
            print(f'Patches: skipped {key}: {error}',file=sys.stderr)
            continue
        writes+=ours
        print(f'Patches: external {key} ({len(ours)} writes)')
    return writes


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--patches-dir',type=Path,help='third-party patch XML files (shadPS4 format)')
    p.add_argument('--patches-config',type=Path,help='patches.json: enabled/disabled external patches')
    p.add_argument('--xml',type=Path,default=Path(__file__).resolve().parent.parent/'patches/Bloodborne.xml')
    p.add_argument('--fps',choices=sorted(FPS_PRESETS),default='uncap')
    p.add_argument('--extra',default='',help='additional patch names, separated by ";"')
    p.add_argument('--app-version',default='01.09')
    p.add_argument('--out',type=Path,default=Path(__file__).resolve().parent.parent/'out')
    p.add_argument('--settings',type=Path,default=Path(__file__).resolve().parent.parent/'bbport.ini')
    p.add_argument('--game-dir',type=Path,default=Path(os.environ.get('BB_GAME_DIR','../CUSA03173')))
    p.add_argument('--render-res',default='',help='render resolution WxH (overrides the preset)')
    p.add_argument('--print-preset-size',action='store_true',help='print the selected preset size, if reduced')
    p.add_argument('--output-res',default='',help='output resolution WxH (the upscaler\'s)')
    p.add_argument('--ui-res',choices=['1920x1080','3840x1080'],default='1920x1080',
                   help='game UI coordinates; 3840x1080 selects the 32:9 camera/HUD template')
    p.add_argument('--print-scaled',action='store_true',
                   help='print "RENDER OUTPUT" (WxH) when bbport.ini selects an output other than 1080p')
    a=p.parse_args()
    if a.print_scaled:
        sizes=scaled_sizes(read_settings(a.settings))
        if sizes: print(f'{sizes[0][0]}x{sizes[0][1]} {sizes[1][0]}x{sizes[1][1]}')
        return
    if a.print_preset_size:
        settings=read_settings(a.settings)
        if 'BB_UPSCALER' in os.environ:
            settings['upscaler']='fsr3' if os.environ['BB_UPSCALER']=='fsr3' else 'off'
        if 'BB_UPSCALE_PRESET' in os.environ:
            settings['preset']=os.environ['BB_UPSCALE_PRESET']
        size=render_size(settings)
        if size: print(f'{size[0]}x{size[1]}')
        return
    settings=read_settings(a.settings)
    names=FPS_PRESETS[a.fps]+[n.strip() for n in a.extra.split(';') if n.strip()]
    names+=[n for n in effect_patches(read_settings(a.settings)) if n not in names]
    names+=[n for n in language_patches() if n not in names]
    community=os.environ.get('BB_CHINESE_SOURCE')=='community'
    if community:
        names=[n for n in names if n!=UNLOCK_GAME_REGION]
    if intel_tonemap_fix() and INTEL_TONEMAP not in names:
        names.append(INTEL_TONEMAP)
    if skip_network_choice() and SKIP_NETWORK_CHOICE not in names:
        names.append(SKIP_NETWORK_CHOICE)
    names=compatible_effect_names(names, settings)
    names=validate_patch_requirements(names,a.game_dir)
    segments=eboot_segments((a.out/'eboot.elf').read_bytes())
    writes=compile_patches(a.xml,names,a.app_version,segments)
    size=render_size(read_settings(a.settings),a.render_res) if a.render_res else None
    # The UI keeps its own coordinate space (1920x1080, or the 32:9 patch's 3840x1080).
    # The renderer scales it independently of the scene into the output-size image.
    ui=tuple(int(v) for v in a.ui_res.split('x'))
    if size:
        writes+=resolution_writes(a.xml,size,a.app_version,segments,ui)
        if size[0]*size[1]>OUTPUT_SIZE[0]*OUTPUT_SIZE[1]:
            heap='Increased Graphics Heap Sizes'
            writes+=compile_patches(a.xml,[heap],a.app_version,segments)
            names.append(heap)
        print(f'Patches: scene {size[0]}x{size[1]}; UI {ui[0]}x{ui[1]}')
    if a.patches_dir:
        # After the built-in ones: an external patch of the same bytes wins.
        selected=external_selection(external_patches(a.patches_dir,a.app_version,a.xml),a.patches_config)
        if community:
            selected=[entry for entry in selected if entry[2].get('Name')!=UNLOCK_GAME_REGION]
        writes+=compile_external(selected,segments)
    if community:
        # A renamed external region patch must not silently select the official assets.
        region=compile_patches(a.xml,[UNLOCK_GAME_REGION],a.app_version,segments)
        if any(offset < r+len(b) and r < offset+len(data)
               for offset,data in writes for r,b in region):
            raise ValueError('Chinese Retranslation conflicts with an external region patch; disable that patch')
    validate_velocity_writes(writes, settings)
    # BBPATCH2: the patch base, so the loader can rebase pointers the patches write into
    # relocated slots (60/90 FPS++ replace function pointers).
    blob=struct.pack('<8sQQ',b'BBPATCH2',EBOOT_BASE,len(writes))
    for offset,data in writes: blob+=struct.pack('<QQ',offset,len(data))+data
    (a.out/'patches.bin').write_bytes(blob)
    # Consumed by run.sh after compilation: startup sizes alone miss third-party heap patches.
    (a.out/'patches.dmem').write_text(f'{required_dmem_mb(writes)}\n')
    print(f'Patches: FPS preset {a.fps}; {len(writes)} writes from {names or "none"}')


if __name__=='__main__':
    main()
