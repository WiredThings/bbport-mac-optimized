"""Give the separately running Metal game a stable macOS application identity."""
import os
from pathlib import Path
import plistlib
import shutil
import subprocess

from package_optimized import loads, SYSTEM

GAME_BUNDLE_ID = 'io.github.bbport.optimized.game'
GAME_APP = Path('bin/Bloodborne Game.app')
GAME_ICON = Path(__file__).parent / 'resources/BloodborneGame.icns'


def create_game_app(app):
    """Only call on an owned staging app, before signing the outer bundle."""
    app = Path(app)
    engine = app / 'Contents/Resources/bbport'
    bundle = engine / GAME_APP
    if bundle.exists():
        shutil.rmtree(bundle)
    executable = bundle / 'Contents/MacOS/bb-probe-metal'
    executable.parent.mkdir(parents=True)
    original = engine / 'bin/bb-probe-metal'
    shutil.copy2(original, executable)
    for ref in loads(original):
        if ref.startswith(SYSTEM):
            continue
        if not ref.startswith('@loader_path/'):
            raise ValueError('Game bundle requires already relocated dependencies: ' + ref)
        dependency = (original.parent / ref.removeprefix('@loader_path/')).resolve()
        if not dependency.is_file() or not dependency.is_relative_to(engine.resolve()):
            raise ValueError('Game dependency must remain inside the app: ' + ref)
        relocated = '@loader_path/' + os.path.relpath(dependency, executable.parent)
        subprocess.run(['install_name_tool', '-change', ref, relocated, str(executable)], check=True)
    outer = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    info = dict(CFBundleIdentifier=GAME_BUNDLE_ID, CFBundleName='Bloodborne',
                CFBundleDisplayName='Bloodborne', CFBundleExecutable=executable.name,
                CFBundlePackageType='APPL', CFBundleInfoDictionaryVersion='6.0',
                CFBundleShortVersionString=outer['CFBundleShortVersionString'],
                CFBundleVersion=outer['CFBundleVersion'], CFBundleIconFile='AppIcon',
                LSMinimumSystemVersion=outer.get('LSMinimumSystemVersion', '26.0'),
                NSHighResolutionCapable=True, NSPrincipalClass='NSApplication')
    (bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    resources = bundle / 'Contents/Resources'
    resources.mkdir()
    # Use the game cover even before SDL initializes and after the runtime icon
    # override is discarded. The launcher's Hunter's Mark stays on the outer app.
    shutil.copy2(GAME_ICON, resources / 'AppIcon.icns')
    subprocess.run(['codesign', '--force', '--sign', '-', str(executable)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(bundle)], check=True)
    return executable
