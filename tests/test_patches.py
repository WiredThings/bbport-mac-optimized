from paths import ROOT
import contextlib
import io
import os
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
from pathlib import Path

import patches

from patches import (EBOOT_BASE, OUTPUT_SIZE, RESOLUTION_TEMPLATE, SCENE_HEIGHT,
                     SCENE_WIDTH, UI_HEIGHT, UI_WIDTH, compile_patches,
                     render_size, resolution_writes, scaled_sizes, effect_patches,
                     validate_patch_requirements, external_patches, external_selection,
                     compile_external)

XML = ROOT / 'patches/Bloodborne.xml'
SEGMENTS = [(0, 0x6000000)]


class ChineseLanguagePatchTests(unittest.TestCase):
    def compile_launch(self, language, extra='', source='official', external=None):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            # Only segment bounds are needed to compile patches; no game code runs.
            elf = bytearray(120)
            struct.pack_into('<Q', elf, 0x20, 64)
            struct.pack_into('<HH', elf, 0x36, 56, 1)
            struct.pack_into('<IIQQQQQQ', elf, 64, 1, 5, 0, 0, 0, 0, 0x6000000, 0)
            (out / 'eboot.elf').write_bytes(elf)
            env = {'BB_INTEL_TONEMAP_FIX': '0', 'BB_SKIP_NETWORK_CHOICE': '0'}
            env['BB_CHINESE_SOURCE'] = source
            if language is not None:
                env['BB_LANGUAGE'] = language
            args = ['patches.py', '--out', str(out), '--settings', str(out / 'missing.ini'),
                    '--fps', '30', '--extra', extra]
            if external:
                folder=out/'external'
                folder.mkdir()
                (folder/'region.xml').write_text(external)
                args+=['--patches-dir',str(folder)]
            log = io.StringIO()
            with patch.dict(os.environ, env, clear=True), patch.object(sys, 'argv', args), \
                    contextlib.redirect_stdout(log):
                patches.main()
            data = (out / 'patches.bin').read_bytes()
            magic, base, count = struct.unpack_from('<8sQQ', data)
            self.assertEqual((magic, base), (b'BBPATCH2', EBOOT_BASE))
            writes = []
            pos = 24
            for _ in range(count):
                offset, size = struct.unpack_from('<QQ', data, pos)
                pos += 16
                writes.append((offset, data[pos:pos + size]))
                pos += size
            self.assertEqual(pos, len(data))
            return writes, log.getvalue()

    def test_both_chinese_choices_emit_region_unlock_into_loader_blob(self):
        expected = [(0x023B9190 - EBOOT_BASE, bytes.fromhex('b805000000c3')),
                    (0x023B8730 - EBOOT_BASE, bytes.fromhex('31c0c3' + '90' * 12)),
                    (0x01C2C890 - EBOOT_BASE, bytes.fromhex('31c0c3' + '90' * 12))]
        for language in ('10', '11'):
            with self.subTest(language=language):
                writes, log = self.compile_launch(language)
                self.assertEqual(writes, expected)
                self.assertIn('Unlock Game Region', log)

    def test_other_languages_leave_region_checks_unchanged(self):
        for language in (None, '1', '0', '8', '9'):
            with self.subTest(language=language):
                writes, log = self.compile_launch(language)
                self.assertEqual(writes, [])
                self.assertNotIn('Unlock Game Region', log)

    def test_explicit_region_patch_is_not_duplicated_for_chinese(self):
        automatic, _ = self.compile_launch('11')
        explicit, _ = self.compile_launch('11', 'Unlock Game Region')
        self.assertEqual(explicit, automatic)

    def test_community_route_suppresses_automatic_and_explicit_region_unlock(self):
        for language in ('1', '10', '11'):
            writes, _ = self.compile_launch(language, 'Unlock Game Region', source='community')
            self.assertEqual(writes, [])

    def test_community_route_skips_known_external_region_and_rejects_renamed_collision(self):
        template = '''<Patch><Metadata Name="{}" AppVer="01.09" isEnabled="true">
            <PatchList><Line Type="bytes" Address="0x023b9190" Value="b805000000c3"/></PatchList>
            </Metadata></Patch>'''
        writes, _ = self.compile_launch('1', source='community', external=template.format('Unlock Game Region'))
        self.assertEqual(writes, [])
        with self.assertRaisesRegex(ValueError, 'conflicts with an external region'):
            self.compile_launch('1', source='community', external=template.format('Renamed region unlock'))
        safe = template.format('Unrelated patch').replace('0x023b9190', '0x023b0000')
        writes, _ = self.compile_launch('1', source='community', external=safe)
        self.assertEqual(len(writes), 1)


def immediate(writes, address):
    # The XML splits a 9-byte MOV imm32 + NOP into 4+4+1 byte writes.
    data = bytearray(9)
    for offset, value in writes:
        if address <= offset < address + 9:
            data[offset-address:offset-address+len(value)] = value
    assert data[5:] == b'\x0f\x1f\x40\x00'
    return data[0], struct.unpack_from('<I', data, 1)[0]


class NativeUiTests(unittest.TestCase):
    def test_720p_32_9_patch_preserves_camera_and_hud_fixes(self):
        original = compile_patches(XML, [patches.ULTRAWIDE_TEMPLATE], '01.09', SEGMENTS)
        writes = compile_patches(XML, ['Resolution Patch 2560x720 (32:9)'], '01.09', SEGMENTS)
        for address, size in ((SCENE_WIDTH, 2560), (SCENE_HEIGHT, 720),
                              (UI_WIDTH, 2560), (UI_HEIGHT, 720)):
            self.assertEqual(immediate(writes, address)[1], size)
        viewports = {SCENE_WIDTH, SCENE_HEIGHT, UI_WIDTH, UI_HEIGHT}
        self.assertEqual([w for w in original if w[0] not in viewports],
                         [w for w in writes if w[0] not in viewports])
        self.assertEqual(patches.required_dmem_mb(writes), 9152)

    def test_presets_keep_ui_native(self):
        for preset, expected in [(1, (1280, 720)), (2, (1130, 636)),
                                 (3, (960, 540)), (4, (640, 360))]:
            with self.subTest(preset=preset):
                size = render_size({'upscaler': 'fsr3', 'preset': str(preset)})
                self.assertEqual(size, expected)
                writes = resolution_writes(XML, size, '01.09', SEGMENTS)
                self.assertEqual(immediate(writes, SCENE_WIDTH), (0xB8, size[0]))
                self.assertEqual(immediate(writes, SCENE_HEIGHT), (0xB8, size[1]))
                self.assertEqual(immediate(writes, UI_WIDTH), (0xB8, OUTPUT_SIZE[0]))
                self.assertEqual(immediate(writes, UI_HEIGHT), (0xB9, OUTPUT_SIZE[1]))

    def test_explicit_scene_resolution_keeps_native_ui(self):
        writes = resolution_writes(XML, (800, 450), '01.09', SEGMENTS)
        self.assertEqual(immediate(writes, SCENE_WIDTH)[1], 800)
        self.assertEqual(immediate(writes, UI_WIDTH)[1], 1920)
        self.assertEqual(immediate(writes, UI_HEIGHT)[1], 1080)

    def test_ultrawide_upscaling_keeps_camera_and_hud_at_output_size(self):
        ui=(3840,1080)
        for scene in ((2560,720),(1920,540)):
            with self.subTest(scene=scene):
                writes=resolution_writes(XML,scene,'01.09',SEGMENTS,ui)
                self.assertEqual(immediate(writes,SCENE_WIDTH)[1],scene[0])
                self.assertEqual(immediate(writes,SCENE_HEIGHT)[1],scene[1])
                self.assertEqual(immediate(writes,UI_WIDTH)[1],ui[0])
                self.assertEqual(immediate(writes,UI_HEIGHT)[1],ui[1])
                original=compile_patches(XML,[patches.ULTRAWIDE_TEMPLATE],'01.09',SEGMENTS)
                changed={SCENE_WIDTH,SCENE_HEIGHT,UI_WIDTH,UI_HEIGHT}
                self.assertEqual([w for w in writes if w[0] not in changed],
                                 [w for w in original if w[0] not in changed])
                self.assertEqual(patches.required_dmem_mb(writes),9152)

    def test_coordinate_and_aspect_fixes_are_preserved(self):
        original = compile_patches(XML, [RESOLUTION_TEMPLATE], '01.09', SEGMENTS)
        modified = resolution_writes(XML, (960, 540), '01.09', SEGMENTS)
        self.assertEqual(len(original), len(modified))
        changed = {SCENE_WIDTH, SCENE_HEIGHT, UI_WIDTH, UI_HEIGHT}
        self.assertEqual([w for w in original if w[0] not in changed],
                         [w for w in modified if w[0] not in changed])

    def test_unexpected_or_missing_ui_instruction_is_rejected(self):
        for remove in (False, True):
            tree = ET.parse(XML)
            for meta in tree.getroot().iter('Metadata'):
                if meta.get('Name') == RESOLUTION_TEMPLATE and meta.get('AppVer') == '01.09':
                    patch_list = meta.find('PatchList')
                    for line in list(patch_list):
                        if int(line.get('Address'), 0) == UI_WIDTH + EBOOT_BASE:
                            if remove:
                                patch_list.remove(line)
                            else:
                                line.set('Value', '0x000500B9')
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'patch.xml'
                tree.write(path)
                with self.assertRaises(ValueError):
                    resolution_writes(path, (960, 540), '01.09', SEGMENTS)

    def test_native_and_disabled_upscaler_need_no_resolution_patch(self):
        self.assertIsNone(render_size({'preset': '0'}))
        self.assertIsNone(render_size({'upscaler': 'off', 'preset': '3'}))

    def test_output_other_than_1080p_scales_the_scene(self):
        self.assertIsNone(scaled_sizes({'output_res': '1920x1080', 'preset': '2'}))
        # Steam Deck: below 1080p the scene is still the preset's fraction of the output.
        for preset, expected in [(0, (1280, 720)), (2, (752, 424)), (4, (426, 240))]:
            with self.subTest(preset=preset):
                self.assertEqual(scaled_sizes({'output_res': '1280x720', 'preset': str(preset)}),
                                 (expected, (1280, 720)))
        self.assertEqual(scaled_sizes({'output_res': '3840x2160', 'preset': '3'}),
                         ((1916, 1078), (3840, 2160)))
        # TAA is native-only and uses the live host targets.
        self.assertIsNone(scaled_sizes({'output_res': '1280x720', 'upscaler': 'taa', 'preset': '3'}))

    def test_metalfx_preserves_the_existing_quality_scene_patch(self):
        for output in ['1280x720', '1920x1080', '2560x1440']:
            for preset in range(6):
                settings = dict(output_res=output, preset=str(preset))
                self.assertEqual(scaled_sizes(dict(settings, upscaler='metalfx')),
                                 scaled_sizes(dict(settings, upscaler='fsr3')))
                self.assertEqual(render_size(dict(settings, upscaler='metalfx')),
                                 render_size(dict(settings, upscaler='fsr3')))


class DebugPatchTests(unittest.TestCase):
    def test_camera_patch_is_optional_and_compatible_with_fps_and_debug_menu(self):
        self.assertEqual(effect_patches({'debug_camera': '0', 'debug_menu': '0'}), [])
        camera = effect_patches({'debug_camera': '1'})
        writes = compile_patches(XML, camera, '01.09', SEGMENTS)
        self.assertGreater(len(writes), 0)
        camera_bytes = {offset+i: byte for offset, data in writes for i, byte in enumerate(data)}
        for patch in ('Uncap FPS++', '60 FPS++', '90 FPS++', 'Restore Debug Menu (READ NOTES)'):
            for offset, data in compile_patches(XML, [patch], '01.09', SEGMENTS):
                for i, byte in enumerate(data):
                    if offset+i in camera_bytes:
                        self.assertEqual(camera_bytes[offset+i], byte, patch)

    def test_debug_menu_needs_both_fonts_where_the_game_reads_them(self):
        names = effect_patches({'debug_menu': '1'})
        menu = 'Restore Debug Menu (READ NOTES)'
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            self.assertEqual(validate_patch_requirements(['Restore Debug Camera'], game),
                             ['Restore Debug Camera'])
            # Without the fonts the patch is left out (the game would crash opening the menu).
            self.assertNotIn(menu, validate_patch_requirements(names, game))
            # The old instructions' folder: still left out, the game reads adhoc/font.
            old = game / 'dvdroot_ps4/font'
            old.mkdir(parents=True)
            (old / 'DbgFont14h.ccm').write_bytes(b'test')
            (old / 'DbgFont14h.tpf').write_bytes(b'test')
            self.assertNotIn(menu, validate_patch_requirements(names, game))
            # A mod's own case (Adhoc/Font/dbgfont14h.*) is found; an empty file is not enough.
            font = game / 'dvdroot_ps4/Adhoc/Font'
            font.mkdir(parents=True)
            (font / 'dbgfont14h.ccm').write_bytes(b'test')
            (font / 'dbgfont14h.tpf').touch()
            self.assertNotIn(menu, validate_patch_requirements(names, game))
            (font / 'dbgfont14h.tpf').write_bytes(b'test')
            self.assertIn(menu, validate_patch_requirements(names, game))

    def test_conflicting_enemy_control_patch_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'conflicts with Enemy Control'):
            validate_patch_requirements(['Enemy Control', 'Restore Debug Camera'], Path('.'))


EXTERNAL = """<?xml version="1.0"?>
<Patch>
  <TitleID><ID>CUSA03173</ID><ID>CUSA00207</ID></TitleID>
  <Metadata Title="Bloodborne" Name="On" Author="x" PatchVer="1.0" AppVer="01.09" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="bytes" Address="0x00401000" Value="9090"/></PatchList>
  </Metadata>
  <Metadata Title="Bloodborne" Name="Off" Author="x" PatchVer="1.0" AppVer="01.09" AppElf="eboot.bin">
    <PatchList><Line Type="bytes32" Address="0x00402000" Value="0x12345678"/></PatchList>
  </Metadata>
  <Metadata Title="Bloodborne" Name="Mask" Author="x" PatchVer="1.0" AppVer="01.09" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="mask" Value="90 ?? 90" Offset="0"/></PatchList>
  </Metadata>
  <Metadata Title="Bloodborne" Name="Old" Author="x" PatchVer="1.0" AppVer="01.00" AppElf="eboot.bin" isEnabled="true">
    <PatchList><Line Type="bytes" Address="0x00403000" Value="90"/></PatchList>
  </Metadata>
</Patch>"""


class ExternalPatchTests(unittest.TestCase):
    def test_selection_and_unsupported_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'extra.xml').write_text(EXTERNAL)
            (folder / 'other.xml').write_text(EXTERNAL.replace('CUSA03173', 'CUSA99999')
                                              .replace('CUSA00207', 'CUSA99998'))
            (folder / 'broken.xml').write_text('<Patch>')
            found = external_patches(folder)
            self.assertEqual([key for key, _, _ in found],
                             ['extra.xml/On', 'extra.xml/Off', 'extra.xml/Mask'])
            # The file's isEnabled; the mask patch is skipped as a whole.
            writes = compile_external(external_selection(found, None), SEGMENTS)
            self.assertEqual(writes, [(0x1000, bytes.fromhex('9090'))])
            config = folder / 'patches.json'
            config.write_text('{"enabled": ["extra.xml/Off"], "disabled": ["extra.xml/On"]}')
            writes = compile_external(external_selection(found, config), SEGMENTS)
            self.assertEqual(writes, [(0x2000, (0x12345678).to_bytes(4, 'little'))])

    def test_built_in_file_is_not_external(self):
        self.assertEqual(external_patches(XML.parent), [])


class GraphicsMemoryTests(unittest.TestCase):
    def test_ultrawide_and_heap_patches_require_extra_direct_memory(self):
        for name in ('Resolution Patch 3840x1080 (32:9)', 'Increased Graphics Heap Sizes',
                     'Resolution Patch 2560x1080 (21:9)'):
            with self.subTest(name=name):
                writes=compile_patches(XML,[name],'01.09',SEGMENTS)
                self.assertEqual(patches.required_dmem_mb(writes),9152)
        self.assertEqual(patches.required_dmem_mb([]),5056)
        self.assertEqual(patches.required_dmem_mb(
            compile_patches(XML,[RESOLUTION_TEMPLATE],'01.09',SEGMENTS)),5056)

    def test_final_bytes_determine_budget_including_split_and_overlapping_writes(self):
        heap=list(patches.GRAPHICS_HEAP_WRITES.items())
        split=[(offset+i,bytes([byte])) for offset,data in heap for i,byte in enumerate(data)]
        self.assertEqual(patches.required_dmem_mb(split),9152)
        restored=[(offset,b'\x00\x00') for offset,_ in heap]
        self.assertEqual(patches.required_dmem_mb(split+restored),5056)
        # One remaining enlarged heap still needs the larger pool.
        self.assertEqual(patches.required_dmem_mb(split+restored[:1]),9152)
        offset,data=heap[0]
        self.assertEqual(patches.required_dmem_mb([(offset-1,b'\x90'+data+b'\x90')]),9152)


class IntelTonemapTests(unittest.TestCase):
    def cpuinfo(self, vendor):
        path = Path(tempfile.mkdtemp()) / 'cpuinfo'
        path.write_text(f'processor\t: 0\nvendor_id\t: {vendor}\nmodel name\t: x\n')
        return str(path)

    def test_on_for_intel_off_for_amd(self):
        self.assertTrue(patches.intel_tonemap_fix({}, self.cpuinfo('GenuineIntel')))
        self.assertFalse(patches.intel_tonemap_fix({}, self.cpuinfo('AuthenticAMD')))

    def test_environment_forces_it(self):
        amd, intel = self.cpuinfo('AuthenticAMD'), self.cpuinfo('GenuineIntel')
        self.assertTrue(patches.intel_tonemap_fix({'BB_INTEL_TONEMAP_FIX': '1'}, amd))
        self.assertFalse(patches.intel_tonemap_fix({'BB_INTEL_TONEMAP_FIX': '0'}, intel))

    def test_patch_exists_for_109(self):
        xml = ET.parse(ROOT / 'patches/Bloodborne.xml')
        names = [m.get('Name') for m in xml.iter('Metadata')]
        self.assertIn(patches.INTEL_TONEMAP, names)


if __name__ == '__main__':
    unittest.main()


class MetalGameVelocityPatchTests(unittest.TestCase):
    def test_velocity_keeps_constructor_and_preserves_blur_setting(self):
        settings = {'upscaler': 'metalfx', 'effect_motion_blur': '0', 'effect_ssao': '0'}
        env = {'BB_RENDERER': 'metal', 'BB_METAL_OBJECT_MOTION': '1', 'BB_UPSCALER': 'metalfx'}
        with patch.dict(os.environ, env, clear=True):
            names = effect_patches(settings)
            self.assertNotIn('Disable Motion Blur (perf increase)', names)
            self.assertIn(patches.EFFECTS['effect_ssao'][0], names)
            self.assertEqual(settings['effect_motion_blur'], '0')
            self.assertEqual(patches.compatible_effect_names(['Disable Motion Blur (perf increase)'], settings), [])
            patches.validate_velocity_writes([], settings)
            with self.assertRaisesRegex(ValueError, 'constructor'):
                patches.validate_velocity_writes([(0x026C2549-EBOOT_BASE, b'\x00')], settings)
            with self.assertRaises(ValueError):
                patches.validate_velocity_writes([(0x026C2548-EBOOT_BASE, b'\x90\x00')], settings)

    def test_other_backends_and_disabled_feature_keep_existing_perf_patch(self):
        for env in ({'BB_RENDERER': 'vulkan', 'BB_METAL_OBJECT_MOTION': '1'},
                    {'BB_RENDERER': 'metal', 'BB_METAL_OBJECT_MOTION': '0'},
                    {'BB_RENDERER': 'metal', 'BB_METAL_OBJECT_MOTION': '1', 'BB_UPSCALER': 'off'}):
            with self.subTest(env=env), patch.dict(os.environ, env, clear=True):
                self.assertIn('Disable Motion Blur (perf increase)', effect_patches({'upscaler': 'metalfx', 'effect_motion_blur': '0'}))
