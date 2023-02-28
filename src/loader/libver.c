#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include "libver.h"

/*
 * Returns normalized version string
 * - "dev" for unofficial release
 * - "%d.%d.%d" for official release (e.g. "1.3.0" for "v1.3.0")
 * - "%d.%d.%d-%s%d" for candidate release (e.g. "1.3.0-rc0" for "v1.3.0-rc0")
 */
const char *
libverNormalizedVersion(const char *version) {

    if ((version == NULL) || (*version != 'v')) {
        return "dev";
    }
    ++version;
    size_t versionSize = strlen(version);

    for (int i = 0; i < versionSize; ++i) {
        // Only digit and "." are accepted
        if ((isdigit(version[i]) == 0) && version[i] != '.' &&
            version[i] != '-' && version[i] != 't' && version[i] != 'c' &&
            version[i] != 'r') {
            return "dev"; 
        }
        if (i == 0 || i == versionSize) {
            // First and last character must be number
            if (isdigit(version[i]) == 0) {
                return "dev";
            }
        }
    }
    return version;
}

/*
 * Check if normalized version is a dev version
 * Returns TRUE if it is a dev version FALSE if not
 */
bool
libverIsNormVersionDev(const char *normVersion) {
    return !strncmp(normVersion, "dev", sizeof("dev"));
}
