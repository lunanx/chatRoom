#!/usr/bin/env python3
"""诊断版会话列表集成测试：明确指出连接/哪个请求阶段超时。"""
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
QUERY_CONVERSATION_LIST_REQUEST = 38
QUERY_CONVERSATION_LIST_SUCCESS = 39
QUERY_CONVERSATION_LIST_FAILED = 40


def recv_exact(sock: socket.socket, size: int, step: str) -> bytes:
    received = bytearray()
    while len(received) < size:
        try:
            chunk = sock.recv(size - len(received))
        except socket.timeout as exc:
            raise TimeoutError(
                f"阶段「{step}」等待服务器响应超时；已收到 {len(received)}/{size} 字节。"
                "请同时查看服务端终端日志。"
            ) from exc
        if not chunk:
            raise RuntimeError(
                f"阶段「{step}」服务器提前关闭连接：需要 {size} 字节，实际收到 {len(received)} 字节"
            )
        received.extend(chunk)
    return bytes(received)


def send_request(sock: socket.socket, command: int, request_id: int, body: dict, step: str):
    body_bytes = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    header = struct.pack(HEADER_FORMAT, MAGIC, VERSION, command, len(body_bytes), request_id)
    print(f"[INFO] {step}：发送 command={command}, request_id={request_id}, body={body}", flush=True)
    try:
        sock.sendall(header + body_bytes)
    except socket.timeout as exc:
        raise TimeoutError(f"阶段「{step}」发送请求超时。") from exc

    raw_header = recv_exact(sock, HEADER_SIZE, step + " / 等待响应头")
    magic, version, response_command, body_length, response_id = struct.unpack(HEADER_FORMAT, raw_header)
    if body_length > MAX_BODY_LENGTH:
        raise RuntimeError(f"阶段「{step}」响应 Body 超过协议上限：{body_length} 字节")
    raw_body = recv_exact(sock, body_length, step + " / 等待响应 Body")
    response_body = json.loads(raw_body.decode("utf-8"))

    assert magic == MAGIC, f"阶段「{step}」magic 不匹配：{magic:#x}"
    assert version == VERSION, f"阶段「{step}」协议版本不匹配：{version}"
    assert response_id == request_id, f"阶段「{step}」request_id 不匹配：{response_id} != {request_id}"
    assert isinstance(response_body, dict), f"阶段「{step}」响应 JSON 根节点必须是对象"
    assert response_body.get("code") == response_command, f"阶段「{step}」响应头命令与 JSON code 不一致"
    print(f"[INFO] {step}：收到 response_command={response_command}, body={response_body}", flush=True)
    return response_command, response_body


def main() -> int:
    parser = argparse.ArgumentParser(description="诊断会话列表接口；输出具体卡住的步骤")
    parser.add_argument("--host", required=True, help="服务端可达 IP 地址；不要填写尖括号")
    parser.add_argument("--port", type=int, default=8888)
    parser.add_argument("--username", required=True, help="已有测试账号用户名")
    parser.add_argument("--password", required=True, help="测试账号密码")
    parser.add_argument("--timeout", type=float, default=8.0, help="连接/响应超时秒数，默认 8")
    args = parser.parse_args()

    print(f"[INFO] 正在连接 {args.host}:{args.port}，连接超时 {args.timeout}s", flush=True)
    try:
        sock = socket.create_connection((args.host, args.port), timeout=args.timeout)
    except socket.timeout as exc:
        raise TimeoutError(f"TCP 连接到 {args.host}:{args.port} 超时；先检查 IP、端口和防火墙。") from exc

    with sock:
        sock.settimeout(args.timeout)
        print("[PASS] TCP 已连接", flush=True)

        command, body = send_request(
            sock, QUERY_CONVERSATION_LIST_REQUEST, 7001, {}, "未登录时查询会话列表"
        )
        assert command == QUERY_CONVERSATION_LIST_FAILED, (command, body)
        assert isinstance(body.get("message"), str) and body["message"], body
        print("[PASS] 未认证的会话列表请求被拒绝", flush=True)

        command, body = send_request(
            sock, LOGIN_REQUEST, 7002,
            {"username": args.username, "password": args.password}, "测试账号登录"
        )
        if command != LOGIN_SUCCESS:
            if command == LOGIN_FAILED:
                raise RuntimeError(f"测试账号登录失败（不是超时）：{body.get('message', body)}")
            raise RuntimeError(f"登录返回意外响应：command={command}, body={body}")
        print("[PASS] 测试账号登录成功", flush=True)

        command, body = send_request(
            sock, QUERY_CONVERSATION_LIST_REQUEST, 7003, {}, "登录后查询会话列表"
        )
        assert command == QUERY_CONVERSATION_LIST_SUCCESS, (command, body)
        items = body.get("data")
        assert isinstance(items, list), f"成功响应的 data 必须是数组：{body}"
        for index, item in enumerate(items):
            assert isinstance(item, dict), f"data[{index}] 必须是对象"
            assert isinstance(item.get("conversation_id"), int) and item["conversation_id"] > 0, item
            assert item.get("type") in (1, 2), item
            assert isinstance(item.get("display_name"), str), item
            assert isinstance(item.get("last_message"), str), item
            assert isinstance(item.get("last_time"), str), item
        print(f"[PASS] data 格式正确，共 {len(items)} 条会话", flush=True)

        command, body = send_request(sock, 4, 7004, {}, "测试结束后退出登录")
        if command != LOGOUT_SUCCESS:
            raise RuntimeError(f"退出登录失败：command={command}, body={body}")
        print("[PASS] 测试结束后退出登录成功", flush=True)

    print("All conversation-list integration checks passed.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, RuntimeError, json.JSONDecodeError, struct.error) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr, flush=True)
        raise SystemExit(1)
