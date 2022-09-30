#define _GNU_SOURCE
#include <fcntl.h>
#include <stdlib.h>

#include "ns.h"
#include "setup.h"
#include "scopestdlib.h"

#define LDSCOPE_IN_CHILD_NS "/tmp/ldscope"
#define LDSCOPE_IN_HOST_NS "/usr/lib/appscope/ldscope"          // TODO
#define LDSCOPE_CONFIG_IN_CHILD "/tmp/scope_filter"         // TODO
#define LDSCOPE_CONFIG_IN_HOST "/usr/lib/appscope/scope_filter" // TODO
#define VALID_NS_DEPTH 2
#define SCOPE_CRONTAB "* * * * * root /tmp/scope_att.sh\n"
#define SCOPE_CRON_SCRIPT "#! /bin/bash\ntouch /tmp/scope_test\nrm /etc/cron.d/scope_cron\n%s start -f < %s/scope_filter\n"
#define SCOPE_CRON_PATH "/etc/cron.d/scope_cron"
#define SCOPE_SCRIPT_PATH "/tmp/scope_att.sh"
#define SCOPE_EXEC_PATH "/usr/lib/appscope"       // TODO: not correct, needs to be dynamic
#define SCOPE_EXEC "/usr/lib/appscope/scope"      // TODO: not correct, needs to be dynamic
#define SCOPE_EXEC_IN_CHILD "/tmp/scope"      // TODO: not correct, needs to be dynamic

/*
 * Extract file from parent namespace into child namespace.
 *
 * Returns TRUE in case of success, FALSE otherwise.
 */
static bool
extractMemToChildNamespace(char *inputMem, size_t inputSize, const char *outFile, mode_t outPermFlag)
{
    bool status = FALSE;
    int outFd;

    if ((outFd = scope_open(outFile, O_RDWR | O_CREAT, outPermFlag)) == -1) {
        scope_perror("scope_open failed");
        return status;
    }

    if (scope_ftruncate(outFd, inputSize) != 0) {
        goto cleanupDestFd;
    }

    char *dest = scope_mmap(NULL, inputSize, PROT_READ | PROT_WRITE, MAP_SHARED, outFd, 0);
    if (dest == MAP_FAILED) {
        goto cleanupDestFd;
    }

    scope_memcpy(dest, inputMem, inputSize);

    scope_munmap(dest, inputSize);

    status = TRUE;

cleanupDestFd:

    scope_close(outFd);

    return status;
}

static bool
setNamespace(pid_t pid, const char *ns)
{
    char nsPath[PATH_MAX] = {0};
    int nsFd;
    if (scope_snprintf(nsPath, sizeof(nsPath), "/proc/%d/ns/%s", pid, ns) < 0) {
        scope_perror("scope_snprintf failed");
        return FALSE;
    }

    if ((nsFd = scope_open(nsPath, O_RDONLY)) == -1) {
        scope_perror("scope_open failed");
        return FALSE;
    }

    if (scope_setns(nsFd, 0) != 0) {
        scope_perror("setns failed");
        return FALSE;
    }

    return TRUE;
}

static bool
join_namespace(pid_t hostPid)
{
    bool status = FALSE;
    size_t ldscopeSize = 0;
    size_t cfgSize = 0;

    char path[PATH_MAX] = {0};

    if (scope_readlink("/proc/self/exe", path, sizeof(path) - 1) == -1) {
        return status;
    }

    char *ldscopeMem = setupLoadFileIntoMem(&ldscopeSize, path);
    if (ldscopeMem == NULL) {
        return status;
    }

    // Configuration is optional
    char *scopeCfgMem = setupLoadFileIntoMem(&cfgSize, getenv("SCOPE_CONF_PATH"));

    /*
    * Reassociate current process to the "child namespace"
    * - PID namespace - allows to "child process" of the calling process 
    *   be created in separate namespace
    *   In other words the calling process will not change it's ownPID
    *   namespace
    * - mount namespace - allows to copy file(s) into a "child namespace"
    */
    if (setNamespace(hostPid, "pid") == FALSE) {
        goto cleanupMem;
    }
    if (setNamespace(hostPid, "mnt") == FALSE) {
        goto cleanupMem;
    }

    status = extractMemToChildNamespace(ldscopeMem, ldscopeSize, LDSCOPE_IN_CHILD_NS, 0775);

    if (scopeCfgMem) {
        char scopeCfgPath[PATH_MAX] = {0};

        scope_snprintf(scopeCfgPath, sizeof(scopeCfgPath), LDSCOPE_CONFIG_IN_CHILD, hostPid);
        status = extractMemToChildNamespace(scopeCfgMem, cfgSize, scopeCfgPath, 0664);
        // replace the SCOPE_CONF_PATH with namespace path
        setenv("SCOPE_CONF_PATH", scopeCfgPath, 1);
    }   

cleanupMem:

    scope_munmap(ldscopeMem, ldscopeSize);

    if (scopeCfgMem) {
        scope_munmap(scopeCfgMem, cfgSize);
    }

    return status;
}

/*
 * Check for PID in the child namespace.
 *
 * Returns TRUE if specific process contains two namespaces FALSE otherwise.
 */
bool
nsIsPidInChildNs(pid_t pid, pid_t *nsPid)
{
    char path[PATH_MAX] = {0};
    char buffer[4096];
    bool status = FALSE;
    int lastNsPid = 0;
    int nsDepth = 0;

    if (scope_snprintf(path, sizeof(path), "/proc/%d/status", pid) < 0) return FALSE;

    FILE *fstream = scope_fopen(path, "r");

    if (fstream == NULL) {
        return FALSE;
    }

    while (scope_fgets(buffer, sizeof(buffer), fstream)) {
        if (scope_strstr(buffer, "NSpid:")) {
            const char delimiters[] = ": \t";
            char *entry, *last;

            entry = scope_strtok_r(buffer, delimiters, &last);
            // Skip NsPid string
            entry = scope_strtok_r(NULL, delimiters, &last);
            // Iterate over NsPids values
            while (entry != NULL) {
                lastNsPid = scope_atoi(entry);
                entry = scope_strtok_r(NULL, delimiters, &last);
                nsDepth++;
            }
            break;
        }
    }

    /*
    * TODO: we currently tested nesting depth 
    * equals VALID_NS_DEPTH, check more depth level
    */
    if (nsDepth == VALID_NS_DEPTH) {
        status = TRUE;
        *nsPid = lastNsPid;
    }

    scope_fclose(fstream);

    return status;
}

 /*
 * Setup the service for specified child process
 * Returns status of operation SERVICE_STATUS_SUCCESS in case of success, other values in case of failure
 */
service_status_t
nsService(pid_t pid, const char *serviceName) {

    if (setNamespace(pid, "mnt") == FALSE) {
        return SERVICE_STATUS_ERROR_OTHER;
    }

    return setupService(serviceName);
}

 
 /*
 * Configure the child mount namespace
 * - switch the mount namespace to child
 * - configure the setup
 * Returns status of operation 0 in case of success, other values in case of failure
 */
int
nsConfigure(pid_t pid, void *scopeCfgFilterMem, size_t filterFileSize)
{
    if (setNamespace(pid, "mnt") == FALSE) {
        scope_fprintf(scope_stderr, "setNamespace mnt failed\n");
        return EXIT_FAILURE;
    }

    if (setupConfigure(scopeCfgFilterMem, filterFileSize)) {
        scope_fprintf(scope_stderr, "setup child namespace failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
 
 /*
 * Perform fork and exec which cause that direct children
 * effectively will join a new PID namespace.
 *
 * Reassociating the PID namespace (setns CLONE_NEWPID) has somewhat different from 
 * other namespace types. Reassociating the calling thread with a PID namespace
 * changes only the PID namespace that subsequently created child processes of
 * the caller will be placed in. It does not change the PID namespace of the caller itself.
 *
 * Returns status of operation
 */
int
nsForkAndExec(pid_t parentPid, pid_t nsPid)
{
    if (join_namespace(parentPid) == FALSE) {
        scope_fprintf(scope_stderr, "error: join_namespace failed\n");
        return EXIT_FAILURE; 
    }
    pid_t child = fork();
    if (child < 0) {
        scope_fprintf(scope_stderr, "error: fork() failed\n");
        return EXIT_FAILURE;
    } else if (child == 0) {
        // Child
        char *nsAttachPidStr = NULL;
        if (scope_asprintf(&nsAttachPidStr, "%d", nsPid) <= 0) {
            scope_perror("error: asprintf() failed\n");
            return EXIT_FAILURE;
        }
        int execArgc = 0;
        char **execArgv = scope_calloc(4, sizeof(char *));
        if (!execArgv) {
            scope_fprintf(scope_stderr, "error: calloc() failed\n");
            return EXIT_FAILURE;
        }

        execArgv[execArgc++] = LDSCOPE_IN_CHILD_NS;

        execArgv[execArgc++] = "-a";
        execArgv[execArgc++] = nsAttachPidStr;

        return execve(LDSCOPE_IN_CHILD_NS, execArgv, environ);
    }
    // Parent
    int status;
    scope_waitpid(child, &status, 0);
    if (WIFEXITED(status)) {
        int exitChildStatus = WEXITSTATUS(status);
        if (exitChildStatus == 0) {
            scope_fprintf(scope_stderr, "Attach to process %d in child process succeeded\n", parentPid);
        } else {
            scope_fprintf(scope_stderr, "Attach to process %d in child process failed\n", parentPid);
        }
        return exitChildStatus;
    }
    scope_fprintf(scope_stderr, "error: attach failed() failed\n");
    return EXIT_FAILURE;
}

/* Create the cron file
 *
 * When the start command is executed within a container we can't
 * set ns to that of a host process. Therefore, start a process in the
 * host context using crond. This process will run a script which will
 * run the start command in the context of the host. It should run once and
 * then clean up after itself.
 *
 * This should be called after the fs namespace has been switched.
 */
static bool
create_cron(void)
{
    int outFd;
    char buf[1024];
    char path[PATH_MAX] = {0};

    // Create the script to be executed by cron
    if (scope_snprintf(path, sizeof(path), SCOPE_SCRIPT_PATH) < 0) {
        scope_perror("create_cron: script path: error: snprintf() failed\n");
        scope_fprintf(scope_stdout, "path: %s\n", path);
        return FALSE;
    }

    if ((outFd = scope_open(path, O_RDWR | O_CREAT, 0775)) == -1) {
        scope_perror("create_cron: script path: scope_open failed");
        scope_fprintf(scope_stdout, "path: %s\n", path);
        return FALSE;
    }

    if (scope_snprintf(buf, sizeof(buf), SCOPE_CRON_SCRIPT, SCOPE_EXEC, SCOPE_EXEC_PATH) < 0) {
        scope_perror("create_cron: script: error: snprintf() failed\n");
        scope_close(outFd);
        return FALSE;
    }

    if (scope_write(outFd, buf, scope_strlen(buf)) == -1) {
        scope_perror("create_cron: script: scope_write failed");
        scope_fprintf(scope_stdout, "path: %s\n", path);
        scope_close(outFd);
        return FALSE;
    }

    if (scope_close(outFd) == -1) {
        scope_perror("create_cron: script: scope_close failed");
        scope_fprintf(scope_stdout, "path: %s\n", path);
        return FALSE;
    }

    // Create the cron entry
    if (scope_snprintf(path, sizeof(path), SCOPE_CRON_PATH) < 0) {
        scope_perror("create_cron: cron: error: snprintf() failed\n");
        scope_fprintf(scope_stdout, "path: %s\n", path);
        return FALSE;
    }

    if (scope_snprintf(buf, sizeof(buf), SCOPE_CRONTAB) < 0) {
        scope_perror("create_cron: cron: error: snprintf() failed\n");
        scope_fprintf(scope_stdout, "path: %s\n", path);
        return FALSE;
    }

    if ((outFd = scope_open(path, O_RDWR | O_CREAT, 0775)) == -1) {
        scope_perror("create_cron: cron: scope_open failed");
        scope_fprintf(scope_stdout, "path: %s\n", path);
        return FALSE;
    }

    // crond will detect this file entry and run on its' next cycle
    if (scope_write(outFd, buf, scope_strlen(buf)) == -1) {
        scope_perror("create_cron: cron: scope_write failed");
        scope_fprintf(scope_stdout, "path: %s\n", path);
        scope_close(outFd);
        return FALSE;
    }

    if (scope_close(outFd) == -1) {
        scope_perror("create_cron: cron: scope_close failed");
        scope_fprintf(scope_stdout, "path: %s\n", path);
        return FALSE;
    }

    return TRUE;

}

static bool
setHostNamespace(const char *ns)
{
    int nsFd;
    char *nsPp;
    char procPath[64];
    char nsPath[PATH_MAX] = {0};

    // $CRIBL_EDGE_FS_ROOT else /hostfs
    if ((nsPp = getenv("CRIBL_EDGE_FS_ROOT"))) {
        scope_strncpy(nsPath, nsPp, sizeof(nsPath));
    } else {
        scope_strncpy(nsPath, "/hostfs", 10);
    }

    if (scope_snprintf(procPath, sizeof(procPath), "/proc/1/ns/%s", ns) < 0) {
        scope_perror("setHostNamespace: scope_snprintf failed");
        return FALSE;
    }

    scope_strncat(nsPath, procPath, sizeof(procPath) - 1);

    if ((nsFd = scope_open(nsPath, O_RDONLY)) == -1) {
        scope_perror("setHostNamespace: scope_open failed");
        return FALSE;
    }

    if (scope_setns(nsFd, 0) != 0) {
        scope_perror("setHostNamespace: setns failed");
        return FALSE;
    }

    // TODO: should we close here?
    return TRUE;
}

static bool
join_host_namespace(void)
{
    bool status = FALSE;
    size_t ldscopeSize = 0;
    size_t cfgSize = 0;
    size_t scopeSize = 0;
    char path[PATH_MAX] = {0};

    if (scope_readlink("/proc/self/exe", path, sizeof(path) - 1) == -1) {
        return status;
    }

    char *ldscopeMem = setupLoadFileIntoMem(&ldscopeSize, path);
    if (ldscopeMem == NULL) {
        return status;
    }

    // Configuration is optional
    char *scopeCfgPath = NULL;

    if ((scopeCfgPath = getenv("SCOPE_CONF_PATH")) == NULL) {
        scope_snprintf(path, sizeof(path), LDSCOPE_CONFIG_IN_CHILD);
        scopeCfgPath = path;
    }

    char *scopeCfgMem = setupLoadFileIntoMem(&cfgSize, scopeCfgPath);

    // Get the scope exec in addition to ldscope
    scope_snprintf(path, sizeof(path), SCOPE_EXEC_IN_CHILD);
    char *scopeMem = setupLoadFileIntoMem(&scopeSize, path);
    if (scopeMem == NULL) {
        return status;
    }

    /*
     * Reassociate current process to the host namespace
     * - mount namespace - allows to copy file(s) into the host fs
     */
    if (setHostNamespace("mnt") == FALSE) {
        goto cleanupMem;
    }

    /*
     * At this point we are using the host fs.
     * Ensure that we have the dest dir
     */
    if (opendir(SCOPE_EXEC_PATH) == NULL) {
        if (mkdir(SCOPE_EXEC_PATH, 0755) == -1) {
            scope_perror("join_host_namespace: mkdir failed");
            goto cleanupMem;
        }
    }

    if ((status = extractMemToChildNamespace(ldscopeMem, ldscopeSize, LDSCOPE_IN_HOST_NS, 0775)) == FALSE) {
        goto cleanupMem;
    }

    if (scopeCfgMem) {
        char scopeCfgPath[PATH_MAX] = {0};

        scope_snprintf(scopeCfgPath, sizeof(scopeCfgPath), LDSCOPE_CONFIG_IN_HOST);
        if ((status == extractMemToChildNamespace(scopeCfgMem, cfgSize, scopeCfgPath, 0664)) == FALSE) {
            goto cleanupMem;
        }

        // replace the SCOPE_CONF_PATH with namespace path
        setenv("SCOPE_CONF_PATH", scopeCfgPath, 1);
    }

    status = extractMemToChildNamespace(scopeMem, scopeSize, SCOPE_EXEC, 0775);

cleanupMem:

    scope_munmap(ldscopeMem, ldscopeSize);

    if (scopeCfgMem) {
        scope_munmap(scopeCfgMem, cfgSize);
    }

    scope_munmap(scopeMem, scopeSize);

    return status;
}

int
nsHostStart(void)
{
    scope_fprintf(scope_stdout, "Executing from a container, run the start command from the host\n");

    if (join_host_namespace() == FALSE) {
        scope_fprintf(scope_stderr, "error: join_host_namespace failed\n");
        return EXIT_FAILURE;
    }

    create_cron();

    return EXIT_SUCCESS;
}
