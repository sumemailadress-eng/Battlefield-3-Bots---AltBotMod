#include "modes.h"
#include <string.h>
#include <ctype.h>
#include <math.h>
static int eq(const char *a,const char *b) { if(!a||!b)return 0; while(*a&&*b)if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return 0; return *a==*b; }
const BMMap bm_maps[29]={
 {"MP_001","bazaar","Grand Bazaar",0,0},{"MP_003","teheran","Teheran Highway",0,0},
 {"MP_007","caspian","Caspian Border",0,0},{"MP_011","seine","Seine Crossing",0,0},
 {"MP_012","firestorm","Operation Firestorm",0,0},{"MP_013","damavand","Damavand Peak",0,0},
 {"MP_017","canals","Noshahr Canals",0,0},{"MP_018","kharg","Kharg Island",0,0},{"MP_Subway","metro","Operation Metro",0,0},
 {"XP1_001","karkand","Strike at Karkand",0,1},{"XP1_002","oman","Gulf of Oman",0,0},
 {"XP1_003","sharqi","Sharqi Peninsula",0,1},{"XP1_004","wake","Wake Island",0,1},
 {"XP2_Palace","donya","Donya Fortress",1,0},{"XP2_Office","operation925","Operation 925",1,0},
 {"XP2_Factory","scrapmetal","Scrapmetal",1,0},{"XP2_Skybar","ziba","Ziba Tower",1,0},
 {"XP3_Alborz","alborz","Alborz Mountains",0,0},{"XP3_Shield","shield","Armored Shield",0,0},
 {"XP3_Desert","bandar","Bandar Desert",0,0},{"XP3_Valley","valley","Death Valley",0,0},
 {"XP4_Parl","azadi","Azadi Palace",0,0},{"XP4_Quake","epicenter","Epicenter",0,0},
 {"XP4_FD","markaz","Markaz Monolith",0,0},{"XP4_Rubble","talah","Talah Market",0,1},
 {"XP5_001","riverside","Operation Riverside",0,0},{"XP5_002","nebandan","Nebandan Flats",0,0},
 {"XP5_003","kiasar","Kiasar Railroad",0,0},{"XP5_004","sabalan","Sabalan Pipeline",0,0}
};
enum BMMode bm_mode(const char *s) {
 // AltBotMod V15: every mode the server runs, sorted as Fun-Bots' SetGameMode sorts them. Squad Rush is Rush with one
 // M-COM per sector; Gun Master is team play like TDM (the game hands out its weapons); Scavenger, Tank Superiority,
 // Air Superiority and Capture the Flag are played on the level's own flags / HQs like Conquest (a mode whose level
 // turns out to have no flags falls back to team play - see mode_no_flags_fallback in core.c).
 if(eq(s,"rush")||eq(s,"RushLarge0")||eq(s,"sqrush")||eq(s,"sr")||eq(s,"SquadRush0"))return BM_MODE_RUSH;
 if(eq(s,"tdm")||eq(s,"TeamDeathMatch0")||eq(s,"TeamDeathMatchC0")||eq(s,"gm")||eq(s,"gunmaster")||eq(s,"GunMaster0"))return BM_MODE_TDM;
 if(eq(s,"sqdm")||eq(s,"SquadDeathMatch0"))return BM_MODE_SQDM;
 const char *cq[]={"cq","cqsmall","cq32","cqs","cqlarge","cq64","cql","cqa","cqas","cqas1","dom","Domination0",
   "ConquestLarge0","ConquestSmall0","ConquestAssaultLarge0","ConquestAssaultSmall0","ConquestAssaultSmall1",
   "scav","scavenger","Scavenger0","tank","ts","TankSuperiority0","air","as","AirSuperiority0","ctf","CaptureTheFlag0","BFLAG","BFLAG0"};
 for(unsigned k=0;k<sizeof cq/sizeof cq[0];k++)if(eq(s,cq[k]))return BM_MODE_CQ;
 return BM_MODE_UNKNOWN;
}
int bm_mode_teams(enum BMMode mode) { return mode==BM_MODE_SQDM?4:2; }
int bm_mode_squad_rush(const char *s) { return eq(s,"SquadRush0")||eq(s,"sqrush")||eq(s,"sr"); }
int bm_mode_gun_master(const char *s) { return eq(s,"GunMaster0")||eq(s,"gm")||eq(s,"gunmaster"); }
const BMMap *bm_map(const char *s) {
 if(eq(s,"tehran"))s="teheran";
 if(eq(s,"noshahr"))s="canals";
 for(int k=0;k<29;k++)if(eq(s,bm_maps[k].code)||eq(s,bm_maps[k].alias)||eq(s,bm_maps[k].name))return &bm_maps[k];
 return 0;
}
const char *bm_map_mode(const BMMap *m,const char *s) {
 if(!m)return 0;
 if(!s||!*s)s="cq";
 if(eq(s,"cq"))return m->closeQuarters?"Domination0":m->assault?"ConquestAssaultLarge0":"ConquestLarge0";
 if(eq(s,"cqs")||eq(s,"cqsmall")||eq(s,"cq32"))return m->closeQuarters?"Domination0":m->assault?"ConquestAssaultSmall0":"ConquestSmall0";
 if(eq(s,"cql")||eq(s,"cqlarge")||eq(s,"cq64"))s=m->assault?"ConquestAssaultLarge0":"ConquestLarge0";
 if(eq(s,"cqa"))s="ConquestAssaultLarge0";
 if(eq(s,"cqas"))s="ConquestAssaultSmall0";
 if(eq(s,"cqas1"))s="ConquestAssaultSmall1";
 if(eq(s,"rush"))s="RushLarge0";
 if(eq(s,"tdm"))s=m->closeQuarters?"TeamDeathMatchC0":"TeamDeathMatch0";
 if(eq(s,"sqdm"))s="SquadDeathMatch0";
 if(eq(s,"sqrush")||eq(s,"sr"))s="SquadRush0";
 if(eq(s,"gm")||eq(s,"gunmaster"))s="GunMaster0";
 if(eq(s,"dom"))s="Domination0";
 if(eq(s,"scav")||eq(s,"scavenger"))s="Scavenger0";
 if(eq(s,"tank")||eq(s,"ts"))s="TankSuperiority0";
 if(eq(s,"air")||eq(s,"as"))s="AirSuperiority0";
 if(eq(s,"ctf"))s="CaptureTheFlag0";
 // AltBotMod V15: the extra modes, on the levels whose server data carries them (read from each level's sublevels):
 // Squad Rush on every non-Close-Quarters map, Gun Master on Close Quarters and Aftermath, Scavenger on Aftermath,
 // Tank Superiority on Armored Kill, Air Superiority and Capture the Flag on End Game.
 if(eq(s,"SquadRush0"))return m->closeQuarters?0:"SquadRush0";
 if(eq(s,"GunMaster0"))return m->closeQuarters||!strncmp(m->code,"XP4_",4)?"GunMaster0":0;
 if(eq(s,"Scavenger0"))return !strncmp(m->code,"XP4_",4)?"Scavenger0":0;
 if(eq(s,"TankSuperiority0"))return !strncmp(m->code,"XP3_",4)?"TankSuperiority0":0;
 if(eq(s,"AirSuperiority0"))return !strncmp(m->code,"XP5_",4)?"AirSuperiority0":0;
 if(eq(s,"CaptureTheFlag0"))return !strncmp(m->code,"XP5_",4)?"CaptureTheFlag0":0;
 // Canonical names retain the requested size. Availability comes from the
 // official map/mode table, not a generic Conquest prefix.
 if(eq(s,"RushLarge0"))return m->closeQuarters?0:"RushLarge0";
 if(eq(s,"SquadDeathMatch0"))return "SquadDeathMatch0";
 if(eq(s,"TeamDeathMatchC0"))return "TeamDeathMatchC0";
 if(eq(s,"TeamDeathMatch0"))return m->closeQuarters?0:"TeamDeathMatch0";
 if(eq(s,"Domination0"))return m->closeQuarters?"Domination0":0;
 if(m->closeQuarters)return 0;
 if(eq(s,"ConquestLarge0"))return m->assault?0:"ConquestLarge0";
 if(eq(s,"ConquestSmall0"))return m->assault?0:"ConquestSmall0";
 if(eq(s,"ConquestAssaultLarge0"))return m->assault?"ConquestAssaultLarge0":0;
 if(eq(s,"ConquestAssaultSmall0"))return m->assault||eq(m->code,"XP1_002")?"ConquestAssaultSmall0":0;
 if(eq(s,"ConquestAssaultSmall1"))return !strncmp(m->code,"XP1_",4)&&m->assault?"ConquestAssaultSmall1":0;
 return 0;
}
int bm_legacy_cache_allowed(const char *level,const char *mode) {
 const BMMap *map=bm_map(level);
 return map && eq(mode,bm_map_mode(map,"cq"));
}
int bm_roster_team(const char *s,enum BMMode mode,int fallback) {
 if(s){for(int k=1;k<=4;k++){char label[5]={' ','S','Q',(char)('0'+k),0}; if(strstr(s,label))return mode==BM_MODE_SQDM?k:(k-1)%2+1;}
 if(strstr(s," US"))return 1;
 if(strstr(s," RU"))return 2;}
 return fallback;
}
void bm_roster_targets(int teams,int capacity,int reserve,int wanted,const int humans[5],int targets[5]) {
 memset(targets,0,5*sizeof *targets); if(teams<1||teams>4)return;
 for(int k=0;k<wanted;k++){int best=0;for(int t=1;t<=teams;t++)if(humans[t]+targets[t]<capacity-reserve&&(!best||humans[t]+targets[t]<humans[best]+targets[best]))best=t; if(!best)break;targets[best]++;}
}
float bm_dispatch_score(int role,const int counts[BM_DISPATCH_COUNT],float distance) {
 if(role<0||role>=BM_DISPATCH_COUNT||!isfinite(distance)||distance<0)return 1e30f;
 return counts[role]*100000.0f+role*5000.0f+fminf(distance,4000);
}
