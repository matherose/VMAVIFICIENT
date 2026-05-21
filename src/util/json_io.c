#include "json_io.h"

#include "vmavificient/vmav_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

vmav_status_t vmav_json_read_file(const char *path, struct cJSON **out_root) {
    if (path == NULL || out_root == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_json_read_file: null arg");
    }
    *out_root = NULL;

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return VMAV_ERR(VMAV_ERR_IO, "fopen('%s'): %s", path, "no such file or unreadable");
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return VMAV_ERR(VMAV_ERR_IO, "fseek failed on '%s'", path);
    }
    const long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return VMAV_ERR(VMAV_ERR_IO, "ftell failed on '%s'", path);
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return VMAV_ERR(VMAV_ERR_IO, "rewind failed on '%s'", path);
    }

    char *buf = malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(fp);
        return VMAV_ERR(VMAV_ERR_NO_MEM, "json buf malloc(%ld)", size + 1);
    }
    const size_t read = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (read != (size_t)size) {
        free(buf);
        return VMAV_ERR(VMAV_ERR_IO, "short read on '%s' (%zu/%ld)", path, read, size);
    }
    buf[size] = '\0';

    cJSON *root = cJSON_ParseWithLength(buf, (size_t)size);
    free(buf);
    if (root == NULL) {
        const char *err = cJSON_GetErrorPtr();
        return VMAV_ERR(VMAV_ERR_PARSE,
                        "cJSON parse failed on '%s' near '%.32s'",
                        path,
                        err != NULL ? err : "?");
    }
    *out_root = root;
    return VMAV_OK_STATUS;
}

vmav_status_t vmav_json_write_atomic(const char *path, const struct cJSON *root) {
    if (path == NULL || root == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_json_write_atomic: null arg");
    }
    char *text = cJSON_PrintBuffered(root, 256, /*formatted=*/1);
    if (text == NULL) {
        return VMAV_ERR(VMAV_ERR_NO_MEM, "cJSON_Print failed");
    }
    const size_t text_len = strlen(text);

    /* Write to a sibling .tmp file then rename. Avoids partial files
     * after a crash. */
    char tmp_path[VMAV_PATH_MAX];
    const int wrote =
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%llu", path, (unsigned long long)vmav_time_now_ms());
    if (wrote < 0 || (size_t)wrote >= sizeof(tmp_path)) {
        free(text);
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "atomic-write tmp path too long");
    }

    FILE *fp = fopen(tmp_path, "wb");
    if (fp == NULL) {
        free(text);
        return VMAV_ERR(VMAV_ERR_IO, "fopen tmp '%s' failed", tmp_path);
    }
    const size_t written = fwrite(text, 1, text_len, fp);
    free(text);
    if (written != text_len) {
        fclose(fp);
        (void)remove(tmp_path);
        return VMAV_ERR(VMAV_ERR_IO, "short write to '%s'", tmp_path);
    }
    if (fflush(fp) != 0) {
        fclose(fp);
        (void)remove(tmp_path);
        return VMAV_ERR(VMAV_ERR_IO, "fflush failed on '%s'", tmp_path);
    }
    fclose(fp);

    if (rename(tmp_path, path) != 0) {
        (void)remove(tmp_path);
        return VMAV_ERR(VMAV_ERR_IO, "rename '%s' -> '%s' failed", tmp_path, path);
    }
    return VMAV_OK_STATUS;
}

void vmav_json_free(struct cJSON *root) {
    if (root == NULL) {
        return;
    }
    cJSON_Delete(root);
}

const char *vmav_json_get_string(const struct cJSON *obj, const char *key, const char *defval) {
    if (obj == NULL || key == NULL) {
        return defval;
    }
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (field == NULL || !cJSON_IsString(field) || field->valuestring == NULL) {
        return defval;
    }
    return field->valuestring;
}

double vmav_json_get_number(const struct cJSON *obj, const char *key, double defval) {
    if (obj == NULL || key == NULL) {
        return defval;
    }
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (field == NULL || !cJSON_IsNumber(field)) {
        return defval;
    }
    return field->valuedouble;
}

bool vmav_json_get_bool(const struct cJSON *obj, const char *key, bool defval) {
    if (obj == NULL || key == NULL) {
        return defval;
    }
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (field == NULL) {
        return defval;
    }
    if (cJSON_IsTrue(field)) {
        return true;
    }
    if (cJSON_IsFalse(field)) {
        return false;
    }
    return defval;
}

int vmav_json_get_int(const struct cJSON *obj, const char *key, int defval) {
    const double d = vmav_json_get_number(obj, key, (double)defval);
    return (int)d;
}
