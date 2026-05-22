/* PGS bitmap subtitle → SRT text via Tesseract OCR.
 *
 * Direct port of v1's src/subtitle_convert/subtitle_convert.c — same
 * algorithm bit-for-bit so the SRT output is stable across the v1→v2
 * cut. See vmav_subtitle.h for the public surface. */

#include "vmavificient/vmav_log.h"
#include "vmavificient/vmav_subtitle.h"
#include "vmavificient/vmav_ui.h"

#include <allheaders.h> /* leptonica */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <sys/stat.h>
#include <tesseract/capi.h> /* tesseract */

/* ====================================================================== */
/*  Language mapping                                                      */
/* ====================================================================== */

const char *vmav_subtitle_iso639_to_tesseract(const char *iso639) {
    if (iso639 == NULL || iso639[0] == '\0') {
        return "eng";
    }

    static const struct {
        const char *iso;
        const char *tess;
    } map[] = {
        {"eng", "eng"}, {"fre", "fra"}, {"fra", "fra"},     {"ger", "deu"},     {"deu", "deu"},
        {"spa", "spa"}, {"ita", "ita"}, {"por", "por"},     {"dut", "nld"},     {"nld", "nld"},
        {"rus", "rus"}, {"jpn", "jpn"}, {"chi", "chi_sim"}, {"zho", "chi_sim"}, {"kor", "kor"},
        {"ara", "ara"}, {"pol", "pol"}, {"swe", "swe"},     {"nor", "nor"},     {"dan", "dan"},
        {"fin", "fin"}, {"tur", "tur"}, {"hin", "hin"},     {"cze", "ces"},     {"ces", "ces"},
        {"hun", "hun"}, {"rum", "ron"}, {"ron", "ron"},     {"tha", "tha"},     {"vie", "vie"},
        {"gre", "ell"}, {"ell", "ell"}, {"heb", "heb"},     {"ukr", "ukr"},     {"bul", "bul"},
        {"hrv", "hrv"}, {NULL, NULL},
    };

    for (int i = 0; map[i].iso != NULL; i++) {
        if (strcmp(iso639, map[i].iso) == 0) {
            return map[i].tess;
        }
    }
    /* Unknown — pass through so custom traineddata names work. */
    return iso639;
}

/* ====================================================================== */
/*  Filename builder                                                      */
/* ====================================================================== */

void vmav_subtitle_build_srt_filename(char *buf,
                                      size_t bufsize,
                                      const char *base_name,
                                      const char *language,
                                      vmav_naming_french_variant_t fv,
                                      bool is_forced,
                                      bool is_sdh) {
    if (buf == NULL || bufsize == 0 || base_name == NULL) {
        return;
    }
    const char *lang = (language != NULL && language[0] != '\0') ? language : "und";
    const char *lang_suffix = lang;
    const char *type_suffix = ".full";
    const char *variant_sep = "";

    if (strcmp(lang, "fre") == 0 || strcmp(lang, "fra") == 0) {
        switch (fv) {
        case VMAV_FR_VFQ:
            lang_suffix = "fre.ca";
            variant_sep = ".";
            break;
        case VMAV_FR_VFI:
            lang_suffix = "fre.vfi";
            variant_sep = ".";
            break;
        case VMAV_FR_VFF:
        case VMAV_FR_UNKNOWN:
        default:
            lang_suffix = "fre.fr";
            variant_sep = ".";
            break;
        }
    }
    if (is_forced) {
        type_suffix = ".forced";
    } else if (is_sdh) {
        type_suffix = ".sdh";
    }
    snprintf(buf, bufsize, "%s.%s%s%s.srt", base_name, lang_suffix, variant_sep, type_suffix);
}

/* ====================================================================== */
/*  Codec ID classification                                               */
/* ====================================================================== */

bool vmav_subtitle_is_pgs(const vmav_track_t *track) {
    if (track == NULL) {
        return false;
    }
    return track->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE;
}

bool vmav_subtitle_is_text(const vmav_track_t *track) {
    if (track == NULL) {
        return false;
    }
    return track->codec_id == AV_CODEC_ID_SUBRIP || track->codec_id == AV_CODEC_ID_ASS ||
           track->codec_id == AV_CODEC_ID_SSA || track->codec_id == AV_CODEC_ID_WEBVTT ||
           track->codec_id == AV_CODEC_ID_MOV_TEXT || track->codec_id == AV_CODEC_ID_TEXT;
}

/* ====================================================================== */
/*  SRT timestamp formatting                                              */
/* ====================================================================== */

static void format_srt_time(char *buf, size_t bufsize, int64_t ms) {
    if (ms < 0) {
        ms = 0;
    }
    const int hours = (int)(ms / 3600000);
    ms %= 3600000;
    const int minutes = (int)(ms / 60000);
    ms %= 60000;
    const int seconds = (int)(ms / 1000);
    const int millis = (int)(ms % 1000);
    snprintf(buf, bufsize, "%02d:%02d:%02d,%03d", hours, minutes, seconds, millis);
}

/* ====================================================================== */
/*  PGS segment types                                                     */
/* ====================================================================== */

#define PGS_PDS 0x14 /* Palette Definition Segment */
#define PGS_ODS 0x15 /* Object Definition Segment */
#define PGS_PCS 0x16 /* Presentation Composition Segment */
#define PGS_WDS 0x17 /* Window Definition Segment */
#define PGS_END 0x80 /* End of Display Set */

#define PCS_NORMAL 0x00
#define PCS_ACQ_POINT 0x40
#define PCS_EPOCH_START 0x80

typedef struct {
    uint8_t y, cr, cb, a;
} pgs_palette_entry_t;

typedef struct {
    bool valid;
    pgs_palette_entry_t palette[256];
    bool palette_set;
    uint8_t *rle_data;
    size_t rle_size;
    size_t rle_capacity;
    int obj_width;
    int obj_height;
    int composition_state;
} pgs_display_set_t;

static void pgs_ds_init(pgs_display_set_t *ds) {
    memset(ds, 0, sizeof(*ds));
}

static void pgs_ds_reset(pgs_display_set_t *ds) {
    free(ds->rle_data);
    pgs_ds_init(ds);
}

static uint16_t read_be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

/* Try zlib-decompress (matroska content compression). Returns malloc'd
 * buffer + sets *out_size, or NULL if not compressed / fails. */
static uint8_t *try_zlib_decompress(const uint8_t *data, int data_size, int *out_size) {
    if (data_size < 2 || data[0] != 0x78) {
        return NULL;
    }
    uLongf buf_size = (uLongf)data_size * 8;
    if (buf_size < 65536) {
        buf_size = 65536;
    }
    uint8_t *buf = malloc(buf_size);
    if (buf == NULL) {
        return NULL;
    }
    int rc = uncompress(buf, &buf_size, data, (uLong)data_size);
    if (rc == Z_BUF_ERROR) {
        buf_size = (uLongf)data_size * 32;
        uint8_t *tmp = realloc(buf, buf_size);
        if (tmp == NULL) {
            free(buf);
            return NULL;
        }
        buf = tmp;
        rc = uncompress(buf, &buf_size, data, (uLong)data_size);
    }
    if (rc != Z_OK) {
        free(buf);
        return NULL;
    }
    *out_size = (int)buf_size;
    return buf;
}

static void parse_pgs_packet(pgs_display_set_t *ds, const uint8_t *data, int data_size) {
    int decomp_size = 0;
    uint8_t *decomp = try_zlib_decompress(data, data_size, &decomp_size);
    if (decomp != NULL) {
        data = decomp;
        data_size = decomp_size;
    }

    int pos = 0;
    while (pos < data_size) {
        if (pos + 3 > data_size) {
            break;
        }
        const uint8_t seg_type = data[pos];
        const uint16_t seg_size = read_be16(&data[pos + 1]);
        pos += 3;
        if (pos + seg_size > data_size) {
            break;
        }
        const uint8_t *seg = &data[pos];

        switch (seg_type) {
        case PGS_PCS:
            if (seg_size >= 11) {
                ds->composition_state = seg[7];
                ds->valid = false;
            }
            break;

        case PGS_PDS:
            if (seg_size >= 2) {
                const int entries_size = seg_size - 2;
                const uint8_t *entries = seg + 2;
                for (int i = 0; i + 5 <= entries_size; i += 5) {
                    const uint8_t idx = entries[i];
                    ds->palette[idx].y = entries[i + 1];
                    ds->palette[idx].cr = entries[i + 2];
                    ds->palette[idx].cb = entries[i + 3];
                    ds->palette[idx].a = entries[i + 4];
                }
                ds->palette_set = true;
            }
            break;

        case PGS_ODS: {
            /* id(2) + version(1) + seq_flag(1) + data_length(3) + width(2) +
             * height(2) + rle_data(...). seq_flag bits: 0x80 first, 0x40 last. */
            if (seg_size < 4) {
                break;
            }
            const uint8_t seq_flag = seg[3];
            const bool is_first = (seq_flag & 0x80) != 0;

            if (is_first) {
                if (seg_size < 11) {
                    break;
                }
                ds->obj_width = read_be16(&seg[7]);
                ds->obj_height = read_be16(&seg[9]);
                const size_t rle_len = (size_t)seg_size - 11;
                free(ds->rle_data);
                ds->rle_capacity = rle_len + 65536;
                ds->rle_data = malloc(ds->rle_capacity);
                if (ds->rle_data != NULL) {
                    memcpy(ds->rle_data, seg + 11, rle_len);
                    ds->rle_size = rle_len;
                } else {
                    ds->rle_size = 0;
                }
            } else if (seg_size > 4 && ds->rle_data != NULL) {
                /* Continuation fragment — append RLE data starting at offset 4. */
                const size_t rle_len = (size_t)seg_size - 4;
                const size_t new_size = ds->rle_size + rle_len;
                if (new_size > ds->rle_capacity) {
                    ds->rle_capacity = new_size + 65536;
                    uint8_t *tmp = realloc(ds->rle_data, ds->rle_capacity);
                    if (tmp == NULL) {
                        break;
                    }
                    ds->rle_data = tmp;
                }
                memcpy(ds->rle_data + ds->rle_size, seg + 4, rle_len);
                ds->rle_size = new_size;
            }
            if (ds->obj_width > 0 && ds->obj_height > 0 && ds->rle_size > 0) {
                ds->valid = true;
            }
            break;
        }

        case PGS_END:
        default:
            break;
        }
        pos += seg_size;
    }

    free(decomp);
}

/* ====================================================================== */
/*  PGS RLE decoding                                                      */
/* ====================================================================== */

/* PGS RLE scheme:
 *   Non-zero byte: 1 pixel of that color index
 *   0x00 + 0x00:                end of line
 *   0x00 + 0LLLLLLL:            L transparent pixels (1..63)
 *   0x00 + 01LLLLLL LLLLLLLL:   L transparent pixels (64..16383)
 *   0x00 + 10LLLLLL + CC:       L pixels of color CC (1..63)
 *   0x00 + 11LLLLLL LLLLLLLL + CC: L pixels of color CC (64..16383) */
static uint8_t *decode_pgs_rle(const uint8_t *rle, size_t rle_size, int w, int h) {
    uint8_t *bitmap = calloc((size_t)w * (size_t)h, 1);
    if (bitmap == NULL) {
        return NULL;
    }
    int x = 0;
    int y = 0;
    size_t i = 0;
    while (i < rle_size && y < h) {
        const uint8_t b = rle[i++];
        if (b != 0) {
            if (x < w) {
                bitmap[y * w + x] = b;
            }
            x++;
            continue;
        }
        if (i >= rle_size) {
            break;
        }
        const uint8_t flags = rle[i++];
        if (flags == 0) {
            x = 0;
            y++;
            continue;
        }
        int len = 0;
        uint8_t color = 0;
        if ((flags & 0xC0) == 0x00) {
            len = flags & 0x3F;
            color = 0;
        } else if ((flags & 0xC0) == 0x40) {
            if (i >= rle_size) {
                break;
            }
            len = ((flags & 0x3F) << 8) | rle[i++];
            color = 0;
        } else if ((flags & 0xC0) == 0x80) {
            if (i >= rle_size) {
                break;
            }
            len = flags & 0x3F;
            color = rle[i++];
        } else {
            if (i + 1 >= rle_size) {
                break;
            }
            len = ((flags & 0x3F) << 8) | rle[i++];
            color = rle[i++];
        }
        for (int j = 0; j < len && x < w; j++) {
            bitmap[y * w + (x++)] = color;
        }
    }
    return bitmap;
}

/* ====================================================================== */
/*  Bitmap → PIX + OCR pre-processing                                     */
/* ====================================================================== */

/* Convert palette-indexed PGS bitmap to 8-bit grayscale PIX.
 * Binarize: bright text (Y > 127) → black (0), else white (255).
 * Transparent pixels (alpha < 128) → white. */
static PIX *
pgs_bitmap_to_pix(const uint8_t *bitmap, int w, int h, const pgs_palette_entry_t *palette) {
    PIX *pix = pixCreate(w, h, 8);
    if (pix == NULL) {
        return NULL;
    }
    l_uint32 *data = pixGetData(pix);
    const int wpl = pixGetWpl(pix);
    for (int y = 0; y < h; y++) {
        l_uint32 *line = data + y * wpl;
        for (int x = 0; x < w; x++) {
            const uint8_t idx = bitmap[y * w + x];
            const pgs_palette_entry_t *pe = &palette[idx];
            if (pe->a < 128) {
                SET_DATA_BYTE(line, x, 255);
                continue;
            }
            SET_DATA_BYTE(line, x, pe->y > 127 ? 0 : 255);
        }
    }
    return pix;
}

/* Pad, scale, and Otsu-binarize a PIX for OCR. v1 tuning preserved. */
static PIX *prepare_pix_for_ocr(PIX *src) {
    const int w = pixGetWidth(src);
    const int h = pixGetHeight(src);
    const int pad = (h < 30) ? 30 : 20;
    const int new_w = w + 2 * pad;
    const int new_h = h + 2 * pad;

    PIX *padded = pixCreate(new_w, new_h, 8);
    if (padded == NULL) {
        return src;
    }
    pixSetAll(padded);
    pixRasterop(padded, pad, pad, w, h, PIX_SRC, src, 0, 0);
    pixDestroy(&src);

    /* Tesseract is most accurate at 40-80px x-height; subtitle bitmaps
     * are 20-50px so scale 3x; for taller (60-120) scale 2x. */
    float scale = 1.0f;
    const int ph = pixGetHeight(padded);
    if (ph < 60) {
        scale = 3.0f;
    } else if (ph < 120) {
        scale = 2.0f;
    }
    if (scale > 1.0f) {
        PIX *scaled = pixScale(padded, scale, scale);
        if (scaled != NULL) {
            pixDestroy(&padded);
            padded = scaled;
        }
    }

    PIX *binary =
        pixOtsuThreshOnBackgroundNorm(padded, NULL, 10, 15, 100, 50, 255, 2, 2, 0.1f, NULL);
    if (binary != NULL) {
        pixDestroy(&padded);
        return binary;
    }
    return padded;
}

/* ====================================================================== */
/*  OCR helpers                                                           */
/* ====================================================================== */

static void strip_whitespace(char *text) {
    if (text == NULL) {
        return;
    }
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\n' || text[len - 1] == '\r' ||
                       text[len - 1] == '\t')) {
        text[--len] = '\0';
    }
    char *start = text;
    while (*start == ' ' || *start == '\n' || *start == '\r' || *start == '\t') {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
}

/* Run OCR on `pix`, append "<index>\n<start> --> <end>\n<text>\n\n"
 * to `fp`. Returns true if an entry was actually written. */
static bool ocr_and_emit_srt(
    TessBaseAPI *tess, PIX *pix, FILE *fp, int *sub_index, int64_t start_ms, int64_t end_ms) {
    TessBaseAPISetImage2(tess, pix);
    char *text = TessBaseAPIGetUTF8Text(tess);
    bool wrote = false;
    if (text != NULL && text[0] != '\0') {
        strip_whitespace(text);
        if (text[0] != '\0') {
            (*sub_index)++;
            char start_str[32];
            char end_str[32];
            format_srt_time(start_str, sizeof(start_str), start_ms);
            format_srt_time(end_str, sizeof(end_str), end_ms);
            fprintf(fp, "%d\n%s --> %s\n%s\n\n", *sub_index, start_str, end_str, text);
            wrote = true;
        }
    }
    if (text != NULL) {
        TessDeleteText(text);
    }
    return wrote;
}

/* Locate the tessdata directory. TESSDATA_PREFIX env wins; otherwise
 * fall back to common system paths. Returns NULL only if absolutely
 * nothing usable was found — Tesseract will then default to its own
 * built-in search. */
static const char *resolve_tessdata_path(void) {
    const char *env = getenv("TESSDATA_PREFIX");
    if (env != NULL && env[0] != '\0') {
        return env;
    }
    static const char *fallback[] = {
        "/usr/local/share/tessdata",
        "/opt/homebrew/share/tessdata",
        "/usr/share/tessdata",
        NULL,
    };
    for (int i = 0; fallback[i] != NULL; i++) {
        struct stat st;
        if (stat(fallback[i], &st) == 0) {
            return fallback[i];
        }
    }
    return NULL;
}

/* ====================================================================== */
/*  Main convert function                                                 */
/* ====================================================================== */

vmav_status_t vmav_subtitle_convert_pgs(const char *input_path,
                                        const vmav_track_t *track,
                                        const char *output_path,
                                        const char *tesseract_lang,
                                        vmav_subtitle_convert_t *out) {
    if (input_path == NULL || track == NULL || output_path == NULL || out == NULL) {
        return VMAV_ERR(VMAV_ERR_BAD_ARG, "vmav_subtitle_convert_pgs: null arg");
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->output_path, sizeof(out->output_path), "%s", output_path);

    struct stat st;
    if (stat(output_path, &st) == 0 && st.st_size > 0) {
        out->skipped = true;
        VMAV_LOG_INFO("subtitle_convert: '%s' already exists (%lld bytes), skipping",
                      output_path,
                      (long long)st.st_size);
        return VMAV_OK_STATUS;
    }

    /* Pick tessdata language. */
    const char *tess_lang = (tesseract_lang != NULL && tesseract_lang[0] != '\0')
                                ? tesseract_lang
                                : vmav_subtitle_iso639_to_tesseract(track->language);
    const char *tessdata = resolve_tessdata_path();

    TessBaseAPI *tess = TessBaseAPICreate();
    if (TessBaseAPIInit3(tess, tessdata, tess_lang) != 0) {
        TessBaseAPIDelete(tess);
        return VMAV_ERR(VMAV_ERR_NOT_FOUND,
                        "tesseract init failed for lang '%s' (tessdata='%s')",
                        tess_lang,
                        tessdata != NULL ? tessdata : "(default)");
    }
    TessBaseAPISetPageSegMode(tess, PSM_SINGLE_BLOCK);
    TessBaseAPISetVariable(tess, "preserve_interword_spaces", "1");

    AVFormatContext *ifmt_ctx = NULL;
    AVPacket *pkt = NULL;
    FILE *srt_fp = NULL;
    PIX *prev_pix = NULL;
    vmav_ui_progress_t *prog = NULL;
    pgs_display_set_t ds;
    pgs_ds_init(&ds);
    vmav_status_t status = VMAV_OK_STATUS;
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    int rc;

    rc = avformat_open_input(&ifmt_ctx, input_path, NULL, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        status = VMAV_ERR(VMAV_ERR_IO, "open '%s': %s", input_path, errbuf);
        goto cleanup;
    }
    rc = avformat_find_stream_info(ifmt_ctx, NULL);
    if (rc < 0) {
        av_make_error_string(errbuf, sizeof(errbuf), rc);
        status = VMAV_ERR(VMAV_ERR_FFMPEG, "stream info: %s", errbuf);
        goto cleanup;
    }
    if (track->stream_index < 0 || (unsigned)track->stream_index >= ifmt_ctx->nb_streams) {
        status = VMAV_ERR(VMAV_ERR_BAD_ARG,
                          "stream index %d out of range (nb=%u)",
                          track->stream_index,
                          ifmt_ctx->nb_streams);
        goto cleanup;
    }
    AVStream *sub_stream = ifmt_ctx->streams[track->stream_index];

    srt_fp = fopen(output_path, "w");
    if (srt_fp == NULL) {
        status = VMAV_ERR(VMAV_ERR_IO, "cannot create '%s'", output_path);
        goto cleanup;
    }

    pkt = av_packet_alloc();
    if (pkt == NULL) {
        status = VMAV_ERR(VMAV_ERR_NO_MEM, "av_packet_alloc");
        goto cleanup;
    }

    int64_t duration_ms = 0;
    if (ifmt_ctx->duration > 0) {
        duration_ms = ifmt_ctx->duration / (AV_TIME_BASE / 1000);
    }
    if (duration_ms > 0) {
        prog = vmav_ui_progress_new(stderr, "subtitle-ocr", (uint64_t)duration_ms);
    }

    int sub_index = 0;
    int64_t prev_pts_ms = -1;

    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != track->stream_index) {
            av_packet_unref(pkt);
            continue;
        }
        int64_t pts_ms = 0;
        if (pkt->pts != AV_NOPTS_VALUE) {
            pts_ms = av_rescale_q(pkt->pts, sub_stream->time_base, (AVRational){1, 1000});
        }
        parse_pgs_packet(&ds, pkt->data, pkt->size);

        if (ds.valid && ds.palette_set && ds.rle_data != NULL) {
            /* There's a pending bitmap from the previous display set —
             * its end-time is this set's start-time. OCR + emit it. */
            if (prev_pix != NULL && prev_pts_ms >= 0) {
                ocr_and_emit_srt(tess, prev_pix, srt_fp, &sub_index, prev_pts_ms, pts_ms);
                pixDestroy(&prev_pix);
                prev_pix = NULL;
            }
            /* Decode the new bitmap and stash for the next iteration. */
            uint8_t *bitmap = decode_pgs_rle(ds.rle_data, ds.rle_size, ds.obj_width, ds.obj_height);
            if (bitmap != NULL) {
                PIX *pix = pgs_bitmap_to_pix(bitmap, ds.obj_width, ds.obj_height, ds.palette);
                free(bitmap);
                if (pix != NULL) {
                    prev_pix = prepare_pix_for_ocr(pix);
                    prev_pts_ms = pts_ms;
                }
            }
            /* Reset for next display set, but keep palette (PDS may not repeat). */
            free(ds.rle_data);
            ds.rle_data = NULL;
            ds.rle_size = 0;
            ds.rle_capacity = 0;
            ds.valid = false;
        } else if (!ds.valid && prev_pix != NULL && prev_pts_ms >= 0) {
            /* Display set with no bitmap = clearing event → end previous sub. */
            if (ds.composition_state == PCS_NORMAL || ds.composition_state == PCS_ACQ_POINT ||
                ds.composition_state == PCS_EPOCH_START) {
                ocr_and_emit_srt(tess, prev_pix, srt_fp, &sub_index, prev_pts_ms, pts_ms);
                pixDestroy(&prev_pix);
                prev_pix = NULL;
                prev_pts_ms = -1;
            }
        }
        av_packet_unref(pkt);

        if (prog != NULL && pts_ms > 0) {
            vmav_ui_progress_set(prog, (uint64_t)pts_ms);
        }
    }

    /* Final flush: any subtitle still pending gets a 3-second default duration. */
    if (prev_pix != NULL && prev_pts_ms >= 0) {
        ocr_and_emit_srt(tess, prev_pix, srt_fp, &sub_index, prev_pts_ms, prev_pts_ms + 3000);
        pixDestroy(&prev_pix);
    }

    out->subtitle_count = sub_index;
    if (prog != NULL) {
        char msg[32];
        snprintf(msg, sizeof(msg), "%d subs", sub_index);
        vmav_ui_progress_finish(prog, msg);
    }

cleanup:
    if (prog != NULL) {
        vmav_ui_progress_free(prog);
    }
    pgs_ds_reset(&ds);
    if (pkt != NULL) {
        av_packet_free(&pkt);
    }
    if (srt_fp != NULL) {
        fclose(srt_fp);
    }
    if (ifmt_ctx != NULL) {
        avformat_close_input(&ifmt_ctx);
    }
    if (tess != NULL) {
        TessBaseAPIEnd(tess);
        TessBaseAPIDelete(tess);
    }
    if (!vmav_status_ok(status) && !out->skipped) {
        remove(output_path);
    }
    return status;
}
