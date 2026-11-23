/* SPDX-License-Identifier: GPL-2.0-only */
/* Diskless QEMU (mcimx6ul-evk) PID 1 for the PW4 handoff, never a Kindle
 * launcher. Built twice: as the first Linux's init, which makes the device
 * tree from its own /sys/firmware/fdt the way the device does (pw_fdt_rewrite),
 * loads the module (profile 4), is refused what breaks the build's rules,
 * runs a PROBE, then commits; and with -DSECONDARY as the init of the
 * public initramfs the second Linux starts from. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "stage_abi.h"
#include "native-abi.h"
#include "sha256.h"
#include "pw4_fdt.h"
static unsigned checks;
static void end(int ok)
{
#ifdef SECONDARY
    printf("PW4_SECOND_KERNEL_%s checks=%u\n",ok?"PASS":"FAIL",checks);
#else
    printf("PW4_FIRST_KERNEL_FAIL checks=%u unexpected_return=%d\n",checks,ok);
#endif
    fflush(stdout);sync();reboot(RB_POWER_OFF);for(;;)pause();
}
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL line=%u errno=%d\n",__LINE__,errno);end(0);}}while(0)
static void read_text(const char *path,char *out,size_t limit)
{
    int fd=open(path,O_RDONLY);REQUIRE(fd>=0);
    ssize_t n=read(fd,out,limit-1);REQUIRE(n>=0&&(size_t)n<limit-1);
    out[n]=0;REQUIRE(close(fd)==0);
}
#ifndef SECONDARY
static void *read_blob(const char *path,unsigned *bytes)
{
    int fd=open(path,O_RDONLY);REQUIRE(fd>=0);
    unsigned cap=1<<20,n=0;unsigned char *p=malloc(cap);REQUIRE(p);
    for(;;){
        if(n==cap){cap*=2;REQUIRE(cap<=64u<<20);p=realloc(p,cap);REQUIRE(p);}
        ssize_t r=read(fd,p+n,cap-n);REQUIRE(r>=0);if(!r)break;n+=(unsigned)r;
    }
    REQUIRE(close(fd)==0&&n>0);*bytes=n;return p;
}
static void write_text(const char *path,const char *text)
{
    int fd=open(path,O_WRONLY);REQUIRE(fd>=0);
    REQUIRE(write(fd,text,strlen(text))==(ssize_t)strlen(text));REQUIRE(close(fd)==0);
}
static int upload(int fd,void *const *bytes,const unsigned *sizes)
{
    struct pws_begin b={.version=1};
    for(unsigned i=0;i<3;++i) {
        SHA256_CTX h;sha256_init(&h);sha256_update(&h,bytes[i],sizes[i]);
        sha256_final(&h,b.images[i].sha256);b.images[i].bytes=sizes[i];
    }
    REQUIRE(ioctl(fd,PWS_BEGIN,&b)==0);
    for(unsigned i=0;i<3;++i)for(unsigned off=0;off<sizes[i];) {
        unsigned n=sizes[i]-off;if(n>65536)n=65536;
        struct pws_put p={.slot=i,.offset=off,.bytes=n,.user_buffer=(uintptr_t)((char *)bytes[i]+off)};
        REQUIRE(ioctl(fd,PWS_PUT,&p)==0);off+=n;
    }
    if(ioctl(fd,PWS_SEAL,0))return errno;
    REQUIRE(ioctl(fd,PWS_PREPARE,0)==0);
    return 0;
}
/* The device tree as the device makes it: this kernel's own blob with the
 * build's /chosen. */
static void *device_tree(const char *cmdline,unsigned initrd_bytes,unsigned *bytes)
{
    unsigned n;unsigned char *src=read_blob("/sys/firmware/fdt",&n);
    unsigned padded=(n+3)&~3u;src=realloc(src,padded);REQUIRE(src);memset(src+n,0,padded-n);
    unsigned char *out=malloc(PW_FDT_CAP);REQUIRE(out);
    struct pw_range rd={0x84000000u,initrd_bytes},zone={0x80000000u,0x08000000u};
    pw_u32 used=0;
    REQUIRE(pw_fdt_rewrite(src,padded,cmdline,rd,zone,out,PW_FDT_CAP,&used)==0);
    free(src);*bytes=used;return out;
}
#endif
int main(void)
{
    struct utsname u;char text[8192];
    if(getpid()!=1||getuid()!=0||uname(&u))return 77;
    REQUIRE(mount("proc","/proc","proc",0,NULL)==0);
    REQUIRE(mount("sysfs","/sys","sysfs",0,NULL)==0);
    read_text("/proc/cmdline",text,sizeof(text));
#ifdef SECONDARY
    printf("PW4_SECOND_KERNEL uname=%s cmdline=%s",u.release,text);
    REQUIRE(strstr(u.release,"-pw4-android81-qemu"));
    REQUIRE(strstr(text,"pw4_virtual_stage=second"));
    struct stat st;
    REQUIRE(stat("/public-marker",&st)==0&&S_ISREG(st.st_mode));
    REQUIRE(stat("/device-marker",&st)==0&&S_ISREG(st.st_mode));   /* the appended archive was unpacked */
    REQUIRE(stat("/pw3_first_kernel.ko",&st)<0&&errno==ENOENT);
    read_text("/proc/iomem",text,sizeof(text));REQUIRE(strstr(text,"System RAM"));
    end(1);
#else
    REQUIRE(strstr(u.release,"-pw4-loader-smoke"));
    printf("PW4_FIRST_KERNEL uname=%s\n",u.release);
    unsigned n;void *module=read_blob("/pw3_first_kernel.ko",&n);
    REQUIRE(syscall(SYS_init_module,module,(size_t)n,"")==0);free(module);
    read_text("/sys/class/misc/pw3-first-kernel/dev",text,sizeof(text));
    unsigned major_num,minor_num;REQUIRE(sscanf(text,"%u:%u",&major_num,&minor_num)==2);
    REQUIRE(mknod("/dev/pw3-first-kernel",S_IFCHR|0600,makedev(major_num,minor_num))==0);
    int fd=open("/dev/pw3-first-kernel",O_RDWR);REQUIRE(fd>=0);
    write_text("/sys/power/pm_test","core");
    errno=0;REQUIRE(ioctl(fd,PWS_NATIVE_COMMIT,0)==-1&&errno==EPERM);
    char cmdline[1024];read_text("/target-cmdline",cmdline,sizeof(cmdline));
    char *nl=strchr(cmdline,'\n');if(nl)*nl=0;
    unsigned kb,pb,db,ib,tb,bad_tb;
    void *kernel=read_blob("/target-zImage",&kb);
    void *pub=read_blob("/target-public-initramfs",&pb),*dev=read_blob("/target-device-archive",&db);
    unsigned start=(pb+3u)&~3u;ib=start+db;
    unsigned char *initramfs=calloc(1,ib);REQUIRE(initramfs);
    memcpy(initramfs,pub,pb);memcpy(initramfs+start,dev,db);
    void *tree=device_tree(cmdline,ib,&tb);
    void *other=device_tree("console=ttymxc0,115200 something=else",ib,&bad_tb);
    void *good[3]={kernel,tree,initramfs};unsigned sizes[3]={kb,tb,ib};
    /* What the build pins and what the device's part must keep to. */
    ((unsigned char *)kernel)[0x100]^=1;REQUIRE(upload(fd,good,sizes)==EBADMSG);((unsigned char *)kernel)[0x100]^=1;
    initramfs[pb/2]^=1;REQUIRE(upload(fd,good,sizes)==EBADMSG);initramfs[pb/2]^=1;
    {void *v[3]={kernel,other,initramfs};unsigned s[3]={kb,bad_tb,ib};REQUIRE(upload(fd,v,s)==ENOEXEC);}
    {void *v[3]={kernel,tree,pub};unsigned s[3]={kb,tb,pb};REQUIRE(upload(fd,v,s)==ENOEXEC);}
    /* The real thing. */
    REQUIRE(upload(fd,good,sizes)==0);
    struct pws_plan_status ps={0};REQUIRE(ioctl(fd,PWS_PLAN_STATUS,&ps)==0&&ps.state==PWS_PREPARED&&ps.copy_plan_verified&&ps.boot_footprint_verified);
    struct pws_native_report r={0};
    for(unsigned round=0;round<2;round++){
        REQUIRE(ioctl(fd,PWS_NATIVE_PROBE,0)==0);
        REQUIRE(ioctl(fd,PWS_NATIVE_REPORT,&r)==0);
        printf("PW4_PROBE round=%u pm=%d backend=%d rejected=%u cpu=%u regs=%u gates=%u dcp_stopped=%u\n",
               round,r.pm_result,r.backend_result,r.callback_rejected,r.cpu_profile_valid,r.registers_valid,r.gates_clear,r.dcp_stopped);
        REQUIRE(r.version==5&&r.profile==4&&r.pm_called==1&&!r.commit_requested&&r.suspend_samples==1&&r.resume_samples==1&&
                r.irq_fiq_masked&&r.cpu_profile_valid&&r.registers_valid&&r.gates_clear&&r.returned_to_caller&&!r.pm_result&&
                r.backend_check_performed&&!r.backend_result&&!r.callback_rejected);
    }
    printf("PW4_FIRST_KERNEL_READY checks=%u\n",checks);fflush(stdout);sync();
    int rc=ioctl(fd,PWS_NATIVE_COMMIT,0);
    printf("PW4_HANDOFF_RETURN rc=%d errno=%d\n",rc,errno);end(0);
#endif
    return 1;
}
