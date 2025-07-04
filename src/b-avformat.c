/*
 * Copyright (C) 2019-2025 Yahweasel and contributors
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
    const uint8_t *displaymatrix = NULL;
    double rot = 0;
    size_t side_data_size = 0;

    if (!st) {
        return AVERROR(EINVAL);
    }

    // check the metadata for rotation
    tag = av_dict_get(st->metadata, "rotate", NULL, 0);
    if (tag && tag->value) {
        rot = fmod(360.0 + atoi(tag->value), 360.0);
        return (int)(rot + 0.5);
    }

    // check side data
    for (int i = 0; i < st->codecpar->nb_coded_side_data; i++) {
        AVPacketSideData *side_data = &st->codecpar->coded_side_data[i];
        if (side_data->type == AV_PKT_DATA_DISPLAYMATRIX) {
            displaymatrix = side_data->data;
            side_data_size = side_data->size;
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

const char *ff_get_input_format_name(AVFormatContext* fmt_ctx) {
    return fmt_ctx->iformat ? fmt_ctx->iformat->name : NULL;
}

const char *ff_get_major_brand(AVFormatContext* fmt_ctx) {
    AVDictionaryEntry *tag = NULL;
    tag = av_dict_get(fmt_ctx->metadata, "major_brand", NULL, 0);

    if (tag) return tag->value;

    return NULL;
}

double avstream_get_frame_rate(AVStream *st) {
    AVRational fps = (st->avg_frame_rate.num != 0) 
        ? st->avg_frame_rate
        : st->r_frame_rate;
    if (fps.den == 0) return 0.0;
    return (double)fps.num / (double)fps.den;
}

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

void cleanup(AVFormatContext *in_fmt, AVFormatContext *out_fmt) {
    if (out_fmt && !(out_fmt->oformat->flags & AVFMT_NOFILE)) 
        avio_closep(&out_fmt->pb);
    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);
}

int ff_extract_audio(const char *in_filename, const char *out_filename) {
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

    while (av_read_frame(in_fmt, &pkt) >= 0) {
        if (pkt.stream_index == audio_stream_index) {
            pkt.stream_index = out_stream->index;
            av_interleaved_write_frame(out_fmt, &pkt);
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
            audio_stream_index = i;
            break;
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

    double end_time = start_time + duration;
    while (av_read_frame(in_fmt, &pkt) >= 0) {
        if (pkt.stream_index == audio_stream_index) {
            double pts_time = pkt.pts * av_q2d(in_stream->time_base);
            if (pts_time >= end_time) {
                av_packet_unref(&pkt);
                break;
            }
            if (pts_time >= start_time) {
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

static const int LIBAVFORMAT_VERSION_INT_V = LIBAVFORMAT_VERSION_INT;
#undef LIBAVFORMAT_VERSION_INT
int LIBAVFORMAT_VERSION_INT() { return LIBAVFORMAT_VERSION_INT_V; }
