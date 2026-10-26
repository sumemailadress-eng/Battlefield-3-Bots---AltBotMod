#include "air.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

#define PI 3.14159265359f
static float clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float dot(const float a[3], const float b[3]) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static int finite3(const float v[3]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }
static float wrap(float x) { return remainderf(x, 2*PI); }
static void cross(const float a[3], const float b[3], float r[3])
{ r[0]=a[1]*b[2]-a[2]*b[1]; r[1]=a[2]*b[0]-a[0]*b[2]; r[2]=a[0]*b[1]-a[1]*b[0]; }

void bm_air_defaults(BMAirConfig *c)
{
    memset(c, 0, sizeof *c);
    c->jets=c->helicopters=1;
    c->landings=c->deploymentMenu=1;
    c->pilotsPerTeam=6; c->findRadius=120; c->jetSpeed=86.9444f;
    c->jetHeight=350; c->jetCeiling=600; c->chopperHeight=110; c->jetOrbit=300; c->chopperOrbit=70;
    c->waitSeconds=8; c->attackSeconds=8; c->jetAreaMargin=900;   // (V34: 900 m - V30 600 m, V29 350 m)
    c->yawSign=c->pitchSign=c->rollSign=1;
}
// Primary weapons and muzzle offsets from supplied VehicleData.lua. VU part
// indexes are deliberately absent: they are not native object offsets.
static const BMAirProfile profiles[] = {
    {"F/A-18E",BM_AIR_JET,900,0,{0,0.880f,6.540f},1},
    {"A-10",BM_AIR_JET,900,0,{0,1.534f,5.110f},1},
    {"Su-25TM",BM_AIR_JET,900,0,{0,1.031f,3.853f},1},
    {"Su-35/Su-37",BM_AIR_JET,900,0,{0,1.549f,8.190f},1},
    {"F-35B",BM_AIR_JET,900,0,{-0.001f,1.025f,5.963f},1},
    {"AH-1Z",BM_AIR_ATTACK,300,0,{3.374f,0.258f,1.802f},2},
    {"Mi-28",BM_AIR_ATTACK,300,0,{0.006f,0.499f,1.427f},2},
    {"AH-6",BM_AIR_SCOUT,900,0,{0.453f,-0.062f,0.848f},1},
    {"Z-11W",BM_AIR_SCOUT,900,0,{0.495f,-0.199f,2.158f},1},
    {"Ka-60",BM_AIR_TRANSPORT,0,0,{0,0,0},3},
    {"UH-1Y",BM_AIR_TRANSPORT,0,0,{0,0,0},3}
};
const BMAirProfile *bm_air_profile(const char *name)
{
    char n[160]; unsigned j=0;
    if (!name) return NULL;
    for (unsigned i=0; name[i] && j+1<sizeof n; i++)
        if (isalnum((unsigned char)name[i])) n[j++]=(char)toupper((unsigned char)name[i]);
    n[j]=0;
    // Explicit known flyable families. AC130, MAV, UH60 and unknown aircraft
    // are not silently routed through a controller for a different airframe.
    if (strstr(n,"F16") || strstr(n,"F18") || strstr(n,"FA18")) return &profiles[0];
    if (strstr(n,"A10THUNDERBOLT") || !strcmp(n,"A10")) return &profiles[1];
    if (strstr(n,"SU25")) return &profiles[2];
    if (strstr(n,"SU35") || strstr(n,"SU37")) return &profiles[3];
    if (strstr(n,"F35B") || !strcmp(n,"F35")) return &profiles[4];
    if (strstr(n,"AH1Z") || strstr(n,"VIPER")) return &profiles[5];   // Caspian V4: the live US Viper is named "Viper"
    if (strstr(n,"MI28") || strstr(n,"HAVOC")) return &profiles[6];
    if (strstr(n,"AH6LITTLEBIRD") || strstr(n,"LITTLEBIRD")) return &profiles[7];
    if (strstr(n,"Z11W") || strstr(n,"WZ11")) return &profiles[8];   // AltBotMod V14: the live RU scout is named "Wz-11" (V5-V13 missed it)
    if (strstr(n,"KA60") || strstr(n,"KASATKA")) return &profiles[9];
    if (strstr(n,"VENOM") || strstr(n,"UH1Y")) return &profiles[10];
    return NULL;
}
int bm_air_allowed(const BMAirConfig *c, const BMAirProfile *p)
{ return c && p && (p->kind==BM_AIR_JET ? c->jets : c->helicopters); }

int bm_air_transform_valid(const float *m)
{
    if (!m) return 0;
    // Native affine vectors use xyz; their fourth SIMD lanes are never read
    // by flight control. Unused padding cannot veto a parked aircraft.
    for (int row=0;row<4;row++)for(int k=0;k<3;k++)if(!isfinite(m[row*4+k]))return 0;
    for (int i=0;i<3;i++) {
        float n=dot(m+4*i,m+4*i);
        if (n<0.8f || n>1.2f || fabsf(m[12+i])>100000) return 0;
    }
    float r[3]; cross(m,m+4,r);
    return fabsf(dot(m,m+4))<0.15f && fabsf(dot(m,m+8))<0.15f &&
           fabsf(dot(m+4,m+8))<0.15f && dot(r,m+8)>0.8f;
}
void bm_air_start(BMAirController *s, enum BMAirKind kind, int flying)
{
    memset(s,0,sizeof *s); s->kind=kind;
    s->phase=flying ? BM_FLIGHT_CRUISE : BM_FLIGHT_TAKEOFF;
    s->timer=kind==BM_AIR_JET ? 12 : 6;
}
// FunBots PidController.lua: anti-windup, clamping and integral direction reset.
// dt scaling preserves its per-update increments at our native 30-Hz cadence.
float bm_air_pid(BMAirPid *s, float e, float kp, float ki, float kd, float dt)
{
    if (!s) return 0;
    if (!isfinite(e) || !isfinite(dt) || dt<=0 || dt>0.25f) { memset(s,0,sizeof *s); return 0; }
    float scale=30*dt, inc=ki*e*scale;
    float integral=s->integral+inc;
    float out=kp*e+kd*(e-s->last)/scale+integral;
    s->last=e;
    if (out>1 || out<-1) integral-=inc;
    if (e*integral<0) integral=0;
    s->integral=clamp(integral,-2,2);
    return clamp(out,-1,1);
}
void bm_air_motion(BMAirMotion *s, const float p[3], float dt)
{
    if (!finite3(p) || !isfinite(dt) || dt<=0 || dt>0.25f) { memset(s,0,sizeof *s); return; }
    if (!s->initialized) { memcpy(s->anchor,p,12); s->initialized=1; return; }
    s->elapsed+=dt;
    if (s->elapsed<0.19f) return;
    float v[3]; for (int k=0;k<3;k++) v[k]=(p[k]-s->anchor[k])/s->elapsed;
    // Teleports / stale transforms cannot become a 1000-m/s flight estimate.
    if (dot(v,v)>400*400) { memset(s->velocity,0,12); s->ready=0; }
    else {
        for (int k=0;k<3;k++) s->velocity[k]=s->ready ? 0.6f*s->velocity[k]+0.4f*v[k] : v[k];
        s->ready=1;
    }
    memcpy(s->anchor,p,12); s->elapsed=0;
}
int bm_air_intercept(const float origin[3],const float target[3],const float v[3],
                     float speed,float gravity,float aim[3],float *time)
{
    if (!origin || !target || !v || !aim || !finite3(origin) || !finite3(target) || !finite3(v) || !isfinite(speed) ||
        speed<=0 || !isfinite(gravity) || gravity<0) return 0;
    float r[3]; for (int k=0;k<3;k++) r[k]=target[k]-origin[k];
    double a=(double)dot(v,v)-(double)speed*speed, b=2.0*dot(r,v), c=dot(r,r), t=-1;
    // Guard the degenerate/negative-root cases missing in the Lua quadratic.
    if (fabs(a)<0.0001) { if (fabs(b)>0.0001) t=-c/b; else if (c<0.0001) t=0; }
    else {
        double discriminant=b*b-4*a*c;
        if (discriminant>=0) {
            double root=sqrt(discriminant), t1=(-b-root)/(2*a), t2=(-b+root)/(2*a);
            if (t1>=0) t=t1;
            if (t2>=0 && (t<0 || t2<t)) t=t2;
        }
    }
    if (t<0 || !isfinite(t) || t>5) return 0;
    for (int k=0;k<3;k++) aim[k]=target[k]+v[k]*(float)t;
    aim[1]+=0.5f*gravity*(float)(t*t);
    if (time) *time=(float)t;
    return finite3(aim);
}
int bm_air_deviation(const BMAirObservation *o,const float target[3],float *yaw,float *pitch)
{
    float to[3], c[3];
    for (int k=0;k<3;k++) to[k]=target[k]-o->position[k];
    float len=sqrtf(dot(to,to));
    if (!isfinite(len) || len<0.001f) { *yaw=*pitch=0; return 0; }
    for (int k=0;k<3;k++) to[k]/=len;
    cross(o->forward,to,c);
    *yaw=atan2f(dot(c,o->up),clamp(dot(o->forward,to),-1,1));
    *pitch=asinf(clamp(dot(c,o->right),-1,1));
    return isfinite(*yaw) && isfinite(*pitch);
}
static void recover(BMAirController *s,int air)
{
    s->phase=BM_FLIGHT_RECOVER; s->timer=air ? 4 : (s->kind==BM_AIR_JET ? 6 : 3);   // V20: helicopters back in after 3 s (6)
    memset(&s->yaw,0,sizeof s->yaw); memset(&s->tilt,0,sizeof s->tilt);
    memset(&s->roll,0,sizeof s->roll); s->attackTime=0; s->shotTime=0;
}
// ---------------------------------------------------------------------------------------------------------------
// AltBotMod V20 - air combat. Ideas from the Fun-Bots dev branch (VehicleJetControl.lua, 2026-10: collision
// evasion, extending after a pass, wings-level pull-out, PID gun tracking, distance-scaled fire window), plus
// strike runs on ground targets (roll in from an initial point, dive, guns + rockets, pull out, extend, come
// round from the other side) and missile locks. V19 jets flew straight at the enemy's lead point: two jets on
// each other met head-on and rammed, and against the ground they flew one shallow pass and recovered.
// ---------------------------------------------------------------------------------------------------------------
#define JET_PULL_TIME      2.2f    // s of the current sink rate looked ahead for the pull-out
#define JET_FLOOR          95.0f   // m above the ground a planned pull-out keeps
#define JET_AVOID          50.0f   // closest approach to any aircraft within JET_AVOID_TIME
#define JET_AVOID_HEADON   150.0f  // ... when closing head-on faster than JET_HEADON_SPEED
#define JET_AVOID_TIME     2.5f
#define JET_HEADON_SPEED   120.0f
#define JET_GUN_RANGE      1200.0f
#define JET_BREAK_RANGE    380.0f  // a head-on gun pass ends here: break, never ram
#define JET_OVERSHOOT      160.0f
#define JET_EXTEND_MIN     3.0f
#define JET_EXTEND_MAX     5.0f    // (V29: 5 s, was 7 - at 200+ m/s an extend left the map)
#define JET_IP_DISTANCE    1700.0f // initial point of a strike run, from the target (V29: clamped into the jet's area)
#define JET_DIVE_SPEED     112.0f
#define JET_BURST          2.0f    // cannon: 2 s on, 0.6 s off (overheat)
#define JET_STALL_SPEED    55.0f   // AltBotMod V34: slower than this (m/s) a jet is falling, not flying ...
#define JET_STALL_EXIT     85.0f   // ... until it is this fast again
#define JET_BURST_CYCLE    2.6f
#define AIR_LOCK_TIME      2.8f    // s on the nose, missile selected, before it is fired (V22: 2.0 - and the missile is now selected for it)
#define AIR_SECONDARY_WAIT 10.0f
static const char *tactic_names[BM_TACTIC_COUNT]={"none","pursues","guns","breaks off","extends","sets up a strike run on",
    "dives on","pulls out","evades traffic","strafes","turns back into the combat area","recovers speed (nose down)"};
const char *bm_air_tactic_name(int t){ return t>=0 && t<BM_TACTIC_COUNT ? tactic_names[t] : "?"; }
// Time-based PID (Fun-Bots PidController semantics: Ki per error*second, Kd per error/second).
static float pid_time(BMAirPid *s,float e,float kp,float ki,float kd,float dt)
{
    if(!isfinite(e) || !isfinite(dt) || dt<=0 || dt>0.25f){memset(s,0,sizeof *s);return 0;}
    float d=(e-s->last)/dt; s->last=e;
    float integral=clamp(s->integral+ki*e*dt,-0.6f,0.6f);
    float out=kp*e+integral+kd*d;
    if(out>-1 && out<1) s->integral=integral;          // anti-windup
    return clamp(out,-1,1);
}
// World-frame break point: 150 m ahead, 150 m to the side `side` (horizontal unit vector, or NULL: the right),
// 120 m up or down. Body axes would put "right" under the horizon in a bank.
static void break_point(const BMAirObservation *o,const float *side,int climb,float point[3])
{
    float len=hypotf(o->forward[0],o->forward[2]);
    float fx=len>0.05f ? o->forward[0]/len : 0, fz=len>0.05f ? o->forward[2]/len : 1;
    float sx=fz, sz=-fx;                                      // horizontal right of the heading
    if(side){ sx=side[0]; sz=side[2]; }
    point[0]=o->position[0]+fx*150+sx*150; point[2]=o->position[2]+fz*150+sz*150;
    point[1]=o->position[1]+(climb ? 120 : -120);
}
// Fun-Bots _GetEvasionPoint: any aircraft passing closer than JET_AVOID (head-on: JET_AVOID_HEADON) within
// JET_AVOID_TIME. Break away from where it will be at the closest approach (dead head-on: to the right); the
// higher one climbs, the lower one dives. Both sides compute the mirror image, so they part. 0 = no threat.
static int jet_evasion(const BMAirObservation *o,float point[3])
{
    float best=JET_AVOID_TIME, miss[3]={0,0,0}; int threat=0, climb=1;
    for(int k=0;k<o->trafficCount && k<BM_AIR_TRAFFIC;k++){
        const float *t=o->traffic[k]; if(!finite3(t) || !finite3(t+3)) continue;
        float r[3],v[3]; for(int a=0;a<3;a++){r[a]=t[a]-o->position[a]; v[a]=t[3+a]-o->velocity[a];}
        float range2=dot(r,r), speed2=dot(v,v);
        if(range2<1 || range2>1200*1200 || speed2<1) continue;
        float time=-dot(r,v)/speed2; if(time<=0 || time>=best) continue;
        float m[3]; for(int a=0;a<3;a++) m[a]=r[a]+v[a]*time;
        float closing=-dot(r,v)/sqrtf(range2), limit=closing>JET_HEADON_SPEED ? JET_AVOID_HEADON : JET_AVOID;
        if(dot(m,m)<limit*limit){ best=time; threat=1; climb=r[1]<=0; memcpy(miss,m,12); }
    }
    if(!threat) return 0;
    float h=hypotf(miss[0],miss[2]);
    if(h>10){ float side[3]={-miss[0]/h,0,-miss[2]/h}; break_point(o,side,climb,point); }
    else break_point(o,NULL,climb,point);
    return 1;
}
// Steering towards `aim`.
//  mode 0: the V9 bank-and-pull guidance (plain cruising, unchanged).
//  mode 1: lift vector on the point (Fun-Bots dev "roll into the turn"): roll until the point lies straight
//          "up" in the cockpit, then pull. Holds height in hard turns: a point below the horizon gets a bank
//          beyond 90 deg instead of a climbing turn.
//  mode 2: as 1, but PID gun tracking once the point is within 0.35 rad of the nose (the lead point moves all
//          the time; P alone lags behind it and swings across it).
static void jet_steer(BMAirController *s,const BMAirObservation *o,const float aim[3],int mode,float bankLimit,
                      float pitchMin,float pitchMax,float dt,BMAirOutput *out)
{
    bm_air_deviation(o,aim,&out->yawError,&out->pitchError);
    float roll=atan2f(o->right[1],o->up[1]);
    if(mode==2 && fabsf(out->yawError)<0.35f && fabsf(out->pitchError)<0.35f && fabsf(roll)<1.4f){
        out->pitch=pid_time(&s->aimPitch,out->pitchError,5,4,0.4f,dt);
        out->yaw=pid_time(&s->aimYaw,out->yawError,5,4,0.4f,dt);
        out->roll=clamp(-3*out->yawError,-1,1);
        return;
    }
    s->aimPitch.integral=s->aimYaw.integral=0;
    if(mode>=1){
        float to[3]; for(int k=0;k<3;k++) to[k]=aim[k]-o->position[k];
        float len=sqrtf(dot(to,to)); if(len<1){out->roll=out->pitch=out->yaw=0;return;}
        for(int k=0;k<3;k++) to[k]/=len;
        float fwd=dot(to,o->forward), lat=dot(to,o->right), vert=dot(to,o->up);
        float off=acosf(clamp(fwd,-1,1));                       // angle off the nose
        float phi=atan2f(lat,vert);                            // 0: the point is straight "up" in the cockpit
        if(off<0.6f && fabsf(phi)>2.0f){
            // A little below the nose: no roll over the top - wings towards level, push.
            out->roll=clamp(-2*roll,-1,1);
            out->pitch=clamp(off*2.5f,0,0.6f);
            out->yaw=clamp(lat*2,-0.5f,0.5f);
        } else {
            out->roll=clamp(-1.6f*phi,-1,1);
            float lift=cosf(phi);                              // pull only with the lift vector on the point
            out->pitch=lift>0 ? -clamp(off*2.5f,0,1)*lift : 0;
            out->yaw=clamp(lat*1.5f,-0.5f,0.5f);
        }
        // A bank limit for the gentler manoeuvres; the flight path pitch band bounds climbs and dives.
        if(fabsf(roll)>bankLimit){                             // beyond the limit: roll back to it (never freeze)
            float back=-(roll-(roll>0 ? bankLimit : -bankLimit))*2;
            if(roll*out->roll>0 || fabsf(back)>fabsf(out->roll)) out->roll=clamp(back,-1,1);
            if(fabsf(roll)>bankLimit+0.4f && out->pitch<0) out->pitch*=0.3f;   // no pulling towards the ground meanwhile
        }
        float path=asinf(clamp(o->forward[1],-1,1));
        if(path>pitchMax && out->pitch<0) out->pitch*=fabsf(roll)<1.2f ? 0.2f : 1;
        if(path<pitchMin) { out->pitch=fminf(out->pitch,-0.4f); if(fabsf(roll)>1.0f) out->roll=clamp(-2*roll,-1,1); }
        return;
    }
    float dx=aim[0]-o->position[0],dz=aim[2]-o->position[2],horizontal=hypotf(dx,dz);
    float heading=atan2f(o->forward[0],o->forward[2]);
    float headingError=horizontal>1 ? wrap(atan2f(dx,dz)-heading) : 0;
    float desiredPitch=clamp(atan2f(aim[1]-o->position[1],fmaxf(100,horizontal)),pitchMin,pitchMax);
    if(fabsf(headingError)>1.2f) desiredPitch=clamp(desiredPitch,-0.12f,0.12f);
    float desiredRoll=-clamp(headingError*0.85f,-bankLimit,bankLimit);
    if(fabsf(roll)>1.2f) desiredRoll=0;
    out->roll=clamp(wrap(desiredRoll-roll)*2,-1,1);
    float guideYaw=heading+clamp(headingError,-1.0f,1.0f);
    float guide[3]={o->position[0]+sinf(guideYaw)*250,o->position[1]+tanf(desiredPitch)*250,o->position[2]+cosf(guideYaw)*250};
    float bodyYaw,bodyPitch; bm_air_deviation(o,guide,&bodyYaw,&bodyPitch);
    out->pitch=clamp(bodyPitch*3,-1,1);
    out->yaw=clamp(bodyYaw*0.35f,-0.5f,0.5f);
}
static void jet_throttle(BMAirController *s,float want,float speed,float dt,BMAirOutput *out)
{
    float t=bm_air_pid(&s->throttle,want-speed,3,0.05f,0.2f,dt);
    out->throttle=fmaxf(0,t); out->brake=fmaxf(0,-t);
}
// Wings level, then pull. Returns 1 while it still has to pull.
static int jet_pull(const BMAirObservation *o,BMAirOutput *out)
{
    float roll=atan2f(o->right[1],o->up[1]);
    out->roll=clamp(-2*roll,-1,1);
    out->pitch=fabsf(roll)<1.2f ? -1 : 0;
    out->yaw=0; out->throttle=1; out->brake=0;
    return 1;
}
static void jet_extend_start(BMAirController *s,int pullup,int tactic)
{
    s->phase=BM_FLIGHT_EXTEND; s->extendTime=0; s->pullup=pullup; s->tactic=tactic;
    s->attackTime=s->diveTime=s->burstTime=0; s->ipValid=0; s->ipStage=0; s->lockTime=0;
    memset(&s->aimPitch,0,sizeof s->aimPitch); memset(&s->aimYaw,0,sizeof s->aimYaw);
}
// Missile lock (air targets) / rocket salvo (ground targets, once per dive). Sets out->secondary.
static void air_secondary(BMAirController *s,int ready,float dt,BMAirOutput *out)
{
    if(ready) s->lockTime+=dt; else s->lockTime=fmaxf(0,s->lockTime-2*dt);
    // AltBotMod V22: the seeker locks only while the missile is the weapon in hand - it is selected from the first moment on
    // the nose and held through the lock (V20-V21.1 selected it 0.1 s before firing: no lock, no missile ever left)
    out->armSecondary=s->secondaryWait<=0 && s->lockTime>0;
    if(s->secondaryWait<=0 && s->lockTime>=AIR_LOCK_TIME){ out->secondary=1; out->armSecondary=1; s->secondaryWait=AIR_SECONDARY_WAIT; s->lockTime=0; }
}
// AltBotMod V29: 1 while the jet is turning back into its area (out->* set). V30: the turn starts 3 s before the edge (V29: 6 s)
// and ends 200 m inside heading in; the area is wider (JetAreaMarginMeters 600, at least 1000 m each way from its middle).
static int jet_area_inside(const BMAirObservation *o,float x,float z,float inset)
{
    return x>o->areaMin[0]+inset && x<o->areaMax[0]-inset && z>o->areaMin[1]+inset && z<o->areaMax[1]-inset;
}
static int jet_area_tick(BMAirController *s,const BMAirObservation *o,const float goal[3],float top,float floorY,float dt,BMAirOutput *out)
{
    float lead=3.0f, px=o->position[0]+o->velocity[0]*lead, pz=o->position[2]+o->velocity[2]*lead;
    float cx=0.5f*(o->areaMin[0]+o->areaMax[0]), cz=0.5f*(o->areaMin[1]+o->areaMax[1]);
    int outNow=!jet_area_inside(o,o->position[0],o->position[2],0), outSoon=!jet_area_inside(o,px,pz,0);
    if(!s->returning && (outNow || outSoon)) s->returning=1;
    if(s->returning){
        float vx=cx-o->position[0], vz=cz-o->position[2], inward=o->velocity[0]*vx+o->velocity[2]*vz;
        if(jet_area_inside(o,o->position[0],o->position[2],200) && jet_area_inside(o,px,pz,0) && inward>0) s->returning=0;   // (V30: 200 m in, heading in - V29 asked 300 m and 150 m)
    }
    if(!s->returning) return 0;
    if(s->phase==BM_FLIGHT_EXTEND || s->phase==BM_FLIGHT_APPROACH || s->phase==BM_FLIGHT_ATTACK){ s->phase=BM_FLIGHT_CRUISE; s->pullup=0; }
    s->ipValid=0; s->lockTime=0; s->attackTime=s->diveTime=0;
    float point[3]={cx,fminf(top,fmaxf(goal[1],floorY+JET_FLOOR+150)),cz};
    jet_steer(s,o,point,1,1.4f,-0.15f,0.35f,dt,out);
    jet_throttle(s,JET_DIVE_SPEED,hypotf(o->velocity[0],o->velocity[2]),dt,out);   // (a tight turn: not too fast)
    out->fire=0; out->attacking=0; out->secondary=0; out->armSecondary=0; s->tactic=BM_TACTIC_RETURN;
    return 1;
}
// Jet in CRUISE / APPROACH / ATTACK / EXTEND, no emergency. `goal` is the patrol goal (terrain/ceiling clamped).
static void jet_combat(BMAirController *s,const BMAirProfile *p,const BMAirConfig *c,const BMAirObservation *o,
                       float dt,BMAirOutput *out,int tracking,const float goal[3],float ceiling,float clearance,float speed)
{
    float floorY=o->groundValid ? o->groundY : -1e30f;
    if(o->aheadGroundValid) floorY=fmaxf(floorY,o->aheadGroundY);
    if(isfinite(o->extraClearance) && o->extraClearance>0) floorY+=fminf(o->extraClearance,90);   // V21: learned - more height here
    float predicted=o->position[1]-floorY+fminf(o->velocity[1],0)*JET_PULL_TIME;
    if(!o->groundValid && !o->aheadGroundValid) predicted=1000;
    s->secondaryWait=fmaxf(0,s->secondaryWait-dt);
    int attacking=0, ready=0;
    // 1. Terrain first: a sink rate that would take the jet under the floor ends whatever it does.
    // V20.1: a jet that is still climbing (after takeoff, over a rising hill) just keeps climbing - no pull-out
    // state, no extend. Live V20 (Caspian Border): 12 "pulls out" ~10 s after takeoff, none after a dive.
    if(predicted<JET_FLOOR && o->velocity[1]>2 && s->phase!=BM_FLIGHT_ATTACK && !(s->phase==BM_FLIGHT_EXTEND && s->pullup)){
        float len=hypotf(o->forward[0],o->forward[2]);
        float fx=len>0.05f ? o->forward[0]/len : 0, fz=len>0.05f ? o->forward[2]/len : 1;
        float point[3]={o->position[0]+fx*400,o->position[1]+160,o->position[2]+fz*400};
        jet_steer(s,o,point,1,0.8f,0.0f,0.45f,dt,out);
        out->throttle=1; out->brake=0; out->fire=0; out->attacking=0;
        return;
    }
    if(predicted<JET_FLOOR){
        if(s->phase==BM_FLIGHT_ATTACK || s->phase==BM_FLIGHT_APPROACH || s->phase==BM_FLIGHT_CRUISE) jet_extend_start(s,1,BM_TACTIC_PULLOUT);
        s->pullup=1;
    }
    if(s->phase==BM_FLIGHT_EXTEND && s->pullup){
        jet_pull(o,out); s->tactic=BM_TACTIC_PULLOUT; s->extendTime+=dt;
        if(o->velocity[1]>4 && predicted>JET_FLOOR+40) s->pullup=0;
        if(s->extendTime>8) s->pullup=0;                   // never pull for ever
        out->recovering=0; out->attacking=0; out->fire=0;
        return;
    }
    // 2. Traffic: friends and foes alike, the target included. No shots while breaking.
    float evade[3];
    if(jet_evasion(o,evade)){
        if(s->phase==BM_FLIGHT_ATTACK || s->phase==BM_FLIGHT_APPROACH) jet_extend_start(s,0,BM_TACTIC_EVADE);
        s->tactic=BM_TACTIC_EVADE;
        jet_steer(s,o,evade,1,1.6f,-0.5f,0.5f,dt,out);
        out->throttle=1; out->brake=0; out->fire=0; out->attacking=0;
        return;
    }
    float range=0,ata=PI,azimuth=PI,devYaw=0,devPitch=0,r[3]={0,0,0};
    float top=ceiling-25; if(o->position[1]>ceiling) top=ceiling-80;   // above the ceiling: every aim point lies below it
    // 2b. AltBotMod V29: the combat area. Out of the box, or out of it within 6 s at this speed and heading: turn back
    // towards its middle at the patrol height, whatever the jet was doing (an extend, a strike run's set-up, a chase after a
    // target that flew out) - until it is 300 m inside and headed in. V20-V28 extended up to 7 s straight on and set strike
    // runs up 1.7 km from the target, and jets flew off the map and died ("they just fly away and die out of bounds").
    if(o->areaValid && jet_area_tick(s,o,goal,top,floorY,dt,out)) return;
    if(tracking){
        for(int k=0;k<3;k++) r[k]=o->target[k]-o->position[k];
        range=sqrtf(dot(r,r));
        ata=range>1 ? acosf(clamp(dot(o->forward,r)/range,-1,1)) : 0;
        bm_air_deviation(o,out->aim,&devYaw,&devPitch);
        float heading=atan2f(o->forward[0],o->forward[2]);
        azimuth=hypotf(r[0],r[2])>1 ? fabsf(wrap(atan2f(r[0],r[2])-heading)) : 0;
    }
    // 3. Extend: away from the fight, level at the patrol height, full throttle; then come round again.
    if(s->phase==BM_FLIGHT_EXTEND){
        s->extendTime+=dt;
        float len=hypotf(o->forward[0],o->forward[2]);
        float fx=len>0.05f ? o->forward[0]/len : 0, fz=len>0.05f ? o->forward[2]/len : 1;
        float point[3]={o->position[0]+fx*400,fminf(top,fmaxf(goal[1],floorY+JET_FLOOR+120)),o->position[2]+fz*400};
        if(s->tactic!=BM_TACTIC_EVADE && s->tactic!=BM_TACTIC_BREAK) s->tactic=BM_TACTIC_EXTEND;
        jet_steer(s,o,point,1,1.0f,-0.15f,0.30f,dt,out);
        out->throttle=1; out->brake=0;
        float away=tracking ? (o->targetAir ? 750.0f : 1100.0f) : 0;
        if(s->extendTime>=JET_EXTEND_MAX || (s->extendTime>=JET_EXTEND_MIN && (!tracking || range>away))){
            s->phase=tracking ? BM_FLIGHT_APPROACH : BM_FLIGHT_CRUISE; s->tactic=BM_TACTIC_NONE; s->passes++;
        }
        return;
    }
    if(!tracking){
        if(s->phase==BM_FLIGHT_APPROACH || s->phase==BM_FLIGHT_ATTACK) s->phase=BM_FLIGHT_CRUISE;
        s->tactic=BM_TACTIC_NONE; s->lockTime=0; s->ipValid=0;
        float g[3]; memcpy(g,goal,12); g[1]=fminf(g[1],top);
        if(o->areaValid){   // (V29: the patrol goal inside the combat area too)
            g[0]=clamp(g[0],fminf(o->areaMin[0]+300,0.5f*(o->areaMin[0]+o->areaMax[0])),fmaxf(o->areaMax[0]-300,0.5f*(o->areaMin[0]+o->areaMax[0])));
            g[2]=clamp(g[2],fminf(o->areaMin[1]+300,0.5f*(o->areaMin[1]+o->areaMax[1])),fmaxf(o->areaMax[1]-300,0.5f*(o->areaMin[1]+o->areaMax[1])));
        }
        jet_steer(s,o,g,0,1.0f,-0.20f,0.25f,dt,out);
        jet_throttle(s,c->jetSpeed,speed,dt,out);
        return;
    }
    if(o->targetAir){
        // ---- Dogfight -------------------------------------------------------------------------------------
        float tv[3]; memcpy(tv,o->targetVelocity,12);
        float rel[3]; for(int k=0;k<3;k++) rel[k]=tv[k]-o->velocity[k];
        float closing=range>1 ? -dot(r,rel)/range : 0, tspeed=sqrtf(dot(tv,tv));
        float aspect=tspeed>5 && range>1 ? -dot(tv,r)/(tspeed*range) : 0;      // 1: its nose is on us
        int headOn=aspect>0.8f && ata<0.7f && closing>JET_HEADON_SPEED;
        if(range<JET_OVERSHOOT || (headOn && range<JET_BREAK_RANGE) || (closing>30 && range<closing*2.0f)){   // < 2 s to impact: break
            // Break right; the higher jet climbs, the lower one dives (the other side does the same: they part).
            jet_extend_start(s,0,BM_TACTIC_BREAK);
            float point[3], h=hypotf(r[0],r[2]);
            if(h>1 && !headOn){ float side[3]={-r[0]/h,0,-r[2]/h}; break_point(o,side,r[1]<=0,point); }
            else break_point(o,NULL,r[1]<=0,point);
            jet_steer(s,o,point,1,1.6f,-0.5f,0.5f,dt,out);
            out->throttle=1; out->brake=0;
            return;
        }
        if(o->targetVisible && range<JET_GUN_RANGE && ata<1.05f){
            s->phase=BM_FLIGHT_ATTACK; s->tactic=BM_TACTIC_GUNS; attacking=1;
            jet_steer(s,o,out->aim,2,3.2f,-0.8f,0.8f,dt,out);
            jet_throttle(s,c->jetSpeed+(range>700 ? 20 : 0),speed,dt,out);
        } else {
            // Pursuit: pure pursuit while far off the nose (the tightest turn), lead pursuit once it comes round.
            s->phase=BM_FLIGHT_APPROACH; s->tactic=BM_TACTIC_PURSUE;
            const float *point=ata<0.8f ? out->aim : o->target;
            float aim[3]; memcpy(aim,point,12); aim[1]=fminf(aim[1],top);
            jet_steer(s,o,aim,1,3.2f,-0.6f,0.6f,dt,out);
            jet_throttle(s,c->jetSpeed+clamp((range-900)*0.05f,0,45),speed,dt,out);
        }
        float window=clamp(atanf(50/fmaxf(range,1)),0.1f,0.3f);
        ready=attacking && devYaw*devYaw+devPitch*devPitch<window*window;
        air_secondary(s,attacking && fabsf(devYaw)<0.3f && fabsf(devPitch)<0.3f && range>250 && range<1600,dt,out);
    } else {
        // ---- Strike run on a ground target ----------------------------------------------------------------
        float horizontal=hypotf(r[0],r[2]), height=o->position[1]-o->target[1];
        float diveAngle=atan2f(height,fmaxf(horizontal,1));
        if(s->phase==BM_FLIGHT_ATTACK){
            s->diveTime+=dt; s->tactic=BM_TACTIC_DIVE; attacking=1;
            // (V26: a deeper, longer run - out at 200 m / 50 m up / 12 s, was 280 / 70 / 10 - so the guns get their time)
            if(predicted<JET_FLOOR+20 || range<200 || height<50 || s->diveTime>12 || ata>1.1f ||
               (!o->targetVisible && s->diveTime>2.5f)){
                jet_extend_start(s,1,BM_TACTIC_PULLOUT); jet_pull(o,out); return;
            }
            float aim[3]; memcpy(aim,out->aim,12);
            jet_steer(s,o,aim,2,1.4f,-0.9f,0.2f,dt,out);
            jet_throttle(s,JET_DIVE_SPEED,speed,dt,out);
            float window=clamp(atanf(30/fmaxf(range,1)),0.06f,0.3f);   // (V26: 30 m, was 25)
            ready=o->targetVisible && range<1400 && devYaw*devYaw+devPitch*devPitch<window*window;
            // AltBotMod V34: no secondary on a ground target - a bot's jet carries the stock heat seekers (air-to-air: they
            // never locked a tank and flew off unguided, V20-V33). Guns on the ground, missiles on aircraft.
            (void)0;
        } else {
            s->phase=BM_FLIGHT_APPROACH;
            // Roll in when the target sits on the nose at a usable dive angle.
            // AltBotMod V26: roll in whenever the target is ahead (within 34 deg), 300 m-2.5 km off, 3-46 deg down: V20-V25
            // needed it within 20 deg at 6-46 deg down, so most set-ups were flown round again (V25 live: 178 set-ups, 50 dives)
            if(o->targetVisible && range>300 && range<2500 && diveAngle>0.05f && diveAngle<0.8f && azimuth<0.6f &&
               predicted>JET_FLOOR+30){
                s->phase=BM_FLIGHT_ATTACK; s->tactic=BM_TACTIC_DIVE; s->diveTime=0; s->burstTime=0;
                s->secondaryThisPass=0; attacking=1;
                jet_steer(s,o,out->aim,2,1.4f,-0.9f,0.2f,dt,out);
                jet_throttle(s,JET_DIVE_SPEED,speed,dt,out);
                float window=clamp(atanf(25/fmaxf(range,1)),0.05f,0.25f);
                ready=range<1300 && devYaw*devYaw+devPitch*devPitch<window*window;
            } else {
                s->tactic=BM_TACTIC_SETUP;
                if(!s->ipValid){
                    // The initial point lies on this side of the target, swung 30 deg round each pass so the runs
                    // come in from changing directions; the height stays in a band above the target.
                    float ux=-r[0],uz=-r[2],len=hypotf(ux,uz);
                    if(len<1){ux=-o->forward[0];uz=-o->forward[2];len=fmaxf(0.1f,hypotf(ux,uz));}
                    ux/=len; uz/=len;
                    float a=(s->passes&1 ? 0.52f : -0.52f), ca=cosf(a), sa=sinf(a);
                    s->ip[0]=o->target[0]+(ux*ca-uz*sa)*JET_IP_DISTANCE;
                    s->ip[2]=o->target[2]+(ux*sa+uz*ca)*JET_IP_DISTANCE;
                    if(o->areaValid){   // (V29: inside the combat area, 400 m from its edge)
                        s->ip[0]=clamp(s->ip[0],fminf(o->areaMin[0]+400,0.5f*(o->areaMin[0]+o->areaMax[0])),fmaxf(o->areaMax[0]-400,0.5f*(o->areaMin[0]+o->areaMax[0])));
                        s->ip[2]=clamp(s->ip[2],fminf(o->areaMin[1]+400,0.5f*(o->areaMin[1]+o->areaMax[1])),fmaxf(o->areaMax[1]-400,0.5f*(o->areaMin[1]+o->areaMax[1])));
                    }
                    s->ip[1]=clamp(o->position[1],o->target[1]+200,o->target[1]+350);   // (V26: 200-350 m up, was 300-450)
                    s->ip[1]=fminf(s->ip[1],top);
                    s->ipValid=1; s->ipStage=0;
                }
                float point[3];
                if(s->ipStage==0){
                    memcpy(point,s->ip,12);
                    if(hypotf(s->ip[0]-o->position[0],s->ip[2]-o->position[2])<350) s->ipStage=1;
                } else {
                    memcpy(point,o->target,12); point[1]=s->ip[1];         // turn in at the run height
                    if(horizontal<800 || horizontal>3500) s->ipValid=0;    // too close to dive: go round again
                }
                point[1]=fminf(fmaxf(point[1],floorY+JET_FLOOR+60),top);
                jet_steer(s,o,point,1,1.5f,-0.25f,0.30f,dt,out);
                jet_throttle(s,c->jetSpeed+15,speed,dt,out);
            }
        }
    }
    // Cannon: 2 s bursts with a 0.6 s pause against overheating, only while the shot passes the lead point.
    if(attacking && ready){ s->burstTime=fmodf(s->burstTime+dt,JET_BURST_CYCLE); out->fire=s->burstTime<JET_BURST; }
    else out->fire=0;
    out->attacking=attacking;
    (void)p;(void)clearance;
}
int bm_air_step(BMAirController *s,const BMAirProfile *p,const BMAirConfig *c,
                const BMAirObservation *o,float dt,BMAirOutput *out)
{
    if (!out) return 0;
    memset(out,0,sizeof *out);
    if (!s || !p || !c || !o) return 0;
    float matrix[16]={0}; memcpy(matrix,o->right,12); memcpy(matrix+4,o->up,12);
    memcpy(matrix+8,o->forward,12); memcpy(matrix+12,o->position,12); matrix[15]=1;
    if (!p || !bm_air_transform_valid(matrix) || !finite3(o->goal) || !finite3(o->velocity) ||
        !isfinite(dt) || dt<=0 || dt>0.25f ||
        !isfinite(o->obstacleDistance) ||
        (o->groundValid && !isfinite(o->groundY)) ||
        (o->aheadGroundValid && !isfinite(o->aheadGroundY))) return 0;
    int jet=p->kind==BM_AIR_JET;
    float clearance=o->groundValid ? o->position[1]-o->groundY : 1000;
    float speed=sqrtf(dot(o->velocity,o->velocity));
    int landing=!jet && o->landing && p->kind==BM_AIR_TRANSPORT;
    float groundMinimum=jet ? 60 : (landing ? 0.6f : o->pickup ? 3 : 20);
    float learnedExtra=isfinite(o->extraClearance) ? fminf(fmaxf(o->extraClearance,0),90) : 0;   // V21: learned
    if(!landing && !o->pickup) groundMinimum+=learnedExtra;
    int hazard=(o->groundValid && clearance<groundMinimum && s->phase!=BM_FLIGHT_TAKEOFF) ||
               (o->obstacleDistance>0 && o->obstacleDistance<fmaxf(20,speed*2));
    float goal[3]; memcpy(goal,o->goal,12);
    if (o->groundValid) goal[1]=fmaxf(goal[1],o->groundY+groundMinimum+(landing?0.5f:o->pickup?1:20));
    if (o->aheadGroundValid) goal[1]=fmaxf(goal[1],o->aheadGroundY+groundMinimum+(landing?0.5f:o->pickup?1:20));
    float ceiling=fmaxf(o->goal[1]+50,o->goal[1]+c->jetCeiling-c->jetHeight);
    if (o->groundValid) ceiling=fmaxf(ceiling,o->groundY+groundMinimum+60);
    if (o->aheadGroundValid) ceiling=fmaxf(ceiling,o->aheadGroundY+groundMinimum+60);
    int aboveCeiling=jet && o->position[1]>ceiling;
    if (jet) goal[1]=fminf(goal[1],ceiling-25);
    if (jet && o->areaValid) {   // AltBotMod V29: a jet's goal lies inside its area (300 m in) - its patrol never leads it out
        float cx=0.5f*(o->areaMin[0]+o->areaMax[0]), cz=0.5f*(o->areaMin[1]+o->areaMax[1]);
        goal[0]=clamp(goal[0],fminf(o->areaMin[0]+300,cx),fmaxf(o->areaMax[0]-300,cx));
        goal[2]=clamp(goal[2],fminf(o->areaMin[1]+300,cz),fmaxf(o->areaMax[1]-300,cz));
    }
    if (s->phase==BM_FLIGHT_TAKEOFF || s->phase==BM_FLIGHT_RECOVER) {
        s->timer-=dt;
        if (s->timer<=0 && (!o->groundValid || clearance>groundMinimum)) s->phase=BM_FLIGHT_CRUISE;
        if (jet && (aboveCeiling || (s->phase==BM_FLIGHT_TAKEOFF && clearance>=100 && speed>=35))) s->phase=BM_FLIGHT_CRUISE;
    }
    float muzzle[3];
    for (int k=0;k<3;k++) muzzle[k]=o->position[k]+o->right[k]*p->muzzle[0]+o->up[k]*p->muzzle[1]+o->forward[k]*p->muzzle[2];
    float range=0,pitchToTarget=0;
    // AltBotMod V22: the guns' rounds leave with the aircraft's own speed - lead by the target's velocity relative to ours
    float relativeVelocity[3];
    for (int k=0;k<3;k++) relativeVelocity[k]=o->targetVelocity[k]-(o->velocityValid && finite3(o->velocity) ? o->velocity[k] : 0);
    int tracking=c->weapons && p->speed>0 && o->targetValid && finite3(o->target) && finite3(o->targetVelocity) &&
        bm_air_intercept(muzzle,o->target,relativeVelocity,p->speed,p->gravity,out->aim,NULL);
    // AltBotMod V20: jets fight with jet_combat (dogfights, strike runs, evasion, extend); emergencies, the
    // ceiling, takeoff and recovery stay with the flight law below.
    // (V29: also a jet with nothing to fight that is leaving its area, or turning back into it - jet_area_tick)
    // AltBotMod V34: a jet below flying speed (V33 live: F/A-18s at 9-26 m/s, 900 m up, nose high in the turn back into the
    // area - pulling harder all the while, "jets don't do anything") puts its nose 20 deg down, wings level, full power
    // until it is fast again. Not on the runway, not low (the flight law's ground recovery takes that).
    if(jet && s->phase!=BM_FLIGHT_TAKEOFF && o->velocityValid){
        if(!s->stall && speed<JET_STALL_SPEED && clearance>120) s->stall=1;
        if(s->stall && (speed>JET_STALL_EXIT || clearance<60)) s->stall=0;
        if(s->stall){
            float roll=atan2f(o->right[1],o->up[1]), path=asinf(clamp(o->forward[1],-1,1));
            out->roll=clamp(-2*roll,-1,1);
            out->pitch=fabsf(roll)<1.0f ? clamp((path+0.35f)*2.0f,-0.5f,1.0f) : 0;   // (+: nose down)
            out->yaw=0; out->throttle=1; out->brake=0; out->fire=0; out->attacking=0; out->secondary=0; out->armSecondary=0;
            s->tactic=BM_TACTIC_STALL; out->tactic=s->tactic; out->recovering=1;
            out->yaw=clamp(out->yaw*c->yawSign,-1,1); out->pitch=clamp(out->pitch*c->pitchSign,-1,1); out->roll=clamp(out->roll*c->rollSign,-1,1);
            return 1;
        }
    }
    int areaCall=jet && o->areaValid && (s->returning || !jet_area_inside(o,o->position[0]+o->velocity[0]*6,o->position[2]+o->velocity[2]*6,0) ||
                                         !jet_area_inside(o,o->position[0],o->position[2],0));
    if(jet && !hazard && (!aboveCeiling || areaCall) && (tracking || s->phase==BM_FLIGHT_EXTEND || o->trafficCount>0 || areaCall) &&
       (s->phase==BM_FLIGHT_CRUISE || s->phase==BM_FLIGHT_APPROACH || s->phase==BM_FLIGHT_ATTACK || s->phase==BM_FLIGHT_EXTEND)){
        jet_combat(s,p,c,o,dt,out,tracking,goal,ceiling,clearance,speed);
        out->tactic=s->tactic; out->recovering=0;
        if(!o->velocityValid){out->throttle=1;out->brake=0;}
        out->yaw=clamp(out->yaw*c->yawSign,-1,1);
        out->pitch=clamp(out->pitch*c->pitchSign,-1,1);
        out->roll=clamp(out->roll*c->rollSign,-1,1);
        return 1;
    }
    if(s->phase==BM_FLIGHT_EXTEND) s->phase=BM_FLIGHT_CRUISE;   // a helicopter never extends; a jet in an emergency recovers
    int attack=tracking && o->targetVisible,approach=0;
    if(tracking){
        float d[3];for(int k=0;k<3;k++)d[k]=o->target[k]-o->position[k];
        range=sqrtf(dot(d,d));pitchToTarget=atan2f(d[1],hypotf(d[0],d[2]));
        // Pursuit is independent of weapon visibility/range. LOS and a safe
        // firing cone still gate every shot. Helicopters back off from a target
        // underneath instead of perpetually circling above an unshootable cone.
        if(!o->targetVisible || range>(jet?2000:p->kind==BM_AIR_ATTACK?1000:800) || range<(jet?(o->targetAir?30:100):40)){attack=0;approach=1;}   // V20: gunships 1000 m, scouts 800 m (700)
        if(!o->targetAir && (o->position[1]-o->target[1]<(jet?80:15) || fabsf(pitchToTarget)>(jet?0.65f:0.6f))){attack=0;approach=1;}   // V20: helicopters dive steeper (34 deg, was 30)
        if(!jet && pitchToTarget>0.5f){attack=0;approach=1;}
        // AltBotMod V9: helicopter against an aircraft - a gun duel at 150-800 m, never a ram. V8 flew straight at the
        // intercept point, kept closing while firing and the two met in the middle. Inside 150 m, or inside 250 m and
        // still closing at more than 8 m/s, it breaks off to its stand-off ring (approach below) and re-attacks from there.
        if(!jet && o->targetAir){
            float closing=-(d[0]*(o->targetVelocity[0]-o->velocity[0])+d[1]*(o->targetVelocity[1]-o->velocity[1])+
                            d[2]*(o->targetVelocity[2]-o->velocity[2]))/fmaxf(range,1);
            // AltBotMod V24: a duel is fought - 80 m or closing fast inside 150 m before it breaks off (V9-V23: 150 m, or 250 m
            // closing at 8 m/s; with the 350 m stand-off ring after every break the two circled and hardly fired: "choppers
            // almost never attack each other")
            if(range<80 || (range<150 && closing>15)){attack=0;approach=1;}
            else if(range<=800 && o->targetVisible && approach && range>=40 && pitchToTarget<=0.5f){attack=1;approach=0;}
        }
        if(jet){
            float heading=atan2f(o->forward[0],o->forward[2]);
            float toward=atan2f(out->aim[0]-o->position[0],out->aim[2]-o->position[2]);
            if(fabsf(wrap(toward-heading))>1.05f){attack=0;approach=1;}
        }
        if(aboveCeiling){attack=approach=0;}
    }
    if(!jet && tracking && o->targetAir && range<70)hazard=1;   // V9: about to collide - climb out first
    if(hazard){if(s->phase!=BM_FLIGHT_RECOVER)recover(s,0);attack=approach=0;}
    // V20: a helicopter keeps a ground target under fire three times as long before it repositions
    float attackCap=jet ? c->attackSeconds : c->attackSeconds*3;   // (V24: also against an aircraft - a helicopter duel is not broken off after 8 s)
    if(s->phase==BM_FLIGHT_ATTACK && (!attack || s->attackTime>=attackCap)){
        if(approach && !hazard){s->phase=BM_FLIGHT_APPROACH;s->attackTime=s->shotTime=0;}
        else recover(s,s->lastTargetAir);
    }
    if(approach && (s->phase==BM_FLIGHT_CRUISE || s->phase==BM_FLIGHT_APPROACH))s->phase=BM_FLIGHT_APPROACH;
    if(!approach && !attack && s->phase==BM_FLIGHT_APPROACH)s->phase=BM_FLIGHT_CRUISE;
    if(attack && (s->phase==BM_FLIGHT_CRUISE || s->phase==BM_FLIGHT_APPROACH)){s->phase=BM_FLIGHT_ATTACK;s->attackTime=s->shotTime=0;}
    int attacking=s->phase==BM_FLIGHT_ATTACK && attack;
    if (attacking) { s->attackTime+=dt; s->lastTargetAir=o->targetAir; }
    if (s->phase==BM_FLIGHT_TAKEOFF || s->phase==BM_FLIGHT_RECOVER) {
        // The old position+70-up goal moved upward every frame and could keep
        // the jet climbing indefinitely. Use a finite cruise/recovery height.
        float len=hypotf(o->forward[0],o->forward[2]);
        float fx=len>0.05f ? o->forward[0]/len : 0, fz=len>0.05f ? o->forward[2]/len : 1;
        if (jet) {
            goal[0]=o->position[0]+fx*350; goal[2]=o->position[2]+fz*350;
            float safe=o->groundValid ? o->groundY+120 : goal[1];
            goal[1]=fminf(ceiling-25,fmaxf(goal[1],safe));
        } else if (s->phase==BM_FLIGHT_TAKEOFF) {
            goal[0]=o->position[0]; goal[2]=o->position[2];
        }
    }
    if(s->phase==BM_FLIGHT_APPROACH && approach){
        memcpy(goal,out->aim,12);
        goal[1]=jet?fminf(o->goal[1],ceiling-25):o->goal[1];
        if(!o->targetAir && (fabsf(pitchToTarget)>(jet?0.65f:PI/6) || range<(jet?150:80))){
            float dx=o->position[0]-o->target[0],dz=o->position[2]-o->target[2],horizontal=hypotf(dx,dz);
            float standOff=jet?fmaxf(650,fabsf(o->position[1]-o->target[1])/tanf(0.45f)):
                fmaxf(140,fabsf(o->position[1]-o->target[1])/tanf(0.45f));   // V20: 140 m (180), a steeper firing angle
            if(horizontal<1){dx=-o->forward[0];dz=-o->forward[2];horizontal=fmaxf(0.1f,hypotf(dx,dz));}
            goal[0]=o->target[0]+dx/horizontal*standOff;goal[2]=o->target[2]+dz/horizontal*standOff;
        }
        if(!jet && o->targetAir){   // V9: to the stand-off ring 350 m out, 40 deg round from where it is now (not head-on)
            float dx=o->position[0]-o->target[0],dz=o->position[2]-o->target[2],horizontal=hypotf(dx,dz);
            if(horizontal<1){dx=-o->forward[0];dz=-o->forward[2];horizontal=fmaxf(0.1f,hypotf(dx,dz));}
            float ux=dx/horizontal,uz=dz/horizontal,ca=0.766f,sa=0.643f;
            goal[0]=o->target[0]+(ux*ca-uz*sa)*250;goal[2]=o->target[2]+(ux*sa+uz*ca)*250;   // (V24: 250 m, was 350)
            goal[1]=fmaxf(o->goal[1],o->target[1]+15);
        }
        if(o->groundValid)goal[1]=fmaxf(goal[1],o->groundY+groundMinimum+20);
        if(o->aheadGroundValid)goal[1]=fmaxf(goal[1],o->aheadGroundY+groundMinimum+20);
    }
    const float *aim=attacking ? out->aim : goal;
    float roll=atan2f(o->right[1],o->up[1]), pitch=asinf(clamp(o->forward[1],-1,1));
    if (jet) {
        bm_air_deviation(o,aim,&out->yawError,&out->pitchError);
        float dx=aim[0]-o->position[0],dz=aim[2]-o->position[2],horizontal=hypotf(dx,dz);
        float heading=atan2f(o->forward[0],o->forward[2]);
        float headingError=horizontal>1 ? wrap(atan2f(dx,dz)-heading) : 0;
        float desiredPitch=atan2f(aim[1]-o->position[1],fmaxf(100,horizontal));
        desiredPitch=clamp(desiredPitch,attacking ? -0.65f : -0.20f,attacking ? 0.55f : 0.25f);
        if (fabsf(headingError)>1.2f && !attacking) desiredPitch=clamp(desiredPitch,-0.12f,0.12f);
        if (s->phase==BM_FLIGHT_TAKEOFF) desiredPitch=fmaxf(0,desiredPitch);
        if (aboveCeiling) desiredPitch=-0.20f;
        if (hazard) desiredPitch=fmaxf(desiredPitch,0.30f);
        float desiredRoll=-clamp(headingError*0.85f,-1.0f,1.0f);
        if (fabsf(roll)>1.2f || hazard || aboveCeiling) desiredRoll=0;
        out->roll=clamp(wrap(desiredRoll-roll)*2,-1,1);
        // World flight-path pitch remains signed even when the waypoint is
        // behind the aircraft; cross-product asin lost that hemisphere.
        // Translate the finite world guidance direction into body axes as
        // FunBots does. World pitch alone loses the elevator needed in a bank.
        float guideYaw=heading+clamp(headingError,-1.0f,1.0f);
        float guide[3]={o->position[0]+sinf(guideYaw)*250,
            o->position[1]+tanf(desiredPitch)*250,o->position[2]+cosf(guideYaw)*250};
        float bodyYaw,bodyPitch;bm_air_deviation(o,guide,&bodyYaw,&bodyPitch);
        out->pitch=clamp(bodyPitch*3,-1,1);
        if(aboveCeiling && !hazard)out->pitch=clamp((pitch-desiredPitch)*3,-1,1)*(cosf(roll)<0 ? -1 : 1);
        // FunBots JetControl uses +deviation for rudder. Helicopters retain
        // their separate -PID yaw controller; this is not a global sign flip.
        out->yaw=clamp(bodyYaw*0.35f,-0.5f,0.5f);
        float throttle=bm_air_pid(&s->throttle,c->jetSpeed-speed,3,0.05f,0.2f,dt);
        out->throttle=fmaxf(0,throttle); out->brake=fmaxf(0,-throttle);
        if (!o->velocityValid || s->phase==BM_FLIGHT_TAKEOFF || hazard) { out->throttle=1; out->brake=0; }
        // Do not haul the nose up before there is runway speed.
        if (s->phase==BM_FLIGHT_TAKEOFF && speed<35 && out->pitch<0) out->pitch=speed>20 ? -0.15f : 0;
        if (hazard && pitch<0.3f) out->pitch=-1;
    } else {
        float dx=aim[0]-o->position[0], dz=aim[2]-o->position[2], distance=hypotf(dx,dz);
        float heading=atan2f(o->forward[0],o->forward[2]);
        out->yawError=distance>0.1f ? wrap(atan2f(dx,dz)-heading) : 0;
        float desiredPitch;
        if (attacking) desiredPitch=atan2f(aim[1]-o->position[1],distance);
        else {
            float forwardSpeed=dot(o->velocity,o->forward);
            // Keep FunBots' -0.35 cruise tilt; level/brake near a hover goal,
            // during a tight yaw turn, or above the practical cruise speed.
            desiredPitch=clamp(-0.35f*clamp(distance/80,0,1)+fmaxf(0,forwardSpeed-15)*0.02f,-0.35f,0.25f);
            desiredPitch*=clamp(1-fabsf(out->yawError)/1.5f,0,1);
            if(landing){
                float local=dx*o->forward[0]+dz*o->forward[2];
                desiredPitch=clamp(-local*0.015f+forwardSpeed*0.05f,-0.25f,0.25f);
                if(distance<2)out->yawError=0;
            }
            if (s->phase==BM_FLIGHT_TAKEOFF || hazard) desiredPitch=0;
        }
        out->pitchError=desiredPitch-pitch;
        out->yaw=-bm_air_pid(&s->yaw,out->yawError,5,0.05f,0.2f,dt);
        out->pitch=-bm_air_pid(&s->tilt,out->pitchError,6,0.1f,0.2f,dt);
        float bank=fabsf(out->yawError)>0.15f ? (out->yawError>0 ? -0.1f : 0.1f) : 0;
        if(landing){
            float side=dx*o->right[0]+dz*o->right[2],sideSpeed=dot(o->velocity,o->right);
            bank=clamp(-side*0.015f+sideSpeed*0.04f,-0.18f,0.18f);
        }
        out->roll=bm_air_pid(&s->roll,bank-roll,6,0.1f,0.2f,dt);
        // Height error is normalized and damped by measured vertical speed.
        // The Lua controller lacks this damping; direct metres saturate here.
        float heightError=(goal[1]-o->position[1])*0.03f-o->velocity[1]*0.12f;
        out->throttle=clamp(0.5f+bm_air_pid(&s->throttle,heightError,3,0.05f,0.2f,dt),0,1);
        out->brake=0;
        if (hazard && o->velocity[1]<5) out->throttle=1;
    }
    // V22: a helicopter fires when the nose is within ~6 m of the aim point at the target's range (V20: 0.15 / 0.2 rad - up
    // to 60 m off at 300 m: "pilots miss a lot"); a jet keeps its 0.2 (its law above handles the gun pass)
    float aimRange=finite3(out->aim) ? sqrtf((out->aim[0]-o->position[0])*(out->aim[0]-o->position[0])+(out->aim[1]-o->position[1])*(out->aim[1]-o->position[1])+(out->aim[2]-o->position[2])*(out->aim[2]-o->position[2])) : 300;
    float tolerance=jet ? 0.2f : clamp(atanf((o->targetAir ? 12.0f : 6.0f)/fmaxf(aimRange,1)),o->targetAir ? 0.035f : 0.025f,0.12f);   // (V24: a helicopter is ~15 m long - 12 m, was 8)
    int ready=attacking && !hazard && fabsf(out->yawError)<tolerance && fabsf(out->pitchError)<tolerance;
    // Plane cycle: 0.3 s pause / 1.3 s firing, as in VehicleAttacking.lua.
    // V20 helicopter cycle: 0.25 s pause / 0.95 s firing (V19: 0.3 / 0.3).
    float cycle=jet ? 1.6f : 1.2f, pause=jet ? 0.3f : 0.25f;
    if (attacking) s->shotTime=fmodf(s->shotTime+dt,cycle);
    else s->shotTime=0;
    out->fire=ready && s->shotTime>=pause;
    // V20: gunships lock missiles on aircraft (2 s on the nose, 10 s apart)
    s->secondaryWait=fmaxf(0,s->secondaryWait-dt);
    if(!jet && p->kind==BM_AIR_ATTACK) air_secondary(s,attacking && o->targetAir && fabsf(out->yawError)<0.25f && fabsf(out->pitchError)<0.25f,dt,out);
    out->tactic=attacking ? (jet ? BM_TACTIC_GUNS : BM_TACTIC_STRAFE) : (s->phase==BM_FLIGHT_APPROACH ? BM_TACTIC_PURSUE : BM_TACTIC_NONE);
    s->tactic=out->tactic;
    out->attacking=attacking; out->recovering=s->phase==BM_FLIGHT_RECOVER;
    out->yaw=clamp(out->yaw*c->yawSign,-1,1);
    out->pitch=clamp(out->pitch*c->pitchSign,-1,1);
    out->roll=clamp(out->roll*c->rollSign,-1,1);
    return 1;
}
