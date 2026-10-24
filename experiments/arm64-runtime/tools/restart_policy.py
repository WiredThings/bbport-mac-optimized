"""Isolated restart protocol. Never accepts a command or paths from the request."""
from pathlib import Path
import stat
OFFLINE_KEYS=('BB_ARM_NET_REGION_FIX','BB_ARM_NET_DIAG','BB_NET_MODULE','BB_NET_BACKEND','BB_THD_TOKEN','BB_THD_ONLINE_ID','BB_THD_SERVER','BB_THD_AUTH','BB_THD_HTTP_ORIGIN','BB_THD_HTTP_ROUTE','BB_THD_STUN','BB_THD_RULESET','BB_P2P_PORT','BB_COMMUNITY_SERVER','BB_SHADNET_SERVER','BB_SHADNET_WEBAPI','BB_SHADNET_NPID','BB_SHADNET_PASSWORD','BB_SHADNET_TOKEN','SHADPS4_HTTP_HOST_OVERRIDES_JSON')
def take_request(path,nonce):
    info=path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink!=1 or info.st_size>128:
        raise ValueError('restart request is linked, oversized or nonregular')
    data=path.read_text()
    if data not in (f'ARMRESTART1\n{nonce}\n0\n',f'ARMRESTART1\n{nonce}\n1\n'):
        raise ValueError('restart request identity/format mismatch')
    path.unlink()
    return data.endswith('1\n')
def offline_environment(env,loading):
    clean={k:v for k,v in env.items() if k not in OFFLINE_KEYS}
    clean.update(BB_ONLINE='0',BB_ARM_ALLOW_NETWORK='0',BB_SKIP_NETWORK_CHOICE='1')
    clean.pop('BB_QUICKLOAD_PENDING',None)
    clean.pop('BB_ARM_AUTO_CONTINUE',None)
    if loading:
        if clean.get('BB_QUICKSAVE')!='1':raise ValueError('quickload without enabled checkpoints')
        clean['BB_QUICKLOAD_PENDING']='1'
    return clean

def clone_private_session(source,target,root):
    """Clone a stopped QA session; never restore directly into its evidence source."""
    import shutil,re
    source=Path(source);target=Path(target);root=Path(root).resolve()
    if source.is_symlink() or source.resolve().parent!=root or target.parent.resolve()!=root or target.exists():
        raise ValueError('QA clone must be private and target must be fresh')
    marker=source/'.arm-runtime-session'
    if marker.is_symlink() or not marker.is_file() or marker.stat().st_size>16384:
        raise ValueError('QA session marker missing/linked/oversized')
    identity=marker.read_text().splitlines()
    if len(identity)!=5 or identity[0]!='ARMQA1' or not re.fullmatch('[0-9a-f]{32}',identity[-1]):
        raise ValueError('QA session identity malformed')
    for f in source.rglob('*'):
        if f.is_symlink() or not (f.is_file() or f.is_dir()):raise ValueError('QA source contains links or nonregular files')
    shutil.copytree(source,target)
    return identity[-1]


def fresh_travel_controls(state, root):
    """Archive transient controller files only in an already-created private clone.

    Resume identity and all saves stay unchanged. A previous run's commands,
    armed marker and completion marker must never replay in the new run.
    """
    import secrets
    state = Path(state)
    root = Path(root).resolve()
    if state.is_symlink() or state.resolve().parent != root or not state.is_dir():
        raise ValueError('travel controls require a direct private state child')
    names = ('world-warper', '.private-world-warper', '.private-test-finished')
    present = []
    for name in names:
        item = state / name
        if not item.exists() and not item.is_symlink():
            continue
        info = item.lstat()
        if stat.S_ISLNK(info.st_mode) or (name == 'world-warper' and not stat.S_ISDIR(info.st_mode)) or (name != 'world-warper' and (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1)):
            raise ValueError('linked or invalid travel control')
        if item.is_dir() and any(f.is_symlink() or not (f.is_dir() or f.is_file()) for f in item.rglob('*')):
            raise ValueError('linked or invalid nested travel control')
        present.append(item)
    if present:
        archive = state / ('.qa-prior-travel-controls-' + secrets.token_hex(8))
        archive.mkdir(mode=0o700)
        for item in present:
            item.rename(archive / item.name)
    travel = state / 'world-warper'
    travel.mkdir(mode=0o700)
    return travel
