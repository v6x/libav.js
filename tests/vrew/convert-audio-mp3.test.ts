/*
 * ff_convert_audio_to_mp3 (src/b-avformat.c) 및 이 함수가 구동하는 static
 * 헬퍼들(transcode_mp3_select_sample_rate / _select_sample_fmt /
 * _convert_to_fifo / _encode_from_fifo)에 대한 vitest 테스트.
 *
 * tests/tests 아래 스위트와 달리 dist/의 prebuilt `vrew` 빌드를 직접 로드하므로
 * `all` 빌드도, ffmpeg CLI도 필요 없다. tests/files/bbb_input.mp4 안의
 * (비디오 + AAC 오디오) 중 오디오를 그대로 변환기에 흘려보낸다.
 *
 * 실행: npm run test:vrew  (Node 18+ 필요 — .nvmrc 참고)
 */

import { describe, it, expect, beforeAll, afterAll } from "vitest";
import * as fs from "fs";
import * as path from "path";
import { createRequire } from "module";
import type * as LibAVJS from "../../dist/libav.types";

const ROOT = path.resolve(__dirname, "..", "..");
const DIST = path.join(ROOT, "dist");

// The vrew build is CommonJS; load it and its wasm from dist/.
const require = createRequire(import.meta.url);
const LibAVFactory = require(
  path.join(DIST, "libav-vrew.js"),
) as LibAVJS.LibAVWrapper;

async function probeAudio(libav: LibAVJS.LibAV, filename: string) {
  const [fmt_ctx, streams] = await libav.ff_init_demuxer_file(filename);
  try {
    const stream = streams.find(
      (s) => s.codec_type === libav.AVMEDIA_TYPE_AUDIO,
    );
    if (!stream) throw new Error(`No audio stream found in ${filename}`);

    const codecpar = stream.codecpar;
    const codec_id = await libav.AVCodecParameters_codec_id(codecpar);
    return {
      streamCount: streams.length,
      name: await libav.avcodec_get_name(codec_id),
      channels: await libav.AVCodecParameters_ch_layout_nb_channels(codecpar),
      sample_rate: await libav.AVCodecParameters_sample_rate(codecpar),
    };
  } finally {
    await libav.avformat_close_input_js(fmt_ctx);
  }
}

function makeWavU8(sampleRate: number, channels: number, seconds: number) {
  const numSamples = Math.floor(sampleRate * seconds);
  const dataLen = numSamples * channels; // 8-bit → 샘플당 1바이트
  const buf = new ArrayBuffer(44 + dataLen);
  const dv = new DataView(buf);
  let o = 0;
  const wStr = (s: string) => {
    for (let i = 0; i < s.length; i++) dv.setUint8(o++, s.charCodeAt(i));
  };

  const w32 = (v: number) => {
    dv.setUint32(o, v, true);
    o += 4;
  };
  const w16 = (v: number) => {
    dv.setUint16(o, v, true);
    o += 2;
  };
  wStr("RIFF");
  w32(36 + dataLen);
  wStr("WAVE");
  wStr("fmt ");
  w32(16); // fmt 청크 크기
  w16(1); // PCM
  w16(channels);
  w32(sampleRate);
  w32(sampleRate * channels); // byte rate (8-bit)
  w16(channels); // block align
  w16(8); // bits per sample → pcm_u8
  wStr("data");
  w32(dataLen);
  for (let i = 0; i < dataLen; i++) dv.setUint8(o++, 128); // 8-bit 무음 = 128
  return new Uint8Array(buf);
}

describe("ff_convert_audio_to_mp3", () => {
  let libav: LibAVJS.LibAV;

  beforeAll(async () => {
    libav = await LibAVFactory.LibAV({ base: DIST, noworker: true });

    const input = fs.readFileSync(path.join(ROOT, "tests/files/bbb_input.mp4"));
    await libav.writeFile("in.mp4", new Uint8Array(input));
    await libav.writeFile(
      "garbage.mp3",
      new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]),
    );
  });

  afterAll(() => {
    if (libav && typeof libav.terminate === "function") libav.terminate();
  });

  it("기본값으로 스테레오 48kHz MP3를 생성한다", async () => {
    const ret = await libav.ff_convert_audio_to_mp3(
      "in.mp4",
      "basic.mp3",
      0,
      0,
      0,
    );
    expect(ret).toBe(0);

    const p = await probeAudio(libav, "basic.mp3");
    expect(p.name).toBe("mp3");
    expect(p.channels).toBe(2);
    expect(p.sample_rate).toBe(48000);

    const size = (await libav.readFile("basic.mp3")).length;
    expect(size).toBeGreaterThan(0);
    await libav.unlink("basic.mp3");
  });

  it("비디오+오디오 입력에서 오디오 단일 스트림만 출력한다", async () => {
    const ret = await libav.ff_convert_audio_to_mp3(
      "in.mp4",
      "single.mp3",
      0,
      0,
      0,
    );
    expect(ret).toBe(0);

    const p = await probeAudio(libav, "single.mp3");
    expect(p.streamCount).toBe(1);
    expect(p.name).toBe("mp3");
    await libav.unlink("single.mp3");
  });

  describe("out_channels 채널 수 결정", () => {
    it("out_channels가 1인 경우 mono로 출력한다", async () => {
      const ret = await libav.ff_convert_audio_to_mp3(
        "in.mp4",
        "mono.mp3",
        1,
        0,
        0,
      );
      expect(ret).toBe(0);
      const p = await probeAudio(libav, "mono.mp3");
      expect(p.name).toBe("mp3");
      expect(p.channels).toBe(1);
      await libav.unlink("mono.mp3");
    });

    it("out_channels가 2인 경우 (mono 입력을) stereo로 출력한다", async () => {
      await libav.writeFile("mono_in.wav", makeWavU8(48000, 1, 0.1));
      const src = await probeAudio(libav, "mono_in.wav");
      expect(src.channels).toBe(1);

      const ret = await libav.ff_convert_audio_to_mp3(
        "mono_in.wav",
        "st.mp3",
        2,
        0,
        0,
      );
      expect(ret).toBe(0);
      const p = await probeAudio(libav, "st.mp3");
      expect(p.channels).toBe(2);
      await libav.unlink("mono_in.wav");
      await libav.unlink("st.mp3");
    });

    it("out_channels가 인코더 상한을 넘는 경우 clamp 한다", async () => {
      const ret = await libav.ff_convert_audio_to_mp3(
        "in.mp4",
        "x3.mp3",
        3,
        0,
        0,
      );
      expect(ret).toBe(0);
      const p = await probeAudio(libav, "x3.mp3");
      expect(p.channels).toBe(2);
      await libav.unlink("x3.mp3");
    });

    it("out_channels가 0보다 작거나 같은 경우 MAX(원본 채널 수, 2) 로 결정된다", async () => {
      // 0(미지정)과 -1(invalid)이 같은 기본값 경로를 타는지 확인.
      for (const [v, name] of [
        [0, "ch_zero.mp3"],
        [-1, "ch_neg.mp3"],
      ] as const) {
        const ret = await libav.ff_convert_audio_to_mp3(
          "in.mp4",
          name,
          v,
          0,
          0,
        );
        expect(ret).toBe(0);
        const p = await probeAudio(libav, name);
        expect(p.channels).toBe(2);
        await libav.unlink(name);
      }
    });
  });

  describe("bit_rate 비트레이트 결정", () => {
    it("bit_rate 지정값이 그대로 반영된다 (256k 파일 > 64k 파일)", async () => {
      const b64 = await libav.ff_convert_audio_to_mp3(
        "in.mp4",
        "b64.mp3",
        0,
        64000,
        0,
      );
      expect(b64).toBe(0);
      const b256 = await libav.ff_convert_audio_to_mp3(
        "in.mp4",
        "b256.mp3",
        0,
        256000,
        0,
      );
      expect(b256).toBe(0);

      const s64 = (await libav.readFile("b64.mp3")).length;
      const s256 = (await libav.readFile("b256.mp3")).length;
      expect(s64).toBeGreaterThan(0);
      expect(s256).toBeGreaterThan(s64);

      await libav.unlink("b64.mp3");
      await libav.unlink("b256.mp3");
    });

    it("bit_rate 가 0보다 작거나 같은 경우 기본값 128k으로 결정된다", async () => {
      // 0(미지정)과 -1(invalid) 모두 기본값 경로 → 산출물 크기가 정확히 같아야 한다.
      const def = await libav.ff_convert_audio_to_mp3(
        "in.mp4",
        "br_zero.mp3",
        0,
        0,
        0,
      );
      const neg = await libav.ff_convert_audio_to_mp3(
        "in.mp4",
        "br_neg.mp3",
        0,
        -1,
        0,
      );
      expect(def).toBe(0);
      expect(neg).toBe(0);
      const sizeDefault = (await libav.readFile("br_zero.mp3")).length;
      const sizeNeg = (await libav.readFile("br_neg.mp3")).length;
      expect(sizeNeg).toBe(sizeDefault);
      await libav.unlink("br_zero.mp3");
      await libav.unlink("br_neg.mp3");
    });
  });

  describe("sample_rate 샘플레이트 결정", () => {
    it("출력 rate는 원본 rate를 그대로 따른다", async () => {
      const src = await probeAudio(libav, "in.mp4");
      const ret = await libav.ff_convert_audio_to_mp3(
        "in.mp4",
        "sr.mp3",
        0,
        0,
        0,
      );
      expect(ret).toBe(0);
      const p = await probeAudio(libav, "sr.mp3");
      expect(p.sample_rate).toBe(src.sample_rate);
      expect(p.sample_rate).toBe(48000);
      await libav.unlink("sr.mp3");
    });

    it("인코더가 원본 rate를 지원하지 않으면 원본 이하 최고 지원 rate로 내린다 (47000 → 44100)", async () => {
      await libav.writeFile("odd.wav", makeWavU8(47000, 1, 0.1));
      const src = await probeAudio(libav, "odd.wav");
      expect(src.sample_rate).toBe(47000);
      const ret = await libav.ff_convert_audio_to_mp3(
        "odd.wav",
        "odd.mp3",
        0,
        0,
        0,
      );
      expect(ret).toBe(0);
      const p = await probeAudio(libav, "odd.mp3");
      expect(p.sample_rate).toBe(44100);

      await libav.unlink("odd.wav");
      await libav.unlink("odd.mp3");
    });
  });

  describe("에러 경로", () => {
    it("출력 확장자로 포맷을 추론할 수 없으면 음수 에러를 반환한다", async () => {
      const ret = await libav.ff_convert_audio_to_mp3(
        "in.mp4",
        "out.bin",
        0,
        0,
        0,
      );
      expect(ret).toBeLessThan(0);
    });

    it("입력 파일이 없으면 음수 에러를 반환한다", async () => {
      const ret = await libav.ff_convert_audio_to_mp3(
        "does-not-exist.wav",
        "missing.mp3",
        0,
        0,
        0,
      );
      expect(ret).toBeLessThan(0);
    });

    it("손상된 입력은 throw 없이 음수 에러를 반환한다", async () => {
      const ret = await libav.ff_convert_audio_to_mp3(
        "garbage.mp3",
        "corrupt.mp3",
        0,
        0,
        0,
      );
      expect(ret).toBeLessThan(0);
    });
  });
});
