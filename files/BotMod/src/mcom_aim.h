// AltBotMod V35 (included by modes_native.inc; needs BMModeProfile and g_modeProfile)
#pragma once
#include <math.h>
// AltBotMod V35: M-COMs whose arming spot and aim were never recorded (present 2: the Squad Rush layouts made from the
// Fun-Bots V3.6.0-RC2 objective meshes, research/generate_sr_mesh_profiles.py). As Fun-Bots RC2 does without a recorded
// spot, the operator walks up to 1.3 m from the M-COM, crouches and looks at it (pitch -0.6). A hold the game does not
// answer with an arm / defuse turns the next try a little further round (0, +-30, +-60, +-90 deg, behind), then lower;
// the aim that worked is kept for the level.
static unsigned char g_mcomAimTry[17],g_mcomAimKnown[17];
static float g_mcomAimYaw[17],g_mcomAimPitch[17],g_mcomAimLastYaw[17],g_mcomAimLastPitch[17];
#define MCOM_UNRECORDED_REACH 1.3f
static int mcom_aim_unknown(int k){return g_modeProfile && k>0 && k<=16 && g_modeProfile->mcom[k].present==2;}
static void mode_mcom_aim(int k,const float *me,float *yaw,float *pitch)
{
    if(!mcom_aim_unknown(k)){*yaw=g_modeProfile->mcom[k].yaw;*pitch=g_modeProfile->mcom[k].pitch;return;}
    if(g_mcomAimKnown[k]){*yaw=g_mcomAimYaw[k];*pitch=g_mcomAimPitch[k];}
    else{
        static const float turn[8]={0.0f,0.52f,-0.52f,1.05f,-1.05f,1.57f,-1.57f,3.14159f};
        const float *c=g_modeProfile->mcom[k].pos;float dx=c[0]-me[0],dz=c[2]-me[2];
        float base=dx*dx+dz*dz>0.3f*0.3f?atan2f(-dx,dz):0.0f;   // (the recorded aims' convention: facing (-sin yaw, cos yaw))
        float y=base+turn[g_mcomAimTry[k]%8];
        while(y<0)y+=6.2831853f;
        while(y>=6.2831853f)y-=6.2831853f;
        *yaw=y;*pitch=(g_mcomAimTry[k]/8)%2?-0.95f:-0.6f;
    }
    g_mcomAimLastYaw[k]=*yaw;g_mcomAimLastPitch[k]=*pitch;
}
