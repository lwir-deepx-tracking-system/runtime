#!/usr/bin/env python3
"""Serve the model-bank page with Runtime LAN inputs and commands on the PC."""

import argparse
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
from pathlib import Path
import struct
import time
from urllib.parse import parse_qs, urlsplit

from lan import CommandClient
from video import VideoReceiver

RESULTS = ["OK", "잘못된 ID", "변경 중", "생성 실패", "이전 모델로 복구",
           "복구 실패", "준비되지 않음", "잘못된 설정"]


def change_result(code):
    return RESULTS[code] if code < len(RESULTS) else f"Runtime result {code}"


class RuntimeWebApi:
    """Map browser operations to the Runtime adapter without HTTP dependencies."""

    def __init__(self, commands, video, model_ids, tracker_ids):
        self.commands = commands
        self.video = video
        self.model_ids = model_ids
        self.tracker_ids = tracker_ids

    def models(self):
        items = self.commands.catalog(False, self.model_ids)
        pending, last = self.commands.change_state(3)
        return dict(catalog=items, current=next((item["id"] for item in items if item["active"]), None),
                    pending=pending, busy=pending is not None,
                    last=dict(at=last[2], text=change_result(last[1])) if last else None)

    def rules(self):
        items = self.commands.catalog(True, self.tracker_ids)
        tracker, warm_min, warm_max, edge, frames, ms, flags = struct.unpack(
            "!IIIfIII", self.commands.pipeline(7))
        pending, last = self.commands.change_state(4)
        return dict(presets=[dict(key=str(item["id"]), name=item["name"]) for item in items],
                    tracker=str(tracker), tracker_backend="C++",
                    pending=dict(tracker=str(pending)) if pending is not None else None,
                    tracker_last=dict(at=last[2], text=change_result(last[1])) if last else None,
                    warm_min=warm_min,
                    warm_max=warm_max, edge_birth=edge, confirm=dict(ms=ms, frames=frames),
                    gmc=bool(flags & 2), class_gate=bool(flags & 1))

    def change(self, command, ident):
        if not 0 < ident <= 0xffffffff:
            raise ValueError("ID must be a positive uint32")
        phase, result = self.commands.pipeline(command, struct.pack("!I", ident), changing=ident)
        state = self.models() if command == 3 else self.rules()
        return dict(state, ok=result == 0, phase=phase, result=result, text=change_result(result))

    def read(self, path):
        if path == "/model":
            return self.models()
        if path == "/rules":
            return self.rules()
        if path == "/time":
            return dict(t=time.time())
        if path == "/healthz":
            frame = self.video.snapshot()
            return dict(video_live=bool(frame and time.monotonic() - frame[4] < 2))
        if path == "/gimbal/people":
            frame = self.video.snapshot()
            people = frame[3] if frame and time.monotonic() - frame[4] < 1 else []
            return dict(width=640, height=480, people=people, state=self.control_state())
        raise NotImplementedError(path)

    def control_state(self, answer=""):
        mode, target, gimbal, error, ident = self.commands.status or (0, 0, 0, 0, 0)
        return dict(running=False, auto=bool(mode and ident), id=ident,
                    lost=target == 2, moving=bool(gimbal), error=error, answer=answer)

    def write(self, path, query):
        if path == "/model/set" and set(query) == {"id"}:
            return self.change(3, int(query["id"]))
        if path == "/rules/set" and "tracker" in query and (
                set(query) == {"tracker"} or
                (set(query) == {"tracker", "engine"} and query["engine"] == "auto")):
            return self.change(4, int(query["tracker"]))
        if path == "/gimbal/ping":
            self.commands.system(1, 4)
            return self.control_state("이 웹 어댑터에서 짐벌 조작은 지원하지 않습니다.")
        raise NotImplementedError(path)


class WebHandler(BaseHTTPRequestHandler):
    """Serve original assets and translate HTTP requests to the browser API."""

    def reply(self, status, content_type, body):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def json_reply(self, status, payload):
        self.reply(status, "application/json; charset=utf-8",
                   json.dumps(payload, ensure_ascii=False).encode())

    def stream(self, fetch_mode):
        self.send_response(200)
        kind = "mixed" if fetch_mode else "x-mixed-replace"
        self.send_header("Content-Type", f"multipart/{kind}; boundary=frame")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.connection.settimeout(3)
        sequence = 0
        while (frame := self.server.api.video.wait(sequence)) is not None:
            sequence, jpeg, *_ = frame
            self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: "
                             + str(len(jpeg)).encode() + b"\r\n\r\n" + jpeg + b"\r\n")

    def handle_api(self, post):
        if self.headers.get("Host") not in self.server.allowed_hosts:
            self.json_reply(403, dict(ok=False, text="로컬 호스트 주소가 필요합니다"))
            return
        url = urlsplit(self.path)
        if not post and url.path == "/stream":
            self.stream(parse_qs(url.query).get("f") == ["1"])
            return
        try:
            if not post and url.path == "/":
                self.reply(200, "text/html; charset=utf-8", self.server.page)
                return
            if not post and url.path == "/snap.jpg":
                frame = self.server.api.video.snapshot()
                if frame is None or time.monotonic() - frame[4] > 2:
                    self.json_reply(503, dict(ok=False, text="영상 대기 중"))
                    return
                self.reply(200, "image/jpeg", frame[1])
                return
            if post:
                if self.headers.get("X-Gimbal") != "1" or self.headers.get("Origin") not in (
                        None, "http://" + self.headers.get("Host", "")):
                    self.json_reply(403, dict(ok=False, text="브라우저 요청 출처가 일치하지 않습니다"))
                    return
                values = parse_qs(url.query)
                if any(len(value) != 1 for value in values.values()):
                    raise ValueError("Duplicate query parameter")
                payload = self.server.api.write(url.path, {k: v[0] for k, v in values.items()})
            else:
                payload = self.server.api.read(url.path)
        except NotImplementedError:
            self.json_reply(HTTPStatus.NOT_IMPLEMENTED, dict(ok=False, text="아직 연결되지 않은 기능입니다"))
        except (KeyError, ValueError):
            self.json_reply(400, dict(ok=False, text="잘못된 요청입니다"))
        except (OSError, struct.error) as error:
            self.json_reply(503, dict(ok=False, text=f"Runtime 통신 실패: {error}"))
        else:
            self.json_reply(200, payload)

    def do_GET(self):
        try:
            self.handle_api(False)
        except OSError:
            # A browser can close a stream while a newer frame is being written.
            return

    def do_POST(self):
        try:
            self.handle_api(True)
        except OSError:
            return


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--page", type=Path, default=Path(__file__).resolve().parents[3] /
                        "model-bank/tracking_server/page/live_page.html")
    parser.add_argument("--board", default="192.168.50.2")
    parser.add_argument("--command-port", type=int, default=5002)
    parser.add_argument("--udp-bind", type=ipaddress.IPv4Address, default="0.0.0.0")
    parser.add_argument("--video-port", type=int, default=5000)
    parser.add_argument("--metadata-port", type=int, default=5001)
    parser.add_argument("--codec", choices=("h264", "jpeg"), default="h264")
    parser.add_argument("--http-port", type=int, default=8080)
    parser.add_argument("--model-ids", nargs="+", type=int, default=range(1, 14))
    parser.add_argument("--tracker-ids", nargs="+", type=int, default=range(1, 8))
    args = parser.parse_args()
    for name in ("http_port", "command_port", "video_port", "metadata_port"):
        if not 0 < getattr(args, name) <= 65535:
            parser.error(f"{name} must be a positive uint16")
    page = args.page.read_bytes()
    client = CommandClient((args.board, args.command_port))
    with ThreadingHTTPServer(("127.0.0.1", args.http_port), WebHandler) as server:
        server.page = page
        server.allowed_hosts = {f"127.0.0.1:{args.http_port}", f"localhost:{args.http_port}"}
        if args.http_port == 80:
            server.allowed_hosts.update(("127.0.0.1", "localhost"))
        video = VideoReceiver(str(args.udp_bind), args.video_port, args.metadata_port, args.codec)
        server.api = RuntimeWebApi(client, video, args.model_ids, args.tracker_ids)
        try:
            print(f"http://127.0.0.1:{args.http_port}/", flush=True)
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            video.close()
            client.close()


if __name__ == "__main__":
    main()
