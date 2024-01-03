/*
 * This test does not require a Slack token to be defined.
 * If you want to see notifications in Slack set the env var "SCOPE_SLACKBOT_TOKEN"
 * to the token for your Slack and change the value of NOTIFY_IQ_SEND to TRUE in
 * the init function below.
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test.h"
#include "scopestdlib.h"
#include "scopetypes.h"
#include "fn.h"
#include "state.h"
#include "notify.h"

#define WRITE "/tmp/modok"
#define DOUBLEX "/tmp/detect.txt.log"
#define SPACES "/tmp/spaces "
#define SUID "/tmp/suid"
#define GUID "/tmp/guid"
#define EXEC "/tmp/exec"
#define SYSGROUP "/tmp/sysgrp"
#define SYSOTHER "/tmp/sysoth"
#define PIGNORE "/var"

extern bool g_notified;
extern bool g_inited;

static void
initThis(void)
{
    int rv;

    // set this to TRUE if you want to see notifications sent to Slack
    rv = setenv(NOTIFY_IQ_SEND, "FALSE", 1);
    assert_int_not_equal(-1, rv);

    setenv(NOTIFY_IQ_VAR, "TRUE", 1);
    assert_int_not_equal(-1, rv);

    notify(NOTIFY_INIT, "");
    initState();

     // create a few files for test
    rv = open(DOUBLEX, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    rv = open(SPACES, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    rv = open(SUID, O_CREAT | O_RDONLY, S_ISUID | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    rv = open(GUID, O_CREAT | O_RDONLY, S_ISGID | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    rv = open(EXEC, O_CREAT | O_RDONLY, S_IRWXU | S_IRGRP | S_IROTH);
    rv = open(SYSGROUP, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    rv = open(SYSOTHER, O_CREAT | O_RDWR);

    // create this one and close it
    rv = open(WRITE, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    close(rv);
}

static void
restoreThis(void)
{
    setenv(NOTIFY_IQ_VAR, "FALSE", 1);
    unlink(DOUBLEX);
    unlink(SPACES);
    unlink(WRITE);
    unlink(SUID);
    unlink(GUID);
    unlink(EXEC);
    unlink(SYSGROUP);
    unlink(SYSOTHER);
}

static void
fileRead(void **state)
{
    int rv;
    char buf[64];

    rv = setenv(NOTIFY_IQ_FILE_READ, "/var, /etc/hosts", 1);
    assert_int_not_equal(-1, rv);

    rv = setenv(NOTIFY_IQ_FILE_WRITE, PIGNORE, 1);
    assert_int_not_equal(-1, rv);

    // Update the path arrays
    g_inited = FALSE;
    notify(NOTIFY_INIT, "");

    // should notify
    g_notified = FALSE;
    doOpen(7, "/etc/hosts", FD, "openFunc");
    doRead(7, 987, 1, buf, 13, "readFunc", BUF, 0);
    assert_int_equal(g_notified, TRUE);
    doClose(7, "closeFunc");

    // should not notify
    g_notified = FALSE;
    doOpen(7, "/usr/lib/os-release", FD, "openFunc");
    doRead(7, 987, 1, buf, 13, "readFunc", BUF, 0);
    assert_int_equal(g_notified, FALSE);
    doClose(7, "closeFunc");
}

static void
fileWrite(void **state)
{
    int rv;
    char buf[64];

    rv = setenv(NOTIFY_IQ_FILE_WRITE, "/etc, /tmp", 1);
    assert_int_not_equal(-1, rv);

    rv = setenv(NOTIFY_IQ_FILE_READ, PIGNORE, 1);
    assert_int_not_equal(-1, rv);

    // Update the path arrays
    g_inited = FALSE;
    notify(NOTIFY_INIT, "");

    // should notify
    g_notified = FALSE;
    doOpen(7, WRITE, FD, "openFunc");
    doWrite(7, 987, 1, buf, 13, "writeFunc", BUF, 0);
    assert_int_equal(g_notified, TRUE);
    doClose(7, "closeFunc");

    // should not notify
    rv = setenv(NOTIFY_IQ_FILE_WRITE, "/etc, /var", 1);
    assert_int_not_equal(-1, rv);

    // Update the path arrays
    g_inited = FALSE;
    notify(NOTIFY_INIT, "");

    g_notified = FALSE;
    doOpen(7, WRITE, FD, "openFunc");
    doWrite(7, 987, 1, buf, 13, "writeFunc", BUF, 0);
    assert_int_equal(g_notified, FALSE);
    doClose(7, "closeFunc");
}

static void
spaceAtEnd(void **state)
{
    // should notify
    g_notified = FALSE;
    doOpen(7, SPACES, FD, "openFunc");
    assert_int_equal(g_notified, TRUE);
    doClose(7, "closeFunc");
}

static void
doubleExtension(void **state)
{
    // should notify
    g_notified = FALSE;
    doOpen(7, DOUBLEX, FD, "openFunc");
    assert_int_equal(g_notified, TRUE);
    doClose(7, "closeFunc");
}

static void
setUID(void **state)
{
    // should notify
    g_notified = FALSE;
    doOpen(7, SUID, FD, "openFunc");
    assert_int_equal(g_notified, TRUE);
    doClose(7, "closeFunc");
}

static void
setGID(void **state)
{
    // should notify
    g_notified = FALSE;
    doOpen(7, GUID, FD, "openFunc");
    assert_int_equal(g_notified, TRUE);
    doClose(7, "closeFunc");
}

static void
modExec(void **state)
{
    char buf[64];

    // should notify
    g_notified = FALSE;
    doOpen(7, EXEC, FD, "openFunc");
    doWrite(7, 987, 1, buf, 13, "writeFunc", BUF, 0);
    assert_int_equal(g_notified, TRUE);
    doClose(7, "closeFunc");
}

static void
sysGrp(void **state)
{
    int rv;

    rv = setenv(NOTIFY_IQ_SYS_DIRS, "/tmp", 1);
    assert_int_not_equal(-1, rv);

    // Update the path arrays
    g_inited = FALSE;
    notify(NOTIFY_INIT, "");

    // should notify
    g_notified = FALSE;
    doOpen(7, SYSGROUP, FD, "openFunc");
    assert_int_equal(g_notified, TRUE);
    doClose(7, "closeFunc");
}

static void
sysOth(void **state)
{
    int rv;

    rv = setenv(NOTIFY_IQ_SYS_DIRS, "/tmp", 1);
    assert_int_not_equal(-1, rv);

    // Update the path arrays
    g_inited = FALSE;
    notify(NOTIFY_INIT, "");

    // set other write perm
    rv = chmod(SYSOTHER, S_IWOTH);
    assert_int_not_equal(-1, rv);

    // should notify
    g_notified = FALSE;
    doOpen(7, SYSOTHER, FD, "openFunc");
    assert_int_equal(g_notified, TRUE);
    doClose(7, "closeFunc");
}

int
main(int argc, char* argv[])
{
    int rv;

    printf("running %s\n", argv[0]);
    initFn();
    initThis();

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(fileRead),
        cmocka_unit_test(fileWrite),
        cmocka_unit_test(spaceAtEnd),
        cmocka_unit_test(doubleExtension),
        cmocka_unit_test(setUID),
        cmocka_unit_test(setGID),
        cmocka_unit_test(modExec),
        cmocka_unit_test(sysGrp),
        cmocka_unit_test(sysOth),
    };

    rv = cmocka_run_group_tests(tests, groupSetup, groupTeardown);
    restoreThis();
    return rv; 
}
