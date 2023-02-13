#define _GNU_SOURCE

#include <fcntl.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "libver.h"
#include "libdir.h"
#include "ns.h"
#include "nsinfo.h"
#include "nsfile.h"
#include "setup.h"
#include "scopetypes.h"

#define SCOPE_CRONTAB "* * * * * root /tmp/att.sh\n"
#define SCOPE_START_SCRIPT "#! /bin/bash\nrm /etc/cron.d/cron\n%s start -f < %s\nrm -- $0\n"
#define SCOPE_STOP_SCRIPT "#! /bin/bash\nrm /etc/cron.d/cron\n%s stop -f\nrm -- $0\n"

// NS Action types
typedef enum {
    START = 0,
    STOP = 1,
} ns_action_t;

/*
 * Extract memory to specific output file.
 *
 * Returns TRUE in case of success, FALSE otherwise.
 */
static bool
extractMemToFile(char *inputMem, size_t inputSize, const char *outFile, mode_t outPermFlag, bool overwrite, uid_t nsUid, gid_t nsGid) {
    bool status = FALSE;
    int outFd;

    if (!access(outFile, R_OK) && !overwrite) {
        return TRUE;
    }

    if ((outFd = nsFileOpenWithMode(outFile, O_RDWR | O_CREAT, outPermFlag, nsUid, nsGid, geteuid(), getegid())) == -1) {
        return status;
    }

    if (ftruncate(outFd, inputSize) != 0) {
        goto cleanupDestFd;
    }

    char *dest = mmap(NULL, inputSize, PROT_READ | PROT_WRITE, MAP_SHARED, outFd, 0);
    if (dest == MAP_FAILED) {
        goto cleanupDestFd;
    }

    memcpy(dest, inputMem, inputSize);

    munmap(dest, inputSize);

    status = TRUE;

cleanupDestFd:

    fchmod(outFd, outPermFlag);

    close(outFd);

    return status;
}

/*
 * Reassociate process identified with pid with a specific namespace described by ns.
 *
 * Returns TRUE if operation was success, FALSE otherwise.
 */
static bool
setNamespace(pid_t pid, const char *ns) {
    char nsPath[PATH_MAX] = {0};
    int nsFd;
    if (snprintf(nsPath, sizeof(nsPath), "/proc/%d/ns/%s", pid, ns) < 0) {
        perror("setNamespace: snprintf failed");
        return FALSE;
    }

    if ((nsFd = open(nsPath, O_RDONLY)) == -1) {
        perror("setNamespace: open failed");
        return FALSE;
    }

    if (setns(nsFd, 0) != 0) {
        perror("setNamespace: setns failed");
        close(nsFd);
        return FALSE;
    }

    close(nsFd);

    return TRUE;
}

/*
 * Get the namespace file descriptor for the namespace you are currently in
 */
static int
getSelfNamespace(const char *ns) {
    char nsPath[PATH_MAX] = {0};
    int nsFd = -1;
    if (snprintf(nsPath, sizeof(nsPath), "/proc/self/ns/%s", ns) < 0) {
        perror("getSelfNamespace: snprintf failed");
        return nsFd;
    }

    if ((nsFd = open(nsPath, O_RDONLY)) == -1) {
        perror("getSelfNamespace: open failed");
        return nsFd;
    }

    return nsFd;
}

/*
 * Joins the namespaces:
 * - child PID (optionally)
 * - mount namespace (mandatory).
 *
 * Returns TRUE if operation was success, FALSE otherwise.
 */
static bool
joinChildNamespace(pid_t hostPid, bool joinPidNs) {
    bool status = FALSE;
    size_t scopeSize = 0;
    size_t cfgSize = 0;
    mkdir_status_t dirRes = MKDIR_STATUS_ERR_OTHER;

    char path[PATH_MAX] = {0};

    uid_t nsUid = nsInfoTranslateUid(hostPid);
    gid_t nsGid = nsInfoTranslateGid(hostPid);

    if (readlink("/proc/self/exe", path, sizeof(path) - 1) == -1) {
        return status;
    }

    char *scopeMem = setupLoadFileIntoMem(&scopeSize, path);
    if (scopeMem == NULL) {
        return status;
    }

    // Configuration is optional
    char *scopeCfgMem = setupLoadFileIntoMem(&cfgSize, getenv("SCOPE_CONF_PATH"));

    /*
    * Reassociate current process to the "child namespace"
    * - PID namespace - allows to "child process" of the calling process 
    *   be created in separate namespace
    *   In other words the calling process will not change it's own PID
    *   namespace
    * - mount namespace - allows to copy file(s) into a "child namespace"
    */
    if (joinPidNs && setNamespace(hostPid, "pid") == FALSE) {
        goto cleanupMem;
    }

    if (setNamespace(hostPid, "mnt") == FALSE) {
        goto cleanupMem;
    }

    const char *loaderVersion = libverNormalizedVersion(SCOPE_VER);
    bool isDevVersion = libverIsNormVersionDev(loaderVersion);

    /* For official version try to use /usr/lib/appscope */
    if (isDevVersion == FALSE) {
        memset(path, 0, PATH_MAX);
        snprintf(path, PATH_MAX, "/usr/lib/appscope/%s/", loaderVersion);
        dirRes = libdirCreateDirIfMissing(path, 0755, nsUid, nsGid);
        if (dirRes <= MKDIR_STATUS_EXISTS) {
            strncat(path, "scope", sizeof(path) - 1);
            status = extractMemToFile(scopeMem, scopeSize, path, 0775, isDevVersion, nsUid, nsGid);
        }
    }

    /* For dev version or if extract for official version try to use /tmp/appscope path */
    if (status == FALSE) {
        memset(path, 0, PATH_MAX);
        snprintf(path, PATH_MAX, "/tmp/appscope/%s/", loaderVersion);
        dirRes = libdirCreateDirIfMissing(path, 0777, nsUid, nsGid);
        if (dirRes <= MKDIR_STATUS_EXISTS) {
            strncat(path, "scope", sizeof(path) - 1);
            status = extractMemToFile(scopeMem, scopeSize, path, 0775, isDevVersion, nsUid, nsGid);
        }
    }

    /* Cleanup if extraction of scope fails */
    if (status == FALSE) {
        goto cleanupMem;
    }

    if (scopeCfgMem) {
        char scopeCfgPath[PATH_MAX] = {0};

        // extract scope.yml configuration
        snprintf(scopeCfgPath, sizeof(scopeCfgPath), "/tmp/scope%d.yml", hostPid);
        status = extractMemToFile(scopeCfgMem, cfgSize, scopeCfgPath, 0664, TRUE, nsUid, nsGid);
        // replace the SCOPE_CONF_PATH with namespace path
        setenv("SCOPE_CONF_PATH", scopeCfgPath, 1);
    }   

cleanupMem:

    munmap(scopeMem, scopeSize);

    if (scopeCfgMem) {
        munmap(scopeCfgMem, cfgSize);
    }

    return status;
}

/*
 * Setup the service for specified child process
 * Returns status of operation SERVICE_STATUS_SUCCESS in case of success, other values in case of failure
 */
service_status_t
nsService(pid_t hostPid, const char *serviceName) {

    uid_t nsUid = nsInfoTranslateUid(hostPid);
    gid_t nsGid = nsInfoTranslateGid(hostPid);

    if (setNamespace(hostPid, "mnt") == FALSE) {
        return SERVICE_STATUS_ERROR_OTHER;
    }

    return setupService(serviceName, nsUid, nsGid);
}

/*
 * Remove scope from all services in a given namespace
 * Returns status of operation SERVICE_STATUS_SUCCESS in case of success, other values in case of failure
 */
service_status_t
nsUnservice(pid_t hostPid) {
    if (setNamespace(hostPid, "mnt") == FALSE) {
        return SERVICE_STATUS_ERROR_OTHER;
    }

    return setupUnservice();
}
 
 /*
 * Configure the child mount namespace
 * - switch the mount namespace to child
 * - configure the setup
 * Returns status of operation 0 in case of success, other values in case of failure
 */
int
nsConfigure(pid_t pid, void *scopeCfgFilterMem, size_t filterFileSize) {
    uid_t nsUid = nsInfoTranslateUid(pid);
    gid_t nsGid = nsInfoTranslateGid(pid);

    if (setNamespace(pid, "mnt") == FALSE) {
        fprintf(stderr, "setNamespace mnt failed\n");
        return EXIT_FAILURE;
    }

    if (setupConfigure(scopeCfgFilterMem, filterFileSize, nsUid, nsGid)) {
        fprintf(stderr, "setup child namespace failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

 /*
 * Unconfigure the child mount namespace
 * - switch the mount namespace to child
 * - unconfigure the setup
 * Returns status of operation 0 in case of success, other values in case of failure
 */
int
nsUnconfigure(pid_t pid) {
    if (setNamespace(pid, "mnt") == FALSE) {
        fprintf(stderr, "setNamespace mnt failed\n");
        return EXIT_FAILURE;
    }

    if (setupUnconfigure()) {
        fprintf(stderr, "setup child namespace failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
 * Get a file from src_path from a child mount namespace
 * and write it to a file at dest_path in the host namespace
 */
int
nsGetFile(const char *src_path, const char *dest_path, pid_t pid) {
    int ns_fd = -1;
    int read_fd = -1;
    int write_fd = -1;
    char *buf = NULL;
    char host_wd[PATH_MAX];

    // Save host current working directory
    if (!getcwd(host_wd, PATH_MAX)) {
        perror("getcwd");
        goto err;
    }

    // Save host mount namespace file descriptor
    ns_fd = getSelfNamespace("mnt");
    if (ns_fd == -1) {
        fprintf(stderr, "error: getSelfNamespace failed\n");
        goto err;
    }

    // Change to namespace
    if (setNamespace(pid, "mnt") == FALSE) {
        fprintf(stderr, "error: setNamespace mnt failed\n");
        goto err;
    }

    // Read the file into memory
    struct stat st;
    if (stat(src_path, &st)) {
        fprintf(stderr, "error: src file not found\n");
        goto err;
    }

    buf = (char *)malloc(st.st_size);
    if (!buf) {
        perror("malloc");
        goto err;
    }

    read_fd = open(src_path, O_RDONLY);
    if (read_fd == -1) {
        perror("open");
        goto err;
    }

    if (read(read_fd, buf, st.st_size) != st.st_size) {
        perror("read");
        goto err;
    }

    // Return to host ns
    if (setns(ns_fd, 0) != 0) {
        perror("setns");
        goto err;
    }

    // Return to host current working directory
    if (chdir(host_wd) == -1) {
        perror("chdir");
        goto err;
    }

    // Write the file to disk
    write_fd = open(dest_path, O_WRONLY | O_CREAT);
    if (write_fd == -1) {
        perror("open");
        goto err;
    }

    if (write(write_fd, buf, st.st_size) != st.st_size) {
        perror("write");
        goto err;
    }

    free(buf);
    close(read_fd);
    close(write_fd);
    close(ns_fd);
    return EXIT_SUCCESS;
err:
    if (buf) free(buf);
    if (read_fd > -1) close(read_fd);
    if (write_fd > -1) close(write_fd);
    if (ns_fd > -1) close(ns_fd);
    return EXIT_FAILURE;
}

 /*
 * Check if libscope.so is loaded in specified PID
 * Returns TRUE if library is loaded, FALSE otherwise.
 */
static bool
isLibScopeLoaded(pid_t pid)
{
    char mapsPath[PATH_MAX] = {0};
    char buffer[9076];
    FILE *fd;
    bool status = FALSE;

    if (snprintf(mapsPath, sizeof(mapsPath), "/proc/%d/maps", pid) < 0) {
        return status;
    }

    if ((fd = fopen(mapsPath, "r")) == NULL) {
        return status;
    }

    while (fgets(buffer, sizeof(buffer), fd)) {
        if (strstr(buffer, "libscope.so")) {
            status = TRUE;
            break;
        }
    }

    fclose(fd);
    return status;
}
 
 /*
 * Perform fork and exec which cause that direct children
 * effectively will join a new PID namespace(optionally) and mount namespace.
 *
 * Note:
 * Reassociating the PID namespace (setns CLONE_NEWPID) has somewhat different from 
 * other namespace types. Reassociating the calling thread with a PID namespace
 * changes only the PID namespace that subsequently created child processes of
 * the caller will be placed in. It does not change the PID namespace of the caller itself.
 *
 * Returns status of operation
 */
int
nsForkAndExec(pid_t parentPid, pid_t nsPid, bool ldattach)
{
    char *opStatus = "Detach";
    char *childOp = "-d";
    bool libLoaded = isLibScopeLoaded(parentPid);

    if (ldattach) {
        childOp = "-a";
        opStatus = (libLoaded == FALSE) ? "Attach" : "Reattach";
    } else if (libLoaded == FALSE) {
        fprintf(stderr, "error: PID: %d has never been attached\n", parentPid);
        return EXIT_FAILURE; 
    }
    /*
    * TODO In case of Reattach/Detach - when libLoaded = TRUE
    * We only need the mount namespace to /dev/shm but currently scope
    * also check the pid namespace
    */

    if (joinChildNamespace(parentPid, parentPid != nsPid) == FALSE) {
        fprintf(stderr, "error: joinChildNamespace failed\n");
        return EXIT_FAILURE; 
    }

    pid_t child = fork();
    if (child < 0) {
        fprintf(stderr, "error: fork() failed\n");
        return EXIT_FAILURE;
    } else if (child == 0) {
        char loaderInChildPath[PATH_MAX] = {0};

        // Child
        char *nsAttachPidStr = NULL;
        if (asprintf(&nsAttachPidStr, "%d", nsPid) <= 0) {
            perror("error: asprintf() failed\n");
            return EXIT_FAILURE;
        }
        int execArgc = 0;
        char **execArgv = calloc(4, sizeof(char *));
        if (!execArgv) {
            fprintf(stderr, "error: calloc() failed\n");
            return EXIT_FAILURE;
        }

        const char *loaderVersion = libverNormalizedVersion(SCOPE_VER);
        bool isDevVersion = libverIsNormVersionDev(loaderVersion);

        snprintf(loaderInChildPath, PATH_MAX, "/usr/lib/appscope/%s/scope", loaderVersion);
        if (access(loaderInChildPath, R_OK) || isDevVersion) {
            memset(loaderInChildPath, 0, PATH_MAX);
            snprintf(loaderInChildPath, PATH_MAX, "/tmp/appscope/%s/scope", loaderVersion);
            if (access(loaderInChildPath, R_OK)) {
                fprintf(stderr, "error: access scope failed\n");
                return EXIT_FAILURE;
            }
        }

        execArgv[execArgc++] = loaderInChildPath;
        execArgv[execArgc++] = childOp;
        execArgv[execArgc++] = nsAttachPidStr;

        return execve(loaderInChildPath, execArgv, environ);
    }
    // Parent
    int status;
    waitpid(child, &status, 0);
    if (WIFEXITED(status)) {
        int exitChildStatus = WEXITSTATUS(status);
        if (exitChildStatus == 0) {
            fprintf(stderr, "%s to process %d in child process succeeded\n", opStatus, parentPid);
        } else {
            fprintf(stderr, "%s to process %d in child process failed\n", opStatus, parentPid);
        }
        return exitChildStatus;
    }
    fprintf(stderr, "error: %s failed() failed\n", opStatus);
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
 * This should be called after the mnt namespace has been switched.
 */
static bool
createCron(const char *hostPrefixPath, const char *script) {
    int outFd;
    char buf[1024] = {0};
    char path[PATH_MAX] = {0};

    // Check access to cron.d directory
    if (snprintf(path, sizeof(path), "%s/etc/cron.d", hostPrefixPath) < 0) {
        perror("createCron: /etc/cron.d error: snprintf() failed\n");
        return FALSE;
    }

    if (access(path, R_OK)) {
        fprintf(stderr, "createCron: error %s does not exist\n", path);
        return FALSE;
    }

    // Create the script to be executed by cron
    memset(path, 0, PATH_MAX);
    if (snprintf(path, sizeof(path), "%s/tmp/att.sh", hostPrefixPath) < 0) {
        perror("createCron: /tmp/att.sh error: snprintf() failed\n");
        fprintf(stderr, "path: %s\n", path);
        return FALSE;
    }

    if ((outFd = open(path, O_RDWR | O_CREAT, 0775)) == -1) {
        perror("createCron: script path: open failed");
        fprintf(stderr, "path: %s\n", path);
        return FALSE;
    }

    // Write cron action - scope start
    if (snprintf(buf, sizeof(buf), script) < 0) {
        perror("createCron: script: error: snprintf() failed\n");
        close(outFd);
        return FALSE;
    }

    if (write(outFd, buf, strlen(buf)) == -1) {
        perror("createCron: script: write failed");
        fprintf(stderr, "path: %s\n", path);
        close(outFd);
        return FALSE;
    }

    if (close(outFd) == -1) {
        perror("createCron: script: close failed");
        fprintf(stderr, "path: %s\n", path);
        return FALSE;
    }

    // Create the cron entry
    memset(path, 0, PATH_MAX);
    if (snprintf(path, sizeof(path), "%s/etc/cron.d/cron", hostPrefixPath) < 0) {
        perror("createCron: /etc/cron.d/cron error: snprintf() failed\n");
        fprintf(stderr, "path: %s\n", path);
        return FALSE;
    }

    if ((outFd = open(path, O_RDWR | O_CREAT, 0775)) == -1) {
        perror("createCron: cron: open failed");
        fprintf(stderr, "path: %s\n", path);
        return FALSE;
    }

    // crond will detect this file entry and run on its' next cycle
    if (write(outFd, SCOPE_CRONTAB, C_STRLEN(SCOPE_CRONTAB)) == -1) {
        perror("createCron: cron: write failed");
        fprintf(stderr, "path: %s\n", path);
        close(outFd);
        return FALSE;
    }

    if (close(outFd) == -1) {
        perror("createCron: cron: close failed");
        fprintf(stderr, "path: %s\n", path);
        return FALSE;
    }

    return TRUE;
}

 /*
 * Check if switching mount namespace is required.
 *
 * Returns status of operation, TRUE if switching is required, FALSE if not.
 */
static bool
switchMntNsRequired(const char *hostFsPrefix) {
    const char* const hostDir[] = {
        "/etc/cron.d/",
        "/usr/lib/",
        "/tmp/",
    };

    for (int i = 0; i < ARRAY_SIZE(hostDir); ++i) {
        char path[PATH_MAX] = {0};
        if (snprintf(path, sizeof(path), "%s%s", hostFsPrefix, hostDir[i]) < 0) {
            perror("switchMntNsRequired: snprintf failed");
            return TRUE;
        }
        if (access(path, W_OK)) {
            return TRUE;
        }
    }
    return FALSE;
}

 /*
 * Performs switching to host mount namespace operation.
 *
 * Returns status of operation, TRUE in case of success, FALSE in case of failure.
 */
static bool
setHostMntNs(const char *hostFsPrefix) {
    int nsFd;
    char nsPath[PATH_MAX] = {0};

    if (snprintf(nsPath, sizeof(nsPath), "%s/proc/1/ns/mnt", hostFsPrefix) < 0) {
        perror("setHostMntNs: snprintf CRIBL_EDGE_FS_ROOT failed");
        return FALSE;
    }

    if ((nsFd = open(nsPath, O_RDONLY)) == -1) {
        perror("setHostMntNs: open failed: host fs is not mounted");
        return FALSE;
    }

    if (setns(nsFd, CLONE_NEWNS) != 0) {
        perror("setHostMntNs: setns failed");
        return FALSE;
    }

    close(nsFd);

    return TRUE;
}

/*
 * Joins the host mount namespace to perform a Start or Stop action.
 * Required conditions:
 * - scope_filter must exist (if action is Start)
 * - scope must exist
 * TODO: unify it with joinChildNamespace
 * Returns TRUE if operation was success, FALSE otherwise.
 */
static bool
joinHostNamespace(ns_action_t action) {
    bool status = FALSE;
    size_t scopeSize = 0;
    size_t cfgSize = 0;
    char path[PATH_MAX] = {0};
    char hostPrefixPath[PATH_MAX] = {0};
    char hostFilterPath[PATH_MAX] = {0};
    char hostScopePath[PATH_MAX] = {0};
    char *scopeFilterCfgMem = NULL;
    char *scopeMem = NULL;
    char script[1024];

    if (readlink("/proc/self/exe", path, sizeof(path) - 1) == -1) {
        return status;
    }

    // Load "scope" into memory
    scopeMem = setupLoadFileIntoMem(&scopeSize, path);
    if (scopeMem == NULL) {
        return status;
    }

    // Load "scope" into memory
    const char *loaderVersion = libverNormalizedVersion(SCOPE_VER);
    bool isDevVersion = libverIsNormVersionDev(loaderVersion);
    memset(path, 0, PATH_MAX);
    snprintf(path, PATH_MAX, "/usr/lib/appscope/%s/scope", loaderVersion);
    if ((access(path, R_OK)) || (isDevVersion)) {
        memset(path, 0, PATH_MAX);
        snprintf(path, PATH_MAX, "/tmp/appscope/%s/scope", loaderVersion);
        if (access(path, R_OK)) {
            goto cleanupMem;
        }
    }
    scopeMem = setupLoadFileIntoMem(&scopeSize, path);
    if (scopeMem == NULL) {
        goto cleanupMem;
    }

    if (action == START) {
        // Load "filter file" into memory
        // First try to use env variable
        char *envFilterVal = getenv("SCOPE_FILTER");
        if (envFilterVal) {
            /*
            * If filter env was defined and wasn't disable 
            * the filter handling, try path interpretation
            */
            size_t envFilterLen = strlen(envFilterVal);
            if (strncmp(envFilterVal, "false", envFilterLen) && (!access(envFilterVal, R_OK))) {
                scopeFilterCfgMem = setupLoadFileIntoMem(&cfgSize, envFilterVal);
            }
        } else {
            /*
            * Try to use defaults
            */
            if (!access(SCOPE_FILTER_USR_PATH, R_OK)) {
                scopeFilterCfgMem = setupLoadFileIntoMem(&cfgSize, SCOPE_FILTER_USR_PATH);
            } else if (!access(SCOPE_FILTER_TMP_PATH, R_OK)) {
                scopeFilterCfgMem = setupLoadFileIntoMem(&cfgSize, SCOPE_FILTER_TMP_PATH);
            }
        }
        if (scopeFilterCfgMem == NULL) {
            goto cleanupMem;
        }
    }

    // Get root fs path
    char *envFsRootVal = getenv("CRIBL_EDGE_FS_ROOT");
    if (envFsRootVal) {
        snprintf(hostPrefixPath, PATH_MAX, "%s", envFsRootVal);
    } else {
        strncpy(hostPrefixPath, "/hostfs", sizeof(hostPrefixPath));
    }

    // Verify access to host filesystem inside the container
    if (access(hostPrefixPath, R_OK)) {
        goto cleanupMem;
    }

    /*
     * Reassociate current process to the host mount namespace
     * - allows to copy file(s) into the host fs
    */
    if (switchMntNsRequired(hostPrefixPath) == TRUE) {
        if (setHostMntNs(hostPrefixPath) == FALSE) {
            goto cleanupMem;
        }
        // if we switch mount namespace we do not use hostfs prefix
        strcpy(hostPrefixPath, "");
    }

    // At this point we are using the host fs
    
    uid_t eUid = geteuid();
    gid_t eGid = getegid();

    /*
     * Ensure that we have the correct dest dir
     */
    memset(path, 0, PATH_MAX);
    snprintf(path, PATH_MAX, "%s/usr/lib/appscope/%s/", hostPrefixPath, loaderVersion);
    mkdir_status_t res = libdirCreateDirIfMissing(path, 0755, eUid, eGid);
    if ((res > MKDIR_STATUS_EXISTS) || (isDevVersion)) {
        memset(path, 0, PATH_MAX);
        snprintf(path, PATH_MAX, "%s/tmp/appscope/%s/", hostPrefixPath, loaderVersion);
        mkdir_status_t res = libdirCreateDirIfMissing(path, 0777, eUid, eGid);
        if (res > MKDIR_STATUS_EXISTS) {
            goto cleanupMem;
        }
        snprintf(hostScopePath, PATH_MAX, "/tmp/appscope/%s/scope", loaderVersion);
    } else {
        snprintf(hostScopePath, PATH_MAX, "/usr/lib/appscope/%s/scope", loaderVersion);
    }

    /*
     * Create a "scope" on the host
     */
    memset(path, 0, PATH_MAX);
    snprintf(path, PATH_MAX, "%s%s", hostPrefixPath, hostScopePath);
    if (extractMemToFile(scopeMem, scopeSize, path, 0775, isDevVersion, eUid, eGid) == FALSE) {
        goto cleanupMem;
    }

    if (action == START) {
        /*
         * Create a "filter file" on the host
         */
        memset(path, 0, PATH_MAX);
        snprintf(path, PATH_MAX, "%s/usr/lib/appscope/scope_filter", hostPrefixPath);
        if ((status == extractMemToFile(scopeFilterCfgMem, cfgSize, path, 0664, TRUE, eUid, eGid)) == FALSE) {
            memset(path, 0, PATH_MAX);
            snprintf(path, PATH_MAX, "%s/tmp/appscope/scope_filter", hostPrefixPath);
            if ((status == extractMemToFile(scopeFilterCfgMem, cfgSize, hostFilterPath, 0664, TRUE, eUid, eGid)) == FALSE) {
                goto cleanupMem;
            }
            strcpy(hostFilterPath, "/tmp/appscope/scope_filter");
        } else {
            strcpy(hostFilterPath, "/usr/lib/appscope/scope_filter");
        }

        /*
         * Create a "cron script" on the host
         */
        snprintf(script, sizeof(script), SCOPE_START_SCRIPT, hostScopePath, hostFilterPath);
        status = createCron(hostPrefixPath, script);
    } else {
        /*
         * Create a "cron script" on the host
         */
        snprintf(script, sizeof(script), SCOPE_STOP_SCRIPT, hostScopePath);
        status = createCron(hostPrefixPath, script);
    }

cleanupMem:
    if (scopeFilterCfgMem) {
        munmap(scopeFilterCfgMem, cfgSize);
    }

    if (scopeMem) {
        munmap(scopeMem, scopeSize);
    }

    return status;
}

 /*
 * Verify if current running process runs in the container.
 * Returns TRUE if process runs in the container FALSE otherwise
 */
static bool
isRunningInContainer(void) {
    struct stat st = {0};
    return (stat("/proc/2/comm", &st) != 0) ? TRUE : FALSE;
}

 /*
 * Perform scope host start operation - this operation begins from container namespace.
 *
 * - switch namespace to host
 * - create cron entry with filter file
 *
 * Returns exit code of operation
 */
int
nsHostStart(void) {
    if (isRunningInContainer() == FALSE) {
        fprintf(stderr, "error: nsHostStart failed process is running on host\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "Executing from a container, running the start command from the host\n");

    if (joinHostNamespace(START) == FALSE) {
        fprintf(stderr, "error: joinHostNamespace failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

 /*
 * Perform scope host stop operation - this operation begins from container namespace.
 *
 * - switch namespace to host
 * - create cron entry with filter file
 *
 * Returns exit code of operation
 */
int
nsHostStop(void) {
     if (isRunningInContainer() == FALSE) {
        fprintf(stderr, "error: nsHostStop failed process is running on host\n");
        return EXIT_FAILURE;
    }
    fprintf(stdout, "Executing from a container, running the stop command from the host\n");

    if (joinHostNamespace(STOP) == FALSE) {
        fprintf(stderr, "error: joinHostNamespace failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
