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
      [
        "/seek.webm",
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

describe("jsfetch HLS playback", () => {
  let factory: LibAVJS.LibAVWrapper;
  let fixture: ServedFixture;
  let server: http.Server;
  let baseUrl: string;
  let libav: LibAVJS.LibAV;
  let originalFetch: typeof fetch;
  let requests: Request[];

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

      serveResource(request, response, body);
    });
    baseUrl = `http://127.0.0.1:${await listen(server)}`;
  });

  beforeEach(async () => {
    libav = await factory.LibAV({ base: DIST, noworker: true });
    originalFetch = globalThis.fetch;
    requests = [];
    globalThis.fetch = ((input: RequestInfo | URL, init?: RequestInit) => {
      requests.push(new Request(input, init));
      return originalFetch(input, init);
    }) as typeof fetch;
  });

  afterEach(() => {
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
});
