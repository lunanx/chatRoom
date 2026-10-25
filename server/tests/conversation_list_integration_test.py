#!/usr/bin/env python3
"""Integration test for the authenticated conversation-list protocol.

The test first confirms that an unauthenticated client is rejected, then logs in
with an existing account and checks that the list endpoint returns a data array.
"""
import argparse
import json
import socket
import struct
import sys

MAGIC = 0x12345678
VERSION = 1
HEADER_FORMAT = "!IHHIQ"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
MAX_BODY_LENGTH = 1024 * 1024

LOGIN_REQUEST = 1
LOGIN_SUCCESS = 2
LOGIN_FAILED = 3
LOGOUT_REQUEST = 4
LOGOUT_SUCCESS = 5
LOGOUT_FAILED = 6
QUERY_CONVERSATION_LIST_REQUEST = 38
QUERY_CONVERSATION_LIST_SUCCESS = 39
QUERY_CONVERSATION_LIST_FAILED = 40


def recv_exact(sock: socket.socket, size: int) -> bytes:
    received = bytearray()
    while len(received) < size:
        chunk = sock.recv(size - len(received))
        if not chunk:
            raise RuntimeError(
                f"服务器提前关闭连接：需要 {size} 字节，实际收到 {len(received)} 字节"
            )
        received.extend(chunk)
    return bytes(received)


def send_request(sock: socket.socket, command: int, request_id: int, body: dict):
    body_bytes = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    header = struct.pack(HEADER_FORMAT, MAGIC, VERSION, command, len(body_bytes), request_id)
    sock.sendall(header + body_bytes)

    raw_header = recv_exact(sock, HEADER_SIZE)
    magic, version, response_command, body_length, response_id = struct.unpack(HEADER_FORMAT, raw_header)
    if body_length > MAX_BODY_LENGTH:
        raise RuntimeError(f"响应 Body 超过协议上限：{body_length} 字节")
    response_body = json.loads(recv_exact(sock, body_length).decode("utf-8"))

    assert magic == MAGIC, f"magic 不匹配：{magic:#x}"
    assert version == VERSION, f"协议版本不匹配：{version}"
    assert response_id == request_id, f"request_id 不匹配：{response_id} != {request_id}"
    assert isinstance(response_body, dict), "响应 JSON 根节点必须是对象"
    assert response_body.get("code") == response_command, "响应头命令与 JSON code 不一致"
    return response_command, response_body


def main() -> int:
    parser = argparse.ArgumentParser(description="验证会话列表的认证、命令号和 data 格式")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8888)
    parser.add_argument("--username", required=True, help="已存在的测试账号用户名")
    parser.add_argument("--password", required=True, help="该账号密码")
    args = parser.parse_args()

    with socket.create_connection((args.host, args.port), timeout=5) as sock:
        sock.settimeout(5)

        # 未登录时，不应返回会话数据。
        command, body = send_request(sock, QUERY_CONVERSATION_LIST_REQUEST, 7001, {})
        assert command == QUERY_CONVERSATION_LIST_FAILED, (command, body)
        assert isinstance(body.get("message"), str) and body["message"], body
        print("[PASS] 未认证的会话列表请求被拒绝")

        command, body = send_request(
            sock, LOGIN_REQUEST, 7002,
            {"username": args.username, "password": args.password},
        )
        if command != LOGIN_SUCCESS:
            if command == LOGIN_FAILED:
                raise RuntimeError(f"测试账号登录失败：{body.get('message', body)}")
            raise RuntimeError(f"登录返回了意外响应：command={command}, body={body}")
        print("[PASS] 测试账号登录成功")

        command, body = send_request(sock, QUERY_CONVERSATION_LIST_REQUEST, 7003, {})
        assert command == QUERY_CONVERSATION_LIST_SUCCESS, (command, body)
        items = body.get("data")
        assert isinstance(items, list), f"成功响应的 data 必须为数组：{body}"
        for index, item in enumerate(items):
            assert isinstance(item, dict), f"data[{index}] 必须是对象"
            assert isinstance(item.get("conversation_id"), int) and item["conversation_id"] > 0, item
            assert item.get("type") in (1, 2), item
            assert isinstance(item.get("display_name"), str), item
            assert isinstance(item.get("last_message"), str), item
            assert isinstance(item.get("last_time"), str), item
        print(f"[PASS] 会话列表 data 格式正确，共 {len(items)} 条")

        command, body = send_request(sock, LOGOUT_REQUEST, 7004, {})
        assert command == LOGOUT_SUCCESS, (command, body)
        print("[PASS] 测试结束后退出登录成功")

    print("All conversation-list integration checks passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, RuntimeError, json.JSONDecodeError, struct.error) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        raise SystemExit(1)
