#!/usr/bin/env python3
# INPUT:  Full-code D2R inventory, translated SPIR-V and Khronos validation tools.
# OUTPUT:  Vulkan metadata/cache with original identities and per-program evidence.
# POS:    Bind D2R's separate NVN texture/sampler banks on the Linux renderer.
import argparse
import concurrent.futures
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"dm/tools"))
import bnsh_vulkan_cache as common
from spirv_static_offsets import fold_sample_offsets


def descriptor(resource, reflected, stage, types):
    for table,kind in (("textures",4),("images",6)):
        for binding in reflected[table]:
            if binding["binding"]!=resource["binding"]: continue
            if binding["array"]!=1 or binding["separate"]: raise ValueError("unreviewed descriptor array/separate object")
            slots=binding["cbuf_slot"]
            first=2 if slots<0 else slots&65535
            second=first if slots<0 or not slots>>16 else (slots>>16)-1
            if first!=2 or second!=2: raise ValueError("D2R texture/sampler bank is not c2")
            handle=binding["handle"]; offset=(handle&0x3fff)*4
            sampler_offset=((handle>>14)&0x3fff)*4; mode=handle>>28
            sampler=-1
            # SeparateSamplerHandle reconstructs the live packed ID with bitwise
            # OR. Compiler dataflow can put its two c2 loads in either order;
            # canonicalize by the nonoverlapping original driver-table banks.
            if mode==1 and 0x568<=offset<0x968 and 0x168<=sampler_offset<0x568:
                offset,sampler_offset=sampler_offset,offset
            if kind==6:
                if mode or not 0x120<=offset<0x168 or (offset-0x120)%8: raise ValueError("D2R image bank")
                slot=(offset-0x120)//8
            else:
                if not 0x168<=offset<0x568 or (offset-0x168)%8: raise ValueError("D2R texture bank")
                slot=(offset-0x168)//8
                if mode==1:
                    if not 0x568<=sampler_offset<0x968 or (sampler_offset-0x568)%8: raise ValueError("D2R sampler bank")
                    sampler=(sampler_offset-0x568)//8
                elif mode: raise ValueError("D2R sampler handle mode")
            pointer=types[str(resource["type_id"])]; target=types[str(pointer["operands"][1])]
            dtype=3 if kind==6 else 1 if target["opcode"]==27 else 2
            if target["opcode"]==25 and target["operands"][1]==5: dtype=5 if kind==6 else 4;kind=10
            return (resource["set"],resource["binding"],dtype,kind,slot,sampler,1,binding["type"])
    return common.descriptor(resource,reflected,stage,types)


def pack(meta,constants,size):
    stage=common.STAGES[meta["stage"]];layout=meta["layout"]; reflected=meta["nvn_resources"]
    bindings=[descriptor(r,reflected,stage,layout["types"]) for r in layout["resources"]]
    # A conditional buffer specialization is usable only while the real NVN
    # slot is a buffer. Direct buffer images use kind=10; combined sampled
    # buffers are normalized by buffer_image_module to the same kind before
    # binding. Both reach bind_shader's mandatory target/view check.
    for slot in meta["job_facts"].get("runtime_buffer_slots",[]):
        guarded=False
        for resource,binding in zip(layout["resources"],bindings):
            if binding[4]!=slot: continue
            if binding[2]==4 and binding[3]==10: guarded=True
            if binding[2]==1 and binding[3]==4:
                pointer=layout["types"][str(resource["type_id"])]
                sampled=layout["types"][str(pointer["operands"][1])]
                if sampled["opcode"]==27:
                    image=layout["types"][str(sampled["operands"][0])]
                    guarded|=image["opcode"]==25 and image["operands"][1]==5
        if not guarded:
            raise ValueError("conditional buffer specialization lacks runtime target guard")
    attrs=[(v["decorations"]["30"][0],0) for v in layout["inputs"] if "30" in v["decorations"]]
    caps=layout["capabilities"];local=meta["job_facts"].get("local",[1,1,1])
    header=struct.pack("<12I",0x4d4b5644,1,stage,len(bindings),len(attrs),len(constants),len(caps),*local,reflected["frag_out_map"],size)
    return header+b"".join(struct.pack("<8i",*b) for b in bindings)+b"".join(struct.pack("<2I",*a) for a in attrs)+struct.pack(f"<{len(caps)}I",*caps)+constants


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--inventory",type=Path,required=True)
    parser.add_argument("--spv",type=Path,required=True)
    parser.add_argument("--validator",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--jobs",type=int,default=4)
    parser.add_argument("--distance-tool",type=Path,help="Native upstream t_unused_distance CLI (optional; runtime also normalizes)")
    parser.add_argument("--resume",type=Path,help="Explicit previous cache; reuse only hash-verified successes for the same inventory/source SPIR-V")
    args=parser.parse_args()
    if not 1<=args.jobs<=8: parser.error("jobs must be 1..8")
    args.out.mkdir(parents=True,exist_ok=False);(args.out/"validated").mkdir()
    inventory=json.loads((args.inventory/"inventory.json").read_text()); originals={p["key"]:p for p in inventory["programs"]}
    jobs=json.loads((args.inventory/"jobs.json").read_text());entries=[];records=[]
    reused={};resume_provenance=None
    if args.resume:
        previous_bytes=(args.resume/"validation.json").read_bytes();previous=json.loads(previous_bytes)
        if previous["inventory_sha256"]!=hashlib.sha256((args.inventory/"inventory.json").read_bytes()).hexdigest():
            raise ValueError("resume inventory provenance")
        reused={r["key"]:r for r in previous["records"] if r["validated"]}
        resume_provenance={"path":str(args.resume.resolve()),"validation_sha256":hashlib.sha256(previous_bytes).hexdigest(),"tool_sha256":previous["tool_sha256"]}
    def one(job):
        key=job["key"];entry=None;row={"key":key,"stage":job["stage"],"validated":False}
        try:
            original=(args.inventory/"original"/(key+".bin")).read_bytes()
            if hashlib.sha256(original).hexdigest()!=originals[key]["full_sha256"]: raise ValueError("original program provenance")
            source=args.spv/(key+".spv")
            if key in reused:
                old=reused[key]
                if hashlib.sha256(source.read_bytes()).hexdigest()!=old["source_spirv_sha256"]: raise ValueError("resume source SPIR-V hash")
                for suffix,field in common.PAYLOAD_FIELDS:
                    data=(args.resume/(key+suffix)).read_bytes()
                    if hashlib.sha256(data).hexdigest()!=old[field]: raise ValueError("resume payload hash")
                    (args.out/(key+suffix)).write_bytes(data)
                raw=gzip.decompress((args.out/(key+".spv.gz")).read_bytes())
                if hashlib.sha256(raw).hexdigest()!=old["spirv_sha256"]: raise ValueError("resume uncompressed SPIR-V hash")
                entry=key.encode()+struct.pack("<II",common.STAGES[job["stage"]],len(original)-0x880)+original
                return dict(old,reused_validated_payload=True),entry
            reflection=json.loads((args.spv/(key+".res.json")).read_text(encoding="utf-8-sig"))
            if reflection["notes"]: raise ValueError("unresolved GPU query facts: "+repr(reflection["notes"]))
            if reflection["key"]!=key or reflection["stage"]!=job["stage"]: raise ValueError("reflection identity")
            source=args.spv/(key+".spv");path=args.out/"validated"/(key+".spv");path.write_bytes(source.read_bytes())
            constants=bytes.fromhex(job["c1"])
            folded=fold_sample_offsets(path,constants,reflection["cbufs"])
            common.declare_unformatted_storage_image_access(path);common.declare_distance_capabilities(path)
            stripped=None
            if args.distance_tool:
                normalized=path.with_suffix(".distance.spv")
                result=subprocess.run([str(args.distance_tool.resolve()),str(path),str(normalized)],check=True,capture_output=True,text=True)
                stripped=json.loads(result.stdout);normalized.replace(path)
            check=subprocess.run([str(args.validator),"--target-env","vulkan1.1",str(path)],capture_output=True,text=True)
            if check.returncode: raise ValueError(check.stderr)
            trimmed=common.trim_unused_capabilities(path,args.validator)
            layout=json.loads(json.dumps(common.spirv_layout(path,job["stage"],True)));raw=path.read_bytes()
            meta={"stage":job["stage"],"layout":layout,"nvn_resources":reflection,"job_facts":job}
            payload=pack(meta,constants,len(raw));compressed=gzip.compress(raw,mtime=0)
            (args.out/(key+".vkmeta")).write_bytes(payload);(args.out/(key+".spv.gz")).write_bytes(compressed)
            entry=key.encode()+struct.pack("<II",common.STAGES[job["stage"]],len(original)-0x880)+original
            row.update(validated=True,capabilities=layout["capabilities"],distance_members_removed=stripped,
                runtime_buffer_slots=job.get("runtime_buffer_slots",[]),
                unused_capabilities_removed=trimmed,static_offsets_folded=folded,
                source_spirv_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),spirv_sha256=hashlib.sha256(raw).hexdigest(),
                metadata_sha256=hashlib.sha256(payload).hexdigest(),gzip_sha256=hashlib.sha256(compressed).hexdigest())
        except (ValueError,OSError,KeyError,subprocess.CalledProcessError) as error: row["error"]=str(error)
        return row,entry
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for row,entry in pool.map(one,jobs):
            records.append(row)
            if entry is not None: entries.append(entry)
            if len(records)%250==0: print("validated",len(records),"exported",len(entries),flush=True)
    index=struct.pack("<III",0x494b5644,2,len(entries))+b"".join(entries)
    (args.out/"shader_index.bin").write_bytes(index)
    report={"programs":len(jobs),"exported":len(entries),"structural_validation_complete":len(entries)==len(jobs),
        "runtime_rendering_verified":False,"identity":"control+full original code","records":records,
        "resume_provenance":resume_provenance,"distance_tool_sha256":None if not args.distance_tool else hashlib.sha256(args.distance_tool.read_bytes()).hexdigest(),
        "index_sha256":hashlib.sha256(index).hexdigest(),
        "inventory_sha256":hashlib.sha256((args.inventory/"inventory.json").read_bytes()).hexdigest(),
        "tool_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    (args.out/"validation.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({"programs":len(jobs),"exported":len(entries),"blocked":len(jobs)-len(entries)}))


if __name__=="__main__": main()
