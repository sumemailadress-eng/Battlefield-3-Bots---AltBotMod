#pragma once
enum BMMode { BM_MODE_UNKNOWN, BM_MODE_CQ, BM_MODE_RUSH, BM_MODE_TDM, BM_MODE_SQDM };
typedef struct BMMap { const char *code,*alias,*name; int closeQuarters,assault; } BMMap;
extern const BMMap bm_maps[29];
enum BMMode bm_mode(const char *name);
int bm_mode_teams(enum BMMode mode);
int bm_mode_squad_rush(const char *name);
int bm_mode_gun_master(const char *name);
const BMMap *bm_map(const char *name);
const char *bm_map_mode(const BMMap *map,const char *requested);
int bm_legacy_cache_allowed(const char *level,const char *mode);
int bm_roster_team(const char *name,enum BMMode mode,int fallback);
void bm_roster_targets(int teams,int capacity,int reserve,int wanted,const int humans[5],int targets[5]);
enum BMDispatch { BM_DISPATCH_ARMOR, BM_DISPATCH_JET, BM_DISPATCH_LIGHT, BM_DISPATCH_HELI, BM_DISPATCH_COUNT };
float bm_dispatch_score(int role,const int counts[BM_DISPATCH_COUNT],float distance);
