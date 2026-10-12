#!/usr/bin/env python3
"""TCP protocol smoke test for the current server dispatcher.

This test uses an invalid (too short) password so it exercises frame decoding,
request dispatch, response encoding, request_id preservation and connection handling
without requiring a live database account.
"""
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
LOGIN_FAILED = 3


def build_frame(command: int, request_id: int, body: dict) -> bytes:
    body_bytes = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    header = struct.pack(HEADER_FMT, MAGIC, VERSION, command, len(body_bytes), request_id)
    return header + body_bytes


def invalid_login_body() -> dict:
    # Password is intentionally too short, so the server rejects it before DB access.
    return {"username": "test_user", "password": "short"}


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
    if body_length > 1024 * 1024:
        raise RuntimeError(f"response body exceeds protocol maximum: {body_length}")
    body = recv_exact(sock, body_length)
    return magic, version, command, request_id, json.loads(body.decode("utf-8"))


def assert_login_failed(response, expected_request_id: int):
    magic, version, command, request_id, body = response
    assert magic == MAGIC, (magic, MAGIC)
    assert version == VERSION, (version, VERSION)
    assert command == LOGIN_FAILED, (command, LOGIN_FAILED)
    assert request_id == expected_request_id, (request_id, expected_request_id)
    assert body["code"] == LOGIN_FAILED, body
    assert isinstance(body.get("message"), str) and body["message"], body


def test_single_frame(sock: socket.socket):
    sock.sendall(build_frame(LOGIN_REQUEST, 1001, invalid_login_body()))
    assert_login_failed(recv_frame(sock), 1001)
    print("[PASS] single request/response frame")


def test_coalesced_frames(sock: socket.socket):
    sock.sendall(
        build_frame(LOGIN_REQUEST, 2001, invalid_login_body())
        + build_frame(LOGIN_REQUEST, 2002, invalid_login_body())
    )
    assert_login_failed(recv_frame(sock), 2001)
    assert_login_failed(recv_frame(sock), 2002)
    print("[PASS] two coalesced request frames")


def test_fragmented_frame(sock: socket.socket):
    packet = build_frame(LOGIN_REQUEST, 3001, invalid_login_body())
    sock.sendall(packet[:7])
    time.sleep(0.05)
    sock.sendall(packet[7:HEADER_SIZE])
    time.sleep(0.05)
    sock.sendall(packet[HEADER_SIZE:])
    assert_login_failed(recv_frame(sock), 3001)
    print("[PASS] fragmented request frame")


def test_invalid_magic(host: str, port: int):
    with socket.create_connection((host, port), timeout=3) as sock:
        packet = bytearray(build_frame(LOGIN_REQUEST, 4001, invalid_login_body()))
        packet[0:4] = struct.pack("!I", 0x99887766)
        sock.sendall(packet)
        sock.settimeout(1.0)
        try:
            data = sock.recv(1)
        except ConnectionResetError:
            data = b""
        except socket.timeout as exc:
            raise AssertionError("server did not close the invalid-protocol connection") from exc
        assert data == b"", data
    print("[PASS] invalid magic connection closed")


def main() -> int:
    parser = argparse.ArgumentParser(description="TCP protocol smoke tests for the current server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8888)
    args = parser.parse_args()

    try:
        with socket.create_connection((args.host, args.port), timeout=3) as sock:
            sock.settimeout(3)
            test_single_frame(sock)
            test_coalesced_frames(sock)
            test_fragmented_frame(sock)

        test_invalid_magic(args.host, args.port)
    except (AssertionError, OSError, RuntimeError, json.JSONDecodeError, struct.error) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1

    print("All TCP smoke tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
