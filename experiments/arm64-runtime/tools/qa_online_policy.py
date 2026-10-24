#!/usr/bin/env python3
"""Explicit private QA credential read. No files, enumeration, formal account or logs."""
from pathlib import Path
import json,subprocess
P=Path(__file__).resolve().parents[1]
def account_environment(account):
 if not isinstance(account,dict)or set(account)!={'name','token'}:raise RuntimeError('QA account shape invalid')
 if not all(isinstance(account[k],str)and 0<len(account[k])<=16384 and '\0'not in account[k]for k in ('name','token')):raise RuntimeError('QA account values invalid')
 return dict(BB_ARM_NET_DIAG='1',BB_ONLINE='1',BB_ARM_ALLOW_NETWORK='1',BB_NET_BACKEND='thd',BB_NET_MODULE=str(P/'build/bbnet-arm64/libbbnet.dylib'),BB_THD_AUTH='https://thehuntersdream.com',BB_THD_HTTP_ORIGIN='https://thehuntersdream.com:20443',BB_THD_HTTP_ROUTE='https://thehuntersdream.com',BB_THD_SERVER='https://thehuntersdream.com:18671',BB_THD_STUN='',BB_THD_RULESET='vanilla',BB_P2P_PORT='3658',BB_THD_ONLINE_ID=account['name'],BB_THD_TOKEN=account['token'],BB_THD_VERIFY_TLS='1',BB_COMMUNITY_SERVER='https://thehuntersdream.com:20443',BB_CLIENT_VERSION='BloodEchoes/ARM64-Isolated-QA',BB_SKIP_NETWORK_CHOICE='0')
def load_private_environment():
 helper=P/'build/qa_online_account'
 if not helper.is_file()or helper.is_symlink():raise RuntimeError('QA credential helper unavailable')
 r=subprocess.run([str(helper),'--export-to-parent-pipe'],capture_output=True,timeout=15)
 if r.returncode or not 0<len(r.stdout)<=65536:raise RuntimeError('QA Keychain read failed; no game launched')
 try:account=json.loads(r.stdout)
 except (ValueError,UnicodeError):raise RuntimeError('QA account response invalid')from None
 return account_environment(account)

def prepare_private_routes(state,environment):
 import importlib.util,tempfile
 state=Path(state)
 if state.is_symlink()or state.resolve().parent!=P/'state'or not state.is_dir():raise ValueError('routes require fresh Shared QA state')
 spec=importlib.util.spec_from_file_location('qa_launcher_online_routes',P/'vendor/blood_echoes/online_routes.py')
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 output=Path(tempfile.mkdtemp(prefix='.qa-online-routes-',dir=state))
 path=module.prepare(state,environment['BB_THD_HTTP_ROUTE'],environment['BB_THD_SERVER'],output)
 return {'SHADPS4_HTTP_HOST_OVERRIDES_JSON':str(path)}
