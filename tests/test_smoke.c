/* Phase 0 smoke test — proves the build+test wiring works on every
 * target before any real test framework is vendored. */
#include "vmavificient/vmav_version.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    if (strlen(VMAV_VERSION_STRING) == 0) {
        fprintf(stderr, "FAIL: VMAV_VERSION_STRING is empty\n");
        return 1;
    }
    if (VMAV_VERSION_MAJOR != 2) {
        fprintf(stderr, "FAIL: expected VMAV_VERSION_MAJOR=2, got %d\n",
                VMAV_VERSION_MAJOR);
        return 1;
    }
    printf("OK vmavificient %s (major=%d minor=%d patch=%d)\n",
           VMAV_VERSION_STRING,
           VMAV_VERSION_MAJOR, VMAV_VERSION_MINOR, VMAV_VERSION_PATCH);
    return 0;
}
