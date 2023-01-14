#ifndef __LOADERUTILS_H__
#define __LOADERUTILS_H__

#ifdef __linux__

#include <elf.h>
#include <link.h>

typedef struct {
    char *cmd;
    char *buf;
    int len;
    unsigned char *text_addr;
    uint64_t       text_len;
} elf_buf_t;

void freeElf(char *, size_t);
elf_buf_t * getElf(char *);
void *getSymbol(const char *, char *);
void *getDynSymbol(const char *, char *);
bool is_static(char *);
bool is_go(char *);
void setPidEnv(int);
char *getpath(const char *);
uint64_t findLibrary(const char *, pid_t, bool);
int getExePath(pid_t, char **);
int findFd(pid_t, const char *);
int getProcUidGid(pid_t, uid_t *, gid_t *);
    
#endif // __linux__
#endif // __LOADERUTILS_H__
