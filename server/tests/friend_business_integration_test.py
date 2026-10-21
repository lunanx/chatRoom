#!/usr/bin/env python3
"""好友业务 TCP 集成测试。需要先启动服务端并确保 MariaDB 可用。

示例：
    python3 server/tests/friend_business_integration_test.py --host 127.0.0.1 --port 8888

脚本会创建三个唯一测试账号，不会删除账号或清理好友申请/好友关系数据。
"""
import argparse
import json
import socket
import struct
import time
import uuid
from typing import Any, Dict, Tuple

MAGIC = 0x12345678
VERSION = 1
HEADER_FORMAT = "!IHHIQ"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
MAX_BODY_LENGTH = 1024 * 1024

LOGIN_REQUEST, LOGIN_SUCCESS, LOGIN_FAILED = 1, 2, 3
REGISTER_REQUEST, REGISTER_SUCCESS, REGISTER_FAILED = 7, 8, 9
FRIEND_REQUEST_SEND = 26
FRIEND_REQUEST_SEND_SUCCESS = 27
FRIEND_REQUEST_SEND_FAILED = 28
FRIEND_REQUEST_LIST_REQUEST = 29
FRIEND_REQUEST_LIST_SUCCESS = 30
FRIEND_REQUEST_LIST_FAILED = 31
FRIEND_REQUEST_HANDLE_REQUEST = 32
FRIEND_REQUEST_HANDLE_SUCCESS = 33
FRIEND_REQUEST_HANDLE_FAILED = 34
FRIEND_LIST_REQUEST = 35
FRIEND_LIST_SUCCESS = 36
FRIEND_LIST_FAILED = 37


class ProtocolClient:
    def __init__(self, host: str, port: int, timeout: float = 5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.request_id = int(time.time() * 1000) % 1_000_000_000

    def close(self) -> None:
        self.sock.close()

    @staticmethod
    def recv_exact(sock: socket.socket, size: int) -> bytes:
        received = bytearray()
        while len(received) < size:
            chunk = sock.recv(size - len(received))
            if not chunk:
                raise RuntimeError(
                    f"服务端提前关闭连接：预期 {size} 字节，实际收到 {len(received)} 字节"
                )
            received.extend(chunk)
        return bytes(received)

    def send(self, command: int, body: Dict[str, Any]) -> Tuple[int, Dict[str, Any]]:
        self.request_id += 1
        request_id = self.request_id
        payload = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.sock.sendall(struct.pack(HEADER_FORMAT, MAGIC, VERSION, command, len(payload), request_id) + payload)
        raw_header = self.recv_exact(self.sock, HEADER_SIZE)
        magic, version, response_command, body_length, response_id = struct.unpack(HEADER_FORMAT, raw_header)
        if body_length > MAX_BODY_LENGTH:
            raise AssertionError(f"响应 Body 超限：{body_length}")
        response = json.loads(self.recv_exact(self.sock, body_length).decode("utf-8"))
        if magic != MAGIC or version != VERSION:
            raise AssertionError(f"协议头不正确：magic={magic:#x}, version={version}")
        if response_id != request_id:
            raise AssertionError(f"request_id 不匹配：{request_id} != {response_id}")
        if not isinstance(response, dict) or response.get("code") != response_command:
            raise AssertionError(f"响应 command/code 不一致：header={response_command}, body={response}")
        return response_command, response


def expect(client: ProtocolClient, command: int, body: Dict[str, Any], expected: int, label: str):
    actual, response = client.send(command, body)
    if actual != expected:
        raise AssertionError(f"{label}：预期响应 {expected}，实际 {actual}，Body={response}")
    print(f"[PASS] {label}: {response.get('message', '')}")
    return response


def register_and_login(client: ProtocolClient, username: str, nickname: str) -> None:
    password = "TestPass123!"
    expect(client, REGISTER_REQUEST,
           {"username": username, "password": password, "nickname": nickname},
           REGISTER_SUCCESS, f"注册测试账号 {username}")
    expect(client, LOGIN_REQUEST, {"username": username, "password": password},
           LOGIN_SUCCESS, f"登录测试账号 {username}")


def main() -> int:
    parser = argparse.ArgumentParser(description="好友申请/处理/好友列表 TCP 集成测试")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8888)
    args = parser.parse_args()

    suffix = uuid.uuid4().hex[:10]
    user_a, user_b, user_c = f"frA{suffix}", f"frB{suffix}", f"frC{suffix}"
    clients = []
    try:
        a = ProtocolClient(args.host, args.port)
        clients.append(a)
        b = ProtocolClient(args.host, args.port)
        clients.append(b)
        c = ProtocolClient(args.host, args.port)
        clients.append(c)
        register_and_login(a, user_a, "好友测试A")
        register_and_login(b, user_b, "好友测试B")
        register_and_login(c, user_c, "好友测试C")

        # A 向 B 发起申请。
        sent = expect(a, FRIEND_REQUEST_SEND, {"target_username": user_b},
                      FRIEND_REQUEST_SEND_SUCCESS, "A 向 B 发送好友申请")
        request_id = sent.get("data", {}).get("friend_request_id")
        if not isinstance(request_id, int) or request_id <= 0:
            raise AssertionError(f"申请成功响应缺少有效 friend_request_id：{sent}")

        # 不允许自加；不允许重复待处理申请。
        expect(a, FRIEND_REQUEST_SEND, {"target_username": user_a},
               FRIEND_REQUEST_SEND_FAILED, "拒绝添加自己")
        expect(a, FRIEND_REQUEST_SEND, {"target_username": user_b},
               FRIEND_REQUEST_SEND_FAILED, "拒绝重复的待处理申请")

        # B 应能查到该申请；A 不能越权替 B 处理。
        pending = expect(b, FRIEND_REQUEST_LIST_REQUEST, {},
                         FRIEND_REQUEST_LIST_SUCCESS, "B 查询收到的申请")
        request_items = pending.get("data")
        if not isinstance(request_items, list) or not any(
            item.get("friend_request_id") == request_id for item in request_items
        ):
            raise AssertionError(f"B 的待处理申请列表没有目标申请：{pending}")
        expect(a, FRIEND_REQUEST_HANDLE_REQUEST,
               {"friend_request_id": request_id, "action": 1},
               FRIEND_REQUEST_HANDLE_FAILED, "拒绝非接收者越权同意")

        expect(b, FRIEND_REQUEST_HANDLE_REQUEST,
               {"friend_request_id": request_id, "action": 1},
               FRIEND_REQUEST_HANDLE_SUCCESS, "B 同意 A 的申请")
        expect(b, FRIEND_REQUEST_HANDLE_REQUEST,
               {"friend_request_id": request_id, "action": 1},
               FRIEND_REQUEST_HANDLE_FAILED, "拒绝重复处理同一申请")

        friends_a = expect(a, FRIEND_LIST_REQUEST, {}, FRIEND_LIST_SUCCESS, "A 查询好友列表")
        friends_b = expect(b, FRIEND_LIST_REQUEST, {}, FRIEND_LIST_SUCCESS, "B 查询好友列表")
        if not any(item.get("username") == user_b for item in friends_a.get("data", [])):
            raise AssertionError(f"同意后 A 的好友列表缺少 B：{friends_a}")
        if not any(item.get("username") == user_a for item in friends_b.get("data", [])):
            raise AssertionError(f"同意后 B 的好友列表缺少 A：{friends_b}")
        expect(a, FRIEND_REQUEST_SEND, {"target_username": user_b},
               FRIEND_REQUEST_SEND_FAILED, "拒绝向已有好友重复发送申请")

        # B 向 C 申请，C 拒绝；拒绝后双方不应建立好友关系。
        sent_bc = expect(b, FRIEND_REQUEST_SEND, {"target_username": user_c},
                         FRIEND_REQUEST_SEND_SUCCESS, "B 向 C 发送好友申请")
        request_bc = sent_bc.get("data", {}).get("friend_request_id")
        if not isinstance(request_bc, int) or request_bc <= 0:
            raise AssertionError(f"B->C 申请成功响应缺少有效 ID：{sent_bc}")
        expect(c, FRIEND_REQUEST_HANDLE_REQUEST,
               {"friend_request_id": request_bc, "action": 2},
               FRIEND_REQUEST_HANDLE_SUCCESS, "C 拒绝 B 的申请")
        friends_b_after_reject = expect(b, FRIEND_LIST_REQUEST, {}, FRIEND_LIST_SUCCESS,
                                        "拒绝后 B 查询好友列表")
        friends_c_after_reject = expect(c, FRIEND_LIST_REQUEST, {}, FRIEND_LIST_SUCCESS,
                                        "拒绝后 C 查询好友列表")
        if any(item.get("username") == user_c for item in friends_b_after_reject.get("data", [])):
            raise AssertionError("C 拒绝申请后，B 的好友列表不应包含 C")
        if any(item.get("username") == user_b for item in friends_c_after_reject.get("data", [])):
            raise AssertionError("C 拒绝申请后，C 的好友列表不应包含 B")

        print("PASS：好友申请、列表、越权校验、同意、拒绝和好友关系查询全链路通过。")
        print("注意：测试创建的三个账号及对应记录会留在数据库中，脚本不会自动删除。")
        return 0
    finally:
        for client in clients:
            try:
                client.close()
            except OSError:
                pass


if __name__ == "__main__":
    raise SystemExit(main())
