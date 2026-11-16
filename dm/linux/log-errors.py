#!/usr/bin/env python3
# INPUT: Launcher/runtime output and an explicitly selected private log path.
# OUTPUT: At most 64 KiB of error records, or a full explicit diagnostic stream.
# POS: Drains output even when logging fails; never changes the game exit path.
import os,re,sys
ERROR=re.compile(r'【红】|(?<![A-Za-z])(error|fail(?:ed|ure)?|fatal|panic|abort(?:ed)?|unsupported|missing|unavailable|exhausted|denied)(?![A-Za-z])|\[nvn-vulkan\]\s+STOP\b|out of memory|segmentation fault|^LINUX_(GUEST_SIGNAL|MEMORY_LIMIT)|^D2R_LAUNCH_EXIT status=[1-9]\d*\b|^module=|^x\d+=',re.I)
LIMIT=65536

def consume(source,target,diagnostics=False):
    tail=bytearray();continuation=False;failed=False
    while True:
        line=source.readline(4096)
        if not line: break
        keep=diagnostics or bool(ERROR.search(line)) or (continuation and line[:1].isspace())
        if keep:
            data=line.encode('utf-8',errors='replace')
            if diagnostics:
                try: target.write(data);target.flush()
                except OSError: failed=True
            else:
                tail.extend(data)
                if len(tail)>LIMIT: del tail[:-LIMIT]
        continuation=keep and bool(line.strip())
    if not diagnostics and tail:
        try: target.write(tail);target.flush()
        except OSError: failed=True
    return failed

if __name__=='__main__':
    path=sys.argv[1];target=None
    try:
        fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_APPEND|os.O_NOFOLLOW,0o600)
        target=os.fdopen(fd,'wb')
    except OSError:
        target=open(os.devnull,'wb')
        print('D2R_LOG_SINK_ERROR: unable to open log; draining output',file=sys.stderr)
    failed=consume(sys.stdin,target,os.environ.get('D2R_DIAGNOSTICS')=='1');target.close()
    try:
        if os.path.isfile(path) and not os.path.islink(path) and os.stat(path).st_size==0: os.unlink(path)
    except OSError: pass
    sys.exit(int(failed))
