#include "vmavificient/vmav_tracks.h"

#include "util/str_utils.h"

#include <stdlib.h>
#include <string.h>

#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>

/* Common title strings (case-insensitive substring) → ISO 639-2/B
 * codes. Used when the container doesn't tag the language field but
 * the title encodes it (very common in scene rips). Order longest-
 * first within each language to avoid partial-match collisions. */
static const struct {
    const char *pattern;
    const char *code;
} LANG_MAP[] = {
    {"fran\xc3\xa7"
     "ais",
     "fre"},
    {"francais", "fre"},
    {"french", "fre"},
    {"english", "eng"},
    {"anglais", "eng"},
    {"deutsch", "ger"},
    {"german", "ger"},
    {"allemand", "ger"},
    {"espa\xc3\xb1"
     "ol",
     "spa"},
    {"espanol", "spa"},
    {"spanish", "spa"},
    {"castillan", "spa"},
    {"italiano", "ita"},
    {"italian", "ita"},
    {"italien", "ita"},
    {"portugu\xc3\xaa"
     "s",
     "por"},
    {"portugues", "por"},
    {"portuguese", "por"},
    {"nederlands", "dut"},
    {"dutch", "dut"},
    {"russian", "rus"},
    {"russe", "rus"},
    {"japanese", "jpn"},
    {"japonais", "jpn"},
    {"chinese", "chi"},
    {"chinois", "chi"},
    {"korean", "kor"},
    {"arabic", "ara"},
    {"arabe", "ara"},
    {"polish", "pol"},
    {"polonais", "pol"},
    {"swedish", "swe"},
    {"norwegian", "nor"},
    {"danish", "dan"},
    {"finnish", "fin"},
    {"turkish", "tur"},
    {"hindi", "hin"},
    {NULL, NULL},
};

static void guess_language_from_title(const char *title, char *out, size_t out_size) {
    if (out == NULL || out_size < 4) {
        return;
    }
    out[0] = '\0';
    if (title == NULL || title[0] == '\0') {
        return;
    }
    for (size_t i = 0; LANG_MAP[i].pattern != NULL; i++) {
        if (vmav_str_contains_ci(title, LANG_MAP[i].pattern)) {
            snprintf(out, out_size, "%s", LANG_MAP[i].code);
            return;
        }
    }
}

static bool title_suggests_forced(const char *title) {
    if (title == NULL) {
        return false;
    }
    return vmav_str_contains_ci(title, "forced") || vmav_str_contains_ci(title, "forc\xc3\xa9");
}

static bool title_suggests_sdh(const char *title) {
    if (title == NULL) {
        return false;
    }
    return vmav_str_contains_ci(title, "sdh") || vmav_str_contains_ci(title, "closed caption") ||
           vmav_str_contains_ci(title, "hearing");
}

static void fill_track(const AVStream *stream, vmav_track_t *t) {
    const AVCodecParameters *cp = stream->codecpar;
    t->stream_index = (int)stream->index;
    t->codec_id = (int)cp->codec_id;
    t->profile = cp->profile;
    t->bit_rate = (int64_t)cp->bit_rate;
    t->channels = cp->ch_layout.nb_channels;

    const char *cname = avcodec_get_name(cp->codec_id);
    snprintf(t->codec_name, sizeof(t->codec_name), "%s", cname != NULL ? cname : "");

    /* Title from metadata. Several keys in the wild; "title" is canonical. */
    const AVDictionaryEntry *title_e =
        av_dict_get(stream->metadata, "title", NULL, AV_DICT_IGNORE_SUFFIX);
    if (title_e != NULL && title_e->value != NULL) {
        snprintf(t->name, sizeof(t->name), "%s", title_e->value);
    }

    /* Language: prefer the explicit metadata, fall back to title-guess. */
    const AVDictionaryEntry *lang_e =
        av_dict_get(stream->metadata, "language", NULL, AV_DICT_IGNORE_SUFFIX);
    if (lang_e != NULL && lang_e->value != NULL && lang_e->value[0] != '\0' &&
        strcmp(lang_e->value, "und") != 0) {
        snprintf(t->language, sizeof(t->language), "%s", lang_e->value);
    } else {
        guess_language_from_title(t->name, t->language, sizeof(t->language));
    }

    /* Disposition flags. Disposition is authoritative when set;
     * title hints supplement (some encoders forget the flag). */
    t->is_forced =
        (stream->disposition & AV_DISPOSITION_FORCED) != 0 || title_suggests_forced(t->name);
    t->is_sdh =
        (stream->disposition & AV_DISPOSITION_HEARING_IMPAIRED) != 0 || title_suggests_sdh(t->name);
}

void vmav_media_tracks_free(vmav_media_tracks_t *t) {
    if (t == NULL) {
        return;
    }
    free(t->audio);
    free(t->subtitle);
    t->audio = NULL;
    t->subtitle = NULL;
    t->audio_count = 0;
    t->subtitle_count = 0;
}

vmav_status_t vmav_media_tracks_probe(const char *path, vmav_media_tracks_t *out) {
    if (path == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_media_tracks_probe: null arg");
    }
    memset(out, 0, sizeof(*out));

    AVFormatContext *fmt_ctx = NULL;
    char errbuf[AV_ERROR_MAX_STRING_SIZE];

    int rc = avformat_open_input(&fmt_ctx, path, NULL, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        return VMAV_ERR(VMAV_ERR_IO, "open '%s': %s", path, errbuf);
    }
    rc = avformat_find_stream_info(fmt_ctx, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        avformat_close_input(&fmt_ctx);
        return VMAV_ERR(VMAV_ERR_FFMPEG, "stream info '%s': %s", path, errbuf);
    }

    /* First pass: count. */
    size_t na = 0;
    size_t ns = 0;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        const enum AVMediaType t = fmt_ctx->streams[i]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_AUDIO) {
            na++;
        } else if (t == AVMEDIA_TYPE_SUBTITLE) {
            ns++;
        }
    }

    if (na > 0) {
        out->audio = calloc(na, sizeof(*out->audio));
        if (out->audio == NULL) {
            avformat_close_input(&fmt_ctx);
            return VMAV_ERR(VMAV_ERR_NO_MEM, "audio arr calloc(%zu)", na);
        }
    }
    if (ns > 0) {
        out->subtitle = calloc(ns, sizeof(*out->subtitle));
        if (out->subtitle == NULL) {
            free(out->audio);
            out->audio = NULL;
            avformat_close_input(&fmt_ctx);
            return VMAV_ERR(VMAV_ERR_NO_MEM, "subs arr calloc(%zu)", ns);
        }
    }

    /* Second pass: fill. */
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *s = fmt_ctx->streams[i];
        const enum AVMediaType t = s->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_AUDIO) {
            fill_track(s, &out->audio[out->audio_count++]);
        } else if (t == AVMEDIA_TYPE_SUBTITLE) {
            fill_track(s, &out->subtitle[out->subtitle_count++]);
        }
    }

    avformat_close_input(&fmt_ctx);
    return VMAV_OK_STATUS;
}
