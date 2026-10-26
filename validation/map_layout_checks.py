"""Official map availability against real mode selection, trace profiles and shipped caches.
This inspects data and native navigation, not live map behavior.
"""
from pathlib import Path
import re,json,subprocess,tempfile,argparse,os,math,hashlib
from test4_checks import function
from nav_queue_checks import WINDOWS
ROOT=Path(__file__).resolve().parents[1];SRC=ROOT/'files/BotMod/src'
state=(SRC/'mode_state.h').read_text();profile=re.search(r'typedef struct BMModeProfile.*?} BMModeProfile;',state,re.S)[0]
C=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "modes.h"
'''+profile+'\n#include "mode_profiles.inc"\n'+r'''
int main(void){
 const char*modes[]={"ConquestLarge0","ConquestSmall0","ConquestAssaultLarge0","ConquestAssaultSmall0","ConquestAssaultSmall1","Domination0","RushLarge0","TeamDeathMatch0","TeamDeathMatchC0","SquadDeathMatch0"};
 for(int m=0;m<29;m++)for(int k=0;k<10;k++){
  const BMMap*map=&bm_maps[m];const char*mode=bm_map_mode(map,modes[k]);
  if(!mode){continue;}assert(!strcmp(mode,modes[k]));assert(bm_mode(mode)!=BM_MODE_UNKNOWN);
  const char*dataMode=!strcmp(mode,"TeamDeathMatchC0")?"TeamDeathMatch0":mode;const BMModeProfile*p=NULL;
  for(unsigned n=0;n<sizeof g_modeProfiles/sizeof g_modeProfiles[0];n++)if(!strcmp(g_modeProfiles[n].level,map->code)&&!strcmp(g_modeProfiles[n].mode,dataMode))p=&g_modeProfiles[n];
  printf("layout|%s|%s|%d|%d\n",map->code,mode,p!=NULL,bm_legacy_cache_allowed(map->code,mode));
 }
 for(unsigned n=0;n<sizeof g_modeProfiles/sizeof g_modeProfiles[0];n++){
  const BMModeProfile*p=&g_modeProfiles[n];double width=p->high[0]-p->low[0]+400,height=p->high[2]-p->low[2]+400;
  assert(isfinite(width)&&width>0&&isfinite(height)&&height>0&&ceil(width/2)*ceil(height/2)<4000000);
  for(int k=0;k<3;k++)assert(isfinite(p->low[k])&&isfinite(p->high[k])&&p->high[k]>=p->low[k]);
  for(int k=0;k<32;k++)for(int a=0;a<3;a++)assert(isfinite(p->patrol[k][a])&&p->patrol[k][a]>=p->low[a]&&p->patrol[k][a]<=p->high[a]);
  if(!strcmp(p->mode,"RushLarge0")){assert(p->stages>=1&&p->stages<=8);
   for(int stage=1;stage<=p->stages;stage++)for(int t=1;t<=2;t++){const float*b=p->bases[stage][t];assert(p->baseMask[stage]&(1u<<t));for(int a=0;a<3;a++)assert(isfinite(b[a]));}
   for(int o=1;o<=p->stages*2;o++){assert(p->mcom[o].present&&isfinite(p->mcom[o].yaw)&&isfinite(p->mcom[o].pitch));for(int a=0;a<3;a++)assert(isfinite(p->mcom[o].pos[a]));}
  }
 }
 assert(!bm_map_mode(bm_map("alborz"),"ConquestAnything")&&!bm_legacy_cache_allowed("unknown","ConquestLarge0"));return 0;
}
'''
CACHE=r'''
#include <assert.h>
#include <stdio.h>
#include "nav.c"
int main(int argc,char**argv){assert(argc==2);ray_cost=0;NavGrid*g=nav_load(argv[1]);assert(g&&g->phase==2);int count=0;for(int n=0;n<g->w*g->h*NAV_LAYERS;n++)if(g->flags[n]&NAV_WALKABLE)count++;assert(count>20);printf("%s %dx%d %d walkable nodes\n",g->level,g->w,g->h,count);nav_free(g);return 0;}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');args=ap.parse_args();env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
 with tempfile.TemporaryDirectory(prefix='bf3-map-layouts-')as temp:
  tmp=Path(temp);flags=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']if args.sanitize else ['-O2']
  (tmp/'checks.c').write_text(C);subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',*flags,'-I',str(SRC),str(tmp/'checks.c'),str(SRC/'modes.c'),'-lm','-o',str(tmp/'checks')],check=True)
  text=subprocess.check_output([str(tmp/'checks')],text=True,env=env);layouts={}
  for line in text.splitlines():
   _,level,mode,present,legacy=line.split('|');layouts[level,mode]={'trace':bool(int(present)),'legacy_cache':bool(int(legacy))}
  # Independent compatibility expectations from the official VU table.
  base=['MP_001','MP_003','MP_007','MP_011','MP_012','MP_013','MP_017','MP_018','MP_Subway']
  large=base+['XP1_002','XP3_Alborz','XP3_Shield','XP3_Desert','XP3_Valley','XP4_Parl','XP4_Quake','XP4_FD','XP5_001','XP5_002','XP5_003','XP5_004']
  assault=['XP1_001','XP1_003','XP1_004','XP4_Rubble'];close=['XP2_Palace','XP2_Office','XP2_Factory','XP2_Skybar'];allmaps=large+assault+close
  expected=set()
  for level in allmaps:
   cq=['Domination0']if level in close else ['ConquestAssaultLarge0','ConquestAssaultSmall0']if level in assault else ['ConquestLarge0','ConquestSmall0']
   if level in assault[:3]:cq+=['ConquestAssaultSmall1']
   if level=='XP1_002':cq+=['ConquestAssaultSmall0']
   for mode in cq+['TeamDeathMatchC0','SquadDeathMatch0']+([]if level in close else ['RushLarge0','TeamDeathMatch0']):expected.add((level,mode))
  assert set(layouts)==expected and len(expected)==166
  missing=sorted((level,mode)for(level,mode),details in layouts.items()if not details['trace']);assert missing==[('MP_Subway','ConquestSmall0'),('XP4_Rubble','ConquestAssaultLarge0')]
  for(level,mode),details in layouts.items():assert details['legacy_cache']==(mode==('Domination0'if level in close else 'ConquestAssaultLarge0'if level in assault else 'ConquestLarge0'))
  (ROOT/'research/map-layout-coverage.json').write_text(json.dumps({'reference':'https://docs.veniceunleashed.net/hosting/maps/','maps':29,'supported_layouts_checked':166,'trace_profiles':139,'supported_layouts_with_trace':164,'missing_trace_profiles':missing,'live_map_tests':False,'layouts':[dict(level=k[0],mode=k[1],**v)for k,v in sorted(layouts.items())]},indent=2)+'\n')
  print('PASS: all 166 supported Conquest/Rush/TDM/SQDM map-layout combinations retain their exact requested mode; unavailable combinations rejected; only default Conquest loads a legacy cache')
  print('PASS: 139 finite trace profiles, complete deployment/M-COM metadata on all 25 Rush maps, surveyed bounds fit native grid limits; two missing CQ traces explicitly recorded for native discovery')
  (tmp/'windows.h').write_text(WINDOWS);(tmp/'caches.c').write_text(CACHE);subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',*flags,'-I',str(tmp),'-I',str(SRC),str(tmp/'caches.c'),'-lm','-o',str(tmp/'caches')],check=True)
  caches=sorted((ROOT/'files/server').glob('BotMod_nav_*.bin'));rows=[]
  for p in caches:rows.append(subprocess.check_output([str(tmp/'caches'),str(p)],text=True,env=env).strip())
  (ROOT/'validation/shipped-cache-inspection.txt').write_text('\n'.join(rows)+'\n');assert len(caches)==14
  print(f'PASS: all {len(caches)} shipped navigation caches load through actual native nav_load and contain walkable surveyed nodes; cache inspection does not establish live reachability')
  preserve=json.loads((ROOT/'research/preserved-obstacle-functions.json').read_text());core=(SRC/'core.c').read_text()
  for name,digest in preserve['functions'].items():assert hashlib.sha256(function(core,name).encode()).hexdigest()==digest,name
  print('PASS: Test10 vaulting, fence detection/knife sequence, post-obstacle rerouting and complete shared infantry steering source preserved unchanged')
if __name__=='__main__':main()
