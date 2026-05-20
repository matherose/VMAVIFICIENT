#include "vmavificient/vmav_ui.h"

#include "vmavificient/vmav_os.h"

#include <stdlib.h>
#include <string.h>

#define VMAV_UI_PROGRESS_LABEL_MAX 64
#define VMAV_UI_PROGRESS_BAR_WIDTH 40
#define VMAV_UI_PROGRESS_RENDER_MS 16U

/* === Progress bar ============================================= */

struct vmav_ui_progress {
    FILE *out;
    char label[VMAV_UI_PROGRESS_LABEL_MAX];
    uint64_t total;
    uint64_t current;
    uint64_t last_render_ms;
    bool is_tty;
    bool unicode_blocks;
    bool finished;
};

static bool detect_utf8_blocks(void) {
    if (vmav_term_no_color()) {
        return false;
    }
    const char *lang = vmav_env_get("LANG");
    if (lang != NULL && (strstr(lang, "UTF-8") != NULL || strstr(lang, "utf8") != NULL)) {
        return true;
    }
    const char *lc_all = vmav_env_get("LC_ALL");
    if (lc_all != NULL && (strstr(lc_all, "UTF-8") != NULL || strstr(lc_all, "utf8") != NULL)) {
        return true;
    }
    return false;
}

vmav_ui_progress_t *vmav_ui_progress_new(FILE *out, const char *label, uint64_t total) {
    if (out == NULL) {
        return NULL;
    }
    vmav_ui_progress_t *p = calloc(1, sizeof(*p));
    if (p == NULL) {
        return NULL;
    }
    p->out = out;
    p->total = total;
    p->is_tty = vmav_term_isatty(fileno(out));
    p->unicode_blocks = p->is_tty && detect_utf8_blocks();
    snprintf(p->label, sizeof(p->label), "%s", label != NULL ? label : "");
    return p;
}

static void render_bar(vmav_ui_progress_t *p, bool final) {
    if (p->out == NULL) {
        return;
    }
    int pct = 0;
    if (p->total > 0) {
        pct = (int)((p->current * 100ULL) / p->total);
        if (pct > 100) {
            pct = 100;
        }
    }
    const int width = VMAV_UI_PROGRESS_BAR_WIDTH;
    const int filled = (p->total > 0) ? (int)((p->current * (uint64_t)width) / p->total) : 0;

    if (p->is_tty) {
        fputc('\r', p->out);
    }
    fprintf(p->out, "%-20s [", p->label);
    if (p->unicode_blocks) {
        for (int i = 0; i < width; i++) {
            fputs(i < filled ? "\xe2\x96\x88" /* █ */ : " ", p->out);
        }
    } else {
        for (int i = 0; i < width; i++) {
            fputc(i < filled ? '#' : ' ', p->out);
        }
    }
    if (p->total > 0) {
        fprintf(p->out,
                "] %3d%%  %llu/%llu",
                pct,
                (unsigned long long)p->current,
                (unsigned long long)p->total);
    } else {
        fprintf(p->out, "] %llu", (unsigned long long)p->current);
    }
    if (!p->is_tty || final) {
        fputc('\n', p->out);
    }
    fflush(p->out);
}

void vmav_ui_progress_set(vmav_ui_progress_t *p, uint64_t current) {
    if (p == NULL || p->finished) {
        return;
    }
    p->current = current;
    const uint64_t now = vmav_time_now_ms();
    if (p->is_tty && (now - p->last_render_ms) < VMAV_UI_PROGRESS_RENDER_MS && current < p->total) {
        return;
    }
    p->last_render_ms = now;
    render_bar(p, false);
}

void vmav_ui_progress_finish(vmav_ui_progress_t *p, const char *final_msg) {
    if (p == NULL || p->finished) {
        return;
    }
    if (p->total > 0) {
        p->current = p->total;
    }
    render_bar(p, true);
    if (final_msg != NULL && final_msg[0] != '\0') {
        fprintf(p->out, "  %s\n", final_msg);
        fflush(p->out);
    }
    p->finished = true;
}

void vmav_ui_progress_free(vmav_ui_progress_t *p) {
    free(p);
}

/* === Spinner ================================================== */

struct vmav_ui_spinner {
    FILE *out;
    char label[VMAV_UI_PROGRESS_LABEL_MAX];
    size_t frame;
    bool is_tty;
    bool finished;
};

vmav_ui_spinner_t *vmav_ui_spinner_new(FILE *out, const char *label) {
    if (out == NULL) {
        return NULL;
    }
    vmav_ui_spinner_t *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    s->out = out;
    s->is_tty = vmav_term_isatty(fileno(out));
    snprintf(s->label, sizeof(s->label), "%s", label != NULL ? label : "");
    if (s->is_tty) {
        fprintf(s->out, "  %s", s->label);
        fflush(s->out);
    }
    return s;
}

void vmav_ui_spinner_tick(vmav_ui_spinner_t *s) {
    if (s == NULL || s->finished || !s->is_tty) {
        return;
    }
    static const char *frames[] = {"|", "/", "-", "\\"};
    const size_t n = sizeof(frames) / sizeof(frames[0]);
    s->frame = (s->frame + 1) % n;
    fprintf(s->out, "\r%s %s", frames[s->frame], s->label);
    fflush(s->out);
}

void vmav_ui_spinner_finish(vmav_ui_spinner_t *s, const char *final_msg) {
    if (s == NULL || s->finished) {
        return;
    }
    if (s->is_tty) {
        fprintf(s->out,
                "\r%s %s\n",
                final_msg != NULL && final_msg[0] != '\0' ? final_msg : "done",
                s->label);
    } else {
        fprintf(s->out,
                "%s %s\n",
                final_msg != NULL && final_msg[0] != '\0' ? final_msg : "done",
                s->label);
    }
    fflush(s->out);
    s->finished = true;
}

void vmav_ui_spinner_free(vmav_ui_spinner_t *s) {
    free(s);
}

/* === Table ==================================================== */

#define VMAV_UI_TABLE_KEY_MAX 64
#define VMAV_UI_TABLE_VALUE_MAX 256

typedef struct row {
    char key[VMAV_UI_TABLE_KEY_MAX];
    char value[VMAV_UI_TABLE_VALUE_MAX];
} row_t;

struct vmav_ui_table {
    char title[VMAV_UI_PROGRESS_LABEL_MAX];
    row_t *rows;
    size_t len;
    size_t cap;
};

vmav_ui_table_t *vmav_ui_table_new(const char *title) {
    vmav_ui_table_t *t = calloc(1, sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    snprintf(t->title, sizeof(t->title), "%s", title != NULL ? title : "");
    return t;
}

vmav_status_t vmav_ui_table_add(vmav_ui_table_t *t, const char *key, const char *value) {
    if (t == NULL || key == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_ui_table_add: null arg");
    }
    if (t->len + 1 > t->cap) {
        const size_t new_cap = t->cap > 0 ? t->cap * 2 : 8;
        row_t *p = realloc(t->rows, new_cap * sizeof(row_t));
        if (p == NULL) {
            return VMAV_ERR(VMAV_ERR_NO_MEM, "table realloc(%zu)", new_cap);
        }
        t->rows = p;
        t->cap = new_cap;
    }
    snprintf(t->rows[t->len].key, sizeof(t->rows[t->len].key), "%s", key);
    snprintf(
        t->rows[t->len].value, sizeof(t->rows[t->len].value), "%s", value != NULL ? value : "");
    t->len++;
    return VMAV_OK_STATUS;
}

void vmav_ui_table_render(const vmav_ui_table_t *t, FILE *out) {
    if (t == NULL || out == NULL) {
        return;
    }
    size_t key_width = 0;
    for (size_t i = 0; i < t->len; i++) {
        const size_t kl = strlen(t->rows[i].key);
        if (kl > key_width) {
            key_width = kl;
        }
    }
    if (t->title[0] != '\0') {
        fprintf(out, "\n  %s\n", t->title);
        for (size_t i = 0; i < strlen(t->title) + 2; i++) {
            fputc('-', out);
        }
        fputc('\n', out);
    }
    for (size_t i = 0; i < t->len; i++) {
        fprintf(out, "  %-*s  %s\n", (int)key_width, t->rows[i].key, t->rows[i].value);
    }
    fflush(out);
}

void vmav_ui_table_free(vmav_ui_table_t *t) {
    if (t == NULL) {
        return;
    }
    free(t->rows);
    free(t);
}
