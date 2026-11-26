/* Explicit one-shot UTC setting; no firmware/debug/disk/admin operations.
 * Build with src/carrier/sha256.c; Windows: -lsetupapi -lcfgmgr32.
 * Protocol matches sync_detector_clock.py. Never retry a write or serial open.
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include "sha256.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
static HANDLE port=INVALID_HANDLE_VALUE;
static const GUID ports_guid={0x4d36e978,0xe325,0x11ce,{0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18}};
static const GUID comport_guid={0x86e0d1e0,0x8089,0x11d0,{0x9c,0xe4,0x08,0x00,0x3e,0x30,0x1f,0x73}};
static double monotonic(void){return GetTickCount64()/1000.0;}
static void pause_ms(unsigned n){Sleep(n);}
static double utc_now(void){FILETIME ft;ULARGE_INTEGER n;GetSystemTimePreciseAsFileTime(&ft);n.LowPart=ft.dwLowDateTime;n.HighPart=ft.dwHighDateTime;return (n.QuadPart-116444736000000000ULL)/10000000.0;}
#else
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <errno.h>
static int port=-1;
static double now(clockid_t id){struct timespec t;if(clock_gettime(id,&t))return -1;return t.tv_sec+t.tv_nsec/1e9;}
static double monotonic(void){return now(CLOCK_MONOTONIC);}
static double utc_now(void){return now(CLOCK_REALTIME);}
static void pause_ms(unsigned n){struct timespec t={n/1000,(long)(n%1000)*1000000};while(nanosleep(&t,&t)&&errno==EINTR){}}
#endif
static char uid[25],selected_uid[25],port_name[256];
static int uid_valid(const char *s){if(strlen(s)!=24)return 0;for(unsigned n=0;n<24;n++)if(!isxdigit((unsigned char)s[n]))return 0;return 1;}
static void upper(char *s){for(;*s;s++)*s=(char)toupper((unsigned char)*s);}
static int candidate(const char *id,const char *name){
    char tmp[25];if(!uid_valid(id))return 0;memcpy(tmp,id,25);upper(tmp);
    if(selected_uid[0]&&strcmp(tmp,selected_uid))return 0;
    memcpy(uid,tmp,25);snprintf(port_name,sizeof port_name,"%s",name);return 1;
}
#ifdef _WIN32
static int discover(void){
    int count=0;HDEVINFO set=SetupDiGetClassDevsA(&ports_guid,0,0,DIGCF_PRESENT);
    if(set==INVALID_HANDLE_VALUE)return -1;
    SP_DEVINFO_DATA info={.cbSize=sizeof info};
    for(DWORD n=0;SetupDiEnumDeviceInfo(set,n,&info);n++){
        char id[512],instance[512],name[64];DEVINST node=info.DevInst;ULONG status,problem;int exact=0;
        if(CM_Get_DevNode_Status(&status,&problem,node,0)!=CR_SUCCESS||problem||!(status&DN_STARTED))continue;
        if(CM_Get_Device_IDA(node,id,sizeof id,0)!=CR_SUCCESS)continue;
        upper(id);if(strncmp(id,"USB\\VID_1209&PID_0001&MI_00\\",28))continue;
        strcpy(instance,id);
        for(unsigned p=0;p<3;p++){
            DEVINST parent;if(CM_Get_Parent(&parent,node,0)!=CR_SUCCESS)break;node=parent;
            if(CM_Get_Device_IDA(node,id,sizeof id,0)!=CR_SUCCESS)break;upper(id);
            const char prefix[]="USB\\VID_1209&PID_0001\\";
            if(!strncmp(id,prefix,sizeof prefix-1)&&uid_valid(id+sizeof prefix-1)){exact=1;break;}
        }
        if(!exact)continue;
        /* DN_STARTED can precede the enabled COM interface during USB return.
         * Match the existing engineering tool's PRESENT interface readiness;
         * never probe readiness by repeatedly opening the serial device. */
        ULONG chars=0;char links[4096];
        if(CM_Get_Device_Interface_List_SizeA(&chars,(LPGUID)&comport_guid,instance,CM_GET_DEVICE_INTERFACE_LIST_PRESENT)!=CR_SUCCESS||chars<2||chars>sizeof links)continue;
        memset(links,0,sizeof links);
        if(CM_Get_Device_Interface_ListA((LPGUID)&comport_guid,instance,links,chars,CM_GET_DEVICE_INTERFACE_LIST_PRESENT)!=CR_SUCCESS||!links[0])continue;
        size_t first=strnlen(links,chars);
        if(first+1>=chars||links[first+1])continue;
        if(CM_Get_DevNode_Status(&status,&problem,info.DevInst,0)!=CR_SUCCESS||problem||!(status&DN_STARTED))continue;
        HKEY key=SetupDiOpenDevRegKey(set,&info,DICS_FLAG_GLOBAL,0,DIREG_DEV,KEY_QUERY_VALUE);
        if(key==INVALID_HANDLE_VALUE)continue;DWORD type=0,size=sizeof name;
        LONG rc=RegQueryValueExA(key,"PortName",0,&type,(BYTE *)name,&size);RegCloseKey(key);
        if(rc||type!=REG_SZ||size<5||size>=sizeof name||name[size-1]||strncmp(name,"COM",3))continue;
        for(char *q=name+3;*q;q++)if(!isdigit((unsigned char)*q)){name[0]=0;break;}
        if(name[0])count+=candidate(strrchr(id,'\\')+1,name);
    }
    SetupDiDestroyDeviceInfoList(set);return count;
}
static int open_port(void){
    char path[280];snprintf(path,sizeof path,"\\\\.\\%s",port_name);
    port=CreateFileA(path,GENERIC_READ|GENERIC_WRITE,0,0,OPEN_EXISTING,0,0);
    if(port==INVALID_HANDLE_VALUE){fprintf(stderr,"Serial CreateFile error %lu.\n",GetLastError());return 0;}
    DCB dcb={.DCBlength=sizeof dcb};if(!GetCommState(port,&dcb))return 0;
    dcb.BaudRate=115200;dcb.ByteSize=8;dcb.Parity=NOPARITY;dcb.StopBits=ONESTOPBIT;
    dcb.fBinary=1;dcb.fOutxCtsFlow=0;dcb.fOutxDsrFlow=0;dcb.fDsrSensitivity=0;
    dcb.fDtrControl=DTR_CONTROL_ENABLE;dcb.fRtsControl=RTS_CONTROL_DISABLE;dcb.fInX=dcb.fOutX=0;
    COMMTIMEOUTS t={.ReadIntervalTimeout=MAXDWORD,.ReadTotalTimeoutConstant=100,.WriteTotalTimeoutConstant=1000};
    return SetCommState(port,&dcb)&&SetCommTimeouts(port,&t)&&EscapeCommFunction(port,SETDTR);
}
static void close_port(void){if(port!=INVALID_HANDLE_VALUE){EscapeCommFunction(port,CLRDTR);CloseHandle(port);port=INVALID_HANDLE_VALUE;}}
static int send_bytes(const unsigned char *p,unsigned size){DWORD n=0;return WriteFile(port,p,size,&n,0)&&n==size;}
static int receive_bytes(unsigned char *p,unsigned size){DWORD n=0;return ReadFile(port,p,size,&n,0)?(int)n:-1;}
#else
static int read_text(const char *base,const char *leaf,char *out,size_t size){char path[1024];snprintf(path,sizeof path,"%s/%s",base,leaf);FILE *f=fopen(path,"r");if(!f)return 0;int ok=fgets(out,(int)size,f)!=NULL;fclose(f);if(ok)out[strcspn(out,"\r\n")]=0;return ok;}
static int discover(void){
    DIR *dir=opendir("/sys/class/tty");if(!dir)return -1;int count=0;struct dirent *e;
    while((e=readdir(dir))){
        if(strncmp(e->d_name,"ttyACM",6))continue;
        char path[1024],resolved[4096],vendor[32],product[32],serial[80],name[256];
        snprintf(path,sizeof path,"/sys/class/tty/%s/device",e->d_name);if(!realpath(path,resolved))continue;
        for(unsigned n=0;n<6;n++){
            if(read_text(resolved,"idVendor",vendor,sizeof vendor)&&read_text(resolved,"idProduct",product,sizeof product)&&
               !strcmp(vendor,"1209")&&!strcmp(product,"0001")&&read_text(resolved,"serial",serial,sizeof serial)){
                if(strlen(e->d_name)+6>=sizeof name)break;snprintf(name,sizeof name,"/dev/%s",e->d_name);
                count+=candidate(serial,name);break;
            }
            char *slash=strrchr(resolved,'/');if(!slash)break;*slash=0;
        }
    }
    closedir(dir);return count;
}
static int open_port(void){
    port=open(port_name,O_RDWR|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);if(port<0)return 0;
    if(flock(port,LOCK_EX|LOCK_NB)||ioctl(port,TIOCEXCL))return 0;
    struct termios t;if(tcgetattr(port,&t))return 0;cfmakeraw(&t);cfsetispeed(&t,B115200);cfsetospeed(&t,B115200);
    t.c_cflag|=CLOCAL|CREAD;t.c_cflag&=~CRTSCTS;t.c_cc[VMIN]=0;t.c_cc[VTIME]=0;
    if(tcsetattr(port,TCSANOW,&t))return 0;int bits=TIOCM_DTR;return ioctl(port,TIOCMBIS,&bits)==0;
}
static void close_port(void){if(port>=0){int bits=TIOCM_DTR;ioctl(port,TIOCMBIC,&bits);close(port);port=-1;}}
static int send_bytes(const unsigned char *p,unsigned size){return write(port,p,size)==(ssize_t)size;}
static int receive_bytes(unsigned char *p,unsigned size){ssize_t n=read(port,p,size);if(n<0&&(errno==EAGAIN||errno==EINTR))return 0;return (int)n;}
#endif
static uint32_t u32(const unsigned char *b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static void put32(unsigned char *b,uint32_t v){for(unsigned n=0;n<4;n++)b[n]=(unsigned char)(v>>(n*8));}
static uint32_t crc32(const unsigned char *p,unsigned size){uint32_t c=~0U;while(size--){c^=*p++;for(unsigned n=0;n<8;n++)c=(c>>1)^(0xedb88320U&-(c&1));}return ~c;}
static int bcd(unsigned n){return (n&15)>9||(n>>4)>9?-1:(int)((n>>4)*10+(n&15));}
static double calendar(uint32_t tr,uint32_t dr,uint32_t ssr){
    if((tr&~0x3f7f7fU)||(dr&~0xffff3fU))return -1;
    int y=bcd(dr>>16)+2000,m=bcd((dr>>8)&31),d=bcd(dr&63),h=bcd(tr>>16),min=bcd((tr>>8)&127),s=bcd(tr&127);
    if(y<2000||y>2099||m<1||m>12||h<0||h>23||min<0||min>59||s<0||s>59)return -1;
    const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};int leap=y%4==0;
    if(d<1||d>days[m-1]+(m==2&&leap))return -1;
    int n=0;for(int yy=2000;yy<y;yy++)n+=365+(yy%4==0);
    for(int mm=1;mm<m;mm++)n+=days[mm-1]+(mm==2&&leap);n+=d-1;
    if(((dr>>13)&7)!=(unsigned)((n+5)%7+1))return -1;
    return 946684800.0+(double)n*86400+h*3600+min*60+s+((-ssr)&1023)/1024.0;
}
static int decode_reply(const unsigned char reply[192],uint32_t sequence,uint32_t words[32]){
    unsigned char digest[32];
    if(memcmp(reply,"CDS1",4)||u32(reply+4)!=sequence||u32(reply+8)||u32(reply+12)||u32(reply+16)||u32(reply+20)!=128||u32(reply+24)||u32(reply+28))return 0;
    struct sha256_state sha;sha256_start(&sha);sha256_update(&sha,reply+64,128);sha256_finish(&sha,digest);
    if(memcmp(digest,reply+32,32))return 0;
    for(unsigned n=0;n<32;n++)words[n]=u32(reply+64+4*n);
    char actual[25];snprintf(actual,sizeof actual,"%08X%08X%08X",words[16],words[17],words[18]);
    if(words[0]!=0x32544443||words[1]!=1||words[2]!=128||strcmp(actual,uid)||words[4]>6||words[5]>5||words[14]>1||words[19]>3)return 0;
    for(unsigned n=25;n<32;n++)if(words[n])return 0;return 1;
}
static int exchange(unsigned op,uint32_t sequence,uint32_t utc,uint32_t words[32]){
    unsigned char request[24]={0},reply[192];memcpy(request,"CDT1",4);
    put32(request+4,op);put32(request+8,sequence);put32(request+12,utc);put32(request+20,crc32(request,20));
    if(!send_bytes(request,sizeof request))return 0;
    double deadline=monotonic()+3;unsigned used=0;
    while(used<sizeof reply&&monotonic()<deadline){int n=receive_bytes(reply+used,sizeof reply-used);if(n<0)return 0;used+=(unsigned)n;if(!n)pause_ms(5);}
    return used==sizeof reply&&monotonic()<deadline&&decode_reply(reply,sequence,words);
}
static int wait_device(int present){double end=monotonic()+30;while(monotonic()<end){int n=discover();if(n<0||n>1)return 0;if((n==1)==present)return 1;pause_ms(100);}return 0;}
static int offset(const uint32_t *w,double first,double last){
    double rtc=calendar(w[11],w[12],w[13]);if(!w[14]||rtc<0||last<first||last-first>3)return 0;
    double lo=rtc-last,hi=rtc+1.0/1024-first;
    printf("RTC minus computer UTC: [%+.6f, %+.6f] seconds; read exchange %.3f ms.\n",lo,hi,(last-first)*1000);
    puts("Computer clock is the reference. RTC drift and camera-clock error are additional; no calibration claim.");
    return lo>=-3&&hi<=3;
}
static int self_test(void){
    const unsigned char text[]="123456789";unsigned char digest[32];struct sha256_state s;
    const unsigned char expected[32]={0x15,0xe2,0xb0,0xd3,0xc3,0x38,0x91,0xeb,0xb0,0xf1,0xef,0x60,0x9e,0xc4,0x19,0x42,0x0c,0x20,0xe3,0x20,0xce,0x94,0xc6,0x5f,0xbc,0x8c,0x33,0x12,0x44,0x8e,0xb2,0x25};
    sha256_start(&s);sha256_update(&s,text,9);sha256_finish(&s,digest);
    if(crc32(text,9)!=0xcbf43926||memcmp(digest,expected,32))return 0;
    for(unsigned phase=0;phase<1024;phase++)if(calendar(1,0xc101,0U-phase)!=946684801.0+phase/1024.0)return 0;
    return calendar(0x235959,0x248229,0)==1709251199.0&&calendar(0x99,0xc101,0)<0;
}
int main(int argc,char **argv){
    int status=0,set=0,test=0,explicit_action=0,ok=0,uncertain_queue=0;uint32_t before[32],queued[32],actual[32];
    for(int n=1;n<argc;n++){
        if(!strcmp(argv[n],"--status")){status=1;explicit_action++;}
        else if(!strcmp(argv[n],"--set")){set=1;explicit_action++;}
        else if(!strcmp(argv[n],"--self-test")){test=1;explicit_action++;}
        else if(!strcmp(argv[n],"--serial")&&n+1<argc&&uid_valid(argv[n+1])){strcpy(selected_uid,argv[++n]);upper(selected_uid);}
        else{puts("Usage: detector-clock [--set | --status | --self-test] [--serial 24_HEX_UID]");return 2;}
    }
    if(explicit_action>1){fputs("Choose one action.\n",stderr);return 2;}
    if(test){ok=self_test();puts(ok?"Protocol CRC/SHA/calendar/1024-phase self-test PASS":"Self-test FAILED");return !ok;}
    puts("CoughDetector clock 2026-11-25. Finish recording and all file transfers first.");
    puts("Copy this tool to a local directory before running. Close the device in the file manager.");
    if(!set&&!status){char answer[32];printf("Type SET to set UTC once from this computer: ");fflush(stdout);if(!fgets(answer,sizeof answer,stdin)||strcmp(answer,"SET\n"))return 2;set=1;}
    int count=discover();if(count!=1){fprintf(stderr,"Expected one detector; found %d. Connect USB; with multiple detectors use --serial.\n",count);goto done;}
    strcpy(selected_uid,uid);printf("Detector %s on %s\n",uid,port_name);
    if(!open_port()){fputs("Cannot exclusively open serial port. Close other users; Linux requires serial/dialout permission. No request sent.\n",stderr);goto done;}
    uint32_t sequence=(uint32_t)(utc_now()*1000)^(uint32_t)(monotonic()*1000000);if(!sequence)sequence=1;
    double first=utc_now();if(!exchange(0,sequence,0,before)){fputs("Status receipt failed. No set request sent.\n",stderr);goto done;}double last=utc_now();
    if(status){ok=offset(before,first,last);goto done;}
    if((before[4]!=0&&before[4]!=5&&before[4]!=6)||before[19]||before[20]||before[21]||before[3]==sequence){fputs("Clock busy or retained failure; no set request sent.\n",stderr);goto done;}
    double requested_at=utc_now();if(requested_at<946684800||requested_at>=4102444800.0){fputs("Computer UTC outside supported range.\n",stderr);goto done;}
    /* Whole-second protocol: send near the NEXT host second instead of
     * systematically losing up to999ms by truncating an arbitrary instant.
     * The fresh readback still bounds USB/set latency; this is no accuracy promise. */
    double next_second=(uint32_t)requested_at+1.0,until=monotonic()+1.2;
    while((requested_at=utc_now())<next_second&&monotonic()<until)pause_ms(1);
    if(requested_at<next_second||requested_at-next_second>.050){fputs("Host second boundary missed; no set request sent.\n",stderr);goto done;}
    printf("Request sampled %.3f ms after host second boundary.\n",(requested_at-next_second)*1000);
    uint32_t requested=(uint32_t)requested_at;uncertain_queue=1;
    if(!exchange(1,sequence,requested,queued)||queued[3]!=sequence||queued[4]!=1||queued[5]||queued[6]!=requested||queued[15]!=before[15]||queued[22]!=before[22]+1||queued[19]!=3){fputs("Set queue unconfirmed; no retry. Holding serial until pending request expires.\n",stderr);goto done;}
    uncertain_queue=0;close_port();puts("One request accepted; waiting for clock application and USB return...");
    if(!wait_device(0)||!wait_device(1)||!open_port()){fputs("USB return/open not verified; no retry.\n",stderr);goto done;}
    first=utc_now();if(!exchange(0,sequence,0,actual)){fputs("Readback receipt failed; setting unconfirmed.\n",stderr);goto done;}last=utc_now();
    if(actual[3]!=sequence||actual[4]!=4||actual[5]||actual[19]||actual[20]||actual[21]||!actual[14]||actual[15]!=before[15]+1||actual[22]!=before[22]+1||actual[6]!=requested||actual[7]<requested||actual[7]>requested+9||actual[8]!=queued[24]||actual[24]!=queued[24]+1||actual[9]!=1){fputs("Distinct one-shot set and USB return were not verified.\n",stderr);goto done;}
    ok=offset(actual,first,last);if(ok)puts("PASS: one clock setting and fresh readback verified. Serial released; reconnect USB to resume file access if needed.");
done:
    if(uncertain_queue)pause_ms(11000);close_port();if(!ok)fputs("FAILED or outside +/-3 s bound. Do not infer successful synchronization.\n",stderr);
    if(!explicit_action){puts("Press Enter to close.");char line[8];if(!fgets(line,sizeof line,stdin))fflush(stdout);}
    return !ok;
}
