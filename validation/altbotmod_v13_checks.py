"""AltBotMod V13 checks: the live map.
1. livemap.c (portable): names made safe for a tab-separated line, the "no progress" watch, the events file cap and
   roll-over, the BMP writer, the map picture's orientation / bounds, drawing in strips = drawing at once, and the
   pictures of three shipped navigation grids (timed per strip of rows).
2. livemap_native.inc with a mock server (players, bots on foot / driving / sniping, a person, flags, a nav grid, RCON
   kills): the live picture, round / kill / stuck / move lines, positions of victims taken from where they were last
   alive, switching off (an "off" picture, the kill queue drained) and on again. Its output is kept as the fixture
   for Server Control's Live map tests (validation/servercontrol/livemap_tests.ps1).
3. The hooks in core.c / chat.c / ground_progress.inc and the four settings."""
from pathlib import Path
import argparse, shutil, subprocess, tempfile, re

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
SERVER = ROOT / 'files/server'
FIXTURE = ROOT / 'validation/servercontrol/livemap_fixture'

WINDOWS = r'''
#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
typedef struct {long long QuadPart;} LARGE_INTEGER;
#define HEAP_ZERO_MEMORY 8
static void *GetProcessHeap(void){return (void*)1;}
static void *HeapAlloc(void *h,unsigned f,size_t n){(void)h;return f&8?calloc(1,n):malloc(n);}
static int HeapFree(void *h,unsigned f,void *p){(void)h;(void)f;free(p);return 1;}
static long long now_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000LL+t.tv_nsec/1000;}
static unsigned GetTickCount(void){return (unsigned)(now_us()/1000);}
static int QueryPerformanceFrequency(LARGE_INTEGER *p){p->QuadPart=1000000;return 1;}
static int QueryPerformanceCounter(LARGE_INTEGER *p){p->QuadPart=now_us();return 1;}
'''

PORTABLE = r'''
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "nav.h"
#include "livemap.h"
static long long us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000LL+t.tv_nsec/1000;}
static long fsize(const char *p){FILE *f=fopen(p,"rb");if(!f)return -1;fseek(f,0,SEEK_END);long n=ftell(f);fclose(f);return n;}
int main(int argc,char **argv){
 char b[64];
 lm_clean(b,sizeof b,"Bot\tOne\r\nX");assert(!strcmp(b,"Bot_One__X"));
 lm_clean(b,sizeof b,"");assert(!strcmp(b,"-"));lm_clean(b,sizeof b,NULL);assert(!strcmp(b,"-"));
 lm_clean(b,5,"abcdefgh");assert(!strcmp(b,"abcd"));
 puts("PASS: names and weapons are made safe for a tab-separated line (tabs / line breaks -> _, empty -> -, cut to size)");
 // no progress: far goal, not moving for the given frames -> once; moving on clears it; no far goal -> never
 LMProgress p={0};float at[3]={10,0,10};
 assert(!lm_progress(&p,at,50,100,900));assert(!lm_progress(&p,at,50,999,900));assert(lm_progress(&p,at,50,1000,900));
 assert(!lm_progress(&p,at,50,2000,900));
 float moved[3]={16,0,10};assert(!lm_progress(&p,moved,50,2001,900));assert(lm_progress(&p,moved,50,2901,900));
 LMProgress q={0};for(unsigned f=0;f<5000;f+=15)assert(!lm_progress(&q,at,10,f,900));   // goal 10 m away: holding there on purpose
 LMProgress r={0};int hits=0;for(unsigned f=0;f<5000;f+=15){float wob[3]={10+(f%60)/20.0f,0,10};hits+=lm_progress(&r,wob,60,f,900);}assert(hits==1);   // jittering on the spot (pressed against a wall): stuck, once
 assert(!lm_progress(&r,NULL,60,9000,900));
 puts("PASS: no-progress watch - reported once after 30 s without 4 m of movement towards a goal over 20 m away; also when jittering on the spot; not for holding near a goal, not while dead, again after moving on");
 // events file: cap and roll-over to <name>.old.txt
 char path[512],old[512];snprintf(path,sizeof path,"%s/events_1.txt",argv[1]);snprintf(old,sizeof old,"%s/events_1.old.txt",argv[1]);
 LMEvents e={0};assert(lm_events_open(&e,path,1000));
 char line[100];memset(line,'x',98);line[98]=0;   // 100 bytes with CRLF
 for(int k=0;k<10;k++)assert(lm_events_line(&e,line));
 lm_events_flush(&e);assert(fsize(path)==1000);assert(fsize(old)<0);
 assert(lm_events_line(&e,line));lm_events_flush(&e);assert(fsize(path)==100);assert(fsize(old)==1000);
 lm_events_close(&e);assert(lm_events_open(&e,path,1000));assert(e.size==100);assert(lm_events_line(&e,"kill\t1"));lm_events_close(&e);assert(fsize(path)==100+8);
 puts("PASS: events file - lines end in CRLF, past the size cap it rolls over to events_<port>.old.txt (one kept) and starts again; reopening appends");
 // picture orientation: one walkable cell at the north-east corner, one at the south-west
 NavGrid *g=nav_create("TEST",0,0,100,60,2);assert(g&&g->w==50&&g->h==30);
 int ne=((g->h-1)*g->w+(g->w-1))*NAV_LAYERS, sw=0;
 g->flags[ne]=NAV_WALKABLE;g->height[ne]=50;g->flags[sw]=NAV_WALKABLE;g->height[sw]=0;g->phase=2;
 int w,h;float bd[4];unsigned char *px=lm_render_grid(g,1024,&w,&h,bd);assert(px&&w==100&&h==60);
 assert(fabsf(bd[0]-0)<0.01f&&fabsf(bd[1]-0)<0.01f&&fabsf(bd[2]-100)<0.01f&&fabsf(bd[3]-60)<0.01f);
 #define LIT(x,y) (px[((y)*w+(x))*3+1]>60)
 assert(LIT(w-1,0)&&LIT(w-2,1));assert(LIT(0,h-1)&&LIT(1,h-2));assert(!LIT(w/2,h/2));assert(!LIT(0,0));assert(!LIT(w-1,h-1));
 snprintf(path,sizeof path,"%s/t.bmp",argv[1]);assert(lm_write_bmp(path,px,w,h));
 FILE *f=fopen(path,"rb");unsigned char hd[54];assert(fread(hd,1,54,f)==54);
 assert(hd[0]=='B'&&hd[1]=='M'&&hd[18]==100&&hd[22]==60&&hd[28]==24);
 // first stored row is the bottom (south) row: its first pixel is the lit south-west cell (BGR)
 unsigned char first[3];assert(fread(first,1,3,f)==3);assert(first[0]==px[((h-1)*w)*3+2]&&first[1]==px[((h-1)*w)*3+1]);fclose(f);
 assert(fsize(path)==54+((100*3+3)&~3)*60);
 puts("PASS: map picture - north (+z) up, east (+x) right, bounds = the grid's outer cell edges; BMP is 24-bit bottom-up BGR with padded rows");
 // strips = whole
 LMRender rr;assert(lm_render_begin(&rr,g,1024));int calls=0;while(!lm_render_rows(&rr,7))calls++;
 assert(calls==8 && !memcmp(rr.rgb,px,(size_t)w*h*3));lm_render_free(&rr);free(px);nav_free(g);
 puts("PASS: drawing the picture a strip of rows at a time gives the same picture as drawing it at once");
 for(int k=2;k<argc;k++){
  NavGrid *m=nav_load(argv[k]);assert(m);
  LMRender R;long long t0=us();assert(lm_render_begin(&R,m,1024));long long tb=us()-t0,worst=0;int strips=0;
  for(;;){long long a=us();int done=lm_render_rows(&R,32);long long d=us()-a;if(d>worst)worst=d;strips++;if(done)break;}
  char out[600];snprintf(out,sizeof out,"%s/map_%s.bmp",argv[1],m->level);assert(lm_write_bmp(out,R.rgb,R.w,R.h));
  long dark=0;for(int i=0;i<R.w*R.h;i++)dark+=R.rgb[i*3]==30&&R.rgb[i*3+1]==32;
  printf("PASS: %s picture %dx%d (%.0f%% walkable), set-up %.1f ms, %d strips of 32 rows, slowest strip %.1f ms\n",m->level,R.w,R.h,100.0-100.0*dark/(R.w*R.h),tb/1000.0,strips,worst/1000.0);
  assert(R.w<=1024&&R.h<=1024&&(R.w==1024||R.h==1024||R.w==2*m->w||R.h==2*m->h));assert(dark<(long)R.w*R.h*0.9);
  lm_render_free(&R);nav_free(m);
 }
 return 0;
}
'''

NATIVE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include "enhanced.h"
#include "nav.h"
#include "chat.h"
#define MAX_PATH 260
typedef int LONG;
#define LM_SEP "/"
#define LM_PLAYER_LIST
#define MOVEFILE_REPLACE_EXISTING 1
static char g_out[400];
static int CreateDirectoryA(const char *p,void *s){(void)s;mkdir(p,0777);return 1;}
static int MoveFileExA(const char *a,const char *b,int f){(void)f;return rename(a,b)==0;}
#define MAX_BOTS 64
#define PATH_MAX_POINTS 64
#define DRIVE_MAX_PTS 128
#define OFF_PLAYER_ONLINE_NAME 0x28
#define OFF_PLAYER_TEAM 0x31c
#define OFF_PLAYER_SOLDIER 0x3c8
#define OFF_PLAYER_VEHICLE 0x3d8
#define OFF_SOLDIER_HEALTH 0x128
#define OFF_SOLDIER_YAW 0x218
#define OFF_SOLDIER_SPAWN_POSITION 0x4b0
static struct { int botCount; unsigned frame; void *bots[MAX_BOTS]; int gamePort; } host,*H=&host;
static BMConfig g_enhanced;
static char g_level[64],g_modeName[64];static LONG g_roundEpoch;static int g_ready;
static int level_ready(void){return g_ready;}
static void botmod_file(char *out,int n,const char *f){snprintf(out,n,"%s/%s",g_out,f);}
static int g_logs;static char g_lastLog[400];
static void bot_log(const char *fmt,...){va_list a;va_start(a,fmt);vsnprintf(g_lastLog,sizeof g_lastLog,fmt,a);va_end(a);g_logs++;}
#define NPL 4
static char *g_pl[NPL];static unsigned g_plCount;static char g_vehicle[16];
static unsigned lm_players(void ***arr){*arr=(void**)g_pl;return g_plCount;}
static char *find_player(const char *n){for(unsigned k=0;k<g_plCount;k++)if(!strcmp(g_pl[k]+OFF_PLAYER_ONLINE_NAME,n))return g_pl[k];return NULL;}
static int bot_index(void *pl){for(int i=0;i<H->botCount;i++)if(H->bots[i]==pl)return i;return -1;}
static int is_bot(void *pl){return bot_index(pl)>=0;}
static int player_team(char *pl){return *(int*)(pl+OFF_PLAYER_TEAM);}
static char *player_vehicle(char *pl){return *(char**)(pl+OFF_PLAYER_VEHICLE);}
static float g_vehPos[3]={500,30,600};
static int vehicle_ok(char *v){return v==g_vehicle;}
static float *vehicle_pos(char *v,int *t){(void)t;return v==g_vehicle?g_vehPos:NULL;}
static const char *vehicle_name(char *v){(void)v;return "M1A2\tAbrams";}
static float *human_pos(char *pl){char *s=*(char**)(pl+OFF_PLAYER_SOLDIER);return s?(float*)(s+OFF_SOLDIER_SPAWN_POSITION):NULL;}
enum { AIR_WALK=1, AIR_WAIT, AIR_FLY, AIR_RIDE };
typedef struct { int phase; float goal[3]; } AirNativeState; static AirNativeState g_air[MAX_BOTS];
typedef struct { int phase,n,next; float goal[3]; float pts[DRIVE_MAX_PTS*3]; } DriveState; static DriveState g_drive[MAX_BOTS];
typedef struct { int count,next; float pts[PATH_MAX_POINTS*3]; float goalX,goalY,goalZ; } BotPath; static BotPath g_paths[MAX_BOTS];
typedef struct { int mode; float targetYaw,x,z; } BotNav; static BotNav g_nav[MAX_BOTS]; static BotNav *bot_nav(int i){return &g_nav[i];}
enum { TASK_NONE, TASK_FOLLOW, TASK_DEFEND, TASK_SUPPLY, TASK_REPAIR, TASK_REVIVE, TASK_COVER, TASK_ROCKET, TASK_FIGHT };
static const char *kTaskNames[] = {"objectives","follow","defend","supply","repair","revive","cover","rocket","fight"};
typedef struct { int task; } BotBrain; static BotBrain g_brain[MAX_BOTS]; static BotBrain *bot_brain(int i){return &g_brain[i];}
static int g_sniper[MAX_BOTS]; static int sniper_holding(int i){return g_sniper[i];}
enum { BM_OBJECTIVE_NONE, BM_OBJECTIVE_ATTACK, BM_OBJECTIVE_GUARD, BM_OBJECTIVE_PATROL };
static int g_role[MAX_BOTS],g_target[MAX_BOTS]; static int director_target(int i,int *role){*role=g_role[i];return g_target[i];}
static int g_flagCount=3; static float g_flagPos[3][3]={{100,0,200},{300,0,-50},{-200,0,400}}; static int g_flagOwner[3]={1,2,0};
static float *flag_pos(int f){return g_flagPos[f];} static int flag_owner(int f){return g_flagOwner[f];}
static NavGrid *g_grid; static NavGrid **nav_slot(void){return &g_grid;}
static struct{char k[40],v[40],w[64];int hs;} g_q[16]; static int g_qn,g_qr;
int chat_poll_kill(char *k,int kl,char *v,int vl,char *w,int wl,int *hs){if(g_qr>=g_qn)return 0;snprintf(k,kl,"%s",g_q[g_qr].k);snprintf(v,vl,"%s",g_q[g_qr].v);snprintf(w,wl,"%s",g_q[g_qr].w);*hs=g_q[g_qr].hs;g_qr++;return 1;}
static void queue_kill(const char *k,const char *v,const char *w,int hs){snprintf(g_q[g_qn].k,40,"%s",k);snprintf(g_q[g_qn].v,40,"%s",v);snprintf(g_q[g_qn].w,64,"%s",w);g_q[g_qn].hs=hs;g_qn++;}
#include "livemap_native.inc"

static char *new_player(const char *name,int team,float x,float y,float z){
 char *pl=calloc(1,0x600),*s=calloc(1,0x500);snprintf(pl+OFF_PLAYER_ONLINE_NAME,40,"%s",name);*(int*)(pl+OFF_PLAYER_TEAM)=team;
 *(char**)(pl+OFF_PLAYER_SOLDIER)=s;*(float*)(s+OFF_SOLDIER_HEALTH)=100;float *p=(float*)(s+OFF_SOLDIER_SPAWN_POSITION);p[0]=x;p[1]=y;p[2]=z;
 g_pl[g_plCount++]=pl;return pl;}
static char *slurp(const char *name){char p[600];snprintf(p,sizeof p,"%s/LiveMap/%s",g_out,name);FILE *f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);char *b=calloc(1,n+1);if(fread(b,1,n,f)!=(size_t)n){fclose(f);free(b);return NULL;}fclose(f);return b;}
static int count(const char *t,const char *what){int n=0;for(const char *p=t;(p=strstr(p,what));p++)n++;return n;}
static void step(int frames){for(int k=0;k<frames;k++){H->frame++;livemap_tick();}}
int main(int argc,char **argv){
 (void)argc;snprintf(g_out,sizeof g_out,"%s",argv[1]);
 bm_defaults(&g_enhanced);assert(g_enhanced.liveMap==1&&g_enhanced.liveMapSeconds==1&&g_enhanced.liveMapTrailSeconds==5&&g_enhanced.liveMapKeepMB==64);
 H->gamePort=25300;snprintf(g_level,sizeof g_level,"MP_007");snprintf(g_modeName,sizeof g_modeName,"ConquestLarge0");g_ready=1;
 char *b0=new_player("AltBot01Assault",1,100,10,150),*b1=new_player("AltBot02Engineer",2,480,30,590),*b2=new_player("AltBot03Recon",2,-150,40,380),*me=new_player("TestPlayer",1,120,5,160);
 H->bots[0]=b0;H->bots[1]=b1;H->bots[2]=b2;H->botCount=3;
 // bot 0 attacks A on foot with a route; bot 1 drives a tank; bot 2 holds a sniper perch; TestPlayer is a person
 g_nav[0].mode=3;g_paths[0].count=5;g_paths[0].next=1;for(int k=0;k<5;k++){g_paths[0].pts[k*3]=100;g_paths[0].pts[k*3+2]=150+k*12;}
 g_paths[0].goalX=100;g_paths[0].goalZ=200;g_role[0]=BM_OBJECTIVE_ATTACK;g_target[0]=0;
 *(char**)(b1+OFF_PLAYER_VEHICLE)=g_vehicle;g_drive[1].phase=3;g_drive[1].n=40;g_drive[1].next=2;g_drive[1].goal[0]=300;g_drive[1].goal[2]=-50;
 for(int k=0;k<40;k++){g_drive[1].pts[k*3]=500-k*5;g_drive[1].pts[k*3+2]=600-k*16;}
 g_sniper[2]=1;g_target[1]=g_target[2]=-1;
 // a 1 km nav grid (rolling hills, a river) so the map picture is drawn
 g_grid=nav_create("MP_007",-400,-300,600,700,4);assert(g_grid);
 for(int cz=0;cz<g_grid->h;cz++)for(int cx=0;cx<g_grid->w;cx++){int n=(cz*g_grid->w+cx)*NAV_LAYERS;g_grid->flags[n]=NAV_WALKABLE|(cx>120&&cx<126?NAV_WATER:0);g_grid->height[n]=20*sinf(cx*0.05f)+15*cosf(cz*0.04f);}
 g_grid->phase=2;
 step(1);
 char *t=slurp("events_25300.txt");assert(t&&!strncmp(t,"round\t",6)&&strstr(t,"\tMP_007\tConquestLarge0\r\n"));free(t);
 t=slurp("live_25300.txt");assert(t);
 assert(!strncmp(t,"AltBotMod live map\t1\t" BM_ENHANCED_VERSION "\r\ntime\t",strlen("AltBotMod live map\t1\t" BM_ENHANCED_VERSION "\r\ntime\t")));
 assert(strstr(t,"flag\tA\t100.0\t200.0\t1\r\n")&&strstr(t,"flag\tB\t300.0\t-50.0\t2\r\n")&&strstr(t,"flag\tC\t-200.0\t400.0\t0\r\n"));
 assert(count(t,"\r\np\t")==4&&strstr(t,"\r\nend\r\n"));
 assert(strstr(t,"p\tAltBot01Assault\t1\t1\t1\t100.0\t10.0\t150.0\t0\t-\tattack A\t100.0\t200.0\t100\t100,162 100,174 100,186 100,198\r\n"));
 assert(strstr(t,"p\tAltBot02Engineer\t1\t2\t1\t500.0\t30.0\t600.0\t0\tM1A2_Abrams\tdriving\t300.0\t-50.0\t100\t490,568 "));
 assert(strstr(t,"p\tAltBot03Recon\t1\t2\t1\t-150.0\t40.0\t380.0\t0\t-\tsniper perch\t0.0\t0.0\t100\t-\r\n"));
 assert(strstr(t,"p\tTestPlayer\t0\t1\t1\t120.0\t5.0\t160.0\t0\t-\tplayer\t0.0\t0.0\t100\t-\r\n"));
 assert(!strstr(t,"picture\t"));free(t);
 puts("PASS: live picture - header, round, flags with owners, one line per player (bot / team / alive / position / vehicle with its tab cleaned / what it is doing / goal / health / the rest of its route, every few points), written whole (a .tmp renamed over it)");
 step(70);
 t=slurp("live_25300.txt");assert(strstr(t,"picture\tmap_MP_007.bmp\t-400.0\t-300.0\t600.0\t700.0\t500\t500\r\n"));free(t);
 char bmp[600];snprintf(bmp,sizeof bmp,"%s/LiveMap/map_MP_007.bmp",g_out);FILE *f=fopen(bmp,"rb");assert(f);fseek(f,0,SEEK_END);assert(ftell(f)==54+1500L*500);fclose(f);
 puts("PASS: map picture - drawn from the nav grid 16 rows a frame (500 rows: 32 frames), written as map_<level>.bmp once, its bounds in the live picture");
 // a kill: TestPlayer shoots the sniper; by the time RCON reports it the body has been thrown 600 m (a ragdoll) - the kill
 // goes where the sniper was last alive
 *(float*)(*(char**)(b2+OFF_PLAYER_SOLDIER)+OFF_SOLDIER_HEALTH)=0;float *sp=(float*)(*(char**)(b2+OFF_PLAYER_SOLDIER)+OFF_SOLDIER_SPAWN_POSITION);sp[0]=999;sp[2]=999;
 queue_kill("TestPlayer","AltBot03Recon","Weapons/M16A4/M16A4",1);
 queue_kill("AltBot02Engineer","TestPlayer","M1A2",0);           // the tank gets TestPlayer
 queue_kill("","AltBot01Assault","DamageArea",0);             // the world (out of bounds)
 queue_kill("Nobody","Ghost","M16A4",0);                       // never seen: no line
 step(20);   // (the events file is flushed with each live picture)
 t=slurp("events_25300.txt");
 assert(strstr(t,"\tMP_007\tTestPlayer\t0\t1\t120.0\t160.0\tAltBot03Recon\t1\t2\t-150.0\t380.0\tWeapons/M16A4/M16A4\t1\t"));
 assert(strstr(t,"\tAltBot02Engineer\t1\t2\t500.0\t600.0\tTestPlayer\t0\t1\t120.0\t160.0\tM1A2\t0\t"));
 assert(strstr(t,"\t-\t0\t0\t100.0\t150.0\tAltBot01Assault\t1\t1\t100.0\t150.0\tDamageArea\t0\t0\r\n"));
 assert(count(t,"kill\t")==3&&!strstr(t,"Ghost"));free(t);
 puts("PASS: kills - killer and victim with bot / team / position, weapon, head shot, distance; the victim where it was last alive (not where the body flew), the world's kills at the victim, players never seen left out");
 // stuck: the tank's own watchdog, twice at the same spot (one event), then the no-progress watch on bot 0
 livemap_stuck(1,g_vehPos,"vehicle");livemap_stuck(1,g_vehPos,"vehicle");
 *(float*)(*(char**)(b2+OFF_PLAYER_SOLDIER)+OFF_SOLDIER_HEALTH)=100;sp[0]=-150;sp[2]=380;   // (back alive for the rest)
 step(30*31-19);
 t=slurp("events_25300.txt");
 assert(count(t,"stuck\t")==3&&!strstr(t,"AltBot03Recon\t2\t-150.0\t380.0\tno progress"));assert(strstr(t,"\tAltBot02Engineer\t2\t500.0\t600.0\tno progress\r\n"));assert(strstr(t,"\tMP_007\tAltBot02Engineer\t2\t500.0\t600.0\tvehicle\r\n"));
 assert(strstr(t,"\tMP_007\tAltBot01Assault\t1\t100.0\t150.0\tno progress\r\n"));
 int moves=count(t,"move\t");assert(moves>=4*6&&moves<=4*8);free(t);
 printf("PASS: stuck spots - the mod's watchdogs (one event per bot and spot in 10 s) and the no-progress watch (bot 0, 50 m from its flag, and the tank, 680 m from its goal, both still after 30 s; the sniper on its perch is not reported); %d move lines in 32 s (4 soldiers every 5 s)\n",moves);
 // a new round: a new round line and round id
 long first=g_lmRound;g_roundEpoch++;step(31);
 t=slurp("events_25300.txt");assert(count(t,"round\t")==2&&g_lmRound>=first);free(t);
 // switched off: an "off" picture, the kill queue drained and nothing written; on again: a fresh picture
 g_enhanced.liveMap=0;queue_kill("TestPlayer","AltBot01Assault","M16A4",0);step(60);
 t=slurp("live_25300.txt");assert(strstr(t,"\r\noff\r\nend\r\n"));free(t);assert(g_qr==g_qn);
 char *ev=slurp("events_25300.txt");int killsBefore=count(ev,"kill\t");free(ev);
 g_enhanced.liveMap=1;step(31);t=slurp("live_25300.txt");assert(!strstr(t,"\r\noff\r\n")&&strstr(t,"\r\np\tTestPlayer"));free(t);
 ev=slurp("events_25300.txt");assert(count(ev,"kill\t")==killsBefore);free(ev);
 puts("PASS: LiveMap=0 - the live picture says off, nothing more is written, kills meanwhile are dropped (not written late); LiveMap=1 again picks up at once; a new round starts a new round id");
 return 0;
}
'''


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    print(r.stdout, end='')
    if r.returncode:
        print(r.stderr); raise SystemExit('FAIL: %s exited %d' % (Path(cmd[0]).name, r.returncode))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'] if a.sanitize else ['-O2']
    with tempfile.TemporaryDirectory(prefix='bf3-altbotmod-v13-') as tmp:
        tmp = Path(tmp)
        (tmp / 'windows.h').write_text(WINDOWS)
        for n in SRC.iterdir():
            if n.suffix in ('.c', '.h', '.inc'): shutil.copy(n, tmp / n.name)
        (tmp / 'portable.c').write_text(PORTABLE); (tmp / 'native.c').write_text(NATIVE)
        cc = ['gcc', '-std=gnu11', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-I', str(tmp)] + san
        subprocess.run(cc + [str(tmp / 'portable.c'), str(tmp / 'nav.c'), str(tmp / 'livemap.c'), '-lm', '-o', str(tmp / 'portable')], check=True)
        out = tmp / 'out'; out.mkdir()
        grids = [str(SERVER / f'BotMod_nav_{lv}.bin') for lv in ['MP_007', 'XP3_Desert', 'MP_011']]
        run([str(tmp / 'portable'), str(out)] + grids)
        subprocess.run(cc + [str(tmp / 'native.c'), str(tmp / 'nav.c'), str(tmp / 'livemap.c'), str(tmp / 'enhanced.c'), str(tmp / 'air.c'), str(tmp / 'modes.c'), '-lm', '-o', str(tmp / 'native')], check=True)
        nout = tmp / 'nout'; nout.mkdir()
        run([str(tmp / 'native'), str(nout)])
        FIXTURE.mkdir(parents=True, exist_ok=True)
        for n in ['live_25300.txt', 'events_25300.txt']:
            shutil.copy(nout / 'LiveMap' / n, FIXTURE / n)
        print('PASS: the mock server\'s live_25300.txt / events_25300.txt are kept as the Server Control Live map test fixture')
    core = (SRC / 'core.c').read_text(); chat = (SRC / 'chat.c').read_text(); gp = (SRC / 'ground_progress.inc').read_text()
    assert 'bm_report_stuck(i, pos, "on foot");' in core and 'bm_report_stuck(i, vp, "vehicle");' in core and 'bm_report_stuck(i,(float *)(so+OFF_SOLDIER_SPAWN_POSITION),"retired")' in core
    assert 'bm_report_stuck(i,position,"vehicle stalled");' in gp
    assert 'bm_stuck_hook = livemap_stuck;' in (SRC / 'livemap_native.inc').read_text() and 'FORGET(g_lmProg);' in core
    assert re.search(r'validate_bots\(\);\s*g_where="live map";g_whereBot=-1;\s*livemap_tick\(\);', core)
    assert '#include "livemap_native.inc"' in core
    assert 'snprintf(g_kills[g_killHead].killer,sizeof g_kills[0].killer,"%s",w[1]);snprintf(g_kills[g_killHead].victim,sizeof g_kills[0].victim,"%s",w[2]);' in chat
    assert 'g_kills[g_killHead].headshot=nw>=5&&!_stricmp(w[4],"true")' in chat
    print('PASS: hooks - the live map runs every frame before the round clean-up return (so kills are drained even then); stuck spots from the on-foot, driving, 15 s stall and retire watchdogs (through the bm_report_stuck hook in enhanced.h, a no-op in the other offline tests); the no-progress watch is compacted with the roster; RCON player.onKill keeps killer, victim, weapon and head shot in its own queue (the round code\'s death queue is unchanged)')


if __name__ == '__main__':
    main()
