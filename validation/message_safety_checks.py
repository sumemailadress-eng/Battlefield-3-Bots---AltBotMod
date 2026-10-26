"""Actual message type inspection, with Win32 safe-copy/atomic primitives mocked.
Uses virtual native image addresses; no scoring payload interpretation is tested.
"""
from pathlib import Path
import argparse,os,subprocess,tempfile
from test4_checks import function
ROOT=Path(__file__).resolve().parents[1];core=(ROOT/'files/BotMod/src/core.c').read_text()
body=r'''
#define _GNU_SOURCE
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
typedef int32_t LONG;typedef size_t SIZE_T;
static unsigned copies;static void*image;
static LONG InterlockedCompareExchange(LONG*p,LONG value,LONG expected){LONG old=*p;if(old==expected)*p=value;return old;}
static void*GetCurrentProcess(void){return NULL;}
static int ReadProcessMemory(void*process,const void*p,void*out,size_t n,size_t*got){(void)process;copies++;*got=0;if(!p||(uintptr_t)p==1)return 0;memcpy(out,p,n);*got=n;return 1;}
static int IsBadStringPtrA(const char*p,size_t n){return !memchr(p,0,n);}
'''+function(core,'message_name').replace('uintptr_t vt=0,fn=0;', 'uint32_t vt=0,fn=0;').replace('sizeof(uintptr_t)', 'sizeof(uint32_t)').replace('uintptr_t address=0;', 'uint32_t address=0;').replace('(void *)(vt+', '(void *)(uintptr_t)(vt+').replace('(void *)fn','(void *)(uintptr_t)fn').replace('(const char *)address','(const char *)(uintptr_t)address')+r'''
int main(void){
 image=mmap((void*)0x00500000,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(image==(void*)0x00500000);
 uint32_t message=0x00500000;uint32_t*vt=image;vt[2]=0x00500100;unsigned char*fn=(unsigned char*)image+0x100;fn[0]=0xb8;uint32_t address=0x00500200;memcpy(fn+1,&address,4);strcpy((char*)image+0x200,"VerifiedFixtureMessage");
 assert(!strcmp(message_name(&message),"VerifiedFixtureMessage")&&copies==3);unsigned first=copies;
 for(int k=0;k<10000;k++){assert(!strcmp(message_name(&message),"VerifiedFixtureMessage"));}assert(copies==first+10000);
 assert(!message_name(NULL)&&!message_name((void*)1));message=3;assert(!message_name(&message));
 message=0x00500020;((uint32_t*)((char*)image+0x20))[2]=1;assert(!message_name(&message));
 message=0x00500040;((uint32_t*)((char*)image+0x40))[2]=0x00500120;((char*)image)[0x120]=0x90;assert(!message_name(&message));
 message=0x00500060;((uint32_t*)((char*)image+0x60))[2]=0x00500140;((char*)image)[0x140]=0xb8;address=1;memcpy((char*)image+0x141,&address,4);assert(!message_name(&message));
 puts("PASS: actual malformed/null message chains fail safely; immutable per-type cache needs one guarded object copy per repeated message instead of re-reading code and strings (10,000 calls)");munmap(image,4096);return 0;
}
'''
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--sanitize',action='store_true');args=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='bf3-message-safety-')as tmp:
  f=Path(tmp)/'checks.c';f.write_text(body);exe=f.with_suffix('');cmd=['gcc','-std=c11','-Wall','-Wextra','-Werror',str(f),'-o',str(exe)]
  if args.sanitize:cmd[1:1]=['-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']
  subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
if __name__=='__main__':main()
