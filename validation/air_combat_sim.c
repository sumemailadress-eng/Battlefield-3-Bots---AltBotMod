// AltBotMod V20 air combat simulation. A rigid-body kinematic model (body-rate pitch/roll/yaw, thrust, drag,
// gravity on the flight path) flies the real bm_air_step controller in closed loop: jet vs jet dogfights from
// random starts, strike runs on ground targets, traffic evasion, helicopters on ground targets.
// This is a model, not Frostbite physics: it checks the tactics (no ramming, firing solutions, pull-outs,
// repeated passes), not exact live behaviour.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../files/BotMod/src/enhanced.h"
#define DT (1.0f/30)
typedef struct Craft {
    const BMAirProfile *p; BMAirController s; BMAirOutput out; BMAirMotion m;
    float pos[3], r[3], u[3], f[3], speed, vel[3];
    int team, alive;
    // results
    int fireFrames, onTargetFrames, secondaries, dives, maxTactic;
    float minAlt;
} Craft;
static float gPitch=1,gRoll=1,gSag=0;   // model variants: control-rate multipliers, gravity bending the flight path
static float dot3(const float *a,const float *b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static void norm(float *v){float l=sqrtf(dot3(v,v));if(l>0)for(int k=0;k<3;k++)v[k]/=l;}
static void rot(float *a,float *b,float ang)   // rotate the pair (a,b) by ang: a'=a cos+b sin, b'=b cos-a sin
{
    float c=cosf(ang),s=sinf(ang),na[3],nb[3];
    for(int k=0;k<3;k++){na[k]=a[k]*c+b[k]*s;nb[k]=b[k]*c-a[k]*s;}
    memcpy(a,na,12);memcpy(b,nb,12);
}
static void orthonormal(Craft *c)
{
    norm(c->f); float d=dot3(c->u,c->f); for(int k=0;k<3;k++)c->u[k]-=c->f[k]*d; norm(c->u);
    c->r[0]=c->u[1]*c->f[2]-c->u[2]*c->f[1]; c->r[1]=c->u[2]*c->f[0]-c->u[0]*c->f[2]; c->r[2]=c->u[0]*c->f[1]-c->u[1]*c->f[0];
}
static void place(Craft *c,const char *name,int team,float x,float y,float z,float heading,float speed)
{
    memset(c,0,sizeof *c); c->p=bm_air_profile(name); c->team=team; c->alive=1;
    c->pos[0]=x;c->pos[1]=y;c->pos[2]=z; c->f[0]=sinf(heading);c->f[2]=cosf(heading); c->u[1]=1; orthonormal(c);
    c->speed=speed; bm_air_start(&c->s,c->p->kind,1); c->minAlt=1e9f;
}
static void physics(Craft *c)
{
    BMAirOutput *o=&c->out; float sf=fminf(1.3f,fmaxf(0.35f,c->speed/87));
    if(c->p->kind==BM_AIR_JET){
        rot(c->f,c->u,-o->pitch*1.15f*sf*gPitch*DT);   // + pitch input = nose down
        rot(c->r,c->u,o->roll*2.6f*gRoll*DT);          // + roll input raises the right wing (atan2(r.y,u.y) grows)
        if(gSag>0){ // gravity across the flight path turns the nose down (the lift of a pull is in the pitch input)
            float g[3]={0,-9.81f*gSag,0},d=dot3(g,c->f),gp[3]; for(int k=0;k<3;k++)gp[k]=g[k]-c->f[k]*d;
            float m=sqrtf(dot3(gp,gp)); if(m>0.01f){ for(int k=0;k<3;k++)gp[k]/=m; rot(c->f,gp,m/c->speed*DT); }
            float up1=dot3(c->u,c->f); for(int k=0;k<3;k++)c->u[k]-=c->f[k]*up1; }
        rot(c->f,c->r,o->yaw*0.35f*DT);                // + yaw = nose right
        orthonormal(c);
        float drag=24*(c->speed/190)*(c->speed/190);
        c->speed+=(o->throttle*24-o->brake*30-drag-9.81f*c->f[1])*DT; if(c->speed<30)c->speed=30;
        for(int k=0;k<3;k++)c->vel[k]=c->f[k]*c->speed;
    } else {
        rot(c->f,c->u,-o->pitch*0.9f*DT); rot(c->r,c->u,o->roll*1.5f*DT); orthonormal(c);
        // heading turns by the (helicopter-signed) yaw input; level the body yaw only
        float h=-o->yaw*1.1f*DT; float f2[3]={c->f[0],0,c->f[2]},r2[3]={c->r[0],0,c->r[2]}; (void)f2;(void)r2;
        rot(c->f,c->r,h); orthonormal(c);
        float hz[3]={c->f[0],0,c->f[2]}; norm(hz);
        float tilt=asinf(fmaxf(-1,fminf(1,c->f[1])));
        for(int k=0;k<3;k+=2)c->vel[k]+=(hz[k]*(-tilt)*14-c->vel[k]*0.08f)*DT;
        c->vel[1]+=((o->throttle-0.5f)*18-c->vel[1]*0.6f)*DT;
        c->speed=sqrtf(dot3(c->vel,c->vel));
    }
    for(int k=0;k<3;k++)c->pos[k]+=c->vel[k]*DT;
    if(c->pos[1]<c->minAlt)c->minAlt=c->pos[1];
    if(c->pos[1]<=0)c->alive=0;
}
static BMConfig cfg;
// One control step. target: another craft, or a ground point (vel) when tgt==NULL.
static void control(Craft *c,Craft *all,int n,Craft *tgt,const float *ground,const float *groundVel,const float *patrol)
{
    BMAirObservation o; memset(&o,0,sizeof o);
    memcpy(o.position,c->pos,12);memcpy(o.right,c->r,12);memcpy(o.up,c->u,12);memcpy(o.forward,c->f,12);
    memcpy(o.velocity,c->vel,12);o.velocityValid=1;o.groundValid=o.aheadGroundValid=1;o.groundY=o.aheadGroundY=0;
    memcpy(o.goal,patrol,12);
    for(int k=0;k<n;k++)if(&all[k]!=c && all[k].alive && o.trafficCount<BM_AIR_TRAFFIC){
        memcpy(o.traffic[o.trafficCount],all[k].pos,12);memcpy(o.traffic[o.trafficCount]+3,all[k].vel,12);o.trafficCount++;}
    if(tgt && tgt->alive){memcpy(o.target,tgt->pos,12);memcpy(o.targetVelocity,tgt->vel,12);o.targetValid=o.targetVisible=o.targetAir=1;
        memcpy(o.goal,tgt->pos,12);}
    else if(ground){memcpy(o.target,ground,12);memcpy(o.targetVelocity,groundVel,12);o.targetValid=o.targetVisible=1;
        memcpy(o.goal,ground,12);o.goal[1]+=c->p->kind==BM_AIR_JET?cfg.air.jetHeight:60;}
    int prevPhase=c->s.phase;
    assert(bm_air_step(&c->s,c->p,&cfg.air,&o,DT,&c->out));
    if(c->out.fire){c->fireFrames++;
        // true angle between the nose and the target's actual position (the lead point is not checked here)
        float d[3];for(int k=0;k<3;k++)d[k]=o.target[k]-c->pos[k];float range=sqrtf(dot3(d,d));
        float cosang=dot3(d,c->f)/fmaxf(range,1); if(acosf(fminf(1,cosang))<atanf(30/range)+0.05f)c->onTargetFrames++;}
    if(c->out.secondary)c->secondaries++;
    if(prevPhase!=BM_FLIGHT_ATTACK && c->s.phase==BM_FLIGHT_ATTACK && !o.targetAir)c->dives++;
}
static float dist3(const float *a,const float *b){float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};return sqrtf(dot3(d,d));}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    bm_defaults(&cfg); cfg.air.weapons=1; srand(7);
    float patrol[3]={0,350,0};
    // 0. Model sanity (the sign conventions match the controller): a jet flying away from its waypoint turns round
    // and reaches it; one in a helicopter too.
    { Craft c[1]; place(&c[0],"F18",1,0,350,0,0,87); float goal[3]={2000,400,-1500},best=1e9f;
      for(int t=0;t<60*30;t++){ BMAirObservation o; (void)o; control(&c[0],c,1,NULL,NULL,NULL,goal); physics(&c[0]);
          float d=hypotf(c[0].pos[0]-goal[0],c[0].pos[2]-goal[2]); if(d<best)best=d; }
      printf("model sanity: jet passes %.0f m from its waypoint\n",best); assert(best<300);
      place(&c[0],"Mi28",1,0,110,0,0,0); float hg[3]={-600,110,400}; best=1e9f;
      for(int t=0;t<60*30;t++){ control(&c[0],c,1,NULL,NULL,NULL,hg); physics(&c[0]);
          float d=hypotf(c[0].pos[0]-hg[0],c[0].pos[2]-hg[2]); if(d<best)best=d; }
      printf("model sanity: helicopter gets within %.0f m of its waypoint\n",best); assert(best<80); }
    // 1. Dogfights from random starts (incl. dead head-on): no collisions, both get gun solutions.
    int fights=300, rams=0, crashes=0, gunsBoth=0, missiles=0; float worstSep=1e9f; long fire=0,onTarget=0;
    for(int f=0;f<fights;f++){
        Craft c[2]; float a=(float)rand()/RAND_MAX*6.283f, d=1500+rand()%2500;
        place(&c[0],"F18",1,0,300+rand()%200,0,(float)rand()/RAND_MAX*6.283f,87);
        place(&c[1],"Su35",2,sinf(a)*d,300+rand()%200,cosf(a)*d,f%3==0?a+3.1416f:(float)rand()/RAND_MAX*6.283f,87);
        if(f%3==0){c[0].f[0]=sinf(a);c[0].f[2]=cosf(a);c[0].f[1]=0;c[0].u[0]=c[0].u[2]=0;c[0].u[1]=1;orthonormal(&c[0]);c[1].pos[1]=c[0].pos[1];} // dead head-on
        float sep=1e9f;
        for(int t=0;t<90*30;t++){
            control(&c[0],c,2,&c[1],NULL,NULL,patrol); control(&c[1],c,2,&c[0],NULL,NULL,patrol);
            physics(&c[0]); physics(&c[1]);
            float s=dist3(c[0].pos,c[1].pos); if(s<sep)sep=s;
            if(!c[0].alive||!c[1].alive)break;
        }
        if(sep<25)rams++; if(!c[0].alive||!c[1].alive)crashes++; if(sep<worstSep)worstSep=sep;
        if(c[0].onTargetFrames>15 && c[1].onTargetFrames>15)gunsBoth++;
        fire+=c[0].fireFrames+c[1].fireFrames; onTarget+=c[0].onTargetFrames+c[1].onTargetFrames; missiles+=c[0].secondaries+c[1].secondaries;
    }
    printf("dogfights: %d, rams(<25 m) %d, ground crashes %d, closest pass %.0f m, both scored gun time %d, gun frames %ld (%.0f%% on target), missiles %d\n",
        fights,rams,crashes,worstSep,gunsBoth,fire,100.0*onTarget/(fire?fire:1),missiles);
    assert(rams==0 && crashes==0);
    assert(gunsBoth>fights*0.6);
    assert(missiles>fights);
    // 2. Strike runs on a static and on a moving ground target.
    for(int moving=0;moving<2;moving++){
        int runs=40,totalDives=0,minDives=99,crash=0,rockets=0; long sf=0,son=0; float lowest=1e9f;
        for(int k=0;k<runs;k++){
            Craft c[1]; float a=(float)rand()/RAND_MAX*6.283f;
            place(&c[0],k%2?"A10_THUNDERBOLT":"Su25",1,sinf(a)*3000,350,cosf(a)*3000,(float)rand()/RAND_MAX*6.283f,87);
            float g[3]={0,1,0},gv[3]={moving?8.0f:0,0,moving?5.0f:0};
            for(int t=0;t<150*30;t++){ control(&c[0],c,1,NULL,g,gv,patrol); physics(&c[0]);
                for(int q=0;q<3;q++)g[q]+=gv[q]*DT; if(!c[0].alive)break; }
            if(!c[0].alive)crash++; totalDives+=c[0].dives; if(c[0].dives<minDives)minDives=c[0].dives;
            sf+=c[0].fireFrames; son+=c[0].onTargetFrames; rockets+=c[0].secondaries; if(c[0].minAlt<lowest)lowest=c[0].minAlt;
        }
        printf("strike runs (%s target): %d jets x 150 s, dives %d (min %d per jet), crashes %d, lowest %.0f m, gun frames %ld (%.0f%% on target), rocket salvos %d\n",
            moving?"moving":"static",runs,totalDives,minDives,crash,lowest,sf,100.0*son/(sf?sf:1),rockets);
        assert(crash==0 && lowest>35 && minDives>=4 && rockets==0 && son>sf/2);   /* V34: no heat seekers at ground targets (V20-V33: one a dive) */
    }
    // 3. Friendly traffic: two jets of one team patrolling head-on at the same height - they must not collide.
    { int near=0; for(int k=0;k<100;k++){ Craft c[2]; float y=350;
        place(&c[0],"F18",1,0,y,-2000,0,87); place(&c[1],"F18",1,(k%7)-3.0f,y,2000,3.14159f,87);
        float p0[3]={0,350,3000},p1[3]={0,350,-3000},sep=1e9f;
        for(int t=0;t<60*30;t++){ control(&c[0],c,2,NULL,NULL,NULL,p0); control(&c[1],c,2,NULL,NULL,NULL,p1);
            physics(&c[0]); physics(&c[1]); float s=dist3(c[0].pos,c[1].pos); if(s<sep)sep=s; }
        if(sep<40)near++; }
      printf("friendly head-on traffic: 100 encounters, %d closer than 40 m\n",near); assert(near==0); }
    // 4. Helicopters on a ground target: they close in and keep it under fire.
    { long frames=0,firing=0; int crash=0;
      for(int k=0;k<20;k++){ Craft c[1]; float a=k*0.31f;
        place(&c[0],k%2?"AH1Z":"Mi28",1,sinf(a)*900,110,cosf(a)*900,a+3.1416f,0);
        float g[3]={0,1,0},gv[3]={0,0,0};
        for(int t=0;t<60*30;t++){ control(&c[0],c,1,NULL,g,gv,patrol); physics(&c[0]); frames++; if(!c[0].alive)break; }
        firing+=c[0].fireFrames; if(!c[0].alive)crash++; }
      printf("gunships vs ground: 20 x 60 s, firing %.0f%% of the time, crashes %d\n",100.0*firing/frames,crash);
      assert(crash==0 && firing>frames*0.15); }
    // 5. Jet vs helicopter (slow air target): guns without overshoot rams.
    { int rams2=0; long on=0; for(int k=0;k<60;k++){ Craft c[2]; float a=(float)rand()/RAND_MAX*6.283f;
        place(&c[0],"F18",1,0,350,0,(float)rand()/RAND_MAX*6.283f,87); place(&c[1],"Mi28",2,sinf(a)*2000,200,cosf(a)*2000,a,0);
        float sep=1e9f, hp[3]={sinf(a)*2000,200,cosf(a)*2000};
        for(int t=0;t<60*30;t++){ control(&c[0],c,2,&c[1],NULL,NULL,patrol); control(&c[1],c,2,NULL,NULL,NULL,hp);
            physics(&c[0]); physics(&c[1]); float s=dist3(c[0].pos,c[1].pos); if(s<sep)sep=s; }
        if(sep<25)rams2++; on+=c[0].onTargetFrames; }
      printf("jet vs helicopter: 60 x 60 s, rams %d, on-target gun frames %ld\n",rams2,on); assert(rams2==0 && on>60*20); }
    // 6. Robustness: the same tactics with sluggish / quick controls and with gravity sag on the flight path.
    float variants[][3]={{0.6f,0.6f,0},{1.5f,1.5f,0},{1,1,1},{0.7f,1,1},{1.3f,0.6f,1}};
    for(unsigned v=0;v<sizeof variants/sizeof variants[0];v++){
        gPitch=variants[v][0]; gRoll=variants[v][1]; gSag=variants[v][2];
        int rams3=0,crash3=0,dives3=0,minDives3=99,scored=0; float low=1e9f;
        for(int f=0;f<80;f++){ Craft c[2]; float a=(float)rand()/RAND_MAX*6.283f,d=1500+rand()%2500;
            place(&c[0],"F18",1,0,350,0,(float)rand()/RAND_MAX*6.283f,87); place(&c[1],"Su35",2,sinf(a)*d,330,cosf(a)*d,f%2?a+3.1416f:a,87);
            float sep=1e9f; for(int t=0;t<60*30;t++){ control(&c[0],c,2,&c[1],NULL,NULL,patrol); control(&c[1],c,2,&c[0],NULL,NULL,patrol);
                physics(&c[0]); physics(&c[1]); float q=dist3(c[0].pos,c[1].pos); if(q<sep)sep=q; if(!c[0].alive||!c[1].alive)break; }
            if(sep<25)rams3++; if(!c[0].alive||!c[1].alive)crash3++; if(c[0].onTargetFrames+c[1].onTargetFrames>15)scored++;
            if(c[0].minAlt<low)low=c[0].minAlt; if(c[1].minAlt<low)low=c[1].minAlt; }
        for(int k=0;k<20;k++){ Craft c[1]; float a=(float)rand()/RAND_MAX*6.283f;
            place(&c[0],"A10_THUNDERBOLT",1,sinf(a)*3000,350,cosf(a)*3000,(float)rand()/RAND_MAX*6.283f,87);
            float g[3]={0,1,0},gv[3]={0,0,0};
            for(int t=0;t<150*30;t++){ control(&c[0],c,1,NULL,g,gv,patrol); physics(&c[0]); if(!c[0].alive)break; }
            if(!c[0].alive)crash3++; dives3+=c[0].dives; if(c[0].dives<minDives3)minDives3=c[0].dives; if(c[0].minAlt<low)low=c[0].minAlt; }
        printf("variant pitch x%.1f roll x%.1f sag %.0f: 80 dogfights rams %d, scored %d; 20 strike jets dives %d (min %d); crashes %d, lowest %.0f m\n",
            gPitch,gRoll,gSag,rams3,scored,dives3,minDives3,crash3,low);
        assert(rams3==0 && crash3==0 && scored>=60 && minDives3>=2 && low>30);
    }
    gPitch=gRoll=1; gSag=0;
    // 7. V20.1 live regression: low and climbing after takeoff (60 m) with a ground target - keep climbing (no
    // pull-out / extend), then dive on it.
    { int bad=0,dove=0; for(int k=0;k<20;k++){ Craft c[1]; float a=k*0.3f;
        place(&c[0],"F18",1,sinf(a)*2500,60,cosf(a)*2500,a+3.1416f+0.6f,80); float r0=0.15f; c[0].f[1]=sinf(r0); orthonormal(&c[0]); for(int q=0;q<3;q++)c[0].vel[q]=c[0].f[q]*c[0].speed;
        float g[3]={0,1,0},gv[3]={0,0,0};
        for(int t=0;t<90*30;t++){ control(&c[0],c,1,NULL,g,gv,patrol); physics(&c[0]);
            if(t<8*30 && c[0].s.phase==BM_FLIGHT_EXTEND)bad++; if(!c[0].alive)break; }
        if(c[0].dives>0)dove++; if(!c[0].alive)bad+=1000; }
      printf("after takeoff: 20 low climbing jets, extends in the first 8 s %d, jets that dove within 90 s %d\n",bad,dove);
      assert(bad==0 && dove>=18); }
    puts("PASS: V20 air combat simulation");
    return 0;
}
