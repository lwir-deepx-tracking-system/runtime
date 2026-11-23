"""Runtime UDP metadata and TCP command boundary for the PC web adapter."""

import math
import socket
import struct
import threading
import time

HEADER = struct.Struct("!4sBBH")
METADATA = struct.Struct("!4sQIQI")
TRACK = struct.Struct("!IffffBf")


def decode_metadata(packet):
    """Return the RTP timestamp, frame ID and original-pixel tracks."""
    magic, frame, timestamp, _, count = METADATA.unpack_from(packet)
    if magic != b"LWIR" or not frame or len(packet) < METADATA.size + count * TRACK.size:
        raise ValueError("Invalid tracking datagram")
    tracks = []
    for index in range(count):
        ident, x, y, width, height, label, confidence = TRACK.unpack_from(
            packet, METADATA.size + index * TRACK.size)
        if ident and width > 0 and height > 0 and all(
            math.isfinite(value) for value in (x, y, width, height, confidence)
        ):
            tracks.append(dict(id=ident, x1=x, y1=y, x2=x + width, y2=y + height,
                               label=label, confidence=confidence))
    return timestamp, frame, tracks


class CommandClient:
    """Serialize requests on one persistent connection and dispatch unsolicited replies."""

    def __init__(self, address):
        self.address = address
        self.lock = threading.Lock()
        self.socket = None
        self.counter = 0
        self.status = None
        self.pending = {}
        self.results = {}

    def _read(self, length, deadline):
        data = bytearray()
        while len(data) < length:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Runtime command deadline expired")
            self.socket.settimeout(remaining)
            chunk = self.socket.recv(length - len(data))
            if not chunk:
                raise ConnectionError("Runtime closed the command stream")
            data.extend(chunk)
        return bytes(data)

    def _close(self):
        if self.socket is not None:
            self.socket.close()
            self.socket = None
        self.status = None
        self.pending.clear()

    def close(self):
        with self.lock:
            self._close()

    def _exchange(self, category, command, payload, matches=None):
        if self.socket is None:
            self.socket = socket.create_connection(self.address, timeout=0.5)
        self.socket.settimeout(0.5)
        self.socket.sendall(HEADER.pack(b"LWIR", category, command, len(payload)) + payload)
        if matches is None:
            return None
        deadline = time.monotonic() + 0.6
        while time.monotonic() < deadline:
            magic, cat, code, length = HEADER.unpack(self._read(HEADER.size, deadline))
            if magic != b"LWIR":
                raise ValueError("Invalid command header")
            reply = self._read(length, deadline)
            if cat == 1 and code in (2, 3) and length == 8:
                self.status = struct.unpack("!BBBBI", reply)
            if cat == 4 and length == 6:
                ident, phase, result = struct.unpack("!IBB", reply)
                if ident in self.pending and phase == 2:
                    _, model_or_tracker = self.pending.pop(ident)
                    self.results[code - 128] = (model_or_tracker, result, time.strftime("%H:%M:%S"))
            if matches(cat, code, reply):
                return reply
        raise TimeoutError("Runtime command deadline expired")

    def pipeline(self, command, payload=b"", changing=None):
        with self.lock:
            self.counter = self.counter % 0xffffffff + 1
            request = struct.pack("!I", self.counter)
            try:
                reply = self._exchange(4, command, request + payload,
                                       lambda cat, code, body: cat == 4 and
                                       code == command + 128 and body.startswith(request))
                if changing is not None:
                    _, phase, result = struct.unpack("!IBB", reply)
                    if phase == 1 and result == 0:
                        self.pending[self.counter] = (command, changing)
                    else:
                        self.results[command] = (changing, result, time.strftime("%H:%M:%S"))
                return reply[4:]
            except (OSError, ValueError, struct.error):
                self._close()
                raise

    def system(self, category, command, payload=b"", ack=True):
        with self.lock:
            try:
                reply = self._exchange(category, command, payload,
                                       (lambda cat, code, body: cat == 1 and code == 1 and
                                        len(body) == 3 and body[:2] == bytes((category, command)))
                                       if ack else None)
                return reply[2] if ack else None
            except (OSError, ValueError, struct.error):
                self._close()
                raise

    def change_state(self, command):
        with self.lock:
            pending = next((ident for code, ident in self.pending.values() if code == command), None)
            return pending, self.results.get(command)

    def catalog(self, tracker, ids):
        """Read catalog information for the configured ID candidates, including sparse IDs."""
        count, = struct.unpack("!I", self.pipeline(2 if tracker else 1))
        catalog = []
        for ident in ids:
            if len(catalog) == count:
                break
            reply = self.pipeline(6 if tracker else 5, struct.pack("!I", ident))
            item, flags, name = struct.unpack("!II64s", reply)
            if item:
                catalog.append(dict(id=item, name=name.split(b"\x00", 1)[0].decode("utf-8", "replace"),
                                    available=bool(flags & 1), active=bool(flags & 2)))
        return catalog
