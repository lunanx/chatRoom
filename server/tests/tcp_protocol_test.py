#!/usr/bin/env python3
import argparse
import json
import socket
import struct
import sys
import time

MAGIC = 0x12345678
VERSION = 1
HEADER_FMT = "!IHHIQ"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
LOGIN_REQUEST = 1


def build_frame(command: int, request_id: int, body: dict) -> bytes:
    body_bytes = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    header = struct.pack(HEADER_FMT, MAGIC, VERSION, command, len(body_bytes), request_id)
    return header + body_bytes


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise RuntimeError("server closed connection before a complete frame was received")
        data.extend(chunk)
    return bytes(data)


def recv_frame(sock: socket.socket):
    header = recv_exact(sock, HEADER_SIZE)
    magic, version, command, body_length, request_id = struct.unpack(HEADER_FMT, header)
    body = recv_exact(sock, body_length)
    return magic, version, command, request_id, body.decode("utf-8")


def test_single_frame(sock: socket.socket):
    sock.sendall(build_frame(LOGIN_REQUEST, 1001, {"username": "test", "password": "123456"}))
    magic, version, command, request_id, body = recv_frame(sock)

    assert magic == MAGIC, (magic, MAGIC)
    assert version == VERSION, (version, VERSION)
    assert command == LOGIN_REQUEST, (command, LOGIN_REQUEST)
    assert request_id == 1001, (request_id, 1001)
    body_obj = json.loads(body)
    assert body_obj == {"username": "test_user1", "password": "123456"}, body_obj
    print("[PASS] single frame")


def test_coalesced_frames(sock: socket.socket):
    packet_a = build_frame(LOGIN_REQUEST, 2001, {"id": "A"})
    packet_b = build_frame(LOGIN_REQUEST, 2002, {"id": "B"})
    sock.sendall(packet_a + packet_b)

    first = recv_frame(sock)
    second = recv_frame(sock)

    assert first[3] == 2001, first
    assert second[3] == 2002, second
    print("[PASS] two coalesced frames")


def test_fragmented_frame(sock: socket.socket):
    packet = build_frame(LOGIN_REQUEST, 3001, {"id": "fragmented"})
    sock.sendall(packet[:7])
    time.sleep(0.05)
    sock.sendall(packet[7:20])
    time.sleep(0.05)
    sock.sendall(packet[20:])

    response = recv_frame(sock)
    assert response[3] == 3001, response
    print("[PASS] fragmented frame")


def test_invalid_magic(host: str, port: int):
    with socket.create_connection((host, port), timeout=3) as sock:
        packet = bytearray(build_frame(LOGIN_REQUEST, 4001, {"bad": "magic"}))
        packet[0:4] = struct.pack("!I", 0x99887766)
        sock.sendall(packet)
        sock.settimeout(1.0)
        try:
            data = sock.recv(1)
        except socket.timeout:
            data = b""
        assert data == b"", data
    print("[PASS] invalid magic connection closed")


def main() -> int:
    parser = argparse.ArgumentParser(description="Minimal TCP protocol smoke tests")
    parser.add_argument("--host", default="192.168.230.128")
    parser.add_argument("--port", type=int, default=8888)
    args = parser.parse_args()

    try:
        with socket.create_connection((args.host, args.port), timeout=3) as sock:
            sock.settimeout(3)
            test_single_frame(sock)
            test_coalesced_frames(sock)
            test_fragmented_frame(sock)

        test_invalid_magic(args.host, args.port)
    except (AssertionError, OSError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1

    print("All TCP smoke tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
