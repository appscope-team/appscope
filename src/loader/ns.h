#ifndef __NS_H__
#define __NS_H__

#include <stdbool.h>
#include <unistd.h>

#include "scopetypes.h"

// Operation performed from host to container
int nsForkAndExec(pid_t, pid_t, bool);
int nsConfigure(pid_t, void *, size_t);
int nsUnconfigure(pid_t);
int nsGetFile(char *, pid_t);
service_status_t nsService(pid_t, const char *);
service_status_t nsUnservice(pid_t);

// Operation performed from container to host
int nsHostStart(void);
int nsHostStop(void);

#endif // __NS_H__
