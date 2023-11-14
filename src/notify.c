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

static bool g_inited = FALSE;
static const char *g_slackApiToken;
static const char *g_slackChannel;

static void
initOpenSSL(void) {
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();
}

static bool
getSlackVars(void)
{
    //Get the Slack token from an env var
    g_slackApiToken = getenv(SLACK_TOKEN_VAR);
    if (g_slackApiToken == NULL) {
        scopeLog(CFG_LOG_ERROR, "%s: No %s environment variable defined", __FUNCTION__, SLACK_TOKEN_VAR);
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
                   "token=%s&channel=%s&text=Process %s (%d) encountered %s",
                   g_slackApiToken, g_slackChannel, pname, getpid(), msg);

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

    scope_free(payload);
    if (success == FALSE) {
        scopeLog(CFG_LOG_ERROR, "%s: failed response from a slack notify message", __FUNCTION__);
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
    if (g_inited == FALSE) {
        g_inited = TRUE;
        initOpenSSL();
        if (getSlackVars() == FALSE) return FALSE;
    }

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
 * Should support multiple types of notification as definedin config.
 *
 * To start, we support slack notification over HTTPS.
 */
bool
notify(const char *msg) {
    // TODO: add config and determine which notification we are using
    return slackNotify(msg);
}
