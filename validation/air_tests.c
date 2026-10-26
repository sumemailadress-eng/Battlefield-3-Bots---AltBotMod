#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../files/BotMod/src/enhanced.h"

#define DT (1.0f/30.0f)
#define PI 3.14159265359f
static unsigned checks;
#define CHECK(x) do { checks++; assert(x); } while(0)
static int closef(float a,float b) { return fabsf(a-b)<0.001f; }
static BMAirObservation pose(void)
{
    BMAirObservation o={0};
    o.right[0]=o.up[1]=o.forward[2]=1;
    o.position[1]=350; o.goal[1]=350; o.goal[2]=1000;
    o.groundValid=1; o.velocityValid=1; o.velocity[2]=86.9444f;
    return o;
}
static void bounded(BMAirOutput *out)
{
    CHECK(isfinite(out->throttle) && out->throttle>=0 && out->throttle<=1);
    CHECK(isfinite(out->brake) && out->brake>=0 && out->brake<=1);
    CHECK(isfinite(out->pitch) && fabsf(out->pitch)<=1);
    CHECK(isfinite(out->yaw) && fabsf(out->yaw)<=1);
    CHECK(isfinite(out->roll) && fabsf(out->roll)<=1);
}
int main(void)
{
    BMConfig config; bm_defaults(&config); BMAirConfig *c=&config.air;
    CHECK(c->jets && c->helicopters && !c->weapons && !c->countermeasures && !c->transportDrops);
    const char *names[]={"F16","F18_Wingman","Vehicles/A10/A10_THUNDERBOLT","A10_THUNDERBOLT_spjet",
        "Su-25TM","Su-35BM Flanker-E","Su37","F35B","AH1Z_coop","Mi28","AH6_Littlebird_EQ",
        "Z-11w","Wz11_SP_Paris","Wz-11","Ka-60","Venom_coop","UH-1Y"};
    for(unsigned k=0;k<sizeof names/sizeof names[0];k++) {
        CHECK(bm_air_profile(names[k])); CHECK(bm_air_allowed(c,bm_air_profile(names[k])));
        CHECK(!bm_vehicle_allowed(&config,bm_vehicle_profile(names[k])));
    }
    CHECK(!bm_air_profile("AC130")); CHECK(!bm_air_profile("MAV")); CHECK(!bm_air_profile("UH60")); CHECK(!bm_air_profile(NULL));
    CHECK(bm_set(&config,"Jets","1")==1); CHECK(bm_set(&config,"Helicopters","1")==1);
    CHECK(bm_air_allowed(c,bm_air_profile("F16")) && bm_air_allowed(c,bm_air_profile("Mi28")));
    CHECK(!bm_vehicle_allowed(&config,bm_vehicle_profile("F16")));
    CHECK(bm_set(&config,"AirPilotsPerTeam","2.5")==-1);
    CHECK(bm_set(&config,"AirPilotsPerTeam","5")==1 && config.air.pilotsPerTeam==5);
    CHECK(bm_set(&config,"AirPilotsPerTeam","6")==1 && config.air.pilotsPerTeam==6);
    CHECK(bm_set(&config,"AirPilotsPerTeam","7")==-1 && config.air.pilotsPerTeam==6);
    CHECK(bm_set(&config,"AirPitchSign","0")==-1);
    CHECK(bm_set(&config,"AirYawSign","NaN")==-1);
    CHECK(bm_set(&config,"JetCruiseMps","1000")==-1);
    CHECK(bm_set(&config,"JetHeightMeters","10")==-1);
    CHECK(bm_set(&config,"AircraftWeapons","0.5")==-1);
    CHECK(bm_set(&config,"AirRollSign","-1")==1); c->rollSign=1;
    float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,350,0,1};
    CHECK(bm_air_transform_valid(matrix)); matrix[5]=-1; CHECK(!bm_air_transform_valid(matrix));
    matrix[5]=1; matrix[8]=NAN; CHECK(!bm_air_transform_valid(matrix)); CHECK(!bm_air_transform_valid(NULL));
    BMAirObservation o=pose(); float yaw,pitch,target[3]={100,350,100};
    CHECK(bm_air_deviation(&o,target,&yaw,&pitch)); CHECK(closef(yaw,PI/4) && closef(pitch,0));
    target[0]=0; target[1]=450;
    CHECK(bm_air_deviation(&o,target,&yaw,&pitch)); CHECK(closef(yaw,0) && closef(pitch,-PI/4));
    CHECK(!bm_air_deviation(&o,o.position,&yaw,&pitch)); CHECK(yaw==0 && pitch==0);
    BMAirPid pid={0};
    CHECK(closef(bm_air_pid(&pid,0.1f,3,0.05f,0.2f,DT),0.325f));
    CHECK(closef(bm_air_pid(&pid,0.2f,3,0.05f,0.2f,DT),0.635f));
    for(int k=0;k<100;k++) CHECK(bm_air_pid(&pid,100,3,0.05f,0.2f,DT)==1);
    CHECK(pid.integral<0.02f); bm_air_pid(&pid,-0.1f,3,0.05f,0.2f,DT); CHECK(pid.integral<=0);
    CHECK(bm_air_pid(&pid,NAN,3,0.05f,0.2f,DT)==0 && pid.integral==0);
    float origin[3]={0}, enemy[3]={0,0,1000}, velocity[3]={0,0,-100}, aim[3],time;
    CHECK(bm_air_intercept(origin,enemy,velocity,900,0,aim,&time)); CHECK(closef(time,1) && closef(aim[2],900));
    velocity[0]=100; velocity[2]=0;
    CHECK(bm_air_intercept(origin,enemy,velocity,900,9.81f,aim,&time));
    CHECK(closef(time,1000/sqrtf(900*900-100*100)) && closef(aim[0],100*time) && closef(aim[1],4.905f*time*time));
    velocity[0]=0; velocity[2]=1000; CHECK(!bm_air_intercept(origin,enemy,velocity,900,0,aim,&time));
    velocity[2]=-900; CHECK(bm_air_intercept(origin,enemy,velocity,900,0,aim,&time)); CHECK(closef(time,1000.0f/1800));
    velocity[2]=900; CHECK(!bm_air_intercept(origin,enemy,velocity,900,0,aim,&time));
    velocity[2]=NAN; CHECK(!bm_air_intercept(origin,enemy,velocity,900,0,aim,&time));
    BMAirMotion motion={0}; float position[3]={0}; bm_air_motion(&motion,position,DT);
    for(int k=1;k<=30;k++) { position[0]=k*DT*30; position[1]=k*DT*10; bm_air_motion(&motion,position,DT); }
    CHECK(motion.ready && closef(motion.velocity[0],30) && closef(motion.velocity[1],10));
    position[0]+=10000;
    for(int k=0;k<6;k++) bm_air_motion(&motion,position,DT);
    CHECK(!motion.ready); // position jump is not interpreted as flight velocity
    BMAirController state; BMAirOutput out;
    const BMAirProfile *jet=bm_air_profile("F16"), *heli=bm_air_profile("AH6_Littlebird");
    bm_air_start(&state,jet->kind,0); o=pose(); o.position[1]=2; o.velocity[2]=0;
    CHECK(bm_air_step(&state,jet,c,&o,DT,&out)); CHECK(out.throttle==1 && out.brake==0 && out.pitch==0 && !out.fire);
    o.velocity[2]=25; CHECK(bm_air_step(&state,jet,c,&o,DT,&out)); CHECK(out.pitch>=-0.15f && out.pitch<=0);
    bm_air_start(&state,jet->kind,1); o=pose(); o.velocity[2]=110;
    CHECK(bm_air_step(&state,jet,c,&o,DT,&out)); CHECK(out.throttle==0 && out.brake>0);
    o.velocity[2]=60; CHECK(bm_air_step(&state,jet,c,&o,DT,&out)); CHECK(out.throttle>0 && out.brake==0);
    bm_air_start(&state,heli->kind,1); o=pose(); o.position[1]=o.goal[1]=110; o.velocity[2]=0;
    CHECK(bm_air_step(&state,heli,c,&o,DT,&out)); CHECK(out.pitch>0 && out.brake==0);
    o.goal[0]=1000; o.goal[2]=0; CHECK(bm_air_step(&state,heli,c,&o,DT,&out)); CHECK(out.yaw<0 && out.roll<0);
    bm_air_start(&state,jet->kind,1); o=pose(); o.targetValid=o.targetVisible=o.targetAir=1;
    o.target[1]=350; o.target[2]=1000; c->weapons=1;
    int fired=0;
    for(int k=0;k<50;k++) { CHECK(bm_air_step(&state,jet,c,&o,DT,&out)); fired|=out.fire; }
    CHECK(fired && state.phase==BM_FLIGHT_ATTACK);
    o.targetVisible=0; CHECK(bm_air_step(&state,jet,c,&o,DT,&out)); CHECK(!out.fire && state.phase==BM_FLIGHT_APPROACH && !out.recovering);
    o.targetValid=0; CHECK(bm_air_step(&state,jet,c,&o,DT,&out)); CHECK(!out.fire && state.phase==BM_FLIGHT_CRUISE);
    bm_air_start(&state,jet->kind,1); o=pose(); o.position[1]=20;
    CHECK(bm_air_step(&state,jet,c,&o,DT,&out)); CHECK(out.recovering && out.throttle==1 && out.brake==0 && !out.fire);
    bm_air_start(&state,heli->kind,1); o=pose(); o.obstacleDistance=15;
    CHECK(bm_air_step(&state,heli,c,&o,DT,&out)); CHECK(out.recovering && !out.fire);
    bm_air_start(&state,jet->kind,1); o=pose(); o.targetValid=o.targetVisible=o.targetAir=1;
    o.target[1]=350; o.target[2]=1000; c->weapons=0;
    for(int k=0;k<50;k++) { CHECK(bm_air_step(&state,jet,c,&o,DT,&out)); CHECK(!out.fire && !out.attacking); }
    o.goal[0]=NAN; CHECK(!bm_air_step(&state,jet,c,&o,DT,&out)); CHECK(out.throttle==0 && out.fire==0);
    CHECK(!bm_air_step(&state,jet,c,NULL,DT,&out));
    // Property checks over thousands of valid poses, target geometries,
    // terrain heights and vertical/horizontal velocities, including inversion.
    const BMAirProfile *all[]={jet,heli,bm_air_profile("AH1Z"),bm_air_profile("Mi28"),bm_air_profile("Venom")};
    c->weapons=1;
    for(unsigned p=0;p<sizeof all/sizeof all[0];p++) {
        bm_air_start(&state,all[p]->kind,1);
        for(int k=0;k<2000;k++) {
            o=pose(); float roll=sinf(k*0.13f)*3.1f;
            o.right[0]=cosf(roll); o.right[1]=sinf(roll);
            o.up[0]=-sinf(roll); o.up[1]=cosf(roll);
            o.position[1]=100+sin(k*0.1)*80; o.goal[0]=sinf(k)*1000;
            o.velocity[0]=sinf(k)*100; o.velocity[1]=cosf(k)*30; o.velocity[2]=100;
            o.targetValid=o.targetVisible=1; o.targetAir=k%2;
            o.target[0]=cosf(k)*400; o.target[1]=o.position[1]+sinf(k)*100; o.target[2]=500;
            o.targetVelocity[1]=cosf(k)*40; o.targetVelocity[0]=90;
            CHECK(bm_air_step(&state,all[p],c,&o,DT,&out)); bounded(&out);
        }
    }
    printf("PASS: %u aircraft profile/config/PID/intercept/flight safety/property checks\n",checks);
    return 0;
}
