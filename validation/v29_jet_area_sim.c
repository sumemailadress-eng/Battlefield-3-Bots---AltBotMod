// (model copied from air_combat_sim.c) AltBotMod V20 air combat simulation. A rigid-body kinematic model (body-rate pitch/roll/yaw, thrust, drag,
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
static float areaMin[2], areaMax[2]; static int areaOn;
static void control(Craft *c,Craft *all,int n,Craft *tgt,const float *ground,const float *patrol)
{
    BMAirObservation o; memset(&o,0,sizeof o);
    memcpy(o.position,c->pos,12);memcpy(o.right,c->r,12);memcpy(o.up,c->u,12);memcpy(o.forward,c->f,12);
    memcpy(o.velocity,c->vel,12);o.velocityValid=1;o.groundValid=o.aheadGroundValid=1;o.groundY=o.aheadGroundY=0;
    memcpy(o.goal,patrol,12);
    for(int k=0;k<n;k++)if(&all[k]!=c && all[k].alive && o.trafficCount<BM_AIR_TRAFFIC){
        memcpy(o.traffic[o.trafficCount],all[k].pos,12);memcpy(o.traffic[o.trafficCount]+3,all[k].vel,12);o.trafficCount++;}
    if(tgt && tgt->alive){memcpy(o.target,tgt->pos,12);memcpy(o.targetVelocity,tgt->vel,12);o.targetValid=o.targetVisible=o.targetAir=1;memcpy(o.goal,tgt->pos,12);}
    else if(ground){memcpy(o.target,ground,12);o.targetValid=o.targetVisible=1;memcpy(o.goal,ground,12);o.goal[1]+=cfg.air.jetHeight;}
    if(areaOn){o.areaValid=1;memcpy(o.areaMin,areaMin,8);memcpy(o.areaMax,areaMax,8);}
    assert(bm_air_step(&c->s,c->p,&cfg.air,&o,DT,&c->out));
    if(c->out.fire)c->fireFrames++;
    if(c->out.secondary)c->secondaries++;
}
static float outside(const float *p){float dx=fmaxf(0,fmaxf(areaMin[0]-p[0],p[0]-areaMax[0])),dz=fmaxf(0,fmaxf(areaMin[1]-p[2],p[2]-areaMax[1]));return hypotf(dx,dz);}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    bm_defaults(&cfg); cfg.air.weapons=1; srand(29);
    areaMin[0]=-1500;areaMin[1]=-1500;areaMax[0]=1500;areaMax[1]=1500;
    // 1. Jets anywhere from the middle to 150 m inside the edge, flying straight out at 87-200 m/s towards a goal far
    //    outside, nothing else to do: from the middle they turn back before the edge; from the edge they are back inside
    //    within seconds. Without the area (V20-V28) they fly on out.
    for(int withArea=0;withArea<2;withArea++){
        areaOn=withArea; float worstMid=0,worstMidT=0,worstEdge=0,worstEdgeT=0; int lost=0;
        for(int k=0;k<48;k++){
            Craft c[1]; float a=(k%24)*0.2618f, sp=87+(k%4)*38, r=k<24?600.0f:1350.0f; float x=sinf(a)*r,z=cosf(a)*r;
            if(fabsf(x)>1350)x=x>0?1350:-1350; if(fabsf(z)>1350)z=z>0?1350:-1350;
            place(&c[0],"F18",1,x,350,z,a,sp); for(int q=0;q<3;q++)c[0].vel[q]=c[0].f[q]*sp;
            float patrol[3]={x+sinf(a)*3000,350,z+cosf(a)*3000};   // (a goal outside: the area must win)
            float out=0,outT=0,cur=0;
            for(int t=0;t<60*30;t++){ control(&c[0],c,1,NULL,NULL,patrol); physics(&c[0]); if(!c[0].alive){lost++;break;}
                float o=outside(c[0].pos); if(o>out)out=o; if(o>0)cur+=DT; else { if(cur>outT)outT=cur; cur=0; } }
            if(cur>outT)outT=cur;
            if(k<24){ if(out>worstMid)worstMid=out; if(outT>worstMidT)worstMidT=outT; }
            else { if(out>worstEdge)worstEdge=out; if(outT>worstEdgeT)worstEdgeT=outT; }
        }
        printf("%s: 48 jets flying straight out (87-200 m/s) - from 600 m in: furthest out %.0f m, longest outside %.1f s; from 150 m in: furthest out %.0f m, longest outside %.1f s; crashes %d\n",
               withArea?"V29 area":"no area (V28)",worstMid,worstMidT,worstEdge,worstEdgeT,lost);
        if(withArea) assert(lost==0 && worstMid<200 && worstMidT<4.0f && worstEdge<900 && worstEdgeT<20.0f);
        else assert(worstMid>2000);
    }
    areaOn=1;
    // 2. Strike runs and dogfights inside a 3 km area for 3 minutes each: the jets stay (nearly) inside, still dive, gun and fire missiles.
    { int dives=0,crash=0; float worst=0,worstT=0; long frames=0,outFrames=0;
      for(int k=0;k<20;k++){ Craft c[1]; float a=(float)rand()/RAND_MAX*6.283f;
          place(&c[0],k%2?"A10_THUNDERBOLT":"Su25",1,sinf(a)*1000,350,cosf(a)*1000,(float)rand()/RAND_MAX*6.283f,87);
          float g[3]={sinf(a+2)*900,1,cosf(a+2)*900},patrol[3]={0,350,0},cur=0; int prev=c[0].s.phase;
          for(int t=0;t<180*30;t++){ control(&c[0],c,1,NULL,g,patrol); physics(&c[0]); if(!c[0].alive){crash++;break;}
              if(prev!=BM_FLIGHT_ATTACK && c[0].s.phase==BM_FLIGHT_ATTACK)dives++; prev=c[0].s.phase;
              float o=outside(c[0].pos); frames++; if(o>0){outFrames++;cur+=DT;} else cur=0; if(o>worst)worst=o; if(cur>worstT)worstT=cur; } }
      printf("strike runs in a 3 km area: 20 jets x 180 s, dives %d, crashes %d, outside %.1f%% of the time, furthest out %.0f m, longest outside %.1f s\n",
             dives,crash,100.0*outFrames/frames,worst,worstT);
      assert(crash==0 && dives>=20*3 && worstT<8.0f && outFrames<frames/20); }
    { int crash=0,missiles=0,scored=0; float worst=0,worstT=0; long frames=0,outFrames=0;
      for(int f=0;f<40;f++){ Craft c[2]; float a=(float)rand()/RAND_MAX*6.283f;
          place(&c[0],"F18",1,0,350,0,(float)rand()/RAND_MAX*6.283f,87); place(&c[1],"Su35",2,sinf(a)*1200,330,cosf(a)*1200,a,87);
          float patrol[3]={0,350,0},cur[2]={0,0};
          for(int t=0;t<120*30;t++){ control(&c[0],c,2,&c[1],NULL,patrol); control(&c[1],c,2,&c[0],NULL,patrol); physics(&c[0]); physics(&c[1]);
              if(!c[0].alive||!c[1].alive){crash++;break;}
              for(int q=0;q<2;q++){ float o=outside(c[q].pos); frames++; if(o>0){outFrames++;cur[q]+=DT;} else cur[q]=0; if(o>worst)worst=o; if(cur[q]>worstT)worstT=cur[q]; } }
          missiles+=c[0].secondaries+c[1].secondaries; if(c[0].fireFrames>15 && c[1].fireFrames>15)scored++; }
      printf("dogfights in a 3 km area: 40 x 120 s, crashes %d, both fired %d, missiles %d, outside %.1f%% of the time, furthest out %.0f m, longest outside %.1f s\n",
             crash,scored,missiles,100.0*outFrames/frames,worst,worstT);
      assert(crash==0 && scored>=20 && missiles>=20 && worstT<8.0f && outFrames<frames/10); }
    return 0;
}
