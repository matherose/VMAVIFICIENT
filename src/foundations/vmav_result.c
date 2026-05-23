#include "vmavificient/vmav_result.h"

#include <stdio.h>

const char *vmav_code_str(vmav_code_t code) {
    switch (code) {
    case VMAV_OK:
        return "ok";
    case VMAV_ERR_GENERIC:
        return "generic-error";
    case VMAV_ERR_IO:
        return "io-error";
    case VMAV_ERR_PARSE:
        return "parse-error";
    case VMAV_ERR_NO_MEM:
        return "out-of-memory";
    case VMAV_ERR_BAD_ARG:
        return "bad-argument";
    case VMAV_ERR_NOT_FOUND:
        return "not-found";
    case VMAV_ERR_NOT_IMPL:
        return "not-implemented";
    case VMAV_ERR_PERMISSION:
        return "permission-denied";
    case VMAV_ERR_TIMEOUT:
        return "timeout";
    case VMAV_ERR_CANCELED:
        return "canceled";
    case VMAV_ERR_SUBPROC:
        return "subprocess-error";
    case VMAV_ERR_FFMPEG:
        return "ffmpeg-error";
    case VMAV_ERR_ENCODE:
        return "encode-error";
    case VMAV_ERR_DECODE:
        return "decode-error";
    case VMAV_ERR_INVARIANT:
        return "invariant-violated";
    case VMAV_ERR_AGAIN:
        return "again";
    case VMAV_ERR_EOF:
        return "end-of-stream";
    }
    return "unknown";
}

vmav_status_t vmav_status_make(vmav_code_t code, const char *file, int line, const char *fmt, ...) {
    vmav_status_t st;
    st.code = code;
    st.file = file;
    st.line = line;
    st.msg[0] = '\0';

    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(st.msg, sizeof(st.msg), fmt, ap);
    va_end(ap);

    return st;
}
