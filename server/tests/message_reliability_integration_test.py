#!/usr/bin/env python3
"""Stage 7 integration test for message idempotency, history permission, and session updates.

Requires a running ChatRoom server and a working MariaDB database.
The script creates three unique test accounts and leaves their database records in place.
It sends best-effort LOGOUT requests during cleanup.
"""
import argparse
import json
import select
import socket
import struct
import sys
import time
import uuid
from typing import Any, Dict, List, Tuple

MAGIC = 0x12345678
VERSION = 1
HEADER_FORMAT = "!IHHIQ"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
MAX_BODY_LENGTH = 1024 * 1024

LOGIN_REQUEST, LOGIN_SUCCESS, LOGIN_FAILED = 1, 2, 3
LOGOUT_REQUEST, LOGOUT_SUCCESS, LOGOUT_FAILED = 4, 5, 6
REGISTER_REQUEST, REGISTER_SUCCESS, REGISTER_FAILED = 7, 8, 9
PRIVATE_MESSAGE_SEND, PRIVATE_MESSAGE_RECV = 12, 13
MESSAGE_SEND_FAILED, MESSAGE_SEND_SUCCESS = 16, 17
CHATCREATE_REQUEST, PRIVATECREATE_SUCCESS, PRIVATECREATE_FAILED = 18, 21, 22
MESSAGE_HISTORY_REQUEST, MESSAGE_HISTORY_SUCCESS, MESSAGE_HISTORY_FAILED = 23, 24, 25
FRIEND_REQUEST_SEND, FRIEND_REQUEST_SEND_SUCCESS, FRIEND_REQUEST_SEND_FAILED = 26, 27, 28
FRIEND_REQUEST_HANDLE_REQUEST, FRIEND_REQUEST_HANDLE_SUCCESS, FRIEND_REQUEST_HANDLE_FAILED = 32, 33, 34
QUERY_FRIEND_LIST_REQUEST, QUERY_FRIEND_LIST_SUCCESS, QUERY_FRIEND_LIST_FAILED = 35, 36, 37
QUERY_CONVERSATION_LIST_REQUEST, QUERY_CONVERSATION_LIST_SUCCESS, QUERY_CONVERSATION_LIST_FAILED = 38, 39, 40

PASSWORD = "TestPass123!"


def recv_exact(sock: socket.socket, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        chunk = sock.recv(size - len(result))
        if not chunk:
            raise RuntimeError(
                f"服务器提前关闭连接：需要 {size} 字节，实际收到 {len(result)} 字节"
            )
        result.extend(chunk)
    return bytes(result)


class ProtocolClient:
    """Minimal protocol client; it skips unsolicited request_id=0 push frames while awaiting a reply."""

    def __init__(self, host: str, port: int, timeout: float):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.timeout = timeout
        self.request_id = (int(time.time() * 1000) % 1_000_000_000) + uuid.uuid4().int % 100_000
        self.authenticated = False
        self.push_frames: List[Tuple[int, Dict[str, Any]]] = []

    def close(self) -> None:
        self.sock.close()

    def recv_frame(self) -> Tuple[int, int, Dict[str, Any]]:
        raw_header = recv_exact(self.sock, HEADER_SIZE)
        magic, version, command, body_length, request_id = struct.unpack(HEADER_FORMAT, raw_header)
        if magic != MAGIC:
            raise AssertionError(f"响应 magic 错误：{magic:#x}")
        if version != VERSION:
            raise AssertionError(f"响应 version 错误：{version}")
        if body_length > MAX_BODY_LENGTH:
            raise AssertionError(f"响应 Body 超过协议上限：{body_length} 字节")
        raw_body = recv_exact(self.sock, body_length)
        body = json.loads(raw_body.decode("utf-8"))
        if not isinstance(body, dict):
            raise AssertionError(f"响应 JSON 根节点必须是对象：{body!r}")
        # 普通请求响应（request_id != 0）必须带有 code，且与帧头命令一致。
        # 服务器主动推送（request_id == 0）直接承载消息内容，不要求有 code 字段。
        if request_id != 0 and body.get("code") != command:
            raise AssertionError(
                f"响应头命令与 JSON code 不一致：command={command}, "
                f"request_id={request_id}, body={body}"
            )
        return command, request_id, body

    def send(self, command: int, body: Dict[str, Any], label: str) -> Tuple[int, Dict[str, Any]]:
        self.request_id += 1
        request_id = self.request_id
        encoded_body = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        frame = struct.pack(HEADER_FORMAT, MAGIC, VERSION, command, len(encoded_body), request_id) + encoded_body
        print(f"[INFO] {label}: send command={command}, request_id={request_id}", flush=True)
        self.sock.sendall(frame)

        while True:
            response_command, response_id, response_body = self.recv_frame()
            if response_id == 0:
                # Server-pushed messages are asynchronous and must not be mistaken for a request response.
                self.push_frames.append((response_command, response_body))
                print(f"[INFO] 收到主动推送 command={response_command}", flush=True)
                continue
            if response_id != request_id:
                raise AssertionError(
                    f"{label}: request_id 不匹配，预期 {request_id}，实际 {response_id}"
                )
            print(f"[INFO] {label}: response={response_command}, message={response_body.get('message', '')}", flush=True)
            return response_command, response_body

    def logout_best_effort(self) -> None:
        if not self.authenticated:
            return
        try:
            command, _ = self.send(LOGOUT_REQUEST, {}, "清理测试账号登录状态")
            if command == LOGOUT_SUCCESS:
                self.authenticated = False
        except (OSError, RuntimeError, AssertionError, json.JSONDecodeError, struct.error):
            pass


def expect(client: ProtocolClient, request_command: int, body: Dict[str, Any],
           expected_command: int, label: str) -> Dict[str, Any]:
    actual, response = client.send(request_command, body, label)
    if actual != expected_command:
        raise AssertionError(
            f"{label}: 预期响应 command={expected_command}，实际 {actual}，body={response}"
        )
    print(f"[PASS] {label}", flush=True)
    return response


def require_positive_int(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise AssertionError(f"{label} 必须是正整数，实际为 {value!r}")
    return value


def register_and_login(client: ProtocolClient, username: str, nickname: str) -> None:
    expect(client, REGISTER_REQUEST,
           {"username": username, "password": PASSWORD, "nickname": nickname},
           REGISTER_SUCCESS, f"注册测试账号 {username}")
    expect(client, LOGIN_REQUEST,
           {"username": username, "password": PASSWORD},
           LOGIN_SUCCESS, f"登录测试账号 {username}")
    client.authenticated = True


def friend_id_for_username(client: ProtocolClient, target_username: str) -> int:
    response = expect(client, QUERY_FRIEND_LIST_REQUEST, {}, QUERY_FRIEND_LIST_SUCCESS,
                      f"查询好友列表并定位 {target_username}")
    friends = response.get("data")
    if not isinstance(friends, list):
        raise AssertionError(f"好友列表 data 必须是数组：{response}")
    for friend in friends:
        if isinstance(friend, dict) and friend.get("username") == target_username:
            return require_positive_int(friend.get("user_id"), "friend.user_id")
    raise AssertionError(f"好友列表中找不到 {target_username}：{response}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="阶段七：验证消息幂等、私聊权限、历史分页与会话摘要更新"
    )
    parser.add_argument("--host", default="127.0.0.1", help="服务端 IP")
    parser.add_argument("--port", type=int, default=8888, help="服务端端口")
    parser.add_argument("--timeout", type=float, default=10.0, help="连接和响应超时秒数")
    args = parser.parse_args()

    suffix = uuid.uuid4().hex[:10]
    user_a = f"s7a{suffix}"
    user_b = f"s7b{suffix}"
    user_c = f"s7c{suffix}"
    clients: List[ProtocolClient] = []

    try:
        a = ProtocolClient(args.host, args.port, args.timeout)
        clients.append(a)
        b = ProtocolClient(args.host, args.port, args.timeout)
        clients.append(b)
        c = ProtocolClient(args.host, args.port, args.timeout)
        clients.append(c)

        register_and_login(a, user_a, "阶段七测试A")
        register_and_login(b, user_b, "阶段七测试B")
        register_and_login(c, user_c, "阶段七测试C")

        # 建立 A-B 好友关系，方便通过好友列表取得服务端的真实 user_id。
        sent = expect(a, FRIEND_REQUEST_SEND, {"target_username": user_b},
                      FRIEND_REQUEST_SEND_SUCCESS, "A 向 B 发送好友申请")
        friend_request_id = require_positive_int(
            sent.get("data", {}).get("friend_request_id") if isinstance(sent.get("data"), dict) else None,
            "data.friend_request_id",
        )
        expect(b, FRIEND_REQUEST_HANDLE_REQUEST,
               {"friend_request_id": friend_request_id, "action": 1},
               FRIEND_REQUEST_HANDLE_SUCCESS, "B 同意 A 的好友申请")

        user_b_id = friend_id_for_username(a, user_b)
        user_a_id = friend_id_for_username(b, user_a)

        # 两边都打开同一个私聊，确保双方连接订阅同一个真实 conversation_id。
        conversation_a = expect(
            a, CHATCREATE_REQUEST, {"type": 1, "target_user_id": user_b_id},
            PRIVATECREATE_SUCCESS, "A 创建/打开与 B 的私聊",
        )
        conversation_id = require_positive_int(conversation_a.get("conversation_id"),
                                               "conversation_id")
        conversation_b = expect(
            b, CHATCREATE_REQUEST, {"type": 1, "target_user_id": user_a_id},
            PRIVATECREATE_SUCCESS, "B 打开与 A 的私聊",
        )
        if require_positive_int(conversation_b.get("conversation_id"), "B.conversation_id") != conversation_id:
            raise AssertionError("A 和 B 打开的私聊没有复用同一个 conversation_id")
        print("[PASS] 同一对好友双向打开时复用同一 conversation_id", flush=True)

        text = f"阶段七幂等测试消息-{suffix}"
        client_msg_id = f"stage7-{uuid.uuid4().hex}"
        message_body = {
            "type": 1,
            "conversation_id": conversation_id,
            "client_msg_id": client_msg_id,
            "content": text,
        }
        sent_message = expect(a, PRIVATE_MESSAGE_SEND, message_body,
                              MESSAGE_SEND_SUCCESS, "A 发送第一条消息")
        server_msg_id = require_positive_int(sent_message.get("server_msg_id"), "server_msg_id")
        seq = require_positive_int(sent_message.get("seq"), "seq")
        if sent_message.get("conversation_id") != conversation_id:
            raise AssertionError(f"发送确认的 conversation_id 不匹配：{sent_message}")

        # 同一个 client_msg_id + 相同内容重试，应返回原消息 ID/seq，不再插入第二条消息。
        duplicated = expect(a, PRIVATE_MESSAGE_SEND, message_body,
                            MESSAGE_SEND_SUCCESS, "使用相同 client_msg_id 重试")
        if duplicated.get("server_msg_id") != server_msg_id or duplicated.get("seq") != seq:
            raise AssertionError(
                "幂等重试应返回原 server_msg_id 和 seq："
                f"first=({server_msg_id}, {seq}), retry=({duplicated.get('server_msg_id')}, {duplicated.get('seq')})"
            )
        print("[PASS] 重试没有创建新消息 ID 或新 seq", flush=True)

        # 同一发送者复用相同 ID 但改变正文，服务端必须拒绝。
        conflicting_body = dict(message_body)
        conflicting_body["content"] = text + "-内容被篡改"
        expect(a, PRIVATE_MESSAGE_SEND, conflicting_body,
               MESSAGE_SEND_FAILED, "拒绝不同正文复用同一 client_msg_id")

        # 等待 B 收到首次广播；重试本身不应再次广播。
        b.sock.settimeout(args.timeout)
        push_command, push_request_id, push_body = b.recv_frame()
        if push_command != PRIVATE_MESSAGE_RECV or push_request_id != 0:
            raise AssertionError(
                f"B 应收到 request_id=0 的私聊主动推送：command={push_command}, "
                f"request_id={push_request_id}, body={push_body}"
            )
        if push_body.get("conversation_id") != conversation_id or push_body.get("content") != text:
            raise AssertionError(f"私聊推送内容不匹配：{push_body}")
        if push_body.get("client_msg_id") != client_msg_id:
            raise AssertionError(f"私聊推送 client_msg_id 不匹配：{push_body}")
        print("[PASS] B 收到一条匹配的私聊主动推送", flush=True)

        readable, _, _ = select.select([b.sock], [], [], 0.75)
        if readable:
            extra_command, extra_request_id, extra_body = b.recv_frame()
            raise AssertionError(
                "相同消息重试不应再次广播，但 B 又收到了帧："
                f"command={extra_command}, request_id={extra_request_id}, body={extra_body}"
            )
        print("[PASS] 幂等重试没有向 B 重复广播", flush=True)

        # 无关用户 C 不能读取或写入 A-B 的会话。
        expect(c, MESSAGE_HISTORY_REQUEST,
               {"conversation_id": conversation_id, "seq": 0},
               MESSAGE_HISTORY_FAILED, "拒绝非会话成员读取历史消息")
        outsider_msg = {
            "type": 1,
            "conversation_id": conversation_id,
            "client_msg_id": f"stage7-outsider-{uuid.uuid4().hex}",
            "content": "这条消息不应被保存",
        }
        expect(c, PRIVATE_MESSAGE_SEND, outsider_msg,
               MESSAGE_SEND_FAILED, "拒绝非会话成员发送消息")

        # 历史消息应只出现一次，且分页 cursor 指向最早消息的 seq。
        history = expect(a, MESSAGE_HISTORY_REQUEST,
                         {"conversation_id": conversation_id, "seq": 0},
                         MESSAGE_HISTORY_SUCCESS, "查询私聊最新历史消息")
        contents = history.get("content")
        if not isinstance(contents, list):
            raise AssertionError(f"历史消息 content 必须是数组：{history}")
        matches = [item for item in contents if isinstance(item, dict) and item.get("text") == text]
        if len(matches) != 1:
            raise AssertionError(f"历史消息中应恰好出现一次测试消息，实际 {len(matches)} 次：{history}")
        if matches[0].get("seq") != seq:
            raise AssertionError(f"历史消息 seq 与发送确认不一致：{matches[0]} vs seq={seq}")
        if history.get("next_cursor") != seq:
            raise AssertionError(f"next_cursor 应指向本页最早消息的 seq={seq}：{history}")
        print("[PASS] 历史记录中只保存一条测试消息，seq/cursor 一致", flush=True)

        older = expect(a, MESSAGE_HISTORY_REQUEST,
                       {"conversation_id": conversation_id, "seq": seq},
                       MESSAGE_HISTORY_SUCCESS, "查询更早一页历史消息")
        if older.get("content") != []:
            raise AssertionError(f"当前只创建了一条新消息，更早一页应该为空：{older}")

        sessions = expect(a, QUERY_CONVERSATION_LIST_REQUEST, {},
                          QUERY_CONVERSATION_LIST_SUCCESS, "刷新会话列表验证摘要")
        items = sessions.get("data")
        if not isinstance(items, list):
            raise AssertionError(f"会话列表 data 必须是数组：{sessions}")
        matching_sessions = [item for item in items
                             if isinstance(item, dict) and item.get("conversation_id") == conversation_id]
        if len(matching_sessions) != 1:
            raise AssertionError(f"会话列表中应恰好有一个目标会话：{sessions}")
        if matching_sessions[0].get("last_message") != text:
            raise AssertionError(f"会话列表的 last_message 未刷新为最新消息：{matching_sessions[0]}")
        print("[PASS] 会话摘要更新为最新消息", flush=True)

        print("\nAll Stage 7 message reliability integration checks passed.")
        print("提示：脚本创建的三个测试账号、好友关系、会话和消息会留在数据库中。")
        return 0
    finally:
        for client in reversed(clients):
            client.logout_best_effort()
            try:
                client.close()
            except OSError:
                pass


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, RuntimeError, TimeoutError,
            json.JSONDecodeError, struct.error) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr, flush=True)
        raise SystemExit(1)
