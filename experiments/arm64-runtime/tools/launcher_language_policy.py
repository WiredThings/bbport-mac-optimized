"""Load the unchanged Swift launcher's exported language decision, only from private fixtures."""
from pathlib import Path
import json
P=Path(__file__).resolve().parents[1]
def load(path):
 path=Path(path)
 if path.is_symlink() or path.resolve().parent!=P/'fixtures' or not path.is_file() or path.stat().st_size>4096:raise ValueError('language profile must be a small private fixture')
 env=json.loads(path.read_text());base={'BB_LANGUAGE','BB_CHINESE_SOURCE'};community={'BB_CHINESE_LANGUAGE','BB_CHINESE_TRANSLATION_DIR'}
 if not isinstance(env,dict) or env.get('BB_CHINESE_SOURCE') not in ('official','community'):raise ValueError('invalid language source')
 if set(env)!=(base|community if env['BB_CHINESE_SOURCE']=='community' else base):raise ValueError('unexpected language profile fields')
 if env['BB_CHINESE_SOURCE']=='community':
  if env['BB_LANGUAGE']!='1' or env['BB_CHINESE_LANGUAGE'] not in ('10','11'):raise ValueError('invalid community language decision')
  folder=Path(env['BB_CHINESE_TRANSLATION_DIR'])
  if folder.is_symlink() or not folder.resolve().is_relative_to(P.parent/'assets'):raise ValueError('translation must be a private asset copy')
 else:
  if env['BB_LANGUAGE'] not in tuple(map(str,range(21))):raise ValueError('invalid official language')
 return env
