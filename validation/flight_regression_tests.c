// Deterministic guidance regressions, including a simple kinematic fixture.
// This fixture is not Frostbite flight physics or a claim of live validation.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../files/BotMod/src/enhanced.h"
#define DT (1.0f/30)
static void attitude(BMAirObservation *o,float yaw,float pitch,float roll)
{
    float r[3]={cosf(yaw),0,-sinf(yaw)};
    float u[3]={-sinf(pitch)*sinf(yaw),cosf(pitch),-sinf(pitch)*cosf(yaw)};
    o->forward[0]=cosf(pitch)*sinf(yaw); o->forward[1]=sinf(pitch); o->forward[2]=cosf(pitch)*cosf(yaw);
    for(int k=0;k<3;k++) { o->right[k]=r[k]*cosf(roll)+u[k]*sinf(roll); o->up[k]=-r[k]*sinf(roll)+u[k]*cosf(roll); }
}
int main(void)
{
    BMConfig cfg; bm_defaults(&cfg); cfg.air.weapons=1;
    const BMAirProfile *jet=bm_air_profile("F16"); BMAirController s; BMAirOutput out;
    BMAirObservation o={0}; o.groundValid=o.velocityValid=1; o.goal[1]=350; o.goal[2]=2000;
    o.position[1]=800; o.velocity[2]=87; attitude(&o,0,0.5f,0);
    bm_air_start(&s,jet->kind,0);
    assert(bm_air_step(&s,jet,&cfg.air,&o,DT,&out));
    assert(s.phase==BM_FLIGHT_CRUISE && out.pitch>0 && !out.fire); // no continuing takeoff climb above ceiling
    // Heading can point past the waypoint; altitude still demands nose down.
    attitude(&o,3.14159265f,0.5f,0);
    assert(bm_air_step(&s,jet,&cfg.air,&o,DT,&out) && out.pitch>0);
    o.position[1]=350; o.targetValid=o.targetVisible=1; o.target[1]=0; o.target[2]=300;
    attitude(&o,0,0,0); bm_air_start(&s,jet->kind,1);
    assert(bm_air_step(&s,jet,&cfg.air,&o,DT,&out));
    assert(s.phase==BM_FLIGHT_APPROACH && !out.fire && fabsf(out.pitch)<0.1f); // turn outward for a sane dive
    // A ground pass remains valid below the former patrol-minus-75 lockout.
    o.position[1]=180; o.target[2]=600; attitude(&o,0,-atan2f(180,600),0);
    bm_air_start(&s,jet->kind,1); int fired=0;
    for(int k=0;k<20;k++) { assert(bm_air_step(&s,jet,&cfg.air,&o,DT,&out)); fired|=out.fire; }
    assert(fired && s.phase==BM_FLIGHT_ATTACK);
    o.position[1]=50; assert(bm_air_step(&s,jet,&cfg.air,&o,DT,&out));
    assert(out.recovering && !out.fire && out.pitch<0); // break off before terrain
    o.position[1]=650; o.targetAir=1; o.target[1]=1000; attitude(&o,0,0,0);
    assert(bm_air_step(&s,jet,&cfg.air,&o,DT,&out) && out.pitch>0 && !out.fire);
    // Recovery also uses a finite waypoint when terrain probes are unavailable.
    o.groundValid=0; o.targetValid=0; s.phase=BM_FLIGHT_RECOVER; s.timer=6;
    assert(bm_air_step(&s,jet,&cfg.air,&o,DT,&out) && out.pitch>0);
    memset(&o,0,sizeof o); o.groundValid=o.velocityValid=1; o.goal[1]=350; o.goal[2]=2000; o.position[1]=800;
    bm_air_start(&s,jet->kind,0); float yaw=0,pitch=0.5f,roll=0,maxHeight=800;
    for(int k=0;k<600;k++) {
        attitude(&o,yaw,pitch,roll);
        for(int axis=0;axis<3;axis++) o.velocity[axis]=87*o.forward[axis];
        assert(bm_air_step(&s,jet,&cfg.air,&o,DT,&out));
        pitch-=out.pitch*cosf(roll)*DT*0.7f;
        roll+=out.roll*DT*0.7f;
        yaw+=(out.yaw*0.5f-sinf(roll)*0.7f)*DT;
        for(int axis=0;axis<3;axis++) o.position[axis]+=o.velocity[axis]*DT;
        maxHeight=fmaxf(maxHeight,o.position[1]);
    }
    assert(maxHeight<850 && o.position[1]<600);
    assert(bm_target_score(1,BM_TARGET_INFANTRY,35)<bm_target_score(1,BM_TARGET_AIR,650));
    assert(bm_target_score(1,BM_TARGET_ENGINEER,60)<bm_target_score(1,BM_TARGET_INFANTRY,60));
    assert(bm_set(&cfg,"ReturnToBase","0")==1 && !cfg.returnToBase);
    assert(bm_set(&cfg,"PassengerTakeover","1")==1 && cfg.passengerTakeover);
    assert(bm_set(&cfg,"JetCeilingMeters","600")==1 && cfg.air.jetCeiling==600);
    assert(bm_set(&cfg,"GroundDriversPerTeam","2.5")==-1);
    assert(bm_set(&cfg,"VehicleReturnMeters","nan")==-1);
    // A jet outside its firing range and behind temporary occlusion pursues
    // the real target instead of returning to a patrol waypoint.
    memset(&o,0,sizeof o);o.position[1]=350;o.goal[1]=350;o.goal[0]=-1500;
    o.targetValid=o.targetAir=1;o.target[1]=350;o.target[0]=2500;o.velocity[2]=87;
    attitude(&o,0,0,0);bm_air_start(&s,jet->kind,1);
    assert(bm_air_step(&s,jet,&cfg.air,&o,DT,&out));assert(s.phase==BM_FLIGHT_APPROACH && !out.fire && out.yaw>0);
    // A helicopter above a ground target moves to a firing stand-off. Once
    // aligned at a safe angle, the actual weapon burst produces shots.
    const BMAirProfile *heli=bm_air_profile("AH1Z");memset(&o,0,sizeof o);
    o.position[1]=100;o.goal[1]=100;o.groundValid=1;o.targetValid=o.targetVisible=1;
    attitude(&o,0,0,0);bm_air_start(&s,heli->kind,1);
    assert(bm_air_step(&s,heli,&cfg.air,&o,DT,&out));assert(s.phase==BM_FLIGHT_APPROACH && !out.fire && fabsf(out.yaw)>0.5f);
    o.target[2]=300;attitude(&o,0,-atan2f(100,300),0);bm_air_start(&s,heli->kind,1);fired=0;
    for(int k=0;k<30;k++){assert(bm_air_step(&s,heli,&cfg.air,&o,DT,&out));fired|=out.fire;}
    assert(fired && s.phase==BM_FLIGHT_ATTACK);
    o.targetVisible=0;assert(bm_air_step(&s,heli,&cfg.air,&o,DT,&out));assert(!out.fire && s.phase==BM_FLIGHT_APPROACH);
    puts("PASS: Test7 jet long-range/occluded pursuit and helicopter stand-off/aligned firing/LOS gate");
    puts("PASS: jet climb ceiling/finite recovery/ground dive/attack run/guidance fixture/threat scores/new settings regressions");
    return 0;
}
