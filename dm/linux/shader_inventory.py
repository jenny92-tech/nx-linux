#!/usr/bin/env python3
# INPUT:  Original D2R RomFS/NSO and the upstream shader-query fact table.
# OUTPUT:  Full-code shader identities, translation jobs and source provenance.
# POS:    Prepare this exact game's Maxwell programs for Linux Vulkan review.
import argparse
import fnmatch
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "dm/tools"))
sys.path.insert(0, str(ROOT / "dm/gfx/tools"))
import dmx
from nso_inventory import Nso
import dmsh_facts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--romfs", type=Path, required=True)
    parser.add_argument("--main", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--pattern", default="*")
    parser.add_argument("--embedded-only", action="store_true", help="Inspect only NSO-embedded programs")
    parser.add_argument("--shader-policy", choices=("strict", "upstream", "optimized"), default="strict")
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists():
        parser.error("output must be a new directory")
    out.mkdir(parents=True)
    (out / "code").mkdir()
    (out / "original").mkdir()
    facts = dmsh_facts.load()
    jobs, programs, occurrences, sources, excluded = {}, {}, [], [], []

    def add(control, code, stage, source, offset):
        if len(control) != 0x880 or control[:12] != bytes.fromhex("341276980100000010000000"):
            raise ValueError(f"unrecognized NVN control header: {source}+{offset:#x}")
        size = struct.unpack_from("<I", control, 0x704)[0]
        csize, coffset, _, program = struct.unpack_from("<4I", control, 0x6fc)
        if len(code) != size or program > size or coffset > size or csize > size - coffset:
            raise ValueError(f"NVN code/constant bounds: {source}+{offset:#x}")
        if stage not in ("vs", "ps", "cs"):
            excluded.append({"source": source, "stage": stage, "reason": "stage needs renderer adaptation"})
            return
        if stage != "cs" and ((struct.unpack_from("<I",code,program)[0] >> 10) & 15) != {"vs":1,"ps":5}[stage]:
            raise ValueError("shader stage and original SPH disagree")
        key = hashlib.sha256(control + code).hexdigest()[:16]
        control_key = hashlib.sha256(control).hexdigest()[:16]
        occurrences.append({"key": key, "source": source, "offset": offset})
        if key in programs:
            if (out / "original" / (key + ".bin")).read_bytes() != control + code:
                raise ValueError("full-code identity collision")
            return
        original = out / "original" / (key + ".bin")
        original.write_bytes(control + code)
        translated_code = out / "code" / (key + ".bin")
        translated_code.write_bytes(code[program:])
        # These are the upstream's control-keyed GPU query facts, not executable
        # offsets. Keep their provenance explicit; absent facts remain absent.
        job = dict(facts.get(control_key, {}))
        if stage == "cs":
            local = list(struct.unpack_from("<3I", control, 0x748))
            shared = struct.unpack_from("<I", control, 0x754)[0]
            if not all(local) or any(n > 1024 for n in local) or local[0]*local[1]*local[2] > 1024 or shared > 98304:
                raise ValueError("invalid original NVN compute dimensions/shared memory")
            if "local" in job and job["local"] != local:
                raise ValueError("paired compute dimensions disagree with original NVN control")
            job.update(local=local, shared=shared)
        job.update(key=key, stage=stage, code=str(translated_code), c1=code[coffset:coffset+csize].hex(),
                   debug=args.shader_policy!="optimized", reduced=args.shader_policy!="strict")
        jobs[key] = job
        programs[key] = {"key":key,"control_key":control_key,"stage":stage,"code_size":size,
            "control_sha256":hashlib.sha256(control).hexdigest(),"code_sha256":hashlib.sha256(code).hexdigest(),
            "full_sha256":hashlib.sha256(control+code).hexdigest(),"upstream_query_facts":control_key in facts}

    dmx.ROOT = str(args.romfs / "data")
    for name, row in sorted(dmx.index().items()):
        if args.embedded_only or "/shaders/" not in name or not fnmatch.fnmatch(Path(name).name,args.pattern):
            continue
        data = dmx.read_row(row)  # Verifies decoded length and the original CKey.
        sources.append({"path":name,"ckey":row[4],"sha256":hashlib.sha256(data).hexdigest(),"bytes":len(data)})
        starts = [match.start() for match in re.finditer(b"BTCDBTCD",data)]
        stage = Path(name).name[:2]
        for i, start in enumerate(starts):
            end = starts[i+1] if i+1 < len(starts) else len(data)
            record = data[start:end]
            control_start = record.find(bytes.fromhex("34127698"),0x38)
            if control_start < 0 or control_start+0x880 > len(record):
                raise ValueError("truncated D2R shader record")
            add(record[control_start:control_start+0x880],record[0x38:control_start],stage,name,start)
    nso = Nso(args.main)
    for segment in nso.segments:
        if segment["name"] != "rodata":
            continue
        data = nso.read_memory(segment["memory_offset"],segment["size_bytes"])
        for match in re.finditer(bytes.fromhex("341276980100000010000000"),data):
            offset = match.start()
            if offset+0x880 > len(data):
                continue
            size = struct.unpack_from("<I",data,offset+0x704)[0]
            if not 0 < size <= offset:
                continue
            code = data[offset-size:offset]
            magic = code[:4]
            program = struct.unpack_from("<I",data,offset+0x708)[0]
            control_stage = struct.unpack_from("<I",data,offset+0x714)[0]
            # NVN's four embedded buffer helpers contain raw Maxwell compute
            # instructions at offset zero, without the optional compiler prefix.
            if control_stage == 5 and program == 0:
                stage = "cs"
            elif magic == bytes.fromhex("79563412") and control_stage == 5:
                stage = "cs"
            elif magic == bytes.fromhex("78563412") and program+4 <= len(code):
                stage = {1:"vs",5:"ps"}.get((struct.unpack_from("<I",code,program)[0]>>10)&15)
            else:
                continue
            if stage:
                add(data[offset:offset+0x880],code,stage,"main.rodata",segment["memory_offset"]+offset)
    manifest = {"original_nso_sha256":nso.source_sha256,"build_id":nso.data[0x40:0x60].hex(),
        "romfs_manifest_sha256":hashlib.sha256((args.romfs/"data/build_manifest.psv").read_bytes()).hexdigest(),
        "query_facts_sha256":hashlib.sha256(Path(dmsh_facts.TABLE).read_bytes()).hexdigest(),
        "compute_facts_source":"original NVN 1.16 control +0x748 dimensions, +0x754 shared bytes",
        "embedded_only":args.embedded_only,"shader_policy":args.shader_policy,
        "programs":list(programs.values()),"occurrences":occurrences,"sources":sources,"excluded":excluded,
        "runtime_rendering_verified":False}
    (out/"inventory.json").write_text(json.dumps(manifest,indent=2)+"\n")
    (out/"jobs.json").write_text(json.dumps(list(jobs.values()),indent=2)+"\n")
    print(json.dumps({"programs":len(programs),"occurrences":len(occurrences),"sources":len(sources),
        "upstream_query_facts":sum(p["upstream_query_facts"] for p in programs.values()),"excluded_stages":len(excluded)}))


if __name__ == "__main__":
    main()
