"""Expose the latest JPEG rendered from the Runtime RTP video and matching boxes."""

from collections import OrderedDict
import socket
import struct
import threading
import time

from lan import decode_metadata


class VideoReceiver:
    def __init__(self, host, port, metadata_port, codec):
        import gi
        import cairo  # Register the cairo context used by cairooverlay.
        gi.require_foreign("cairo")
        gi.require_version("Gst", "1.0")
        from gi.repository import Gst

        self.gst = Gst
        Gst.init(None)
        self.condition = threading.Condition()
        self.metadata = OrderedDict()
        self.timestamps = OrderedDict()
        self.drawn = OrderedDict()
        self.sequence = 0
        self.latest = None
        self.stopped = threading.Event()
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.socket.bind((host, metadata_port))
            self.socket.settimeout(0.25)
            encoding = ("H264", 96, "rtph264depay ! avdec_h264") if codec == "h264" else (
                "JPEG", 26, "rtpjpegdepay ! jpegdec")
            name, payload, decoder = encoding
            self.pipeline = Gst.parse_launch(
                f'udpsrc address="{host}" port={port} '
                f'caps="application/x-rtp,media=video,clock-rate=90000,encoding-name={name},payload={payload}" '
                f'! rtpjitterbuffer latency=60 name=jitter ! {decoder} ! videoconvert '
                '! cairooverlay name=boxes ! videoconvert ! jpegenc '
                '! appsink name=output emit-signals=true sync=false max-buffers=1 drop=true'
            )
            self.pipeline.get_by_name("jitter").get_static_pad("src").add_probe(
                Gst.PadProbeType.BUFFER, self._rtp)
            self.pipeline.get_by_name("boxes").connect("draw", self._draw)
            self.pipeline.get_by_name("output").connect("new-sample", self._sample)
            self.worker = threading.Thread(target=self._metadata, daemon=True)
            self.worker.start()
            if self.pipeline.set_state(Gst.State.PLAYING) == Gst.StateChangeReturn.FAILURE:
                raise RuntimeError("Unable to start RTP video pipeline")
        except BaseException:
            self.stopped.set()
            if hasattr(self, "pipeline"):
                self.pipeline.set_state(Gst.State.NULL)
            self.socket.close()
            raise

    @staticmethod
    def _remember(cache, key, value):
        cache[key] = value
        cache.move_to_end(key)
        if len(cache) > 120:
            cache.popitem(last=False)

    def _metadata(self):
        while not self.stopped.is_set():
            try:
                packet = self.socket.recv(65535)
                timestamp, frame, tracks = decode_metadata(packet)
            except (socket.timeout, ValueError, struct.error):
                continue
            except OSError:
                return
            with self.condition:
                self._remember(self.metadata, timestamp, (frame, tracks, time.monotonic()))

    def _rtp(self, _pad, probe):
        buffer = probe.get_buffer()
        if buffer.pts != self.gst.CLOCK_TIME_NONE and buffer.get_size() >= 12:
            header = buffer.extract_dup(0, 12)
            timestamp, = struct.unpack_from("!I", header, 4)
            with self.condition:
                self._remember(self.timestamps, buffer.pts, timestamp)
        return self.gst.PadProbeReturn.OK

    def _draw(self, _element, context, pts, _duration):
        with self.condition:
            match = self.metadata.get(self.timestamps.get(pts))
            if match is None or time.monotonic() - match[2] > 1:
                return
            self._remember(self.drawn, pts, match[:2])
        context.set_source_rgb(1, 0.68, 0.24)
        context.set_line_width(2)
        context.set_font_size(14)
        for track in match[1]:
            x, y = track["x1"], track["y1"]
            context.rectangle(x, y, track["x2"] - x, track["y2"] - y)
            context.stroke()
            context.move_to(x, max(y - 3, 14))
            context.show_text(str(track["id"]))

    def _sample(self, sink):
        sample = sink.emit("pull-sample")
        buffer = sample.get_buffer()
        jpeg = buffer.extract_dup(0, buffer.get_size())
        with self.condition:
            frame, tracks = self.drawn.pop(buffer.pts, (None, []))
            self.sequence += 1
            self.latest = (self.sequence, jpeg, frame, tracks, time.monotonic())
            self.condition.notify_all()
        return self.gst.FlowReturn.OK

    def wait(self, sequence):
        """Wait up to two seconds for a newer image; slow readers get the latest one."""
        with self.condition:
            ready = self.condition.wait_for(
                lambda: self.sequence > sequence or self.stopped.is_set(), timeout=2)
            return self.latest if ready and not self.stopped.is_set() else None

    def snapshot(self):
        with self.condition:
            return self.latest

    def close(self):
        self.stopped.set()
        with self.condition:
            self.condition.notify_all()
        self.pipeline.set_state(self.gst.State.NULL)
        self.worker.join()
        self.socket.close()
