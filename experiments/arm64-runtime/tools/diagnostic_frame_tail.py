"""Read at most a fixed tail of complete CSV rows; no growing frame history."""
import csv,io

def read_tail(path,max_bytes=65536):
    if not 128<=max_bytes<=65536:raise ValueError('invalid fixed tail budget')
    with path.open('rb')as file:
        header=file.readline(4097)
        if not header.endswith(b'\n')or len(header)>4096:raise ValueError('missing bounded frame header')
        start=file.tell();file.seek(0,2);length=file.tell()
        offset=max(start,length-max_bytes);file.seek(offset)
        if offset>start:file.readline(max_bytes)
        body=file.read(max_bytes)
    if not body.endswith(b'\n'):body=body.rsplit(b'\n',1)[0]+b'\n'if b'\n'in body else b''
    rows=list(csv.DictReader(io.StringIO((header+body).decode('utf-8'))))
    return [r for r in rows if None not in r and all(v is not None for v in r.values())]
