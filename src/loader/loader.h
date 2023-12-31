#ifndef __LOADER_H__
#define __LOADER_H__

#include <unistd.h>

extern int g_log_level;

int cmdService(char *, pid_t);
int cmdUnservice(pid_t);
int cmdAttach(pid_t, const char *);
int cmdDetach(pid_t, const char *);
int cmdRun(pid_t, pid_t, int, char **);
int cmdInstall(const char *);
int cmdRules(const char *, const char *);
int cmdPreload(const char *, const char *);
int cmdMount(pid_t pid, const char *, const char *);

#endif // __LOADER_H__s
