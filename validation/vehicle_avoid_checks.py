"""Actual sync/queued hull checks and owner-local obstruction avoidance."""
from pathlib import Path
import argparse,os,subprocess,tempfile
from nav_queue_checks import WINDOWS,HARNESS,SRC
pre=HARNESS[:HARNESS.index('int main(void)')].replace('wall_x+0.5f','wall_x+0.25f')
MAIN=r'''
int main(void){
 float out[256*3],s[3]={10.5f,0,40.5f},t[3]={70.5f,0,40.5f},avoid[3]={40.5f,0,40.5f};
 NavGrid*g=flat(90,90,-1,0);size_t size=(size_t)g->w*g->h*L;
 unsigned char *mask=calloc(size,1),*original=calloc(size,1);
 for(int u=0;u<g->w*g->h;u++)mask[u*L]=1;
 memcpy(original,mask,size);nav_set_path_mask(mask);nav_set_vehicle_width(1.9f);
 nav_set_vehicle_avoid(avoid,5);assert(!poll(64,g,s,t,out,128));
 nav_set_vehicle_avoid(NULL,0);assert(!poll(65,g,s,t,out,128));
 assert(g_requests[64].vehicleAvoid[3]==5&&!g_requests[65].vehicleAvoid[3]);
 int n=0;
 for(int k=0;k<10000&&!n;k++){
  nav_process_paths(.5);assert(!g_vehicleAvoid[3]);nav_set_vehicle_avoid(avoid,5);
  n=poll(64,g,s,t,out,128);nav_set_vehicle_avoid(NULL,0);
  if(!n&&!nav_last_deferred())break;
 }
 assert(n);connected(g,s,out,n);
 nav_set_vehicle_avoid(avoid,5);float from[3];memcpy(from,s,12);
 for(int k=0;k<n;k++){assert(nav_segment_clear(test_ray,from,out+k*3));memcpy(from,out+k*3,12);}
 nav_set_vehicle_avoid(NULL,0);
 for(int k=0,n2=0;k<10000;k++){nav_process_paths(.5);n2=poll(65,g,s,t,out,128);if(n2){assert(fabsf(out[(n2-1)*3]-t[0])<.01f);for(int j=0;j<n2;j++)assert(fabsf(out[j*3+2]-s[2])<.01f);break;}assert(nav_last_deferred());assert(k<9999);}
 assert(!memcmp(mask,original,size));
 puts("PASS: queued drivers retain distinct avoidance areas across shared work slices; one routes around the obstruction, the other retains its direct street, and the shared mask is unchanged");
 nav_cancel_paths();wall_x=40;wall_gap=0;
 nav_set_path_owner(-1);n=nav_find_path(g,test_ray,s[0],s[1],s[2],t[0],t[1],t[2],out,128);
 assert(n>0 && strstr(nav_last_error(),"clear prefix"));
 for(int k=0;k<n;k++)assert(out[k*3]<40.25f);
 memcpy(from,s,12);for(int k=0;k<n;k++){assert(nav_segment_clear(test_ray,from,out+k*3));memcpy(from,out+k*3,12);}
 assert(!poll(64,g,s,t,out,128));int finished=0;
 for(int k=0;k<10000&&!finished;k++){nav_process_paths(.5);n=poll(64,g,s,t,out,128);finished=!nav_last_deferred();}
 assert(finished && n>0 && nav_last_partial());
 memcpy(from,s,12);for(int k=0;k<n;k++){assert(out[k*3]<40.25f);assert(nav_segment_clear(test_ray,from,out+k*3));memcpy(from,out+k*3,12);}
 // At the obstructed edge itself there is no useful prefix. No collision
 // bypass is returned: local edge exclusions try other grid branches first.
 s[0]=39.5f;assert(!poll(64,g,s,t,out,128));finished=0;
 for(int k=0;k<10000&&!finished;k++){nav_process_paths(.5);n=poll(64,g,s,t,out,128);finished=!nav_last_deferred();}
 assert(finished && !n);
 puts("PASS: sync/queued routes retain only the collision-checked clear prefix, mark continuation, and reject obstructed departures after checking alternative edges");
 nav_set_path_mask(NULL);nav_set_vehicle_avoid(NULL,0);nav_free(g);free(mask);free(original);return 0;
}
'''
def main():
    p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='bf3-vehicle-avoid-') as tmp:
        tmp=Path(tmp);(tmp/'windows.h').write_text(WINDOWS);f=tmp/'checks.c';f.write_text(pre+MAIN)
        cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror','-I',str(tmp),'-I',str(SRC),str(f),'-lm','-o',str(tmp/'checks')]
        if a.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
        subprocess.run(cmd,check=True);subprocess.run([str(tmp/'checks')],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
