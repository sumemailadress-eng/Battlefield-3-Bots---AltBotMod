#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../files/BotMod/src/director_policy.h"
static BMDirectorState state;
static BMDirectorFlag flags[BM_DIRECTOR_FLAGS];
static BMDirectorUnit units[BM_DIRECTOR_UNITS];
static void check(int n,int nf)
{
    for(int team=1;team<=2;team++){
        int guards[BM_DIRECTOR_FLAGS]={0},nfeet=0,total=0,guardTotal=0;
        for(int i=0;i<n;i++)if(units[i].active && !units[i].commanded && units[i].team==team){
            BMDirectorAssignment *a=&state.unit[i];total++;nfeet+=!units[i].mobile;
            assert(a->flag>=0 && a->flag<nf && a->role!=BM_OBJECTIVE_NONE);
            assert(a->flag!=units[i].avoid);
            if(a->role==BM_OBJECTIVE_GUARD){assert(!units[i].mobile && flags[a->flag].owner==team);guards[a->flag]++;guardTotal++;}
            if(a->role==BM_OBJECTIVE_ATTACK)assert(flags[a->flag].owner!=team);
        }
        assert(guardTotal<=nfeet/3);assert(!total || guardTotal<total);
        for(int f=0;f<nf;f++)assert(guards[f]<=6 && guards[f]<=(flags[f].enemy[team]?6:3));
    }
}
int main(void)
{
    const float scales[]={75,300,1400,4000};
    for(int size=0;size<4;size++){
        memset(&state,0,sizeof state);memset(flags,0,sizeof flags);memset(units,0,sizeof units);
        for(int f=0;f<6;f++){flags[f].position[0]=f*scales[size];flags[f].owner=f==0?1:f==5?2:0;}
        for(int i=0;i<64;i++){units[i].active=1;units[i].team=i<32?1:2;units[i].avoid=-1;units[i].mobile=i%32<6;
            units[i].position[0]=i<32?flags[0].position[0]:flags[5].position[0];state.unit[i].flag=-1;}
        bm_director_update(&state,flags,6,units,64,30);check(64,6);
        // AltBotMod V25: nearest flags first - each team takes the two neutral flags nearest its own ground (vehicles also
        // the third), and sends no soldier to the far side of the map (V6-V24: every flag at once, the far ones too).
        for(int team=1;team<=2;team++){
            int nearA=team==1?1:4,nearB=team==1?2:3,far=team==1?4:1;
            int a=0,b=0,farFeet=0;
            for(int i=0;i<64;i++)if(units[i].team==team){a+=state.unit[i].flag==nearA;b+=state.unit[i].flag==nearB;farFeet+=state.unit[i].flag==far && !units[i].mobile;}
            assert(a>0 && b>0 && farFeet==0);
        }
        int previous[64];for(int i=0;i<64;i++)previous[i]=state.unit[i].flag;
        // Long trips keep their objective while both teams make actual progress.
        bm_director_update(&state,flags,6,units,64,300);check(64,6);
        for(int i=0;i<64;i++)if(state.unit[i].role==BM_OBJECTIVE_ATTACK){
            float target=flags[state.unit[i].flag].position[0];
            units[i].position[0]+=target>units[i].position[0]?15:-15;
        }
        bm_director_update(&state,flags,6,units,64,600);check(64,6);
        bm_director_update(&state,flags,6,units,64,900);check(64,6);
        for(int i=0;i<64;i++)if(state.unit[i].role==BM_OBJECTIVE_ATTACK)assert(state.unit[i].flag==previous[i]);
        // A captured flag retains a small nearby infantry group. Drivers and
        // surplus soldiers switch to other enemy/neutral objectives.
        for(int i=0;i<64;i++)if(units[i].team==1 && state.unit[i].flag==2)units[i].position[0]=flags[2].position[0];
        flags[2].owner=1;bm_director_update(&state,flags,6,units,64,930);check(64,6);
        for(int i=0;i<32;i++)if(state.unit[i].flag==2)assert(state.unit[i].role==BM_OBJECTIVE_GUARD && !units[i].mobile);
        // Changing one side never prevents the other side from getting duties.
        for(int i=32;i<64;i++)assert(state.unit[i].role==BM_OBJECTIVE_ATTACK || state.unit[i].role==BM_OBJECTIVE_GUARD);
    }
    puts("PASS: both teams take the flags nearest their own ground first at four map scales (no soldier sent to the far side), retain long attack trips, cap guard groups at 3-6 and release every captured-flag driver/surplus soldier");
    for(int team=1;team<=2;team++){
        memset(&state,0,sizeof state);memset(flags,0,sizeof flags);memset(units,0,sizeof units);
        for(int f=0;f<6;f++){flags[f].owner=team;flags[f].position[0]=f*700;}
        for(int i=0;i<32;i++){units[i].active=1;units[i].team=team;units[i].avoid=-1;units[i].mobile=i<6;state.unit[i].flag=-1;}
        bm_director_update(&state,flags,6,units,32,30);check(32,6);
        int patrols=0;
        for(int i=0;i<32;i++)if(state.unit[i].role==BM_OBJECTIVE_PATROL){patrols++;int f=state.unit[i].flag;memcpy(units[i].position,flags[f].position,12);}
        assert(patrols>=22);
        bm_director_update(&state,flags,6,units,32,60);int old[32];for(int i=0;i<32;i++)old[i]=state.unit[i].flag;
        bm_director_update(&state,flags,6,units,32,60+8*30);check(32,6);
        for(int i=0;i<32;i++)if(state.unit[i].role==BM_OBJECTIVE_PATROL)assert(state.unit[i].flag!=old[i]);
        flags[4].owner=3-team;bm_director_update(&state,flags,6,units,32,330);check(32,6);
        for(int i=0;i<32;i++)if(state.unit[i].role!=BM_OBJECTIVE_GUARD)assert(state.unit[i].flag==4 && state.unit[i].role==BM_OBJECTIVE_ATTACK);
        units[4].commanded=1;bm_director_update(&state,flags,6,units,32,360);assert(state.unit[4].role==BM_OBJECTIVE_NONE);
    }
    puts("PASS: with all flags owned, the majority patrols between different flags; newly lost flags immediately regain attack priority; human-commanded units are excluded on both teams");
    for(int n=1;n<=64;n++){
        memset(&state,0,sizeof state);memset(flags,0,sizeof flags);memset(units,0,sizeof units);
        for(int i=0;i<n;i++){units[i].active=1;units[i].team=1;units[i].avoid=-1;state.unit[i].flag=-1;}
        flags[0].owner=2;bm_director_update(&state,flags,1,units,n,30);check(n,1);
        flags[0].owner=1;bm_director_update(&state,flags,1,units,n,60);check(n,1);
        bm_director_update(&state,flags,1,units,n,500);check(n,1);
    }
    puts("PASS: single-flag and 1-64-bot rosters retain a mobile duty instead of leaving every bot as a guard");
    // Flight orbits count as arrivals; pilots must not circle one friendly flag forever.
    memset(&state,0,sizeof state);memset(flags,0,sizeof flags);memset(units,0,sizeof units);
    flags[0].owner=flags[1].owner=1;flags[1].position[0]=4000;
    for(int i=0;i<2;i++){units[i].active=1;units[i].team=1;units[i].mobile=i?3:2;units[i].avoid=-1;state.unit[i].flag=-1;}
    bm_director_update(&state,flags,2,units,2,30);
    int previous[2];for(int i=0;i<2;i++){previous[i]=state.unit[i].flag;memcpy(units[i].position,flags[previous[i]].position,12);units[i].position[2]=i?450:100;}
    bm_director_update(&state,flags,2,units,2,60);bm_director_update(&state,flags,2,units,2,60+8*30);
    for(int i=0;i<2;i++)assert(state.unit[i].flag!=previous[i] && state.unit[i].role==BM_OBJECTIVE_PATROL);
    puts("PASS: jet and helicopter patrol orbits use aircraft arrival ranges and advance to another friendly flag after eight seconds");
    return 0;
}
