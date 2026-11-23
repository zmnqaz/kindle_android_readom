/* Readom PW4: user lookups from /etc/passwd directly. A static glibc's
 * getpwnam/getpwuid go through NSS, which needs the target's shared
 * libnss_files and failed on the Kindle (dropbear closed every connection
 * at user authentication). fgetpwent reads the file itself. */
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
static struct passwd *find(const char *name, uid_t uid, int by_name)
{
    static struct passwd copy;
    static char buf[5][256];
    struct passwd *p;
    FILE *f = fopen("/etc/passwd", "r");
    if (!f) return NULL;
    while ((p = fgetpwent(f))) {
        if (by_name ? !strcmp(p->pw_name, name) : p->pw_uid == uid) {
            copy = *p;
            snprintf(buf[0], 256, "%s", p->pw_name); copy.pw_name = buf[0];
            snprintf(buf[1], 256, "%s", p->pw_passwd); copy.pw_passwd = buf[1];
            snprintf(buf[2], 256, "%s", p->pw_gecos); copy.pw_gecos = buf[2];
            snprintf(buf[3], 256, "%s", p->pw_dir); copy.pw_dir = buf[3];
            snprintf(buf[4], 256, "%s", p->pw_shell); copy.pw_shell = buf[4];
            fclose(f);
            return &copy;
        }
    }
    fclose(f);
    return NULL;
}
struct passwd *getpwnam(const char *name) { return find(name, 0, 1); }
struct passwd *getpwuid(uid_t uid) { return find(NULL, uid, 0); }

/* The same for the other NSS lookups dropbear's server makes. getspnam is
 * asked for every login to fill in the password hash (common-session.c), and
 * the Kindle's nsswitch made that load a module the static libc cannot:
 * every connection died in _dl_call_libc_early_init. No passwords are used
 * here, so there is nothing to find. Groups come from /etc/group. */
#include <grp.h>
#include <shadow.h>
#include <unistd.h>
struct spwd *getspnam(const char *name) { (void)name; return NULL; }
static struct group *findgr(const char *name, gid_t gid, int by_name)
{
    static struct group copy;
    static char gname[256];
    static char *nomem[1] = {NULL};
    struct group *g;
    FILE *f = fopen("/etc/group", "r");
    if (!f) return NULL;
    while ((g = fgetgrent(f))) {
        if (by_name ? !strcmp(g->gr_name, name) : g->gr_gid == gid) {
            copy = *g;
            snprintf(gname, sizeof(gname), "%s", g->gr_name);
            copy.gr_name = gname; copy.gr_passwd = (char *)""; copy.gr_mem = nomem;
            fclose(f);
            return &copy;
        }
    }
    fclose(f);
    return NULL;
}
struct group *getgrnam(const char *name) { return findgr(name, 0, 1); }
struct group *getgrgid(gid_t gid) { return findgr(NULL, gid, 0); }
int initgroups(const char *user, gid_t group) { (void)user; return setgroups(1, &group); }
int getgrouplist(const char *user, gid_t group, gid_t *groups, int *ngroups)
{
    (void)user;
    if (*ngroups < 1) { *ngroups = 1; return -1; }
    groups[0] = group; *ngroups = 1; return 1;
}
