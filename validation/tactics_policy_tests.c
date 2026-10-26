// Portable tactics boundary tests; missile observation bridge remains unbound.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../files/BotMod/src/enhanced.h"
int main(void){
 BMConfig config;bm_defaults(&config);
 const char *switches[]={"DriveInStages","TransportLandings","LearnAircraftDeployment","WeaponHandling","ArmorTactics","ArmorInfantrySupport","TrafficManagement","VehicleDiagnostics"};
 for(unsigned i=0;i<sizeof switches/sizeof switches[0];i++){
  assert(bm_set(&config,switches[i],"0")==1 && bm_set(&config,switches[i],"1")==1);
  BMConfig before=config;assert(bm_set(&config,switches[i],"0.5")==-1 && !memcmp(&before,&config,sizeof config));
 }
 assert(bm_set(&config,"ArmorRepairHealthFraction","0.35")==1);
 assert(bm_set(&config,"ArmorTacticMeters","70")==1 && bm_set(&config,"ConvoySpacingMeters","14")==1);
 const char *weapons[]={"Weapons/M16A3/U_M16A3","M16A4","M4A1","M249","MK11","SV98","P90","870"};
 const enum BMWeaponFamily family[]={BM_W_RIFLE,BM_W_BURST,BM_W_CARBINE,BM_W_LMG,BM_W_DMR,BM_W_BOLT,BM_W_PDW,BM_W_SHOTGUN};
 for(int i=0;i<8;i++){
  const BMWeaponProfile*w=bm_weapon_profile(weapons[i],0,0);assert(w->family==family[i]);
  for(int distance=1;distance<=600;distance++){
   assert(bm_weapon_range(&config,w)>0 && bm_weapon_range(&config,w)<=w->maxRange);
   assert(bm_weapon_burst(w,distance)>=1 && bm_weapon_burst(w,distance)<=w->burst);
   float tolerance=bm_weapon_tolerance(&config,w,distance);assert(tolerance>0 && isfinite(tolerance));
   assert(bm_weapon_stable(tolerance,0,tolerance) && !bm_weapon_stable(tolerance*3,0,tolerance));
  }
 }
 assert(bm_weapon_profile("unknown",0,1)->family==BM_W_PISTOL);
 assert(!bm_weapon_stable(NAN,0,.1f) && !bm_weapon_stable(0,INFINITY,.1f));
 const BMWeaponProfile*shot=bm_weapon_profile("870",0,0),*bolt=bm_weapon_profile("SV98",1,0);
 assert(bm_weapon_range(&config,shot)<=55 && bm_weapon_burst(bolt,20)==1 && bm_weapon_pause(bolt,20)==30);
 BMTrafficContact contact={20,0,0,1,5,12,1.2f,1.2f,0,1,0,0};
 BMTrafficDecision decision=bm_traffic_contact(&contact,14,34);assert(decision.following && decision.limit<34);
 contact.ahead=5;decision=bm_traffic_contact(&contact,14,34);assert(decision.yield && !decision.limit);
 contact.height=10;decision=bm_traffic_contact(&contact,14,34);assert(!decision.yield && decision.limit==34);
 contact.height=0;contact.ahead=20;contact.alignment=-1;contact.human=1;
 assert(bm_traffic_contact(&contact,14,34).yield);contact.human=0;contact.ownId=0;contact.otherId=1;
 assert(!bm_traffic_contact(&contact,14,34).yield);contact.ownId=2;assert(bm_traffic_contact(&contact,14,34).yield);
 contact.lateral=20;assert(!bm_traffic_contact(&contact,14,34).yield);contact.lateral=NAN;assert(bm_traffic_contact(&contact,14,34).yield);
 BMLandingState landing={0};BMLandingObservation pad={100,30,10,0,1,0,1,0};
 assert(!bm_transport_landing(&landing,&pad,1.0f/30).unload && landing.phase==BM_LAND_APPROACH);
 pad.distance=0;pad.horizontalSpeed=0;pad.clearance=2;
 for(int i=0;i<40;i++)bm_transport_landing(&landing,&pad,1.0f/30);
 assert(landing.phase==BM_LAND_UNLOAD);assert(bm_transport_landing(&landing,&pad,1.0f/30).unload);
 pad.threatened=1;BMLandingDecision land=bm_transport_landing(&landing,&pad,1.0f/30);assert(land.abort && !land.unload && landing.phase==BM_LAND_ASCEND);
 pad.clearance=40;assert(bm_transport_landing(&landing,&pad,1.0f/30).finished && landing.phase==BM_LAND_NONE);
 landing=(BMLandingState){BM_LAND_DESCEND,0,0};pad=(BMLandingObservation){0,2,0,0,1,0,0,1};
 for(int i=0;i<40;i++)bm_transport_landing(&landing,&pad,1.0f/30);
 assert(bm_transport_landing(&landing,&pad,1.0f/30).board);
 pad.cargo=1;for(int i=0;i<100;i++)bm_transport_landing(&landing,&pad,1.0f/30);assert(landing.phase==BM_LAND_ASCEND);
 landing=(BMLandingState){BM_LAND_APPROACH,51,0};pad.padValid=0;assert(bm_transport_landing(&landing,&pad,1.0f/30).abort);
 BMMissileState missile={0};int target;BMMissileObservation observation={&target,600,0,0,1,1,1,1,1,0,0};
 for(int i=0;i<1000;i++)assert(!bm_air_missile(&missile,&observation,1.0f/30).fire); // aim is never lock
 observation.nativeLock=1;assert(bm_air_missile(&missile,&observation,1.0f/30).fire);
 assert(!bm_air_missile(&missile,&observation,1.0f/30).fire);
 observation.incomingConfirmed=1;assert(bm_air_missile(&missile,&observation,1.0f/30).countermeasure);
 assert(!bm_air_missile(&missile,&observation,1.0f/30).countermeasure);
 observation.hostile=0;for(int i=0;i<1000;i++)assert(!bm_air_missile(&missile,&observation,1.0f/30).fire);
 puts("PASS: eight actual weapon families over 4,800 range/cycle/alignment cases; convoy and opposing-driver priority; stable landing/pickup/threat abort; missile timing never treats aim as native lock");
 return 0;
}
