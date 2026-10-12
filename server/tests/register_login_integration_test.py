#!/usr/bin/env python3
"""End-to-end registration/login/logout test for ChatRoom's current TCP protocol."""
import argparse
import json
import socket
import struct
import time

MAGIC = 0x12345678
VERSION = 1
HEADER_FORMAT = "!IHHIQ"  # magic:uint32, version:uint16, command:uint16, body_length:uint32, request_id:uint64
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

LOGIN_REQUEST = 1
LOGIN_SUCCESS = 2
LOGIN_FAILED = 3
LOGOUT_REQUEST = 4
LOGOUT_SUCCESS = 5
LOGOUT_FAILED = 6
REGISTER_REQUEST = 7
REGISTER_SUCCESS = 8
REGISTER_FAILED = 9


def recv_exact(sock: socket.socket, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        chunk = sock.recv(size - len(result))
        if not chunk:
            raise RuntimeError(f"服务器提前关闭连接（预期读取 {size} 字节，实际 {len(result)} 字节）")
        result.extend(chunk)
    return bytes(result)


def send_request(sock: socket.socket, command: int, request_id: int, body: dict):
    body_bytes = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    packet_header = struct.pack(HEADER_FORMAT, MAGIC, VERSION, command, len(body_bytes), request_id)
    sock.sendall(packet_header + body_bytes)

    header = recv_exact(sock, HEADER_SIZE)
    magic, version, response_command, body_length, response_id = struct.unpack(HEADER_FORMAT, header)
    if body_length > 1024 * 1024:
        raise RuntimeError(f"响应 Body 太大：{body_length} 字节")
    response_body = json.loads(recv_exact(sock, body_length).decode("utf-8"))

    if magic != MAGIC:
        raise AssertionError(f"响应 magic 不正确：0x{magic:08x}")
    if version != VERSION:
        raise AssertionError(f"响应 version 不正确：{version}")
    if response_id != request_id:
        raise AssertionError(f"request_id 不匹配：预期 {request_id}，实际 {response_id}")
    if response_body.get("code") != response_command:
        raise AssertionError(f"响应头命令和 JSON code 不一致：{response_command} vs {response_body.get('code')}")

    print(f"request={command}, response={response_command}, body={response_body}")
    return response_command, response_body


def main() -> int:
    parser = argparse.ArgumentParser(description="测试聊天室注册、登录、退出登录和数据库链路")
    parser.add_argument("--host", default="127.0.0.1", help="服务端 IP，默认 127.0.0.1")
    parser.add_argument("--port", type=int, default=8888, help="服务端端口，默认 8888")
    parser.add_argument("--username", default=f"it_{int(time.time())}", help="测试用户名，只能包含字母、数字、下划线，3-32 位")
    parser.add_argument("--password", default="TestPass123!", help="测试密码，默认 TestPass123!")
    parser.add_argument("--nickname", default="集成测试", help="测试昵称")
    args = parser.parse_args()

    credentials = {"username": args.username, "password": args.password}
    register_body = {**credentials, "nickname": args.nickname}

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        sock.settimeout(5)

        command, body = send_request(sock, REGISTER_REQUEST, 1001, register_body)
        if command != REGISTER_SUCCESS:
            raise RuntimeError(
                f"注册没有成功（响应命令 {command}）。若提示用户名已存在，请用 --username 指定新的测试用户名。"
            )

        command, body = send_request(sock, LOGIN_REQUEST, 1002, credentials)
        if command != LOGIN_SUCCESS:
            raise RuntimeError(f"注册成功后登录失败（响应命令 {command}）：{body}")

        command, body = send_request(sock, LOGOUT_REQUEST, 1003, credentials)
        if command != LOGOUT_SUCCESS:
            raise RuntimeError(f"登录成功后退出登录失败（响应命令 {command}）：{body}")

    print("PASS：注册、登录、退出登录的 TCP + JSON + 数据库链路全部成功。")
    print(f"测试账号：{args.username}（为避免误删数据，脚本不会删除该账号）")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, AssertionError, json.JSONDecodeError, struct.error) as exc:
        raise SystemExit(f"FAIL：{exc}")