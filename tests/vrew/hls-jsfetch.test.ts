import { afterAll, afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import * as fs from "fs";
import * as http from "http";
import * as path from "path";
import { createRequire } from "module";
import type * as LibAVJS from "../../dist/libav.types";

const ROOT = path.resolve(__dirname, "..", "..");
const DIST = path.join(ROOT, "dist");
const require = createRequire(import.meta.url);

type ServedFixture = {
  resources: Map<string, Uint8Array>;
  initPath: string;
  segmentPaths: string[];
};

type SlowTailGate = {
  firstRange: Promise<string | null>;
  observeRange: (range: string | null) => void;
  released: Promise<void>;
  release: () => void;
};

const INITIAL_RANGE_BYTES = 64 * 1024;
const INITIAL_RANGE_HEADER = `bytes=0-${INITIAL_RANGE_BYTES - 1}`;

function createSlowTailGate(): SlowTailGate {
  let observeRange: (range: string | null) => void = () => undefined;
  let release: () => void = () => undefined;
  const firstRange = new Promise<string | null>((resolve) => {
    observeRange = resolve;
  });
  const released = new Promise<void>((resolve) => {
    release = resolve;
  });

  return { firstRange, observeRange, released, release };
}

function playlistPaths(playlist: string) {
  const initMatch = playlist.match(/#EXT-X-MAP:URI="([^"]+)"/);
  const segmentPaths = playlist
    .split("\n")
    .map((line) => line.trim())
    .filter((line) => line.length > 0 && !line.startsWith("#"));

  if (!initMatch || segmentPaths.length === 0) {
    throw new Error("The generated HLS fixture needs an init file and a segment");
  }

  return { initPath: initMatch[1], segmentPaths };
}

async function createServedFixture(
  factory: LibAVJS.LibAVWrapper,
): Promise<ServedFixture> {
  const libav = await factory.LibAV({ base: DIST, noworker: true });
  try {
    const input = fs.readFileSync(path.join(ROOT, "tests/files/bbb_input.mp4"));
    await libav.writeFile("source.mp4", new Uint8Array(input));

    const result = await libav.convert_to_hls("source.mp4", "media.m3u8");
    if (result !== 0) {
      throw new Error(`Could not create HLS fixture: ${result}`);
    }

    const playlistBytes = await libav.readFile("media.m3u8");
    const playlist = new TextDecoder().decode(playlistBytes);
    const { initPath, segmentPaths } = playlistPaths(playlist);
    const resources = new Map<string, Uint8Array>([
      ["/source.mp4", new Uint8Array(input)],
      ["/malformed-partial.mp4", new Uint8Array(input)],
      [
        "/seek.webm",
        new Uint8Array(
          fs.readFileSync(path.join(ROOT, "tests/files/bbb_bitrate.webm")),
        ),
      ],
      [
        "/range-ignored.webm",
        new Uint8Array(
          fs.readFileSync(path.join(ROOT, "tests/files/bbb_bitrate.webm")),
        ),
      ],
      ["/media.m3u8", playlistBytes],
      [`/${initPath}`, await libav.readFile(initPath)],
    ]);

    for (const segmentPath of segmentPaths) {
      resources.set(`/${segmentPath}`, await libav.readFile(segmentPath));
    }

    return { resources, initPath, segmentPaths };
  } finally {
    libav.terminate();
  }
}

function listen(server: http.Server) {
  return new Promise<number>((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      server.off("error", reject);
      const address = server.address();
      if (!address || typeof address === "string") {
        reject(new Error("Could not determine fixture server address"));
        return;
      }
      resolve(address.port);
    });
  });
}

function close(server: http.Server) {
  return new Promise<void>((resolve, reject) => {
    server.close((error) => (error ? reject(error) : resolve()));
    server.closeAllConnections();
  });
}

function serveResource(
  request: http.IncomingMessage,
  response: http.ServerResponse,
  body: Uint8Array,
) {
  const commonHeaders = {
    "Accept-Ranges": "bytes",
    "Access-Control-Allow-Credentials": "true",
    "Access-Control-Allow-Origin": "http://localhost",
  };
  const range = request.headers.range;
  if (!range) {
    response.writeHead(200, {
      ...commonHeaders,
      "Content-Length": body.byteLength,
    });
    response.end(body);
    return;
  }

  const match = /^bytes=(\d+)-(\d*)$/.exec(range);
  if (!match) {
    response.writeHead(416, {
      ...commonHeaders,
      "Content-Range": `bytes */${body.byteLength}`,
    });
    response.end();
    return;
  }

  const start = Number(match[1]);
  const requestedEnd = match[2] ? Number(match[2]) : body.byteLength - 1;
  const end = Math.min(requestedEnd, body.byteLength - 1);
  if (start > end) {
    response.writeHead(416, {
      ...commonHeaders,
      "Content-Range": `bytes */${body.byteLength}`,
    });
    response.end();
    return;
  }

  const partial = body.subarray(start, end + 1);
  response.writeHead(206, {
    ...commonHeaders,
    "Content-Length": partial.byteLength,
    "Content-Range": `bytes ${start}-${end}/${body.byteLength}`,
  });
  response.end(partial);
}

function serveMalformedPartialResponse(
  response: http.ServerResponse,
  body: Uint8Array,
) {
  response.writeHead(206, {
    "Accept-Ranges": "bytes",
    "Content-Length": body.byteLength,
    "Content-Range": `bytes 1-${body.byteLength}/${body.byteLength}`,
  });
  response.end(body);
}

function serveIgnoringNonzeroRange(
  request: http.IncomingMessage,
  response: http.ServerResponse,
  body: Uint8Array,
) {
  const match = /^bytes=(\d+)-(\d*)$/.exec(request.headers.range ?? "");
  if (match && Number(match[1]) > 0) {
    response.writeHead(200, {
      "Accept-Ranges": "bytes",
      "Content-Length": body.byteLength,
    });
    response.end(body);
    return;
  }

  serveResource(request, response, body);
}

function serveWithSlowFullObjectTail(
  request: http.IncomingMessage,
  response: http.ServerResponse,
  body: Uint8Array,
  gate: SlowTailGate,
) {
  const range = request.headers.range ?? null;
  gate.observeRange(range);
  if (range !== null) {
    serveResource(request, response, body);
    return;
  }

  const headLength = Math.min(INITIAL_RANGE_BYTES, body.byteLength);
  response.writeHead(200, {
    "Accept-Ranges": "bytes",
    "Content-Length": body.byteLength,
  });
  response.write(body.subarray(0, headLength));
  void gate.released.then(() => response.end(body.subarray(headLength)));
}

describe("jsfetch HLS playback", () => {
  let factory: LibAVJS.LibAVWrapper;
  let fixture: ServedFixture;
  let server: http.Server;
  let baseUrl: string;
  let libav: LibAVJS.LibAV;
  let originalFetch: typeof fetch;
  let requests: Request[];
  let slowTailGates: Map<string, SlowTailGate>;

  beforeAll(async () => {
    factory = require(path.join(DIST, "libav-vrew.js")) as LibAVJS.LibAVWrapper;
    fixture = await createServedFixture(factory);
    server = http.createServer((request, response) => {
      const pathname = new URL(request.url ?? "/", "http://localhost").pathname;
      const body = fixture.resources.get(pathname);
      if (!body) {
        response.writeHead(404).end();
        return;
      }

      const slowTailGate = slowTailGates?.get(pathname);
      if (slowTailGate) {
        serveWithSlowFullObjectTail(request, response, body, slowTailGate);
        return;
      }

      if (pathname === "/malformed-partial.mp4") {
        serveMalformedPartialResponse(response, body);
        return;
      }

      if (pathname === "/range-ignored.webm") {
        serveIgnoringNonzeroRange(request, response, body);
        return;
      }

      serveResource(request, response, body);
    });
    baseUrl = `http://127.0.0.1:${await listen(server)}`;
  });

  beforeEach(async () => {
    libav = await factory.LibAV({ base: DIST, noworker: true });
    originalFetch = globalThis.fetch;
    requests = [];
    slowTailGates = new Map();
    globalThis.fetch = ((input: RequestInfo | URL, init?: RequestInit) => {
      requests.push(new Request(input, init));
      return originalFetch(input, init);
    }) as typeof fetch;
  });

  afterEach(() => {
    for (const gate of slowTailGates.values()) {
      gate.release();
    }
    globalThis.fetch = originalFetch;
    libav.terminate();
  });

  afterAll(async () => {
    if (server) {
      await close(server);
    }
  });

  it("일반 원격 미디어는 브라우저의 기본 인증 모드로 요청한다", async () => {
    const [formatContext] = await libav.ff_init_demuxer_file(
      `jsfetch:${baseUrl}/source.mp4`,
    );
    await libav.avformat_close_input_js(formatContext);

    expect(
      requests.find((request) => request.url.endsWith("/source.mp4"))
        ?.credentials,
    ).toBe("same-origin");
  });

  it("인증이 필요한 원격 미디어는 인증 정보를 포함한다", async () => {
    const [formatContext] = await libav.ff_init_demuxer_file(
      `jsfetch:credentials:${baseUrl}/source.mp4`,
    );
    await libav.avformat_close_input_js(formatContext);

    expect(
      requests.find((request) => request.url.endsWith("/source.mp4"))
        ?.credentials,
    ).toBe("include");
  });

  it("일반 원격 미디어를 탐색해도 브라우저의 기본 인증 모드를 유지한다", async () => {
    const [formatContext, streams] = await libav.ff_init_demuxer_file(
      `jsfetch:${baseUrl}/seek.webm`,
    );
    const packet = await libav.av_packet_alloc();
    try {
      const stream = streams[0];
      const target =
        (3.5 * 60 * stream.time_base_den) / stream.time_base_num;
      await libav.avformat_seek_file_approx(
        formatContext,
        stream.index,
        target,
        0,
        0,
      );
      await libav.av_read_frame(formatContext, packet);

      const rangeRequest = requests.find(
        (request) =>
          request.url.endsWith("/seek.webm") && request.headers.has("range"),
      );
      expect({
        range: rangeRequest?.headers.get("range"),
        credentials: rangeRequest?.credentials,
      }).toEqual({
        range: expect.stringMatching(/^bytes=\d+-/),
        credentials: "same-origin",
      });
    } finally {
      await libav.av_packet_free_js(packet);
      await libav.avformat_close_input_js(formatContext);
    }
  });

  it("인증이 필요한 원격 미디어를 탐색해도 인증 정보를 포함한다", async () => {
    const [formatContext, streams] = await libav.ff_init_demuxer_file(
      `jsfetch:credentials:${baseUrl}/seek.webm`,
    );
    const packet = await libav.av_packet_alloc();
    try {
      const stream = streams[0];
      const target =
        (3.5 * 60 * stream.time_base_den) / stream.time_base_num;
      await libav.avformat_seek_file_approx(
        formatContext,
        stream.index,
        target,
        0,
        0,
      );
      await libav.av_read_frame(formatContext, packet);

      const rangeRequest = requests.find(
        (request) =>
          request.url.endsWith("/seek.webm") && request.headers.has("range"),
      );
      expect({
        range: rangeRequest?.headers.get("range"),
        credentials: rangeRequest?.credentials,
      }).toEqual({
        range: expect.stringMatching(/^bytes=\d+-/),
        credentials: "include",
      });
    } finally {
      await libav.av_packet_free_js(packet);
      await libav.avformat_close_input_js(formatContext);
    }
  });

  it("인증이 필요한 재생목록의 상대 초기화 파일과 세그먼트에도 인증 정보를 포함한다", async () => {
    const [formatContext, streams] = await libav.ff_init_demuxer_file(
      `jsfetch:credentials:${baseUrl}/media.m3u8`,
    );
    await libav.avformat_close_input_js(formatContext);

    expect(streams.length).toBeGreaterThan(0);
    const childRequests = requests.filter((request) => {
      const pathname = new URL(request.url).pathname;
      return (
        pathname === `/${fixture.initPath}` ||
        fixture.segmentPaths.some(
          (segmentPath) => pathname === `/${segmentPath}`,
        )
      );
    });

    expect(
      childRequests.some(
        (request) => new URL(request.url).pathname === `/${fixture.initPath}`,
      ),
    ).toBe(true);
    expect(
      childRequests.some((request) =>
        fixture.segmentPaths.some(
          (segmentPath) =>
            new URL(request.url).pathname === `/${segmentPath}`,
        ),
      ),
    ).toBe(true);
    expect(
      childRequests.every((request) => request.credentials === "include"),
    ).toBe(true);
  });

  it("직접 MP4는 느린 전체 응답을 기다리지 않고 첫 패킷을 읽는다", async () => {
    const gate = createSlowTailGate();
    slowTailGates.set("/source.mp4", gate);
    const reading = (async () => {
      const [formatContext] = await libav.ff_init_demuxer_file(
        `jsfetch:${baseUrl}/source.mp4`,
      );
      const packet = await libav.av_packet_alloc();
      try {
        expect(
          await libav.av_read_frame(formatContext, packet),
        ).toBeGreaterThanOrEqual(0);
      } finally {
        await libav.av_packet_free_js(packet);
        await libav.avformat_close_input_js(formatContext);
      }
    })();

    try {
      await expect(gate.firstRange).resolves.toBe(INITIAL_RANGE_HEADER);
      await reading;
    } finally {
      gate.release();
      await reading.catch(() => undefined);
    }
  });

  it("재생목록의 초기화 파일과 미디어 조각도 느린 전체 응답 없이 첫 패킷을 제공한다", async () => {
    const segmentPath = `/${fixture.segmentPaths[0]}`;
    const gate = createSlowTailGate();
    slowTailGates.set(segmentPath, gate);
    const reading = (async () => {
      const [formatContext] = await libav.ff_init_demuxer_file(
        `jsfetch:${baseUrl}/media.m3u8`,
      );
      const packet = await libav.av_packet_alloc();
      try {
        expect(
          await libav.av_read_frame(formatContext, packet),
        ).toBeGreaterThanOrEqual(0);
      } finally {
        await libav.av_packet_free_js(packet);
        await libav.avformat_close_input_js(formatContext);
      }
    })();

    try {
      await expect(gate.firstRange).resolves.toBe(INITIAL_RANGE_HEADER);
      await reading;

      const firstRanges = [
        "/media.m3u8",
        `/${fixture.initPath}`,
        segmentPath,
      ].map(
        (pathname) =>
          requests.find(
            (request) => new URL(request.url).pathname === pathname,
          )?.headers.get("range") ?? null,
      );
      expect(firstRanges).toEqual([
        INITIAL_RANGE_HEADER,
        INITIAL_RANGE_HEADER,
        INITIAL_RANGE_HEADER,
      ]);
    } finally {
      gate.release();
      await reading.catch(() => undefined);
    }
  });

  it("직접 MP4를 순서대로 읽으면 소진된 범위만 단계적으로 넓혀 요청한다", async () => {
    const [formatContext] = await libav.ff_init_demuxer_file(
      `jsfetch:${baseUrl}/source.mp4`,
    );
    const packet = await libav.av_packet_alloc();
    try {
      while (
        requests.filter(
          (request) =>
            request.url.endsWith("/source.mp4") &&
            request.headers.has("range"),
        ).length < 5
      ) {
        const result = await libav.av_read_frame(formatContext, packet);
        if (result < 0) break;
        await libav.av_packet_unref(packet);
      }

      const ranges = requests
        .filter((request) => request.url.endsWith("/source.mp4"))
        .map((request) => request.headers.get("range"));
      expect(ranges.slice(0, 5)).toEqual([
        INITIAL_RANGE_HEADER,
        "bytes=65536-196607",
        "bytes=196608-458751",
        "bytes=458752-983039",
        "bytes=983040-2031615",
      ]);
    } finally {
      await libav.av_packet_free_js(packet);
      await libav.avformat_close_input_js(formatContext);
    }
  });

  it("탐색한 위치는 64 KiB 범위부터 다시 읽는다", async () => {
    const [formatContext, streams] = await libav.ff_init_demuxer_file(
      `jsfetch:${baseUrl}/seek.webm`,
    );
    try {
      const requestCountBeforeSeek = requests.length;
      const stream = streams[0];
      const target =
        (3.5 * 60 * stream.time_base_den) / stream.time_base_num;
      expect(
        await libav.avformat_seek_file_approx(
          formatContext,
          stream.index,
          target,
          0,
          0,
        ),
      ).toBeGreaterThanOrEqual(0);

      const range = requests
        .slice(requestCountBeforeSeek)
        .find((request) => request.url.endsWith("/seek.webm"))
        ?.headers.get("range");
      const match = /^bytes=(\d+)-(\d+)$/.exec(range ?? "");
      expect(match && Number(match[2]) - Number(match[1]) + 1).toBe(
        INITIAL_RANGE_BYTES,
      );
    } finally {
      await libav.avformat_close_input_js(formatContext);
    }
  });

  it("서버가 요청 위치를 무시한 전체 응답은 탐색 성공으로 처리하지 않는다", async () => {
    const [formatContext, streams] = await libav.ff_init_demuxer_file(
      `jsfetch:${baseUrl}/range-ignored.webm`,
    );
    try {
      const stream = streams[0];
      const target =
        (3.5 * 60 * stream.time_base_den) / stream.time_base_num;
      expect(
        await libav.avformat_seek_file_approx(
          formatContext,
          stream.index,
          target,
          0,
          0,
        ),
      ).toBeLessThan(0);
    } finally {
      await libav.avformat_close_input_js(formatContext);
    }
  });

  it("요청 위치와 맞지 않는 부분 응답은 미디어로 열지 않는다", async () => {
    await expect(
      libav.ff_init_demuxer_file(`jsfetch:${baseUrl}/malformed-partial.mp4`),
    ).rejects.toThrow();
  });

  it("재생목록을 연 직후의 영상 정보로 디코더를 초기화할 수 있다", async () => {
    const formatContext = await libav.avformat_open_input_js(
      `jsfetch:${baseUrl}/media.m3u8`,
      0,
      0,
    );
    let decoder: [number, number, number, number] | undefined;
    try {
      expect(formatContext).toBeGreaterThan(0);
      expect(
        await libav.AVFormatContext_nb_streams(formatContext),
      ).toBeGreaterThan(0);
      const stream = await libav.AVFormatContext_streams_a(formatContext, 0);
      const codecParameters = await libav.AVStream_codecpar(stream);
      const codec = await libav.AVCodecParameters_codec_id(codecParameters);

      decoder = await libav.ff_init_decoder(codec, codecParameters);
      expect(decoder[1]).toBeGreaterThan(0);
    } finally {
      if (decoder) {
        await libav.ff_free_decoder(decoder[1], decoder[2], decoder[3]);
      }
      if (formatContext > 0) {
        await libav.avformat_close_input_js(formatContext);
      }
    }
  });
});
