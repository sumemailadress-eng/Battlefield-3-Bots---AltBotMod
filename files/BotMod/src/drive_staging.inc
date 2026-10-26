// Route long trips through nearby drivable road points first. This shortens
// deployment searches on mountain maps; each leg still uses the real queued
// A*, height-aware mask, vehicle-width collision checks and own-hull retrace.
static int drive_stage_target(int i,const NavGrid *grid,const unsigned char *mask,const float *position,float *target)
{
    DriveState *d=&g_drive[i];
    float distance=sqrtf(dist2(position,d->goal));
    if(!g_enhanced.driveStages || distance<300 || !isfinite(distance))return 0;
    static const float angles[]={0,0.65f,-0.65f,1.15f,-1.15f,0};
    float heading=atan2f(d->goal[0]-position[0],d->goal[2]-position[2]);
    for(int tries=0;d->stageCandidate<6 && tries<2;tries++,d->stageCandidate++){
        int candidate=d->stageCandidate;
        float step=candidate==5?80:160;
        float wanted[3]={position[0]+sinf(heading+angles[candidate])*step,position[1],position[2]+cosf(heading+angles[candidate])*step};
        // Search for the road at local terrain height rather than projecting
        // a far objective's height onto this part of a mountain.
        float floor=0;int found=ground_below(wanted[0],wanted[2],position[1]+100,position[1]-100,&floor);
        if(!found)continue;
        if(!nav_drive_near(grid,mask,wanted[0],floor,wanted[2],35,target))continue;
        if(sqrtf(dist2(position,target))<30 || sqrtf(dist2(target,d->goal))>distance-10)continue;
        return 1;
    }
    return d->stageCandidate<6?-1:0;
}
