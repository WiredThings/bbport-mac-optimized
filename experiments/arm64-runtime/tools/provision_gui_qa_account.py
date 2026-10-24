#!/usr/bin/env python3
"""Pipe an explicitly authorized QA fixture to a fresh QA-only Keychain item. No values logged."""
from pathlib import Path
import json,subprocess,sys
P=Path(__file__).resolve().parents[1]
assert len(sys.argv)==2
case=Path(sys.argv[1]).resolve();assert case.parent==P/'evidence'and case.name.startswith('launcher-gui-run-')
c=json.loads((case/'case.json').read_text());assert c['bundle_id'].startswith('io.github.bbport.optimized.options-qa.arm64.gui.')
# This pre-authorized reader queries one exact QA item, never a formal item or item list.
source=subprocess.run([str(P/'build/qa_online_account'),'--export-to-parent-pipe'],capture_output=True,timeout=15)
try: account=json.loads(source.stdout)
except (ValueError,TypeError):raise SystemExit('QA credential reader returned no valid fixture; values not logged')
if source.returncode or set(account)!={'name','token'}:
 print(json.dumps(dict(installed=False,QA_reader_available=account.get('available',False),status=account.get('status'),values_logged=False)));raise SystemExit(1)
result=subprocess.run([str(P/'build/provision_gui_qa_account'),c['app']],input=source.stdout,capture_output=True,timeout=15)
source=None;account=None
# Provisioning helper output contains only constant status and OSStatus, never secret data.
print(result.stdout.decode(errors='replace').strip())
if result.returncode:raise SystemExit(result.returncode)
(case/'qa-account-fixture.json').write_text(json.dumps(dict(installed=True,source='exact pre-authorized QA Keychain item',destination=c['bundle_id']+'.huntersdream',scope='new QA item only; original GUI OnlineAccountStore unchanged',formal_account_queried=False,values_logged=False,secret_files_created=False),indent=2)+'\n')
