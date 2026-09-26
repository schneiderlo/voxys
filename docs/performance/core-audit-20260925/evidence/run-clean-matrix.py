import json,os,pathlib,subprocess,time
root=pathlib.Path('/tmp/voxys-core-audit-20260925')
workspace=pathlib.Path.cwd()
# The only waiter is the original aggregate suite launched by this audit.
# Do not suspend it: pausing could invalidate tests with wall-clock deadlines.
while True:
 try: command=pathlib.Path('/proc/40404/cmdline').read_bytes()
 except FileNotFoundError: break
 if b'voxy_tests' not in command: break
 time.sleep(5)
env=dict(os.environ,LC_ALL='C',TZ='UTC')
variants=('baseline','village','final')
monitor=[]
for variant in variants: (root/variant).mkdir(exist_ok=True)
for repeat in (1,2,3):
 order=variants[repeat-1:]+variants[:repeat-1]
 for parts in (0,64,256,768):
  for mode in ('idle','edit'):
   for variant in order:
    prefix=root/variant/f'{parts}-{mode}-{repeat}'
    cmd=[str(root/f'{variant}-runner'),str(workspace),str(parts),'3600',str(prefix),mode]
    monitor.append({'unix_time':time.time(),'command':cmd,'processes':subprocess.check_output(['ps','-eo','pid,ppid,stat,comm,pcpu'],text=True)})
    (root/'clean-matrix-monitor.json').write_text(json.dumps(monitor,indent=2)+'\n')
    with prefix.with_suffix('.log').open('w') as output:
     subprocess.run(cmd,stdout=output,stderr=subprocess.STDOUT,env=env,check=True)
    print(variant,parts,mode,repeat,flush=True)
comparison_failed=False
for before,after,name in (('baseline','village','village-comparison'),('village','final','lookup-incremental-comparison'),('baseline','final','final-comparison')):
 cmd=['python3','scripts/performance/compare_free_build_benchmark.py',str(root/before),str(root/after)]
 with (root/f'{name}.json').open('w') as output:
  result=subprocess.run(cmd,stdout=output,stderr=subprocess.STDOUT)
 print(name,result.returncode,flush=True)
 comparison_failed = comparison_failed or result.returncode != 0
raise SystemExit(1 if comparison_failed else 0)
