/*
 * Copyright (C) 2019-2024 Yahweasel and contributors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <malloc.h>

#ifdef __EMSCRIPTEN_PTHREADS__
#include <emscripten.h>
#include <pthread.h>
#endif

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavutil/avutil.h"
#include <libavutil/dict.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/time.h>
#include <libavutil/dict.h>
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/version.h"

#define A(struc, type, field) \
    type struc ## _ ## field(struc *a) { return a->field; } \
    void struc ## _ ## field ## _s(struc *a, type b) { a->field = b; }
 
#define AL(struc, type, field) \
    uint32_t struc ## _ ## field(struc *a) { return (uint32_t) a->field; } \
    uint32_t struc ## _ ## field ## hi(struc *a) { return (uint32_t) (a->field >> 32); } \
    void struc ## _ ## field ## _s(struc *a, uint32_t b) { a->field = b; } \
    void struc ## _ ## field ## hi_s(struc *a, uint32_t b) { a->field |= (((type) b) << 32); }

#define AA(struc, type, field) \
    type struc ## _ ## field ## _a(struc *a, size_t c) { return a->field[c]; } \
    void struc ## _ ## field ## _a_s(struc *a, size_t c, type b) { ((type *) a->field)[c] = b; }

#define RAT(struc, field) \
    int struc ## _ ## field ## _num(struc *a) { return a->field.num; } \
    int struc ## _ ## field ## _den(struc *a) { return a->field.den; } \
    void struc ## _ ## field ## _num_s(struc *a, int b) { a->field.num = b; } \
    void struc ## _ ## field ## _den_s(struc *a, int b) { a->field.den = b; } \
    void struc ## _ ## field ## _s(struc *a, int n, int d) { a->field.num = n; a->field.den = d; }

#define RAT_FAKE(struc, field, num, den) \
    int struc ## _ ## field ## _num(struc *a) { (void) a; return num; } \
    int struc ## _ ## field ## _den(struc *a) { (void) a; return den; } \
    void struc ## _ ## field ## _num_s(struc *a, int b) { (void) a; (void) b; } \
    void struc ## _ ## field ## _den_s(struc *a, int b) { (void) a; (void) b; } \
    void struc ## _ ## field ## _s(struc *a, int n, int d) { (void) a; (void) n; (void) d; }


/* Not part of libav, just used to ensure a round trip to C for async purposes */
void ff_nothing() {}

extern int ffmpeg_main(int argc, char** argv);


/****************************************************************
 * libavutil
 ***************************************************************/

/* AVFrame */
#define B(type, field) A(AVFrame, type, field)
#define BL(type, field) AL(AVFrame, type, field)
#define BA(type, field) AA(AVFrame, type, field)
B(size_t, crop_bottom)
B(size_t, crop_left)
B(size_t, crop_right)
B(size_t, crop_top)
BA(uint8_t *, data)
B(int, format)
B(int, height)
B(int, key_frame)
BA(int, linesize)
B(int, nb_samples)
B(int, pict_type)
BL(int64_t, pts)
BL(int64_t, duration)
B(int, sample_rate)
B(int, width)
#undef B
#undef BL
#undef BA

RAT(AVFrame, sample_aspect_ratio)

#if LIBAVUTIL_VERSION_INT > AV_VERSION_INT(57, 10, 101)
RAT(AVFrame, time_base)
#else
RAT_FAKE(AVFrame, time_base, 1, 1000)
#endif

/* Either way we expose the old channel layout API, but if the new channel
 * layout API is available, we use it */
#if LIBAVUTIL_VERSION_INT > AV_VERSION_INT(57, 23, 100)
/* New API */
#define CHL(struc) \
void struc ## _channel_layoutmask_s(struc *a, uint32_t bl, uint32_t bh) { \
    uint64_t mask =  (((uint64_t) bl)) | (((uint64_t) bh) << 32); \
    av_channel_layout_uninit(&a->ch_layout); \
    av_channel_layout_from_mask(&a->ch_layout, mask);\
} \
uint64_t struc ## _channel_layoutmask(struc *a) { \
    return a->ch_layout.u.mask; \
}\
int struc ## _channels(struc *a) { \
    return a->ch_layout.nb_channels; \
} \
void struc ## _channels_s(struc *a, int b) { \
    a->ch_layout.nb_channels = b; \
}\
int struc ## _ch_layout_nb_channels(struc *a) { \
    return a->ch_layout.nb_channels; \
}\
void struc ## _ch_layout_nb_channels_s(struc *a, int b) { \
    a->ch_layout.nb_channels = b; \
}\
uint32_t struc ## _channel_layout(struc *a) { \
    return (uint32_t) a->ch_layout.u.mask; \
}\
uint32_t struc ##_channel_layouthi(struc *a) { \
    return (uint32_t) (a->ch_layout.u.mask >> 32);\
}\
void struc ##_channel_layout_s(struc *a, uint32_t b) { \
    a->ch_layout.u.mask = (a->ch_layout.u.mask & (0xFFFFFFFFull << 32)) | (((uint64_t) b));\
    uint64_t mask = a->ch_layout.u.mask;\
    av_channel_layout_uninit(&a->ch_layout);\
    av_channel_layout_from_mask(&a->ch_layout, mask);\
}\
void struc ##_channel_layouthi_s(struc *a, uint32_t b) { \
    a->ch_layout.u.mask = (a->ch_layout.u.mask & 0xFFFFFFFFull) | (((uint64_t) b) << 32);\
    uint64_t mask = a->ch_layout.u.mask;\
    av_channel_layout_uninit(&a->ch_layout);\
    av_channel_layout_from_mask(&a->ch_layout, mask);\
}

#else
/* Old API */
#define CHL(struc) \
void struc ## _channel_layoutmask_s(struc *a, uint32_t bl, uint32_t bh) { \
    a->channel_layout = ((uint16_t) bh << 32) | bl; \
} \
uint64_t struc ## _channel_layoutmask(struc *a) { \
    return a->channel_layout; \
}\
int struc ## _channels(struc *a) { \
    return a->channels; \
} \
void struc ## _channels_s(struc *a, int b) { \
    a->channels = b; \
}\
int struc ## _ch_layout_nb_channels(struc *a) { \
    return a->channels; \
}\
void struc ## _ch_layout_nb_channels_s(struc *a, int b) { \
    a->channels = b; \
}\
uint32_t struc ## _channel_layout(struc *a) { \
    return a->channel_layout; \
}\
uint32_t struc ##_channel_layouthi(struc *a) { \
    return a->channel_layout >> 32; \
}\
void struc ##_channel_layout_s(struc *a, uint32_t b) { \
    a->channel_layout = \
        (a->channel_layout & (0xFFFFFFFFull << 32)) | \
        ((uint64_t) b); \
}\
void struc ##_channel_layouthi_s(struc *a, uint32_t b) { \
    a->channel_layout = \
        (((uint64_t) b) << 32) | \
        (a->channel_layout & 0xFFFFFFFFull); \
}

#endif /* Channel layout API version */

CHL(AVFrame)

/* This isn't in libav because there's only one property to scale, but this
 * scaling is sufficiently painful in JavaScript that it's worth wrapping this
 * up in a helper. */
void ff_frame_rescale_ts_js(
    AVFrame *frame,
    int tb_src_num, int tb_src_den,
    int tb_dst_num, int tb_dst_den
) {
    AVRational tb_src = {tb_src_num, tb_src_den},
               tb_dst = {tb_dst_num, tb_dst_den};
    if (frame->pts != AV_NOPTS_VALUE)
        frame->pts = av_rescale_q(frame->pts, tb_src, tb_dst);
}

/* AVPixFmtDescriptor */
#define B(type, field) A(AVPixFmtDescriptor, type, field)
B(uint64_t, flags)
B(uint8_t, nb_components)
B(uint8_t, log2_chroma_h)
B(uint8_t, log2_chroma_w)
#undef B

int AVPixFmtDescriptor_comp_depth(AVPixFmtDescriptor *fmt, int comp)
{
    return fmt->comp[comp].depth;
}

int av_opt_set_int_list_js(void *obj, const char *name, int width, void *val, int term, int flags)
{
    switch (width) {
        case 4:
            return av_opt_set_int_list(obj, name, ((int32_t *) val), term, flags);
        case 8:
            return av_opt_set_int_list(obj, name, ((int64_t *) val), term, flags);
        default:
            return AVERROR(EINVAL);
    }
}


/****************************************************************
 * libavcodec
 ***************************************************************/

/* AVCodec */
#define B(type, field) A(AVCodec, type, field)
#define BA(type, field) AA(AVCodec, type, field)
B(const char *, name)
B(const char *, long_name)
B(const enum AVSampleFormat *, sample_fmts)
BA(enum AVSampleFormat, sample_fmts)
B(const int *, supported_samplerates)
BA(int, supported_samplerates)
B(enum AVMediaType, type)
#undef B
#undef BA

/* AVCodecContext */
#define B(type, field) A(AVCodecContext, type, field)
#define BL(type, field) AL(AVCodecContext, type, field)
B(enum AVCodecID, codec_id)
B(enum AVMediaType, codec_type)
BL(int64_t, bit_rate)
B(uint8_t *, extradata)
B(int, extradata_size)
B(int, frame_size)
B(int, gop_size)
B(int, height)
B(int, keyint_min)
B(int, level)
B(int, max_b_frames)
B(int, pix_fmt)
B(int, profile)
BL(int64_t, rc_max_rate)
BL(int64_t, rc_min_rate)
B(int, sample_fmt)
B(int, sample_rate)
B(int, qmax)
B(int, qmin)
B(int, width)
#undef B
#undef BL

RAT(AVCodecContext, framerate)
RAT(AVCodecContext, sample_aspect_ratio)
RAT(AVCodecContext, time_base)
CHL(AVCodecContext)


/* AVCodecDescriptor */
#define B(type, field) A(AVCodecDescriptor, type, field)
B(enum AVCodecID, id)
B(const char *, long_name)
AA(AVCodecDescriptor, const char *, mime_types)
B(const char *, name)
B(int, props)
B(enum AVMediaType, type)
#undef B

/* AVCodecParameters */
#define B(type, field) A(AVCodecParameters, type, field)
B(enum AVCodecID, codec_id)
B(uint32_t, codec_tag)
B(enum AVMediaType, codec_type)
B(uint8_t *, extradata)
B(int, extradata_size)
B(int, format)
B(int64_t, bit_rate)
B(int, profile)
B(int, level)
B(int, width)
B(int, height)
B(enum AVColorRange, color_range)
B(enum AVColorPrimaries, color_primaries)
B(enum AVColorTransferCharacteristic, color_trc)
B(enum AVColorSpace, color_space)
B(enum AVChromaLocation, chroma_location)
B(int, sample_rate)
#undef B

#if LIBAVCODEC_VERSION_INT > AV_VERSION_INT(60, 10, 100)
RAT(AVCodecParameters, framerate)
#else
RAT_FAKE(AVCodecParameters, framerate, 60, 1)
#endif

CHL(AVCodecParameters)

const char *ff_get_colorspace_name(enum AVColorSpace val) {
    return av_color_space_name(val);
}

const char *ff_get_pix_fmt_name(enum AVPixelFormat val) {
    return av_get_pix_fmt_name(val);
}

const char *ff_get_color_range_name(enum AVColorRange val) {
    return av_color_range_name(val);
}

const char *ff_get_input_format_name(AVFormatContext* fmt_ctx) {
    return fmt_ctx->iformat ? fmt_ctx->iformat->name : NULL;
}

const char *ff_get_major_brand(AVFormatContext* fmt_ctx) {
    AVDictionaryEntry *tag = NULL;
    tag = av_dict_get(fmt_ctx->metadata, "major_brand", NULL, 0);

    if (tag) return tag->value;

    return NULL;
}

double ff_get_media_duration(AVFormatContext* fmt_ctx) {
    if (fmt_ctx->duration != AV_NOPTS_VALUE && fmt_ctx->duration > 0) {
        return fmt_ctx->duration / (double)AV_TIME_BASE;
    }

    double max_sec = 0.0;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *st = fmt_ctx->streams[i];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
            continue;
        }
        if (st->duration != AV_NOPTS_VALUE && st->duration > 0) {
            AVRational tb = st->time_base;
            double sec = st->duration * ((double)tb.num / tb.den);
            if (sec > max_sec) max_sec = sec;
        }
    }
    if (max_sec > 0.0) {
        return max_sec;
    }

    double max_fallback = 0.0;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *st = fmt_ctx->streams[i];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
            continue;
        }
        if (av_seek_frame(fmt_ctx, i, INT64_MAX, AVSEEK_FLAG_BACKWARD) < 0)
            continue;

        AVPacket *pkt = av_packet_alloc();
        if (!pkt) continue;

        while (av_read_frame(fmt_ctx, pkt) == 0) {
            if (pkt->stream_index == (int)i && pkt->pts != AV_NOPTS_VALUE) {
                AVRational tb = st->time_base;
                double sec = pkt->pts * ((double)tb.num / tb.den);
                double duration_sec = pkt->duration * ((double)tb.num / tb.den);
                double gap_sec = fmax(0.0, duration_sec);
                double end_sec = sec + gap_sec;
                if (end_sec > max_fallback) {
                    max_fallback = end_sec;
                }
            }
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    }
    return max_fallback;
}

const char *ff_get_timecode(AVFormatContext *fmt_ctx) {
    AVDictionaryEntry *tag = av_dict_get(fmt_ctx->metadata, "timecode", NULL, 0);

    if (tag)
        return tag->value;

    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        tag = av_dict_get(fmt_ctx->streams[i]->metadata, "timecode", NULL, 0);
        if (tag)
            return tag->value;
    }
    return NULL;
}
uint64_t av_channel_layout_default_mask(int nb)
{
    AVChannelLayout l;
    av_channel_layout_default(&l, nb);

    uint64_t mask = (l.order == AV_CHANNEL_ORDER_NATIVE) ? l.u.mask : 0;
    av_channel_layout_uninit(&l);
    return mask;
}


/* AVPacket */
#define B(type, field) A(AVPacket, type, field)
#define BL(type, field) AL(AVPacket, type, field)
B(uint8_t *, data)
BL(int64_t, dts)
BL(int64_t, duration)
B(int, flags)
BL(int64_t, pos)
BL(int64_t, pts)
B(AVPacketSideData *, side_data)
B(int, side_data_elems)
B(int, size)
B(int, stream_index)
#undef B
#undef BL

#if LIBAVCODEC_VERSION_INT > AV_VERSION_INT(59, 4, 100)
RAT(AVPacket, time_base)
#else
RAT_FAKE(AVPacket, time_base, 1, 1000)
#endif


/* AVPacketSideData uses special accessors because it's usually an array */
uint8_t *AVPacketSideData_data(AVPacketSideData *a, int idx) {
    return a[idx].data;
}

int AVPacketSideData_size(AVPacketSideData *a, int idx) {
    return a[idx].size;
}

enum AVPacketSideDataType AVPacketSideData_type(AVPacketSideData *a, int idx) {
    return a[idx].type;
}

int avcodec_open2_js(
    AVCodecContext *avctx, const AVCodec *codec, AVDictionary *options
) {
    return avcodec_open2(avctx, codec, &options);
}

/* Implemented as a binding so that we don't have to worry about struct copies */
void av_packet_rescale_ts_js(
    AVPacket *pkt,
    int tb_src_num, int tb_src_den,
    int tb_dst_num, int tb_dst_den
) {
    AVRational tb_src = {tb_src_num, tb_src_den},
               tb_dst = {tb_dst_num, tb_dst_den};
    av_packet_rescale_ts(pkt, tb_src, tb_dst);
}


/****************************************************************
 * avformat
 ***************************************************************/

/* AVFormatContext */
#define B(type, field) A(AVFormatContext, type, field)
#define BA(type, field) AA(AVFormatContext, type, field)
#define BL(type, field) AL(AVFormatContext, type, field)
BL(int64_t, duration)
B(int, flags)
B(unsigned int, nb_streams)
B(const struct AVOutputFormat *, oformat)
B(AVIOContext *, pb)
BL(int64_t, start_time)
BA(AVStream *, streams)
#undef B
#undef BA
#undef BL

/* AVStream */
#define B(type, field) A(AVStream, type, field)
#define BL(type, field) AL(AVStream, type, field)
B(AVCodecParameters *, codecpar)
B(enum AVDiscard, discard)
BL(int64_t, start_time)
BL(int64_t, duration)
#undef B
#undef BL

RAT(AVStream, time_base)
RAT(AVStream, sample_aspect_ratio)

int avformat_seek_file_min(
    AVFormatContext *s, int stream_index, int64_t ts, int flags
) {
    return avformat_seek_file(s, stream_index, ts, ts, INT64_MAX, flags);
}

int avformat_seek_file_max(
    AVFormatContext *s, int stream_index, int64_t ts, int flags
) {
    return avformat_seek_file(s, stream_index, INT64_MIN, ts, ts, flags);
}

int avformat_seek_file_approx(
    AVFormatContext *s, int stream_index, int64_t ts, int flags
) {
    return avformat_seek_file(s, stream_index, INT64_MIN, ts, INT64_MAX, flags);
}

int avformat_get_rotation(AVStream *st) {
    AVDictionaryEntry *tag = NULL;
    double rot = 0;

    AVCodecParameters *codecpar = NULL;
    uint8_t *displaymatrix = NULL;
    size_t side_data_size = 0;

    if (!st) {
        return AVERROR(EINVAL);
    }

    // check the AVStream#metadata for rotation
    tag = av_dict_get(st->metadata, "rotate", NULL, 0);
    if (tag && tag->value) {
        rot = fmod(360.0 + atoi(tag->value), 360.0);
        return (int)(rot + 0.5);
    }

    // check the AVCodecParameters#coded_side_data
    codecpar = st->codecpar;
    for (int i = 0; i < codecpar->nb_coded_side_data; i++) {
        if (codecpar->coded_side_data[i].type == AV_PKT_DATA_DISPLAYMATRIX) {
            displaymatrix = codecpar->coded_side_data[i].data;
            side_data_size = codecpar->coded_side_data[i].size;
            break;
        }
    }

    if (displaymatrix && side_data_size >= sizeof(int32_t) * 9) {
        rot = av_display_rotation_get((int32_t *)displaymatrix);
        rot = fmod(rot + 360.0, 360.0);
        return (int)(rot + 0.5);
    }

    return 0;
}

double avstream_get_frame_rate(AVStream *st) {
    AVRational fps = (st->avg_frame_rate.num != 0) 
        ? st->avg_frame_rate
        : st->r_frame_rate;
    if (fps.den == 0) return 0.0;
    return (double)fps.num / (double)fps.den;
}

int avstream_get_sample_aspect_ratio_num(AVStream *st) {
    return st->sample_aspect_ratio.num;
}

int avstream_get_sample_aspect_ratio_den(AVStream *st) {
    return st->sample_aspect_ratio.den;
}

/****************************************************************
 * libavfilter
 ***************************************************************/

/* AVFilterInOut */
#define B(type, field) A(AVFilterInOut, type, field)
B(AVFilterContext *, filter_ctx)
B(char *, name)
B(AVFilterInOut *, next)
B(int, pad_idx)
#undef B

/* Buffer sink */
int av_buffersink_get_time_base_num(const AVFilterContext *ctx) {
    return av_buffersink_get_time_base(ctx).num;
}

int av_buffersink_get_time_base_den(const AVFilterContext *ctx) {
    return av_buffersink_get_time_base(ctx).den;
}

#if LIBAVFILTER_VERSION_INT > AV_VERSION_INT(8, 27, 100)
int ff_buffersink_set_ch_layout(AVFilterContext *ctx, unsigned int layoutlo, unsigned int layouthi) {
    uint64_t layout;
    char layoutStr[20];
    layout = ((uint64_t) layouthi << 32) | ((uint64_t) layoutlo);
    sprintf(layoutStr, "0x%llx", layout);
    return av_opt_set(ctx, "ch_layouts", layoutStr, AV_OPT_SEARCH_CHILDREN);
}
#else
int ff_buffersink_set_ch_layout(AVFilterContext *ctx, unsigned int layoutlo, unsigned int layouthi) {
    uint64_t layout[2];
    layout[0] = ((uint64_t) layouthi << 32) | ((uint64_t) layoutlo);
    layout[1] = -1;
    return av_opt_set_int_list(ctx, "channel_layouts", layout, -1, AV_OPT_SEARCH_CHILDREN);
}
#endif


/****************************************************************
 * swscale
 ***************************************************************/
int libavjs_with_swscale() {
#ifdef LIBAVJS_WITH_SWSCALE
    return 1;
#else
    return 0;
#endif
}

#ifndef LIBAVJS_WITH_SWSCALE
/* swscale isn't included, but we need the symbols */
void sws_getContext() {}
void sws_freeContext() {}
void sws_scale_frame() {}

#elif LIBAVUTIL_VERSION_INT <= AV_VERSION_INT(57, 4, 101)
/* No sws_scale_frame in this version */
void sws_scale_frame() {}

#endif


/****************************************************************
 * CLI
 ***************************************************************/
int libavjs_with_cli() {
#ifdef LIBAVJS_WITH_CLI
    return 1;
#else
    return 0;
#endif
}

#ifndef LIBAVJS_WITH_CLI
int ffmpeg_main() { return 0; }
int ffprobe_main() { return 0; }
#endif

void cleanup(AVFormatContext *in_fmt, AVFormatContext *out_fmt) {
    if (out_fmt && !(out_fmt->oformat->flags & AVFMT_NOFILE)) 
        avio_closep(&out_fmt->pb);
    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);
}

int ff_extract_audio(const char *in_filename, const char *out_filename, void (*progress_cb)(int current, int total)) {
    AVFormatContext *in_fmt = NULL, *out_fmt = NULL;
    AVPacket pkt;
    int audio_stream_index = -1;
    int ret = 0;

    if ((ret = avformat_open_input(&in_fmt, in_filename, NULL, NULL)) < 0) goto fail;
    if ((ret = avformat_find_stream_info(in_fmt, NULL)) < 0) goto fail;

    for (unsigned i = 0; i < in_fmt->nb_streams; i++) {
        if (in_fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }
    if (audio_stream_index < 0) {
        ret = AVERROR_STREAM_NOT_FOUND;
        goto fail;
    }

    for (unsigned i = 0; i < in_fmt->nb_streams; i++) {
        if (i != audio_stream_index) {
            in_fmt->streams[i]->discard = AVDISCARD_ALL;
        }
    }

    if ((ret = avformat_alloc_output_context2(&out_fmt, NULL, NULL, out_filename)) < 0) goto fail;


    AVStream *in_stream = in_fmt->streams[audio_stream_index];
    AVStream *out_stream = avformat_new_stream(out_fmt, NULL);
    if (!out_stream) {
        ret = AVERROR_UNKNOWN;
        goto fail;
    }
    ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    if (ret < 0) {
        goto fail;
    }
    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = in_stream->time_base;

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&out_fmt->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            goto fail;
        }
    }

    ret = avformat_write_header(out_fmt, NULL);
    if (ret < 0) {
        goto fail;
    }

    int64_t total_duration = 0;
    if (in_stream->duration != AV_NOPTS_VALUE && in_stream->duration > 0) {
        total_duration = in_stream->duration;
    } else if (in_fmt->duration != AV_NOPTS_VALUE && in_fmt->duration > 0) {
        total_duration = av_rescale_q(in_fmt->duration, AV_TIME_BASE_Q, in_stream->time_base);
    }

    int64_t processed_pts = 0;
    int packet_count = 0;
    const int progress_update_interval = 100;

    while (av_read_frame(in_fmt, &pkt) >= 0) {
        if (pkt.stream_index == audio_stream_index) {
            pkt.stream_index = out_stream->index;
            av_interleaved_write_frame(out_fmt, &pkt);
            
            if (pkt.pts != AV_NOPTS_VALUE) {
                processed_pts = pkt.pts;
            }
            
            packet_count++;
            
            if (progress_cb && packet_count % progress_update_interval == 0) {
                progress_cb(processed_pts, total_duration);
            }
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(out_fmt);
    cleanup(in_fmt, out_fmt);
    return ret;

fail:
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "ff_extract_audio: errorno=%d (%s)\n", ret, errbuf);
        cleanup(in_fmt, out_fmt);
    }
    return ret;
}

int ff_slice_audio(const char *in_filename, const char *out_filename, double start_time, double duration) {
    AVFormatContext *in_fmt = NULL, *out_fmt = NULL;
    AVPacket pkt;
    int audio_stream_index = -1;
    int ret = 0;

    if ((ret = avformat_open_input(&in_fmt, in_filename, NULL, NULL)) < 0) goto fail;
    if ((ret = avformat_find_stream_info(in_fmt, NULL)) < 0) goto fail;

    for (unsigned i = 0; i < in_fmt->nb_streams; i++) {
        if (in_fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (audio_stream_index < 0) {
                audio_stream_index = i;
            } else {
                in_fmt->streams[i]->discard = AVDISCARD_ALL;
            }
        } else {
            in_fmt->streams[i]->discard = AVDISCARD_ALL;
        }
    }
    if (audio_stream_index < 0) {
        ret = AVERROR_STREAM_NOT_FOUND;
        goto fail;
    }

    AVStream *in_stream = in_fmt->streams[audio_stream_index];
    int64_t seek_pts = (int64_t)(start_time * (double)in_stream->time_base.den / in_stream->time_base.num);
    ret = av_seek_frame(in_fmt, audio_stream_index, seek_pts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        goto fail;
    }
    avformat_flush(in_fmt);

    if ((ret = avformat_alloc_output_context2(&out_fmt, NULL, NULL, out_filename)) < 0) {
        goto fail;
    }

    AVStream *out_stream = avformat_new_stream(out_fmt, NULL);
    if (!out_stream) {
        ret = AVERROR_UNKNOWN;
        goto fail;
    }
    ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    if (ret < 0) {
        goto fail;
    }
    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = in_stream->time_base;

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&out_fmt->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            goto fail;
        }
    }

    ret = avformat_write_header(out_fmt, NULL);
    if (ret < 0) {
        goto fail;
    }

    int64_t base_pts = AV_NOPTS_VALUE;
    int64_t duration_pts = (int64_t)(duration * (double)in_stream->time_base.den / in_stream->time_base.num);

    while (av_read_frame(in_fmt, &pkt) >= 0) {
        if (pkt.stream_index == audio_stream_index) {
            if (base_pts == AV_NOPTS_VALUE) {
                base_pts = pkt.pts;
            }

            int64_t pts_time = (pkt.pts - base_pts);
            if (pts_time >= duration_pts) {
                av_packet_unref(&pkt);
                break;
            }
            if (pts_time >= 0) {
                pkt.stream_index = out_stream->index;
                av_interleaved_write_frame(out_fmt, &pkt);
            }
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(out_fmt);
    cleanup(in_fmt, out_fmt);
    return ret;

fail:
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "ff_slice_audio: errorno=%d (%s)\n", ret, errbuf);
        cleanup(in_fmt, out_fmt);
    }
    return ret;
}

int convert_to_hls(const char* in_url, const char* playlist_path) {
    AVFormatContext *in_fmt = NULL, *ofmt = NULL;
    AVDictionary *mux_opts = NULL;
    AVPacket pkt;
    int ret = 0;

    ret = avformat_open_input(&in_fmt, in_url, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "avformat_open_input: errorno=%d (%s)\n", ret, av_err2str(ret));
        goto end;
    }
    ret = avformat_find_stream_info(in_fmt, NULL);
    if (ret < 0) {
        fprintf(stderr, "avformat_find_stream_info: errorno=%d (%s)\n", ret, av_err2str(ret));
        goto end;
    }

    ret = avformat_alloc_output_context2(&ofmt, NULL, "hls", playlist_path);
    if (ret < 0) {
        fprintf(stderr, "avformat_alloc_output_context2: errorno=%d (%s)\n", ret, av_err2str(ret));
        goto end;
    }

    av_dict_set(&mux_opts, "hls_segment_type", "fmp4", 0);
    av_dict_set(&mux_opts, "hls_playlist_type", "vod", 0);
    av_dict_set(&mux_opts, "hls_fmp4_init_filename", "init.mp4", 0);

    int v_idx = av_find_best_stream(in_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (v_idx < 0) { 
        ret = AVERROR_STREAM_NOT_FOUND; 
        goto end; 
    }

    AVStream *in_vst = in_fmt->streams[v_idx];
    AVStream *out_vst = avformat_new_stream(ofmt, NULL);
    if (!out_vst) { ret = AVERROR(ENOMEM); goto end; }

    ret = avcodec_parameters_copy(out_vst->codecpar, in_vst->codecpar);
    if (ret < 0) {
        fprintf(stderr, "avcodec_parameters_copy: errorno=%d (%s)\n", ret, av_err2str(ret));
        goto end;
    }
    out_vst->codecpar->codec_tag = 0;
    out_vst->time_base = in_vst->time_base;

    if (!(ofmt->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt->pb, playlist_path, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "avio_open: errorno=%d (%s)\n", ret, av_err2str(ret));
            goto end;
        }
    }
    ret = avformat_write_header(ofmt, &mux_opts);
    if (ret < 0) {
        fprintf(stderr, "avformat_write_header: errorno=%d (%s)\n", ret, av_err2str(ret));
        goto end;
    }

    av_dict_free(&mux_opts);

    while (av_read_frame(in_fmt, &pkt) >= 0) {
        if (pkt.stream_index == v_idx) {
            av_packet_rescale_ts(&pkt, in_vst->time_base, out_vst->time_base);
            pkt.stream_index = out_vst->index;
            ret = av_interleaved_write_frame(ofmt, &pkt);
            if (ret < 0) {
                fprintf(stderr, "av_interleaved_write_frame: errorno=%d (%s)\n", ret, av_err2str(ret));
            }
        }
        av_packet_unref(&pkt);
    }

    ret = av_write_trailer(ofmt);
    if (ret < 0) {
        fprintf(stderr, "av_write_trailer: errorno=%d (%s)\n", ret, av_err2str(ret));
    }

end:
    if (ofmt && !(ofmt->oformat->flags & AVFMT_NOFILE) && ofmt->pb) avio_closep(&ofmt->pb);
    if (ofmt) avformat_free_context(ofmt);
    if (in_fmt) avformat_close_input(&in_fmt);
    av_dict_free(&mux_opts);
    return ret;
}

/****************************************************************
 * Threading
 ***************************************************************/

#ifdef __EMSCRIPTEN_PTHREADS__
EM_JS(void *, libavjs_main_thread, (void *ignore), {
    // Avoid exiting the runtime so we can receive normal requests
    noExitRuntime = Module.noExitRuntime = true;

    // Hijack the event handler
    var origOnmessage = onmessage;
    onmessage = function(ev) {
        var a;

        function reply(succ, ret) {
            var transfer = [];
            if (typeof ret === "object" && ret && ret.libavjsTransfer)
                transfer = ret.libavjsTransfer;
            postMessage({
                c: "libavjs_ret",
                a: [a[0], a[1], succ, ret]
            }, transfer);
        }

        if (ev.data && ev.data.c === "libavjs_run") {
            a = ev.data.a;
            var succ = true;
            var ret;
            try {
                ret = Module[a[1]].apply(Module, a.slice(2));
            } catch (ex) {
                succ = false;
                ret = ex + "\n" + ex.stack;
            }
            if (succ && ret && ret.then) {
                ret
                    .then(function(ret) { reply(true, ret); })
                    .catch(function(ret) { reply(false, ret + "\n" + ret.stack); });
            } else {
                reply(succ, ret);
            }

        } else if (ev.data && ev.data.c === "libavjs_wait_reader") {
            var name = "" + ev.data.fd;
            var waiters = Module.ff_reader_dev_waiters[name] || [];
            delete Module.ff_reader_dev_waiters[name];
            for (var i = 0; i < waiters.length; i++)
                waiters[i]();

        } else {
            return origOnmessage.apply(this, arguments);
        }
    };

    // And indicate that we're ready
    postMessage({c: "libavjs_ready"});
});

pthread_t libavjs_create_main_thread()
{
    pthread_t ret;
    if (pthread_create(&ret, NULL, libavjs_main_thread, NULL))
        return NULL;
    return ret;
}

#else
void *libavjs_create_main_thread() { return NULL; }

#endif

/****************************************************************
 * Other bindings
 ***************************************************************/

AVFormatContext *avformat_alloc_output_context2_js(AVOutputFormat *oformat,
    const char *format_name, const char *filename)
{
    AVFormatContext *ret = NULL;
    int err = avformat_alloc_output_context2(&ret, oformat, format_name, filename);
    if (err < 0)
        fprintf(stderr, "[avformat_alloc_output_context2_js] %s\n", av_err2str(err));
    return ret;
}

AVFormatContext *avformat_open_input_js(const char *url, AVInputFormat *fmt,
    AVDictionary *options)
{
    AVDictionary** options_p = &options;
    AVFormatContext *ret = avformat_alloc_context();

    if (!ret) {
        fprintf(stderr, "[avformat_open_input_js] Could not allocate AVFormatContext\n");
        return NULL;
    }

    ret->flags |= (AVFMT_FLAG_GENPTS);

    int err = avformat_open_input(&ret, url, fmt, options_p);
    if (err < 0)
        fprintf(stderr, "[avformat_open_input_js] %s\n", av_err2str(err));
    return ret;
}

AVIOContext *avio_open2_js(const char *url, int flags,
    const AVIOInterruptCB *int_cb, AVDictionary *options)
{
    AVIOContext *ret = NULL;
    AVDictionary** options_p = &options;
    int err = avio_open2(&ret, url, flags, int_cb, options_p);
    if (err < 0)
        fprintf(stderr, "[avio_open2_js] %s\n", av_err2str(err));
    return ret;
}

AVFilterContext *avfilter_graph_create_filter_js(const AVFilter *filt,
    const char *name, const char *args, void *opaque, AVFilterGraph *graph_ctx)
{
    AVFilterContext *ret = NULL;
    int err = avfilter_graph_create_filter(&ret, filt, name, args, opaque, graph_ctx);
    if (err < 0)
        fprintf(stderr, "[avfilter_graph_create_filter_js] %s\n", av_err2str(err));
    return ret;
}

AVDictionary *av_dict_copy_js(
    AVDictionary *dst, const AVDictionary *src, int flags
) {
    av_dict_copy(&dst, src, flags);
    return dst;
}

AVDictionary *av_dict_set_js(
    AVDictionary *pm, const char *key, const char *value, int flags
) {
    av_dict_set(&pm, key, value, flags);
    return pm;
}

int av_compare_ts_js(
    unsigned int ts_a_lo, int ts_a_hi,
    int tb_a_num, int tb_a_den,
    unsigned int ts_b_lo, int ts_b_hi,
    int tb_b_num, int tb_b_den
) {
    int64_t ts_a = (int64_t) ts_a_lo + ((int64_t) ts_a_hi << 32);
    int64_t ts_b = (int64_t) ts_b_lo + ((int64_t) ts_b_hi << 32);
    AVRational tb_a = {tb_a_num, tb_b_den},
               tb_b = {tb_b_num, tb_b_den};
    return av_compare_ts(ts_a, tb_a, ts_b, tb_b);
}

/* Errors */
#define ERR_BUF_SZ 256
static char err_buf[ERR_BUF_SZ];

char *ff_error(int err)
{
    av_strerror(err, err_buf, ERR_BUF_SZ - 1);
    return err_buf;
}

int mallinfo_uordblks()
{
    return mallinfo().uordblks;
}
