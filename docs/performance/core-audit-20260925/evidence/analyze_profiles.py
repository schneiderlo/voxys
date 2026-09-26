from pathlib import Path
from collections import Counter, defaultdict
import hashlib, json, re
root=Path('/tmp/voxys-core-audit-20260925')
p=root/'profiles'
def rows(filename):
    out=[]
    for line in (p/filename).read_text().splitlines():
        fields=line.split(maxsplit=5)
        if len(fields)==6 and fields[1].endswith('%') and fields[4].endswith('%'):
            out.append(dict(flat=fields[0],flat_pct=float(fields[1][:-1]),inclusive=fields[3],inclusive_pct=float(fields[4][:-1]),function=fields[5]))
    return out
def choose(filename,functions):
    data=rows(filename)
    return [next(row for row in data if row['function'].startswith(function)) for function in functions]
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()
edit=choose('edit-all-cumulative.txt',['voxy::game::adventure::CreativeVillage::admit','voxy::game::adventure::validateConstruction','voxy::game::expedition::sweepCoveTerrainSphere','voxy::game::adventure::AdventureRuntime::json','partFor'])
idle=choose('idle-all-cumulative.txt',['voxy::game::adventure::AdventureRuntime::json','voxy::game::expedition::sweepCoveTerrainSphere','voxy::game::adventure::AdventureRuntime::refreshHud','voxy::game::adventure::AdventureRuntime::updateTarget','voxy::game::expedition::CoveInputRouter::tick'])
alloc_bytes=choose('alloc-engine-bytes.txt',['voxy::game::adventure::AdventureRuntime::json','voxy::game::adventure::CreativeScenery::admit','voxy::game::adventure::validateConstruction','voxy::game::adventure::AdventureRuntime::appendBlacksmith','voxy::game::adventure::(anonymous namespace)::compileGeometry'])
alloc_objects=choose('alloc-engine-objects.txt',['voxy::game::adventure::validateConstruction','voxy::game::adventure::(anonymous namespace)::compileGeometry','voxy::game::adventure::AdventureSession::validate','voxy::game::adventure::AdventureRuntime::refreshHud','voxy::game::adventure::AdventureRuntime::json'])
trace=(p/'io-edit.strace').read_text().splitlines()
begins=[i for i,l in enumerate(trace) if 'VOXY_REPLAY_BEGIN' in l]
ends=[i for i,l in enumerate(trace) if 'VOXY_REPLAY_END' in l]
assert len(begins)==len(ends)==1 and begins[0]<ends[0]
interval=trace[begins[0]+1:ends[0]]
counts=Counter();seconds=defaultdict(float);unparsed=[]
for line in interval:
    m=re.match(r'\d+ \d\d:\d\d:\d\d\.\d+ (\w+)\(.* <([\d.]+)>$',line)
    if not m:unparsed.append(line);continue
    counts[m[1]]+=1;seconds[m[1]]+=float(m[2])
assert not unparsed,unparsed
io={'begin':trace[begins[0]],'end':trace[ends[0]],'syscalls_excluding_markers':dict(counts),'syscall_duration_seconds_diagnostic_only':dict(seconds),'gpu_queries':sum('DRM_IOCTL_SYNCOBJ_QUERY' in line for line in interval),'file_network_data_calls':sum(count for name,count in counts.items() if name in {'read','write','readv','writev','pread64','pwrite64','preadv','pwritev','sendto','recvfrom','sendmsg','recvmsg','sendmmsg','recvmmsg','open','openat','connect','accept','accept4','fsync','fdatasync'}),'unparsed_lines':unparsed}
(p/'io-summary.json').write_text(json.dumps(io,indent=2)+'\n')
heap=(p/'alloc-edit.0001.heap').read_text().splitlines()[0]
m=re.search(r'\[\s*(\d+):\s*(\d+)\]',heap)
summary={'scope':'Fresh native optimized creative CPU replay, 768 parts. Diagnostic profiles overlapped the baseline GPU suite; no latency, throughput or FPS claim. CPU uses 36000 updates, heap and I/O 3600.','binary_sha256':sha(root/'baseline-runner'),'profile_sha256':{name:sha(p/name) for name in ['cpu-edit.prof','cpu-idle.prof','alloc-edit.0001.heap','io-edit.strace']},'cpu_sampled_ms':{'edit':3370,'idle':900},'cpu_edit_hotspots':edit,'cpu_idle_hotspots':idle,'source_hotspots':[{'source':'src/game/adventure/creative_village.cpp:79','operation':'Exact reservation overlap loop','flat_ms':201,'inclusive_ms':969,'inclusive_pct':969/3370*100},{'source':'src/game/adventure/construction_policy.cpp:69','operation':'partFor nested scans','flat_ms':166,'inclusive_ms':167,'inclusive_pct':167/3370*100}],'allocation':{'allocated_objects':int(m[1]),'allocated_bytes':int(m[2]),'allocated_mib':int(m[2])/2**20,'byte_hotspots':alloc_bytes,'object_hotspots':alloc_objects},'io':io,'cautions':['CPU exclusive percentages use UNFILTERED reports. -show=voxy:: folds filtered frames into visible parents, so its flat column is engine-attributed rather than raw self samples.','Inclusive percentages overlap (village is inside scenery; partFor is inside validation); never add them.','Benchmark validity-string search consumes 28.78% inclusive idle CPU; it is harness work, not a game optimization opportunity.','Allocation percentages are volume rather than retained memory or CPU time; filtered reports fold allocator frames into engine callers.','No startup/durable save/network workload was measured in the replay I/O interval.']}
(root/'profile-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
text=['# Fresh baseline profile attribution','',summary['scope'],'','## CPU hotspots','','Unfiltered exclusive / inclusive percentages; inclusive percentages overlap.','', '| Workload | Function | Exclusive | Inclusive |','|---|---|---:|---:|']
for workload,hotspots in [('edit',edit),('idle',idle)]:
    for row in hotspots:text.append(f"| {workload} | `{row['function']}` | {row['flat_pct']:.2f}% | {row['inclusive_pct']:.2f}% |")
text+=['',f"The village reservation loop alone accounts for 969 of 3,370 sampled milliseconds (**{969/3370*100:.2f}%**); partFor accounts for 167 (**{167/3370*100:.2f}%**). These are source attributions, not independent speedup promises.",'','## Allocation and I/O','',f"Allocation replay: {int(m[2])/2**20:.2f} MiB in {int(m[1]):,} objects. Top byte consumers: JSON 26.38%, scenery 20.11%, construction validation 14.52%, Blacksmith append 11.94%, geometry compiler 11.30%. Validation owns 61.87% of allocation objects including callees.",'',f"Marker-delimited replay I/O: {dict(counts)}; all 60 ioctls are DRM sync-object queries. No file/network read/write/sync calls occurred. Startup and golden writes are outside the interval.",'','## Interpretation limits','']
text+=['- '+warning for warning in summary['cautions']]
text+=['','Exact reproduction/analysis commands: `profile-analysis-commands.txt`; parsing script: `analyze_profiles.py`; full machine record: `profile-summary.json`.','']
(root/'profile-summary.md').write_text('\n'.join(text))
print(json.dumps({'cpu_edit':edit,'cpu_idle':idle,'source':summary['source_hotspots'],'allocation_bytes':int(m[2]),'io':io},indent=2))
