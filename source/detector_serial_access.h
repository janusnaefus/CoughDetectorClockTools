#ifndef DETECTOR_SERIAL_ACCESS_H
#define DETECTOR_SERIAL_ACCESS_H
/* Linux diagnostics only: never opens a port, changes permissions or runs sudo. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <grp.h>
#include <pwd.h>

static int detector_account_name(const char *name)
{
    if(!name||!name[0]||name[0]=='-')return 0;
    for(const unsigned char *p=(const unsigned char *)name;*p;p++)
        if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||
             (*p>='0'&&*p<='9')||*p=='_'||*p=='-'||*p=='.'||*p=='$'))return 0;
    return 1;
}
static int detector_current_group(gid_t group)
{
    if(group==getgid()||group==getegid())return 1;
    int count=getgroups(0,NULL);if(count<0||count>65536)return -1;
    gid_t *groups=malloc((size_t)(count?count:1)*sizeof(*groups));if(!groups)return -1;
    int found=0,actual=getgroups(count,groups);
    if(actual<0)found=-1;
    else for(int n=0;n<actual;n++)if(groups[n]==group)found=1;
    free(groups);return found;
}
static void detector_serial_open_error(FILE *out,const char *path,int error)
{
    fprintf(out,"Cannot open %s: %s. No request sent.\n",path,strerror(error));
    if(error!=EACCES&&error!=EPERM){
        fputs("If busy, close other serial applications; reconnect if the device disappeared.\n",out);return;
    }
    struct stat node;
    if(stat(path,&node)){fputs("Device permissions could not be inspected. Reconnect and try once.\n",out);return;}
    if(!S_ISCHR(node.st_mode)){fputs("The detected path is no longer a character device. Reconnect.\n",out);return;}
    fprintf(out,"Actual node mode %03o, owner UID %lu, group GID %lu; current UID %lu.\n",
        (unsigned)(node.st_mode&0777),(unsigned long)node.st_uid,
        (unsigned long)node.st_gid,(unsigned long)geteuid());
    if(access(path,R_OK|W_OK)==0){
        fputs("Current node permissions already allow read/write. Ask the administrator to inspect other access policies.\n",out);return;
    }
    struct group *group=getgrgid(node.st_gid);
    char group_name[256]={0};
    if(group&&detector_account_name(group->gr_name)&&strlen(group->gr_name)<sizeof(group_name))
        strcpy(group_name,group->gr_name);
    struct passwd *user=getpwuid(geteuid());
    int member=detector_current_group(node.st_gid);
    if(geteuid()!=0&&getuid()==geteuid()&&node.st_uid!=geteuid()&&node.st_gid!=0&&
       (node.st_mode&(S_IRGRP|S_IWGRP))==(S_IRGRP|S_IWGRP)&&member==0&&
       group_name[0]&&user&&detector_account_name(user->pw_name)){
        fprintf(out,"The actual device group '%s' has read/write access; this login is not a member.\n",group_name);
        fprintf(out,"Ask your administrator to approve adding '%s' to this group. On systems with usermod:\n",user->pw_name);
        fprintf(out,"  sudo usermod -aG '%s' -- '%s'\n",group_name,user->pw_name);
        fputs("Then fully log out and log in again, reconnect, and rerun the tool. Nothing was changed automatically.\n",out);
    }else{
        fputs("Ask the administrator to repair this device's permissions/ACL or udev rule. Adding an assumed serial group is not sufficient.\n",out);
    }
}
#endif
