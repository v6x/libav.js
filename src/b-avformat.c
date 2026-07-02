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

#include "libswresample/swresample.h"
#include "libavutil/audio_fifo.h"

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
            if (pkt.pts != AV_NOPTS_VALUE) {
                processed_pts = pkt.pts;
            }

            av_packet_rescale_ts(&pkt, in_stream->time_base, out_stream->time_base);
            pkt.stream_index = out_stream->index;
            av_interleaved_write_frame(out_fmt, &pkt);
            
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
                pkt.pts = pts_time;
                pkt.dts = pts_time;

                av_packet_rescale_ts(&pkt, in_stream->time_base, out_stream->time_base);

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

static int transcode_mp3_select_sample_rate(const AVCodec *codec, int src_rate) {
    const int *rates = NULL;
    int n = 0;
    if (avcodec_get_supported_config(NULL, codec, AV_CODEC_CONFIG_SAMPLE_RATE,
                                     0, (const void **)&rates, &n) < 0 ||
        !rates || n <= 0) {
        return src_rate;
    }

    int below = 0, lowest = rates[0];
    for (int i = 0; i < n; i++) {
        if (rates[i] == src_rate) return src_rate;
        if (rates[i] < src_rate && rates[i] > below) below = rates[i];
        if (rates[i] < lowest) lowest = rates[i];
    }
    return below > 0 ? below : lowest;
}

static enum AVSampleFormat transcode_mp3_select_sample_fmt(const AVCodec *codec) {
    const enum AVSampleFormat *fmts = NULL;
    int n = 0;
    if (avcodec_get_supported_config(NULL, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT,
                                     0, (const void **)&fmts, &n) < 0 ||
        !fmts || n <= 0) {
        return AV_SAMPLE_FMT_S16P;
    }
    for (int i = 0; i < n; i++) {
        if (fmts[i] == AV_SAMPLE_FMT_S16P) return AV_SAMPLE_FMT_S16P;
    }
    return fmts[0];
}

static int transcode_mp3_select_channels(const AVCodec *codec, int requested,
                                         int src_channels) {
    int desired = requested > 0 ? requested : FFMIN(src_channels, 2);
    desired = FFMAX(desired, 1);

    const AVChannelLayout *layouts = NULL;
    int n = 0;
    if (avcodec_get_supported_config(NULL, codec, AV_CODEC_CONFIG_CHANNEL_LAYOUT,
                                     0, (const void **)&layouts, &n) < 0 ||
        !layouts || n <= 0) {
        return FFMIN(desired, 2);
    }
    int max_ch = 0;
    for (int i = 0; i < n; i++) {
        max_ch = FFMAX(max_ch, layouts[i].nb_channels);
    }
    return max_ch > 0 ? FFMIN(desired, max_ch) : desired;
}

static int transcode_mp3_convert_to_fifo(SwrContext *swr, AVAudioFifo *fifo,
                                         int nb_channels, enum AVSampleFormat sample_fmt,
                                         const AVFrame *frame) {
    int cap = swr_get_out_samples(swr, frame ? frame->nb_samples : 0);
    if (cap <= 0) return 0;

    uint8_t **buf = NULL;
    int linesize;
    int ret = av_samples_alloc_array_and_samples(&buf, &linesize, nb_channels,
                                                 cap, sample_fmt, 0);
    if (ret < 0) return ret;

    int converted = swr_convert(swr, buf, cap,
                                frame ? (const uint8_t **)frame->extended_data : NULL,
                                frame ? frame->nb_samples : 0);
    ret = converted;
    if (converted > 0 && av_audio_fifo_write(fifo, (void **)buf, converted) < converted) {
        ret = AVERROR_UNKNOWN;
    }

    av_freep(&buf[0]);
    av_freep(&buf);
    return ret < 0 ? ret : 0;
}

static int transcode_mp3_encode_from_fifo(AVFormatContext *out_fmt, AVStream *out_stream,
                                          AVCodecContext *enc_ctx, AVAudioFifo *fifo,
                                          int nb_samples, int64_t *next_pts, AVPacket *pkt) {
    AVFrame *frame = av_frame_alloc();
    int ret;
    if (!frame) return AVERROR(ENOMEM);

    frame->nb_samples = nb_samples;
    frame->format = enc_ctx->sample_fmt;
    frame->sample_rate = enc_ctx->sample_rate;
    if ((ret = av_channel_layout_copy(&frame->ch_layout, &enc_ctx->ch_layout)) < 0) goto end;
    if ((ret = av_frame_get_buffer(frame, 0)) < 0) goto end;

    if (av_audio_fifo_read(fifo, (void **)frame->data, nb_samples) < nb_samples) {
        ret = AVERROR_UNKNOWN;
        goto end;
    }
    frame->pts = *next_pts;
    *next_pts += nb_samples;

    if ((ret = avcodec_send_frame(enc_ctx, frame)) < 0) goto end;
    while ((ret = avcodec_receive_packet(enc_ctx, pkt)) >= 0) {
        av_packet_rescale_ts(pkt, enc_ctx->time_base, out_stream->time_base);
        pkt->stream_index = out_stream->index;
        ret = av_interleaved_write_frame(out_fmt, pkt);
        if (ret < 0) goto end;
    }
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) ret = 0;

end:
    av_frame_free(&frame);
    return ret;
}

int ff_convert_audio_to_mp3(const char *in_filename, const char *out_filename,
                            int out_channels, int bit_rate,
                            void (*progress_cb)(int current, int total)) {
    AVFormatContext *in_fmt = NULL, *out_fmt = NULL;
    AVCodecContext *dec_ctx = NULL, *enc_ctx = NULL;
    SwrContext *swr = NULL;
    AVAudioFifo *fifo = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    int audio_stream_index = -1;
    int64_t next_pts = 0;
    int ret = 0;

    if ((ret = avformat_open_input(&in_fmt, in_filename, NULL, NULL)) < 0) goto fail;
    if ((ret = avformat_find_stream_info(in_fmt, NULL)) < 0) goto fail;

    for (unsigned i = 0; i < in_fmt->nb_streams; i++) {
        if (in_fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
            audio_stream_index < 0) {
            audio_stream_index = i;
        } else {
            in_fmt->streams[i]->discard = AVDISCARD_ALL;
        }
    }
    if (audio_stream_index < 0) {
        ret = AVERROR_STREAM_NOT_FOUND;
        goto fail;
    }

    AVStream *in_stream = in_fmt->streams[audio_stream_index];

    const AVCodec *decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
    if (!decoder) {
        ret = AVERROR_DECODER_NOT_FOUND;
        goto fail;
    }
    dec_ctx = avcodec_alloc_context3(decoder);
    if (!dec_ctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    if ((ret = avcodec_parameters_to_context(dec_ctx, in_stream->codecpar)) < 0) goto fail;
    dec_ctx->pkt_timebase = in_stream->time_base;
    if ((ret = avcodec_open2(dec_ctx, decoder, NULL)) < 0) goto fail;

    const AVCodec *encoder = avcodec_find_encoder_by_name("libmp3lame");
    if (!encoder) encoder = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!encoder) {
        ret = AVERROR_ENCODER_NOT_FOUND;
        goto fail;
    }
    enc_ctx = avcodec_alloc_context3(encoder);
    if (!enc_ctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    int nb_channels = transcode_mp3_select_channels(
        encoder, out_channels, dec_ctx->ch_layout.nb_channels);
    av_channel_layout_default(&enc_ctx->ch_layout, nb_channels);
    enc_ctx->sample_rate = transcode_mp3_select_sample_rate(encoder, dec_ctx->sample_rate);
    enc_ctx->sample_fmt = transcode_mp3_select_sample_fmt(encoder);
    enc_ctx->bit_rate = bit_rate > 0 ? bit_rate : 128000;
    enc_ctx->time_base = (AVRational){1, enc_ctx->sample_rate};

    if ((ret = avformat_alloc_output_context2(&out_fmt, NULL, NULL, out_filename)) < 0) goto fail;
    if (out_fmt->oformat->flags & AVFMT_GLOBALHEADER)
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if ((ret = avcodec_open2(enc_ctx, encoder, NULL)) < 0) goto fail;

    AVStream *out_stream = avformat_new_stream(out_fmt, NULL);
    if (!out_stream) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    if ((ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx)) < 0) goto fail;
    out_stream->time_base = enc_ctx->time_base;

    if ((ret = swr_alloc_set_opts2(&swr,
            &enc_ctx->ch_layout, enc_ctx->sample_fmt, enc_ctx->sample_rate,
            &dec_ctx->ch_layout, dec_ctx->sample_fmt, dec_ctx->sample_rate,
            0, NULL)) < 0) goto fail;
    if ((ret = swr_init(swr)) < 0) goto fail;

    int frame_size = enc_ctx->frame_size > 0 ? enc_ctx->frame_size : 1152;
    fifo = av_audio_fifo_alloc(enc_ctx->sample_fmt, nb_channels, frame_size);
    if (!fifo) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        if ((ret = avio_open(&out_fmt->pb, out_filename, AVIO_FLAG_WRITE)) < 0) goto fail;
    }
    if ((ret = avformat_write_header(out_fmt, NULL)) < 0) goto fail;

    int64_t total_duration = 0;
    if (in_stream->duration != AV_NOPTS_VALUE && in_stream->duration > 0) {
        total_duration = in_stream->duration;
    } else if (in_fmt->duration != AV_NOPTS_VALUE && in_fmt->duration > 0) {
        total_duration = av_rescale_q(in_fmt->duration, AV_TIME_BASE_Q, in_stream->time_base);
    }

    int64_t processed_pts = 0;
    int packet_count = 0;
    const int progress_update_interval = 100;

    while ((ret = av_read_frame(in_fmt, pkt)) >= 0) {
        if (pkt->stream_index != audio_stream_index) {
            av_packet_unref(pkt);
            continue;
        }

        if (pkt->pts != AV_NOPTS_VALUE) {
            processed_pts = pkt->pts;
        }

        ret = avcodec_send_packet(dec_ctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) goto fail;

        while ((ret = avcodec_receive_frame(dec_ctx, frame)) >= 0) {
            ret = transcode_mp3_convert_to_fifo(swr, fifo, nb_channels,
                                                enc_ctx->sample_fmt, frame);
            av_frame_unref(frame);
            if (ret < 0) goto fail;
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) goto fail;

        while (av_audio_fifo_size(fifo) >= frame_size) {
            if ((ret = transcode_mp3_encode_from_fifo(out_fmt, out_stream, enc_ctx, fifo,
                                                      frame_size, &next_pts, pkt)) < 0)
                goto fail;
        }

        packet_count++;
        if (progress_cb && packet_count % progress_update_interval == 0) {
            progress_cb(processed_pts, total_duration);
        }
    }
    if (ret != AVERROR_EOF) goto fail;

    /* Flush the decoder. */
    if ((ret = avcodec_send_packet(dec_ctx, NULL)) < 0) goto fail;
    while ((ret = avcodec_receive_frame(dec_ctx, frame)) >= 0) {
        ret = transcode_mp3_convert_to_fifo(swr, fifo, nb_channels,
                                            enc_ctx->sample_fmt, frame);
        av_frame_unref(frame);
        if (ret < 0) goto fail;
    }
    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) goto fail;

    /* Flush the resampler. */
    if ((ret = transcode_mp3_convert_to_fifo(swr, fifo, nb_channels,
                                             enc_ctx->sample_fmt, NULL)) < 0) goto fail;

    /* Drain the FIFO; libmp3lame supports a short final frame
     * (AV_CODEC_CAP_SMALL_LAST_FRAME). */
    while (av_audio_fifo_size(fifo) > 0) {
        int nb = FFMIN(av_audio_fifo_size(fifo), frame_size);
        if ((ret = transcode_mp3_encode_from_fifo(out_fmt, out_stream, enc_ctx, fifo,
                                                  nb, &next_pts, pkt)) < 0)
            goto fail;
    }

    /* Flush the encoder. */
    if ((ret = avcodec_send_frame(enc_ctx, NULL)) < 0) goto fail;
    while ((ret = avcodec_receive_packet(enc_ctx, pkt)) >= 0) {
        av_packet_rescale_ts(pkt, enc_ctx->time_base, out_stream->time_base);
        pkt->stream_index = out_stream->index;
        if ((ret = av_interleaved_write_frame(out_fmt, pkt)) < 0) goto fail;
    }
    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) goto fail;

    if ((ret = av_write_trailer(out_fmt)) < 0) goto fail;

    if (progress_cb && total_duration > 0) {
        progress_cb(total_duration, total_duration);
    }

    ret = 0;
    goto end;

fail:
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "ff_convert_audio_to_mp3: errorno=%d (%s)\n", ret, errbuf);
    }
end:
    av_frame_free(&frame);
    av_packet_free(&pkt);
    av_audio_fifo_free(fifo);
    swr_free(&swr);
    avcodec_free_context(&dec_ctx);
    avcodec_free_context(&enc_ctx);
    cleanup(in_fmt, out_fmt);
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

    const char *last_slash = strrchr(playlist_path, '/');
    char *seg_tmpl = NULL;
    if (last_slash) {
        size_t dir_len = (size_t)(last_slash - playlist_path);
        const char *seg_name = "seg_%05d.m4s";

        seg_tmpl = (char *) malloc(dir_len + 1 + strlen(seg_name) + 1);
        if (!seg_tmpl) { ret = AVERROR(ENOMEM); goto end; }

        memcpy(seg_tmpl, playlist_path, dir_len);
        seg_tmpl[dir_len] = '/';
        strcpy(seg_tmpl + dir_len + 1, seg_name);

    } else {
        seg_tmpl = strdup("seg_%05d.m4s");
        if (!seg_tmpl) { ret = AVERROR(ENOMEM); goto end; }
    }

    av_dict_set(&mux_opts, "hls_segment_filename", seg_tmpl, 0);
    free(seg_tmpl);

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
