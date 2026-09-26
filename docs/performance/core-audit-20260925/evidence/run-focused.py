import json,os,pathlib,subprocess
root=pathlib.Path('/tmp/voxys-core-audit-20260925')
workspace=pathlib.Path.cwd()
env=dict(os.environ, VOXY_ADVENTURE_TEST_TERRAIN=str(workspace/'data/generated/td_seed_1234_8192.r16'), VOXY_ADVENTURE_TEST_WORKSPACE=str(workspace), BUILD_WORKSPACE_DIRECTORY=str(workspace))
results=[]
for target in ('adventure','adventure_doors','adventure_walkable','adventure_movement','adventure_navigation','adventure_pointer','adventure_encounters','adventure_runtime'):
 with (root/f'final-{target}.log').open('w') as output:
  command=[str(root/'opt-bin/tests'/target)]
  result=subprocess.run(command,stdout=output,stderr=subprocess.STDOUT,env=env)
  results.append(dict(command=command,exit_code=result.returncode,log=output.name))
 print(target,result.returncode,flush=True)
(root/'focused-test-results.json').write_text(json.dumps(results,indent=2)+'\n')
