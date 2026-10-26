from pathlib import Path
import subprocess,os,json,concurrent.futures,tempfile,argparse,shutil,sys
parser=argparse.ArgumentParser();parser.add_argument('--pwsh',help='Path to PowerShell 7 for installer/collector checks');args=parser.parse_args()
root=Path(__file__).resolve().parents[1];val=root/'validation';env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
checks=[('offline-results','run_checks',False),('combat-mobility-results','combat_mobility_checks',True),('test4-results','test4_checks',True),('ground-job-results','ground_job_checks',True),('nav-queue-results','nav_queue_checks',True),('vehicle-approach-results','vehicle_approach_checks',True),('lag-recovery-results','lag_recovery_checks',True),('stability-navigation-results','stability_navigation_checks',True),('navigation-search-results','navigation_search_limits',True),('driver-departure-results','driver_departure_checks',True),('deployment-recipe-results','deployment_recipe_checks',True),('armor-traffic-results','armor_traffic_checks',True),('spawn-driver-results','spawn_driver_regressions',True),('hq-navigation-results','hq_navigation_checks',True),('combat-flow-results','combat_flow_checks',True)]
checks += [('rush-mode-results','rush_mode_checks',True),('mode-spawn-results','mode_spawn_checks',True),('rush-vehicle-results','rush_vehicle_checks',True),('obstacle-results','obstacle_checks',True),('map-layout-results','map_layout_checks',True),('message-safety-results','message_safety_checks',True),('ground-recovery-results','ground_recovery_checks',True),('vehicle-avoid-results','vehicle_avoid_checks',True),('fleet-spawn-results','fleet_spawn_checks',True),('transport-job-results','transport_job_checks',True),('air-team-capacity-results','air_team_capacity_checks',True)]
checks += [('test12-regression-results','test12_regressions',True),('combat-walk-fire-results','combat_walk_fire_checks',True),('test13-regression-results','test13_regressions',True),('test14-regression-results','test14_regressions',True),('test15-regression-results','test15_regressions',True),('test16-regression-results','test16_regressions',True),('caspian-v2-results','caspian_v2_regressions',True),('caspian-v3-results','caspian_v3_regressions',True),('survey-budget-results','survey_budget_checks',True),('altbotmod-v6-results','altbotmod_v6_checks',True),('altbotmod-v8-results','altbotmod_v8_checks',True),('altbotmod-v9-results','altbotmod_v9_checks',True),('altbotmod-v10-results','altbotmod_v10_checks',True),('altbotmod-v11-results','altbotmod_v11_checks',True),('altbotmod-v12-results','altbotmod_v12_checks',True),('altbotmod-v13-results','altbotmod_v13_checks',True),('altbotmod-v14-results','altbotmod_v14_checks',True),('altbotmod-v15-results','altbotmod_v15_checks',True),('altbotmod-v15-mode-results','altbotmod_v15_mode_checks',True),('altbotmod-v16-results','altbotmod_v16_checks',True),('altbotmod-v17-results','altbotmod_v17_checks',True),('altbotmod-v18-results','altbotmod_v18_checks',True),('altbotmod-v20-results','altbotmod_v20_checks',True),('altbotmod-v21-results','altbotmod_v21_checks',True),('altbotmod-v22-results','altbotmod_v22_checks',True),('altbotmod-v23-results','altbotmod_v23_checks',True),('altbotmod-v24-results','altbotmod_v24_checks',True),('altbotmod-v25-results','altbotmod_v25_checks',True),('altbotmod-v26-results','altbotmod_v26_checks',True),('altbotmod-v27-results','altbotmod_v27_checks',True),('altbotmod-v28-results','altbotmod_v28_checks',True),('altbotmod-v29-results','altbotmod_v29_checks',True),('altbotmod-v31-results','altbotmod_v31_checks',True),('altbotmod-v32-results','altbotmod_v32_checks',True),('altbotmod-v33-results','altbotmod_v33_checks',True),('altbotmod-v34-results','altbotmod_v34_checks',True),('altbotmod-route-coverage-results','altbotmod_route_coverage_checks',True)]
jobs=[(name,[sys.executable,str(val/(script+'.py'))]+(['--sanitize'] if san else [])) for name,script,san in checks]
def run(job):
 name,cmd=job;r=subprocess.run(cmd,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (val/(name+'.txt')).write_text(r.stdout)
 print(('PASS' if r.returncode==0 else 'FAIL'),name,flush=True)
 return name,{'exit_code':r.returncode,'checks':[s for s in r.stdout.splitlines() if s.startswith('PASS:')], 'output':name+'.txt'}
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:results=dict(pool.map(run,jobs))
for name,source in [('air-native-sanitized-results','air_native_tests.c'),('tactics-policy-sanitized-results','tactics_policy_tests.c'),('director-policy-results','director_tests.c')]:
 with tempfile.TemporaryDirectory(prefix='bf3-test12-native-') as tmp:
  exe=str(Path(tmp)/'checks');cmd=['gcc','-std=c11','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(root/'files/BotMod/src'),str(val/source),*[str(root/'files/BotMod/src'/f) for f in ['enhanced.c','air.c','modes.c']],'-lm','-o',exe]
  comp=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
  if comp.returncode: (val/(name+'.txt')).write_text(comp.stdout);results[name]={'exit_code':comp.returncode,'output':name+'.txt'};print('FAIL',name,flush=True)
  else: n,r=run((name,[exe]));results[n]=r
pwsh=args.pwsh or shutil.which('pwsh')
if pwsh:
 # (AltBotMod V9: the Caspian-era installer gates are superseded by altbotmod_installer_tests.ps1, which runs against a
 # built package: pwsh -File validation/altbotmod_installer_tests.ps1 -PackageDir <package>)
 sc=root/'files/ServerControl'
 n,r=run(('servercontrol-results',[pwsh,'-NoProfile','-File',str(val/'servercontrol/servercontrol_tests.ps1'),'-Core',str(sc/'ServerControl-Core.ps1'),'-Gui',str(sc/'AltBotMod-ServerControl.ps1')]));results[n]=r
 n,r=run(('livemap-results',[pwsh,'-NoProfile','-File',str(val/'servercontrol/livemap_tests.ps1'),'-Dir',str(sc)]));results[n]=r
 # AltBotMod V21.1: LAN servers against two fake BF-Portable backends
 n,r=run(('servercontrol-lan-results',[pwsh,'-NoProfile','-File',str(val/'servercontrol/lan_tests.ps1'),'-Dir',str(sc),'-Python',sys.executable,'-Mock',str(val/'servercontrol/mock_backend.py')]));results[n]=r
 # AltBotMod V33: AltBotMod Command (the web launcher) started for real against a mock RCON server
 n,r=run(('command-results',[pwsh,'-NoProfile','-File',str(val/'servercontrol/command_tests.ps1'),'-Dir',str(sc),'-Python',sys.executable,'-Pwsh',pwsh]));results[n]=r
 # AltBotMod V21: the Learning page's data side, against files the real learning store writes (learnfile_tool)
 with tempfile.TemporaryDirectory(prefix='bf3-v21-sc-') as tmp:
  tool=str(Path(tmp)/'learnfile_tool');subprocess.run(['gcc','-std=gnu11','-O1','-w','-I',str(root/'files/BotMod/src'),str(val/'learnfile_tool.c'),str(root/'files/BotMod/src/learn.c'),'-lm','-o',tool],check=True)
  n,r=run(('servercontrol-learning-results',[pwsh,'-NoProfile','-File',str(val/'servercontrol/learning_tests.ps1'),'-Dir',str(sc),'-Tool',tool]));results[n]=r
else: print('PowerShell gates not run: provide --pwsh PATH',flush=True)
(val/'altbotmod-v16-release.json').write_text(json.dumps({'release':'AltBotMod V34 - Squad Up','checks':results,'live_bf3_tested':False,'sanitizers':'ASan/UBSan; leak checking disabled for the restricted process environment','powershell_gates_run':bool(pwsh),'native_rush_scoring_bridge':'UNBOUND'},indent=2)+'\n')
raise SystemExit(any(r['exit_code'] for r in results.values()))
