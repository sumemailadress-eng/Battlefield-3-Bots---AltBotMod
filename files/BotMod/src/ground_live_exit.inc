// Cached mask rejection is not proof that today's departure is obstructed.
// Survey only a short leg against live ground and hull-width collision rays.
// This never opens the shared map mask or bypasses actual walls/vehicles.
static int drive_live_exit(int i,DriveState *d,const float *position,float *point)
{
    static unsigned checkedAt;static int candidate;
    if(checkedAt==H->frame)return 0;
    checkedAt=H->frame;
    if(vehicle_is_boat(d->veh))return 0;
    float *xf=vehicle_xf(d->veh);if(!xf)return 0;
    const BMVehicleProfile *profile=bm_vehicle_profile(vehicle_name(d->veh));
    float heading=atan2f(xf[8],xf[10]);
    static const float offsets[]={0,0.45f,-0.45f,0.9f,-0.9f,1.4f,-1.4f,3.14159265f};
    float angle=heading+offsets[(candidate++ + i)%8],dx=sinf(angle),dz=cosf(angle);
    float startFloor;
    if(!ground_below(position[0],position[2],position[1]+2,position[1]-4,&startFloor))return 0;
    float end[3]={position[0]+dx*10,startFloor,position[2]+dz*10};
    unsigned saved=g_rayFlags;g_rayFlags=RAY_DONT_CHECK_CHARACTER|RAY_CHECK_DETAIL_MESH;
    int clear=1;float previous=startFloor;
    for(int k=1;k<=5 && clear;k++){
        float base=NAN;
        for(int side=-1;side<=1 && clear;side++){
            float width=side*(profile->halfWidth+0.2f),x=position[0]+dx*k*2+dz*width,z=position[2]+dz*k*2-dx*width,y;
            if(!ground_below(x,z,previous+2,previous-2,&y) || nav_hit_class()==NAV_HIT_WATER ||
               fabsf(y-previous)>1.2f || (isfinite(base) && fabsf(y-base)>0.7f))clear=0;
            else {if(side==0)base=y;end[1]=y;}
        }
        previous=end[1];
    }
    // A non-null mask selects vehicle-width rays; it is not dereferenced by
    // nav_segment_clear. A local sentinel avoids pretending to modify a cache.
    static const unsigned char hullRayMask=1;
    nav_set_path_owner(i+MAX_BOTS);nav_set_path_mask(&hullRayMask);
    nav_set_vehicle_width(profile->halfWidth+0.2f);nav_set_vehicle_avoid(NULL,0);
    float start[3]={position[0],startFloor,position[2]};
    if(clear)clear=nav_segment_clear(drive_nav_ray,start,end);
    nav_set_path_mask(NULL);nav_set_vehicle_width(1.5f);nav_set_path_owner(-1);g_rayFlags=saved;
    if(!clear)return 0;
    memcpy(point,end,12);return 1;
}
