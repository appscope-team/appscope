/*
 * Notify a configured endpoint on an enforcement event.
 *
 * Currently we support notification over a Slack channel
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "scopetypes.h"
#include "dbg.h"
#include "scopestdlib.h"
#include "os.h"
#include "notify.h"

// From transport.c: Avoids naming conflict between our src/wrap.c and libssl.a
#define SSL_read SCOPE_SSL_read
#define SSL_write SCOPE_SSL_write
#include "openssl/ssl.h"
#include "openssl/err.h"
#undef SSL_read
#undef SSL_write

static const char *g_slackApiToken;
static const char *g_slackChannel;
notify_info_t g_notify_def = {0};
bool g_notified = FALSE;
bool g_inited = FALSE;

static void
initOpenSSL(void) {
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();
}

static int
setVar(const char *var)
{
    bool rv;
    char *enval;

    // Get env vars that enable notify behavior
    // If the relevant env var is not defined, use the default
    if ((enval = getenv(var)) == NULL) {
        rv = -1;
    } else {
        if (scope_strstr(enval, "TRUE") || scope_strstr(enval, "true")) {
            rv = (int)TRUE;
        } else {
            rv = (int)FALSE;
        }
    }

    return rv;
}

static bool
setList(char *list, char *dest[], size_t max_entries){
    int num_entries = 0;
    char *token = NULL;
    char *save = NULL;
    char *copy;

    if (list == NULL) return FALSE;

    if ((copy = scope_calloc(1, strlen(list) + 1)) == NULL) return FALSE;
    scope_strcpy(copy, list);

    token = scope_strtok_r(copy, ",", &save);
    while ((token != NULL) && (num_entries < max_entries)) {
        if ((dest[num_entries] = scope_strdup(token)) == NULL) {
            scopeLog(CFG_LOG_ERROR, "%s: Can't allocate memory for a list from %s", __FUNCTION__, token);
            scope_free(copy);
            return FALSE;
        }
        num_entries++;
        token = scope_strtok_r(NULL, ",", &save);
    }

    scope_free(copy);
    return TRUE;
}

static bool
getNotifyVars(void)
{
    char *enval;

    if ((g_notify_def.enable = setVar(NOTIFY_IQ_VAR)) == -1) {
        g_notify_def.enable = DEFAULT_ENABLE;
    }

    if ((g_notify_def.exit = setVar(NOTIFY_IQ_EXIT)) == -1) {
        g_notify_def.exit = DEFAULT_EXIT;
    }

    if ((g_notify_def.send = setVar(NOTIFY_IQ_SEND)) == -1) {
        g_notify_def.send = DEFAULT_SEND;
    }

    if ((g_notify_def.libs = setVar(NOTIFY_IQ_LIBS)) == -1) {
        g_notify_def.libs = DEFAULT_LIBS;
    }

    if ((g_notify_def.files = setVar(NOTIFY_IQ_FILES)) == -1) {
        g_notify_def.files = DEFAULT_FILES;
    }

    if ((g_notify_def.functions = setVar(NOTIFY_IQ_FUNCS)) == -1) {
        g_notify_def.functions = DEFAULT_FUNCS;
    }

    if ((g_notify_def.network = setVar(NOTIFY_IQ_NET)) == -1) {
        g_notify_def.network = DEFAULT_NET;
    }

    if ((g_notify_def.exfil = setVar(NOTIFY_IQ_EXFIL)) == -1) {
        g_notify_def.exfil = DEFAULT_EXFIL;
    }

    if ((g_notify_def.dns = setVar(NOTIFY_IQ_DNS)) == -1) {
        g_notify_def.dns = DEFAULT_DNS;
    }

    if ((g_notify_def.white_block = setVar(NOTIFY_WHITE_BLOCK)) == -1) {
        g_notify_def.white_block = DEFAULT_WHITE_BLOCK;
    }

    // Get file read and write lists
    // Tokenize the environment variable value, assumes ',' is the delimeter
    if ((enval = getenv(NOTIFY_IQ_FILE_READ))) {
    } else {
        enval = DEFAULT_FILE_READ;
    }

    setList(enval, g_notify_def.file_read, MAX_FILE_ENTRIES);

    if ((enval = getenv(NOTIFY_IQ_FILE_WRITE))) {
    } else {
        enval = DEFAULT_FILE_WRITE;
    }

    setList(enval, g_notify_def.file_write, MAX_FILE_ENTRIES);

    // Define a list of system dirs
    if ((enval = getenv(NOTIFY_IQ_SYS_DIRS))) {
    } else {
        enval = DEFAULT_SYS_DIRS;
    }

    setList(enval, g_notify_def.sys_dirs, MAX_FILE_ENTRIES);

    // Same for IP white and black lists
    if ((enval = getenv(NOTIFY_IQ_IP_WHITE))) {
    } else {
        enval = DEFAULT_IP_WHITE;
    }

    setList(enval, g_notify_def.ip_white, MAX_IP_ENTRIES);

    if ((enval = getenv(NOTIFY_IQ_IP_BLACK))) {
    } else {
        enval = DEFAULT_IP_BLACK;
    }

    setList(enval, g_notify_def.ip_black, MAX_IP_ENTRIES);

    return TRUE;
}

static bool
getSlackVars(void)
{
    // Get the Slack token from an env var
    g_slackApiToken = getenv(SLACK_TOKEN_VAR);
    if (g_slackApiToken == NULL) {
        scopeLog(CFG_LOG_INFO, "%s: No %s environment variable defined", __FUNCTION__, SLACK_TOKEN_VAR);
        return FALSE;
    }

    g_slackChannel = getenv(SLACK_CHANNEL);
    if (g_slackChannel == NULL) {
        g_slackChannel = SLACK_CHANNEL_ID;
    }

    return TRUE;
}

static void
sendSlackMessage(SSL *ssl, const char *msg) {
    bool success = FALSE;
    char *payload;
    size_t plen = scope_strlen(msg) + scope_strlen(g_slackApiToken) + scope_strlen(g_slackChannel) + 256;
    char pname[PATH_MAX];

    if (osGetProcname(pname, PATH_MAX)) {
        scope_strncpy(pname, "_", PATH_MAX);
    }

    if ((payload = scope_malloc(plen)) == NULL) {
        scopeLog(CFG_LOG_ERROR, "%s: Can't allocate memory for a payload", __FUNCTION__);
        return;
    }
    
    scope_snprintf(payload, plen,
                   "token=%s&channel=%s&text=Process %s (pid %d) on host %s encountered %s",
                   g_slackApiToken, g_slackChannel, pname, getpid(), g_proc.hostname, msg);

    char request[plen];
    scope_snprintf(request, sizeof(request),
                   "POST %s HTTP/1.1\r\n"
                   "Host: %s\r\n"
                   "Connection: close\r\n"
                   "Content-Type: application/x-www-form-urlencoded\r\n"
                   "Content-Length: %zu\r\n\r\n"
                   "%s", 
                   SLACK_API_PATH, SLACK_API_HOST, scope_strlen(payload), payload);

    int rv;
    if ((rv = SCOPE_SSL_write(ssl, request, scope_strlen(request))) <= 0) {
        int err = SSL_get_error(ssl, rv);
        scopeLog(CFG_LOG_ERROR, "%s: error %d sending a request to Slack over an SSL channel", __FUNCTION__, err);
        scope_free(payload);
        return;
    }

    /*
     * Do we want or need to get a response?
     * If the response has an error, all we can do is log the result.
     * If we extend this to allow inputs from Slack, we will need to process responses.
     */
    char response[1024];
    int bytesRead;
    while ((bytesRead = SCOPE_SSL_read(ssl, response, sizeof(response) - 1)) > 0) {
        response[bytesRead] = '\0';
        // TODO: handle the respone correctly. Get the JSON payload and process. 
        if (scope_strstr(response, "\"ok\":true")) {
            success = TRUE;
            break;
        }
        
        //printf("%s", response);
    }

    scopeLog(CFG_LOG_INFO, "Successfully sent a notification message to slack");
    scope_free(payload);
    if (success == FALSE) {
        scopeLog(CFG_LOG_INFO, "%s: failed response from a slack notify message", __FUNCTION__);
    }
}

/*
 * notify using a Slack channel.
 * The token is defined by an environment variable. If not defined we fail.
 * The channel can be definedby an env var. If not, a default is used.
 * Slack requires HTTPS which requires openssl.
 */
static bool
slackNotify(const char *msg) {
    if (g_notify_def.send == FALSE) return FALSE;

    SSL *ssl;
    const SSL_METHOD *method = TLS_client_method();

    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        scopeLog(CFG_LOG_ERROR, "%s: Can't get an SSL context", __FUNCTION__);
        return FALSE;
    }

    int sockfd = scope_socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        scopeLog(CFG_LOG_ERROR, "%s: can't create a socket", __FUNCTION__);
        return FALSE;
    }

    struct hostent *server = scope_gethostbyname(SLACK_API_HOST);
    if (server == NULL) {
        scopeLog(CFG_LOG_ERROR, "%s: can't get the IP addr for slack.com", __FUNCTION__);
        scope_close(sockfd);
        return FALSE;
    }

    struct sockaddr_in serverAddr;
    scope_memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    scope_memcpy(&serverAddr.sin_addr.s_addr, server->h_addr, server->h_length);
    serverAddr.sin_port = scope_htons(HTTPS_PORT);

    if (scope_connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1) {
        scopeLog(CFG_LOG_ERROR, "%s: can't connect to slack", __FUNCTION__);
        scope_close(sockfd);
        return FALSE;
    }

    ssl = SSL_new(ctx);
    if (ssl == NULL) {
        scopeLog(CFG_LOG_ERROR, "%s: can't create a new SSL object", __FUNCTION__);
        scope_close(sockfd);
        return FALSE;
    }

    if (SSL_set_fd(ssl, sockfd) == 0) {
        scopeLog(CFG_LOG_ERROR, "%s: can't assign a file descriptor to an SSL object", __FUNCTION__);
        scope_close(sockfd);
        return FALSE;
    }


    if (SSL_connect(ssl) <= 0) {
        scopeLog(CFG_LOG_ERROR, "%s: can't connect to the slack socket over SSL", __FUNCTION__);
        scope_close(sockfd);
        return FALSE;
    }

    sendSlackMessage(ssl, msg);

    SSL_free(ssl);
    SSL_CTX_free(ctx);
    scope_close(sockfd);

    return TRUE;
}

/*
 * notify entry point.
 * Should support multiple types of notification as defined in config.
 *
 * To start, we support slack notification over HTTPS.
 */
bool
notify(notify_type_t dtype, const char *msg)
{
    if (g_inited == FALSE) {
        // TODO: add config and determine which notification we are using
        initOpenSSL();
        // Should we return here?
        if (getSlackVars() == FALSE) return FALSE;
        if (getNotifyVars() == FALSE) return FALSE;
        g_inited = TRUE;
    }

    bool doit = FALSE, rv = FALSE;

    if (g_notify_def.enable == FALSE) return FALSE;

    switch (dtype) {
    case NOTIFY_INIT:
        return TRUE;

    case NOTIFY_LIBS:
        doit = g_notify_def.libs;
        break;

    case NOTIFY_FILES:
        doit = g_notify_def.files;
        break;

    case NOTIFY_FUNC:
        doit = g_notify_def.functions;
        break;

    case NOTIFY_NET:
        doit = g_notify_def.network;
        break;

    case NOTIFY_DNS:
        doit = g_notify_def.dns;
        break;

    default:
        doit = FALSE;
        break;
    }

    if (doit == TRUE) {
        // TODO: add config and determine which notification we are using
        rv = slackNotify(msg);

        // only for unit test
        g_notified = TRUE;
    }

    if (g_notify_def.exit == TRUE) {
        exit(-1);
    }

    return rv;
}
