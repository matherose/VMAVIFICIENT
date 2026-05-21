#include "vmavificient/vmav_media.h"
#include "vmavificient/vmav_ui.h"

#include "commands.h"

#include <stdio.h>
#include <string.h>

int cmd_analyze_run(int argc, char **argv) {
    if (argc < 2) {
        fputs("vmavificient analyze: expected an input file path.\n"
              "Usage: vmavificient analyze <input>\n",
              stderr);
        return 2;
    }
    const char *path = argv[1];

    vmav_media_info_t info;
    vmav_status_t st = vmav_media_probe(path, &info);
    if (!vmav_status_ok(st)) {
        fprintf(stderr, "vmavificient analyze: %s\n", st.msg);
        return 1;
    }

    vmav_ui_table_t *t = vmav_ui_table_new("Media probe");
    char buf[64];

    vmav_ui_table_add(t, "path", path);
    vmav_ui_table_add(t, "container", info.container_name);
    vmav_ui_table_add(t, "video codec", info.codec_name);

    snprintf(buf, sizeof(buf), "%dx%d", info.width, info.height);
    vmav_ui_table_add(t, "resolution", buf);

    snprintf(buf, sizeof(buf), "%.3f fps", info.framerate);
    vmav_ui_table_add(t, "framerate", buf);

    snprintf(buf, sizeof(buf), "%.2f s", info.duration_s);
    vmav_ui_table_add(t, "duration", buf);

    if (info.bit_rate > 0) {
        snprintf(buf, sizeof(buf), "%lld bps", (long long)info.bit_rate);
        vmav_ui_table_add(t, "bitrate", buf);
    }

    vmav_ui_table_render(t, stdout);
    vmav_ui_table_free(t);
    return 0;
}
