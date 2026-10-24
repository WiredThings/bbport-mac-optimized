#!/usr/bin/env python3
"""Package the existing GUI with isolated paths and the native component stage."""
from pathlib import Path
import sys,time,secrets,shutil,plistlib,json,hashlib,subprocess
from launcher_environment import load
P=Path(__file__).resolve().parents[1]
label=str(time.time_ns());e=P/'evidence'/('launcher-gui-run-'+label);e.mkdir()
root=P/'state'/('gui-root-'+label);data=root/'optimized-launcher';data.mkdir(parents=True)
user=P/'state'/('gui-user-'+label);user.mkdir();source=P/'state/qa-launcher-community-seed'
shutil.copytree(source/'savedata',user/'savedata');shutil.copy2(source/'bbport.ini',data/'bbport.ini')
for f in list((user/'savedata/1').iterdir()):
 if f.name in ('CUSA03173','CUSA03173.memory'):f.rename(f.with_name('CUSA00207'+f.name[9:]))
nonce=secrets.token_hex(16);pad=root/'pad.txt';pad.write_text('');game=P.parent/'assets/CUSA03173-1.09';out=data/'out';out.mkdir()
(user/'.arm-runtime-session').write_text('ARMQA1\n'+str(out/'boot-linked.bin')+'\n'+str(game)+'\n'+str(out/'content.bin')+'\n'+nonce+'\n')
extra=dict(BB_ARM_LAUNCHER_COMPAT='1',BB_ARM_SESSION_NONCE=nonce,BB_ARM_RESTART_NONCE=secrets.token_hex(16),BB_ARM_QA_PAD_FILE=str(pad),BB_AUDIO='none',BB_ARM_PACKED_VENEERS='1',BB_ARM_RESOURCE_HOOKS='1',BB_GUEST_HOOKS='1',BB_ARM_RESOURCE_FAST='1',BB_ARM_RESOURCE_LEAF='1',BB_ARM_EXTRA_HEAPS='1',BB_MTL_HOT_STAGING_REUSE='1',BB_GPU_USER_DIR=str(P/'state/gpu-stutter-fresh-1792770276384734000'))
prefs=dict(optimizedFoldersImported=True,gameFolder=str(game),repoOverride='',userFolder=str(user),language='11',communityChinese=True,chineseSimplifiedFolder=str(P.parent/'assets/chinese-retranslation-11'),chineseTraditionalFolder='',uiLanguage='zh-Hans',renderer='metal',resolution='1920x1080',ultrawide=False,fullscreen=False,quicksave=True,online=False,cloudSaves=False,fpsMode='uncap',fpsLimit=0,presentMode='Mailbox',gameMenu=True,skipNetworkChoice=True,metalOcclusion=False,metalObjectMotion=False,modsEnabled=True,metalOptimizations={k:v=='1' for k,v in load().items() if v in ('0','1')},extraEnv='\n'.join(k+'='+v for k,v in extra.items()))
preferences=root/'preferences.plist';preferences.write_bytes(plistlib.dumps(prefs))
app=P/'apps'/('Blood Echoes ARM64 GUI QA '+label+'.app');mac=app/'Contents/MacOS';resources=app/'Contents/Resources';mac.mkdir(parents=True);resources.mkdir()
products=P/'build/launcher-gui/out/Products/Release';shutil.copy2(products/'BBLauncher',mac/'bbport');shutil.copytree(products/'BBLauncher_BBLauncher.bundle',resources/'BBLauncher_BBLauncher.bundle');shutil.copy2(P/'launcher-gui-source/Sources/BBLauncher/Resources/AppIcon.icns',resources/'AppIcon.icns')
(resources/'bbport').symlink_to(P/'launcher-component-stage',target_is_directory=True)
bid='io.github.bbport.optimized.options-qa.arm64.gui.'+label
info=dict(CFBundleIdentifier=bid,CFBundleName='Blood Echoes ARM64 GUI QA',CFBundleDisplayName='Blood Echoes ARM64 GUI QA',CFBundleExecutable='bbport',CFBundlePackageType='APPL',CFBundleShortVersionString='0.2.51',CFBundleVersion='0251',CFBundleIconFile='AppIcon',NSHighResolutionCapable=True,LSMinimumSystemVersion='14.0',BBPreviewDataPath=str(root),BBPreviewPreferencesPath=str(preferences))
(app/'Contents/Info.plist').write_bytes(plistlib.dumps(info))
# Metadata registration only; launching is delegated to Cua Driver.
subprocess.run(['/usr/bin/codesign','--force','--sign','-',str(app)],check=True)
subprocess.run(['/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister',str(app)],check=True)
case=dict(evidence=str(e),app=str(app),bundle_id=bid,root=str(root),data=str(data),user=str(user),pad=str(pad),preferences=str(preferences),cpu_sha256=hashlib.sha256((P/'build/fex/Bin/bb-game-arm64').read_bytes()).hexdigest(),silent=True,online=False,formal_data_modified=False,FPS_claim=False)
(e/'case.json').write_text(json.dumps(case,indent=2)+'\n');print(json.dumps(case,indent=2))
