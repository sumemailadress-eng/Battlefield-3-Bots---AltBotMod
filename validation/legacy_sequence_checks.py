"""Reproduce the corrected failures using preserved Test 9 native functions."""
from pathlib import Path
import tempfile,subprocess
from test4_checks import function
from spawn_driver_regressions import SPAWN,MOTION,core,ROOT,SRC
old=(ROOT/'validation/reference-Test9-sequences.c.txt').read_text()
cases=[('squad-wait',SPAWN.replace(function(core,'cmd_spawn'),function(old,'cmd_spawn')).replace('H->teamBlueprint[i+1]=NULL','H->teamBlueprint[i+1]=&blueprint'),'spawned[0]==1'),
 ('driver-input',MOTION.replace(function(core,'bot_motion_tick'),function(old,'bot_motion_tick')).replace('static void fleet_takeover(int i)', 'static int air_pilot(int i){(void)i;return 0;}\nstatic void fleet_takeover(int i)'),'input[EIA_THROTTLE]>0')]
with tempfile.TemporaryDirectory(prefix='bf3-test9-reproduction-') as tmp:
 for name,body,expected in cases:
  f=Path(tmp)/(name+'.c');f.write_text(body);exe=f.with_suffix('')
  subprocess.run(['gcc','-std=c11','-g','-I',str(SRC),str(f),str(SRC/'enhanced.c'),str(SRC/'air.c'),'-lm','-o',str(exe)],check=True)
  r=subprocess.run([str(exe)],capture_output=True,text=True,cwd=tmp)
  assert r.returncode<0 and expected in r.stderr,(name,r.returncode,r.stderr)
  print('PASS: preserved Test 9 reproduces expected '+name+' failure; Test 10 sequence checks exercise the replacement')
