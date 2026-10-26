// Bounded team assignments inspired by FunBots GameDirector's assignment
// counts. Observations are supplied by the native adapter, never guessed.
#pragma once
#include <math.h>
#include <string.h>
#define BM_DIRECTOR_UNITS 64
#define BM_DIRECTOR_FLAGS 16
enum BMObjectiveRole { BM_OBJECTIVE_NONE, BM_OBJECTIVE_ATTACK, BM_OBJECTIVE_GUARD, BM_OBJECTIVE_PATROL };
typedef struct BMDirectorFlag { int owner,enemy[3]; float position[3]; int recentLoss[3]; } BMDirectorFlag;   // recentLoss[team]: Caspian V2, lost in the last 2 min
typedef struct BMDirectorUnit { int active,team,mobile,commanded,avoid; float position[3]; } BMDirectorUnit;
typedef struct BMDirectorAssignment { int flag,role; unsigned assignedAt,arrivedAt,progressAt;float distance; } BMDirectorAssignment;
typedef struct BMDirectorState { BMDirectorAssignment unit[BM_DIRECTOR_UNITS]; unsigned now; int guardDivisor; } BMDirectorState;   // guardDivisor: Caspian V2, 1 guard per N infantry (0 = 3)
static float bm_director_distance(const float *a,const float *b)
{ return hypotf(a[0]-b[0],a[2]-b[2]); }
static int bm_director_valid(int f,int count) { return f>=0 && f<count; }
static void bm_director_assign(BMDirectorAssignment *a,int f,int role,unsigned now)
{
    if(a->flag!=f || a->role!=role){a->flag=f;a->role=role;a->assignedAt=now;a->arrivedAt=0;a->progressAt=now?now:1;a->distance=INFINITY;}
}
// A distant target matters when it lies on the next objective corridor.
static int bm_director_threat_in_path(const float *origin,const float *goal,const float *enemy)
{
    float dx=goal[0]-origin[0],dz=goal[2]-origin[2],length=hypotf(dx,dz);
    if(length<40)return 1;
    float ex=enemy[0]-origin[0],ez=enemy[2]-origin[2];
    float along=(ex*dx+ez*dz)/length,side=fabsf(ex*dz-ez*dx)/length;
    return along>0 && along<length+30 && side<45;
}
static void bm_director_update(BMDirectorState *state,const BMDirectorFlag *flags,int nf,
                               const BMDirectorUnit *units,int nu,unsigned now)
{
    if(nf<0)nf=0;
    if(nf>BM_DIRECTOR_FLAGS)nf=BM_DIRECTOR_FLAGS;
    if(nu<0)nu=0;
    if(nu>BM_DIRECTOR_UNITS)nu=BM_DIRECTOR_UNITS;
    state->now=now;
    for(int i=0;i<BM_DIRECTOR_UNITS;i++)if(i>=nu || !units[i].active || units[i].commanded ||
        units[i].team<1 || units[i].team>2){state->unit[i].flag=-1;state->unit[i].role=BM_OBJECTIVE_NONE;state->unit[i].arrivedAt=0;}
    for(int team=1;team<=2;team++){
        int n=0,feet=0,hostile=0,counts[BM_DIRECTOR_FLAGS]={0},guards[BM_DIRECTOR_FLAGS]={0};
        unsigned char used[BM_DIRECTOR_UNITS]={0};
        for(int f=0;f<nf;f++)if(flags[f].owner!=team)hostile++;
        for(int i=0;i<nu;i++)if(units[i].active && !units[i].commanded && units[i].team==team){n++;if(!units[i].mobile)feet++;}
        if(!n || !nf)continue;
        // Leave the majority mobile, even when every flag is friendly. Three
        // guards is a group target; a small roster must retain attackers.
        int budget=feet/(state->guardDivisor>0?state->guardDivisor:3);if(budget>feet-(hostile?1:0))budget=feet-(hostile?1:0);
        // Preserve useful local guard groups, with a hard six-soldier ceiling.
        for(int i=0;i<nu;i++){
            BMDirectorAssignment *a=&state->unit[i];int f=a->flag;
            if(!units[i].active || units[i].commanded || units[i].team!=team || units[i].mobile ||
               a->role!=BM_OBJECTIVE_GUARD || !bm_director_valid(f,nf) || flags[f].owner!=team ||
               units[i].avoid==f || !budget || guards[f]>=(flags[f].enemy[team]?6:3))continue;
            used[i]=1;guards[f]++;counts[f]++;budget--;
        }
        // Fill a nearby frontline/captured flag in groups before opening a
        // second guard group. Do not drag attackers back from across the map.
        while(budget>0){
            int bestUnit=-1,bestFlag=-1;float score=INFINITY;
            for(int f=0;f<nf;f++)if(flags[f].owner==team && guards[f]<(flags[f].enemy[team]?6:3)){
                for(int i=0;i<nu;i++)if(!used[i] && units[i].active && !units[i].commanded &&
                    units[i].team==team && !units[i].mobile && units[i].avoid!=f){
                    float d=bm_director_distance(units[i].position,flags[f].position);
                    if(d>110)continue;
                    float value=d-(guards[f]?250:0)-(flags[f].enemy[team]?300:0);
                    if(value<score){score=value;bestUnit=i;bestFlag=f;}
                }
            }
            if(bestUnit<0)break;
            bm_director_assign(&state->unit[bestUnit],bestFlag,BM_OBJECTIVE_GUARD,now);
            used[bestUnit]=1;guards[bestFlag]++;counts[bestFlag]++;budget--;
        }
        // Keep attack routes committed. Ownership changes release the whole
        // surplus force immediately; distance alone does not churn a trip.
        for(int i=0;i<nu;i++)if(!used[i] && units[i].active && !units[i].commanded && units[i].team==team){
            BMDirectorAssignment *a=&state->unit[i];int f=a->flag;
            int valid=bm_director_valid(f,nf) && units[i].avoid!=f;
            if(hostile && valid && a->role==BM_OBJECTIVE_ATTACK && flags[f].owner!=team){
                float d=bm_director_distance(units[i].position,flags[f].position);
                if(!a->progressAt || !isfinite(a->distance) || d<a->distance-10){a->distance=d;a->progressAt=now?now:1;}
                if(d<35){if(!a->arrivedAt)a->arrivedAt=now?now:1;}else a->arrivedAt=0;
                // Preserve trips that actually approach their target. A stale
                // fight/route cannot monopolize the same attack indefinitely.
                // Ownership changes also rebalance surplus to newly lost flags.
                int front=hostile<(units[i].mobile?3:2)?hostile:(units[i].mobile?3:2);   // (V25: the force is shared over the front, not every flag)
                int share=(n+front-1)/front+1;
                if(counts[f]<share && (hostile==1 ||
                   (now-a->progressAt<45*30 && (!a->arrivedAt || now-a->arrivedAt<20*30)))){used[i]=1;counts[f]++;}
            }
            else if(!hostile && valid && a->role==BM_OBJECTIVE_PATROL){
                if(bm_director_distance(units[i].position,flags[f].position)<(units[i].mobile==3?800:units[i].mobile==2?220:units[i].mobile?65:25)){
                    if(!a->arrivedAt)a->arrivedAt=now?now:1;
                    if(now-a->arrivedAt<8*30){used[i]=1;counts[f]++;}
                }else {a->arrivedAt=0;used[i]=1;counts[f]++;}
            }
        }
        // AltBotMod V25: nearest flags first, then the next ones - the team takes the map from its own side outwards. Each
        // enemy / neutral flag is ranked by how far it lies from the team's ground (its nearest own flag; with none, where
        // the team is); soldiers go for the two nearest, vehicles and aircraft for the three nearest. V6-V24 spread the
        // team over every flag at once, the far ones included, so a round started with bots strung all over the map.
        int rank[BM_DIRECTOR_FLAGS];for(int f=0;f<nf;f++)rank[f]=99;
        if(hostile){
            float centre[3]={0,0,0};int m=0;
            for(int i=0;i<nu;i++)if(units[i].active && units[i].team==team){centre[0]+=units[i].position[0];centre[2]+=units[i].position[2];m++;}
            if(m){centre[0]/=m;centre[2]/=m;}
            float front[BM_DIRECTOR_FLAGS];
            for(int f=0;f<nf;f++){
                if(flags[f].owner==team){front[f]=INFINITY;continue;}
                float best=INFINITY;
                for(int g=0;g<nf;g++)if(flags[g].owner==team){float d=bm_director_distance(flags[g].position,flags[f].position);if(d<best)best=d;}
                front[f]=isfinite(best)?best:bm_director_distance(centre,flags[f].position);
            }
            for(int r=0;r<nf;r++){int pick=-1;for(int f=0;f<nf;f++)if(rank[f]==99 && isfinite(front[f]) && (pick<0 || front[f]<front[pick]))pick=f;if(pick<0)break;rank[pick]=r;}
        }
        // Spread over the uncovered flags of the front, then reinforce.
        // Contested flags get urgency, while counts prevent a one-flag mob.
        for(int turn=0;turn<nu;turn++){
            int i=(turn+(int)(now/30))%nu;
            if(used[i] || !units[i].active || units[i].commanded || units[i].team!=team)continue;
            BMDirectorAssignment *a=&state->unit[i];int previous=a->flag,best=-1;float score=INFINITY;
            int candidates=hostile?hostile:nf;
            int uncovered=0;
            int reach=units[i].mobile?3:2;   // (V25: how deep into the enemy's flags this unit is sent)
            int front=hostile<reach?hostile:reach;if(front<1)front=1;
            if(hostile)for(int f=0;f<nf;f++)if(flags[f].owner!=team && !counts[f] && units[i].avoid!=f && rank[f]<reach &&
                (candidates==1 || f!=previous))uncovered=1;
            for(int f=0;f<nf;f++){
                if((hostile && flags[f].owner==team) || units[i].avoid==f || (hostile && rank[f]>=reach && !flags[f].recentLoss[team]) ||
                   (candidates>1 && f==previous) || (uncovered && counts[f]) ||
                   (hostile && counts[f]>=(n+front-1)/front+1))continue;
                // Caspian V2: a flag the team just lost is retaken first and with a larger group.
                int retake=hostile && flags[f].owner!=team && flags[f].recentLoss[team];
                float value=bm_director_distance(units[i].position,flags[f].position)+counts[f]*(retake?700.0f:1400.0f);
                if(hostile && flags[f].enemy[team])value-=350;
                if(hostile && flags[f].owner==0)value-=250;   // AltBotMod V6: neutral flags are the quickest to take - none left untaken
                if(retake)value-=600;
                if(hostile)value+=rank[f]*300.0f;   // (V25: the nearer flags of the front first)
                if(value<score){score=value;best=f;}
            }
            // A single-flag map still needs a patrol assignment; a temporary
            // failed flag can wait for its cooldown instead of becoming a guard.
            if(best<0 && !hostile && nf==1 && units[i].avoid!=0)best=0;
            // A failed enemy approach gets a useful checked patrol assignment
            // while its cooldown expires, rather than an indefinite NONE job.
            if(best<0)for(int f=0;f<nf;f++)if(flags[f].owner==team && units[i].avoid!=f){
                float value=bm_director_distance(units[i].position,flags[f].position)+counts[f]*1400.0f;
                if(value<score){score=value;best=f;}
            }
            bm_director_assign(a,best,best<0?BM_OBJECTIVE_NONE:flags[best].owner!=team?BM_OBJECTIVE_ATTACK:BM_OBJECTIVE_PATROL,now);
            if(best>=0){counts[best]++;if(best==previous && !hostile)a->arrivedAt=now?now:1;}
        }
    }
}
