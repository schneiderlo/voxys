from pathlib import Path
from collections import Counter, defaultdict
import hashlib, json, re
root=Path('/tmp/voxys-core-audit-20260925');p=root/'final-profiles'
baseline=json.loads((root/'profile-summary.json').read_text())
def rows(file):
    out=[]
    for line in (p/file).read_text().splitlines():
        fields=line.split(maxsplit=5)
        if len(fields)==6 and fields[1].endswith('%') and fields[4].endswith('%'):
            out.append(dict(flat=fields[0],flat_pct=float(fields[1][:-1]),inclusive=fields[3],inclusive_pct=float(fields[4][:-1]),function=fields[5]))
    return out
def choose(file,functions):
    data=rows(file)
    return [next(row for row in data if row['function'].startswith(fn)) for fn in functions]
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()
edit=choose('edit-all-cumulative.txt',['voxy::game::adventure::CreativeVillage::admit','voxy::game::adventure::validateConstruction','voxy::game::expedition::sweepCoveTerrainSphere','voxy::game::adventure::AdventureRuntime::json','partFor'])
idle=choose('idle-all-cumulative.txt',['voxy::game::adventure::AdventureRuntime::json','voxy::game::expedition::sweepCoveTerrainSphere','voxy::game::adventure::AdventureRuntime::refreshHud','voxy::game::adventure::AdventureRuntime::updateTarget','voxy::game::expedition::CoveInputRouter::tick'])
heap=(p/'alloc-edit.0001.heap').read_text().splitlines()[0]
m=re.search(r'heap profile:\s*(\d+):\s*(\d+)\s*\[\s*(\d+):\s*(\d+)\]',heap)
retained_objects,retained_bytes,allocated_objects,allocated_bytes=map(int,m.groups())
trace=(p/'io-edit.strace').read_text().splitlines();begins=[i for i,l in enumerate(trace) if 'VOXY_REPLAY_BEGIN' in l];ends=[i for i,l in enumerate(trace) if 'VOXY_REPLAY_END' in l]
assert len(begins)==len(ends)==1 and begins[0]<ends[0]
interval=trace[begins[0]+1:ends[0]];counts=Counter();duration=defaultdict(float);unparsed=[]
for line in interval:
    match=re.match(r'\d+ \d\d:\d\d:\d\d\.\d+ (\w+)\(.* <([\d.]+)>$',line)
    if match:counts[match[1]]+=1;duration[match[1]]+=float(match[2])
    else:unparsed.append(line)
assert not unparsed,unparsed
io={'begin':trace[begins[0]],'end':trace[ends[0]],'syscalls_excluding_markers':dict(counts),'syscall_duration_seconds_diagnostic_only':dict(duration),'gpu_queries':sum('DRM_IOCTL_SYNCOBJ_QUERY' in line for line in interval),'file_network_data_calls':sum(count for name,count in counts.items() if name in {'read','write','readv','writev','pread64','pwrite64','preadv','pwritev','sendto','recvfrom','sendmsg','recvmsg','sendmmsg','recvmmsg','open','openat','connect','accept','accept4','fsync','fdatasync'}),'unparsed_lines':unparsed}
(p/'io-summary.json').write_text(json.dumps(io,indent=2)+'\n')
allocation={'allocated_bytes':allocated_bytes,'allocated_objects':allocated_objects,'allocated_mib':allocated_bytes/2**20,'delta_bytes':allocated_bytes-baseline['allocation']['allocated_bytes'],'delta_objects':allocated_objects-baseline['allocation']['allocated_objects'],'delta_percent':100*(allocated_bytes/baseline['allocation']['allocated_bytes']-1),'retained_bytes_at_dump':retained_bytes,'retained_objects_at_dump':retained_objects,'byte_hotspots':choose('alloc-engine-bytes.txt',['voxy::game::adventure::AdventureRuntime::json','voxy::game::adventure::CreativeScenery::admit','voxy::game::adventure::validateConstruction','voxy::game::adventure::AdventureRuntime::appendBlacksmith','voxy::game::adventure::(anonymous namespace)::compileGeometry']),'object_hotspots':choose('alloc-engine-objects.txt',['voxy::game::adventure::validateConstruction','voxy::game::adventure::(anonymous namespace)::compileGeometry','voxy::game::adventure::AdventureSession::validate','voxy::game::adventure::AdventureRuntime::refreshHud','voxy::game::adventure::AdventureRuntime::json'])}
comparison=[]
for before,after in zip(baseline['cpu_edit_hotspots'],edit):comparison.append({'baseline':before,'final':after})
residual={'definition':'src/game/adventure/construction_policy.cpp:69','flat_ms':54,'inclusive_ms':55,'inclusive_pct':2.19,'caller':'validateDoorClearance','caller_call_site':'src/game/adventure/construction_policy.cpp:167','caller_expression':'if(partFor(before,solid.part.counter)||partFor(after,solid.part.counter))continue;','caller_expression_inclusive_ms':124,'call_tree_attribution':'All 55 ms of remaining partFor samples are reached through validateDoorClearance; PartLookup fallback is absent from this focused tree.','other_door_lookup':'The move check at line149 has no attributed samples.','changed_lookup_sites':{'src/game/adventure/construction_policy.cpp:234':{'operation':'PartLookup certification','inclusive_ms':2},'src/game/adventure/construction_policy.cpp:236':{'operation':'after-part lookup/expression','inclusive_ms':26},'src/game/adventure/construction_policy.cpp:256':{'operation':'removed-part lookup/expression','inclusive_ms':36}}}
summary={'scope':'Diagnostic final-runner CPU/heap/I/O captures. Captures overlapped baseline GPU suite and do not provide clean latency/throughput/FPS comparisons. No additional workloads were run for this analysis.','binary_sha256':sha(root/'final-runner'),'profile_sha256':{name:sha(p/name) for name in ['cpu-edit.prof','cpu-idle.prof','alloc-edit.0001.heap','io-edit.strace']},'cpu_sampled_ms':{'edit':2510,'idle':1341},'cpu_edit_hotspots':edit,'cpu_idle_hotspots':idle,'edit_comparison':comparison,'remaining_partFor':residual,'village_index_source_attribution':{'constructor_ms':28,'constructor_pct':28/2510*100,'constructor_line_114_inclusive_ms':30,'sort_line_32_inclusive_ms':27,'remaining_fixed_group_overlap_line_196_inclusive_ms':34},'allocation':allocation,'io':io,'cautions':baseline['cautions']+['Baseline idle has900 samples and final idle1341; proportions are similar and total sample-time changes are not a controlled idle regression estimate.','Source inclusive attribution can duplicate inlined call-site samples; do not sum source lines with function totals.']}
(root/'final-profile-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
text=['# Final profile attribution','',summary['scope'],'','| Edit hotspot | Baseline sampled ms / inclusive share | Final sampled ms / inclusive share |','|---|---:|---:|']
for pair in comparison:
    a,b=pair['baseline'],pair['final'];text.append(f"| `{a['function']}` | {a['inclusive']} / {a['inclusive_pct']:.2f}% | {b['inclusive']} / {b['inclusive_pct']:.2f}% |")
text+=['','Sample totals: edit3370→2510ms; idle900→1341ms. These are diagnostic samples, not wall-time speedups. Raw exclusive and inclusive values are both retained in JSON; filtered engine `flat` values fold hidden library/inlined callees into their parents.','','Remaining `partFor`: all55ms (2.19%) come from `validateDoorClearance`, specifically its unchanged accepted-solids loop at `construction_policy.cpp:167`. That entire two-lookup expression has124ms source-attributed inclusive time. The move check at line149 has no samples. This is not the new sorted lookup falling back.','','Village admission falls from1058 to120 sampled milliseconds. Its new index constructor has28ms (sorting27ms); construction-site source attribution is30ms. The unchanged paving-vs-fixed-group loop has34ms. These source numbers overlap and should not be summed.','',f"Allocation: {allocated_bytes:,} bytes ({allocated_bytes/2**20:.2f}MiB), {allocated_objects:,} objects; delta+{allocation['delta_bytes']:,}bytes (+{allocation['delta_bytes']/2**20:.2f}MiB / +{allocation['delta_percent']:.3f}%) and+{allocation['delta_objects']}objects. Retained-at-dump bytes/objects are unchanged at43,788,332 /3,257; those include retained golden observations and are not peak RSS.",'',f"Final replay I/O: {dict(counts)}. GPU queries={io['gpu_queries']}, file/network data calls={io['file_network_data_calls']}. Full source reports, focused caller tree and marker-delimited I/O summary are under `final-profiles/`. Exact commands are in `final-profile-analysis-commands.txt`.",'']
(root/'final-profile-summary.md').write_text('\n'.join(text))
print(json.dumps({'final_summary':str(root/'final-profile-summary.json'),'allocation_delta_bytes':allocation['delta_bytes'],'allocation_delta_objects':allocation['delta_objects'],'io':dict(counts)},indent=2))
