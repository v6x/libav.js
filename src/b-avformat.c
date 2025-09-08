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

int avstream_get_sample_aspect_ratio_num(AVStream *st) {
    return st->sample_aspect_ratio.num;
}

int avstream_get_sample_aspect_ratio_den(AVStream *st) {
    return st->sample_aspect_ratio.den;
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

static const int LIBAVFORMAT_VERSION_INT_V = LIBAVFORMAT_VERSION_INT;
#undef LIBAVFORMAT_VERSION_INT
int LIBAVFORMAT_VERSION_INT() { return LIBAVFORMAT_VERSION_INT_V; }
