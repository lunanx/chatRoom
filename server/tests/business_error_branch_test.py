#!/usr/bin/env python3
"""验证 ChatRoom 注册/登录/退出登录的常见业务异常分支。

正常模式（需要可用的 MariaDB）：
    python3 server/tests/business_error_branch_test.py

可选地验证数据库 online 字段：
    python3 server/tests/business_error_branch_test.py --verify-db-state

数据库连接失败模式（服务端需使用无效数据库配置启动，见 README）：
    python3 server/tests/business_error_branch_test.py --database-unavailable-only

此脚本不会停止服务、删除账号或修改数据库配置。正常模式会创建一个唯一的测试用户。
"""
import argparse
import json
import os
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

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
REGISTER_REQUEST = 7
REGISTER_SUCCESS = 8
REGISTER_FAILED = 9

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DB_CONFIG = PROJECT_ROOT / "config" / "database.conf"


class ProtocolClient:
    def __init__(self, host: str, port: int, timeout: float):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self._request_id = int(time.time()) % 1_000_000 + 10000

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.sock.close()

    def _next_request_id(self) -> int:
        self._request_id += 1
        return self._request_id

    @staticmethod
    def _recv_exact(sock: socket.socket, size: int) -> bytes:
        data = bytearray()
        while len(data) < size:
            chunk = sock.recv(size - len(data))
            if not chunk:
                raise RuntimeError(
                    f"服务端提前关闭连接：预期读取 {size} 字节，实际读取 {len(data)} 字节"
                )
            data.extend(chunk)
        return bytes(data)

    def send_json(self, command: int, body: Dict[str, Any]) -> Tuple[int, Dict[str, Any], int]:
        body_bytes = json.dumps(
            body, ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")
        return self._send_raw(command, body_bytes)

    def send_raw(self, command: int, body: bytes) -> Tuple[int, Dict[str, Any], int]:
        return self._send_raw(command, body)

    def _send_raw(self, command: int, body: bytes) -> Tuple[int, Dict[str, Any], int]:
        request_id = self._next_request_id()
        header = struct.pack(HEADER_FORMAT, MAGIC, VERSION, command, len(body), request_id)
        self.sock.sendall(header + body)

        response_header = self._recv_exact(self.sock, HEADER_SIZE)
        magic, version, response_command, body_length, response_id = struct.unpack(
            HEADER_FORMAT, response_header
        )

        if body_length > MAX_BODY_LENGTH:
            raise AssertionError(f"服务端响应 Body 超出上限：{body_length} 字节")
        response_bytes = self._recv_exact(self.sock, body_length)
        try:
            response_body = json.loads(response_bytes.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise AssertionError(f"服务端返回的 Body 不是合法 UTF-8 JSON：{response_bytes!r}") from exc

        if magic != MAGIC:
            raise AssertionError(f"响应 magic 错误：0x{magic:08x}")
        if version != VERSION:
            raise AssertionError(f"响应 version 错误：{version}")
        if response_id != request_id:
            raise AssertionError(
                f"request_id 不匹配：请求 {request_id}，响应 {response_id}"
            )
        if not isinstance(response_body, dict):
            raise AssertionError(f"响应 JSON 顶层必须是对象：{response_body!r}")
        if response_body.get("code") != response_command:
            raise AssertionError(
                f"响应头 command 与 JSON code 不一致：{response_command} vs {response_body.get('code')}"
            )

        return response_command, response_body, request_id


def expect_response(
    client: ProtocolClient,
    command: int,
    body: Dict[str, Any],
    expected_command: int,
    label: str,
    expected_message_part: Optional[str] = None,
) -> Dict[str, Any]:
    actual_command, response, request_id = client.send_json(command, body)
    message = response.get("message", "")
    if actual_command != expected_command:
        raise AssertionError(
            f"{label}：预期响应命令 {expected_command}，实际 {actual_command}；响应 Body={response}"
        )
    if expected_message_part and expected_message_part not in str(message):
        raise AssertionError(
            f"{label}：预期 message 包含 {expected_message_part!r}，实际 Body={response}"
        )
    print(f"[PASS] {label} (request_id={request_id}, message={message!r})")
    return response


def parse_simple_config(path: Path) -> Dict[str, str]:
    if not path.is_file():
        raise RuntimeError(f"找不到数据库配置文件：{path}")
    config: Dict[str, str] = {}
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise RuntimeError(f"数据库配置第 {line_number} 行格式不正确")
        key, value = line.split("=", 1)
        config[key.strip()] = value.strip()
    required = ("DB_HOST", "DB_PORT", "DB_NAME", "DB_USER", "DB_PASSWORD")
    missing = [key for key in required if not config.get(key)]
    if missing:
        raise RuntimeError(f"数据库配置缺少必要字段：{', '.join(missing)}")
    return config


def mysql_option_value(value: str) -> str:
    """将值转换为 MySQL option file 的双引号形式。"""
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    escaped = escaped.replace("\n", "\\n").replace("\r", "\\r")
    return f'"{escaped}"'


def query_online_state(username: str, config_path: Path) -> int:
    """使用临时权限为 0600 的 mysql option 文件查询 online，不把密码放进命令行参数。"""
    config = parse_simple_config(config_path)
    client_bin = shutil.which("mariadb") or shutil.which("mysql")
    if not client_bin:
        raise RuntimeError("启用了 --verify-db-state，但找不到 mariadb/mysql 命令行客户端")

    cnf_lines = [
        "[client]",
        f"host={mysql_option_value(config['DB_HOST'])}",
        f"port={mysql_option_value(config['DB_PORT'])}",
        f"user={mysql_option_value(config['DB_USER'])}",
        f"password={mysql_option_value(config['DB_PASSWORD'])}",
        f"database={mysql_option_value(config['DB_NAME'])}",
    ]
    fd, cnf_path = tempfile.mkstemp(prefix="chatroom-test-mysql-", suffix=".cnf")
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as cnf_file:
            cnf_file.write("\n".join(cnf_lines) + "\n")
        # username 来自测试脚本生成/验证的用户名集合，仍然进行 SQL 字符串转义。
        safe_username = username.replace("\\", "\\\\").replace("'", "''")
        query = f"SELECT online FROM `User` WHERE username = '{safe_username}' LIMIT 1"
        result = subprocess.run(
            [client_bin, f"--defaults-extra-file={cnf_path}", "--batch", "--skip-column-names", "--execute", query],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
            timeout=8,
            check=False,
        )
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip()
            raise RuntimeError(f"查询数据库 online 状态失败：{detail}")
        output = result.stdout.strip()
        if output not in ("0", "1"):
            raise RuntimeError(
                f"查询不到用户 {username!r} 的唯一 online 状态，数据库输出：{output!r}"
            )
        return int(output)
    finally:
        try:
            os.unlink(cnf_path)
        except FileNotFoundError:
            pass


def expect_online_state(username: str, expected: int, config_path: Path, label: str) -> None:
    actual = query_online_state(username, config_path)
    if actual != expected:
        raise AssertionError(
            f"{label}：数据库 online 预期为 {expected}，实际为 {actual}"
        )
    state_text = "在线" if actual == 1 else "离线"
    print(f"[PASS] {label} (数据库 online={actual}，{state_text})")


def run_database_unavailable(host: str, port: int, timeout: float) -> None:
    """验证数据库连接失败时，用户业务返回通用失败，而不是误报成功/用户不存在。"""
    suffix = uuid.uuid4().hex[:12]
    username = f"dbfail{suffix}"
    credentials = {"username": username, "password": "TestPass123!"}
    register = {**credentials, "nickname": "数据库故障测试"}

    with ProtocolClient(host, port, timeout) as client:
        expect_response(
            client,
            REGISTER_REQUEST,
            register,
            REGISTER_FAILED,
            "数据库连接失败时注册应失败",
            "服务器暂时无法处理",
        )
        expect_response(
            client,
            LOGIN_REQUEST,
            credentials,
            LOGIN_FAILED,
            "数据库连接失败时登录应失败",
            "服务器暂时无法处理",
        )
    print("PASS：数据库连接失败分支符合预期。注意确认服务端此时确实使用了无效数据库配置或数据库不可用。")


def run_normal_tests(
    host: str,
    port: int,
    timeout: float,
    username: str,
    password: str,
    nickname: str,
    verify_db_state: bool,
    db_config_path: Path,
) -> None:
    allowed = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_!@#$%&*")
    if not (3 <= len(username) <= 32) or any(char not in allowed for char in username):
        raise RuntimeError("测试用户名必须为 3–32 个 ASCII 字母、数字或允许的特殊字符")
    if not (8 <= len(password) <= 64) or any(char not in allowed for char in password):
        raise RuntimeError("测试密码必须为 8–64 个 ASCII 字母、数字或允许的特殊字符")

    missing_user = "miss" + uuid.uuid4().hex[:12]
    invalid_username = "bad username"  # 空格不符合服务端用户名规则
    wrong_password_candidates = ("WrongPass987!", "OtherPass789!", "BadPass2468!")
    incorrect_password = next(candidate for candidate in wrong_password_candidates if candidate != password)
    credentials = {"username": username, "password": password}
    register_body = {**credentials, "nickname": nickname}

    print(f"服务端：{host}:{port}")
    print(f"本轮测试账号：{username}")
    print("注意：脚本会在数据库中创建该账号，不会自动删除。")

    with ProtocolClient(host, port, timeout) as client:
        expect_response(
            client,
            REGISTER_REQUEST,
            register_body,
            REGISTER_SUCCESS,
            "有效注册应成功",
            "注册成功",
        )
        if verify_db_state:
            expect_online_state(username, 0, db_config_path, "新注册账号初始应离线")

        expect_response(
            client,
            REGISTER_REQUEST,
            register_body,
            REGISTER_FAILED,
            "重复注册同一用户名应失败",
            "用户名已存在",
        )

        expect_response(
            client,
            REGISTER_REQUEST,
            {"username": missing_user, "password": password},
            REGISTER_FAILED,
            "注册请求缺少 nickname 应失败",
            "注册请求格式不正确",
        )

        malformed_command, malformed_body, malformed_id = client.send_raw(
            REGISTER_REQUEST, b'{"username":'
        )
        if malformed_command != REGISTER_FAILED:
            raise AssertionError(
                f"非法 JSON 应返回 REGISTER_FAILED={REGISTER_FAILED}，实际 {malformed_command}；Body={malformed_body}"
            )
        if "注册请求格式不正确" not in str(malformed_body.get("message", "")):
            raise AssertionError(f"非法 JSON 响应提示不符合预期：{malformed_body}")
        print(f"[PASS] 注册 Body 为非法 JSON 时应失败 (request_id={malformed_id})")

        expect_response(
            client,
            REGISTER_REQUEST,
            {"username": invalid_username, "password": password, "nickname": nickname},
            REGISTER_FAILED,
            "用户名含空格应被拒绝",
            "用户名格式不正确",
        )

        expect_response(
            client,
            REGISTER_REQUEST,
            {"username": "weak" + uuid.uuid4().hex[:8], "password": "short", "nickname": nickname},
            REGISTER_FAILED,
            "过短密码注册应被拒绝",
            "密码长度",
        )

        expect_response(
            client,
            LOGIN_REQUEST,
            {"username": username, "password": incorrect_password},
            LOGIN_FAILED,
            "已注册账号使用错误密码登录应失败",
            "用户名或密码错误",
        )
        if verify_db_state:
            expect_online_state(username, 0, db_config_path, "错误密码登录后 online 应仍为 0")

        expect_response(
            client,
            LOGIN_REQUEST,
            {"username": "miss" + uuid.uuid4().hex[:12], "password": password},
            LOGIN_FAILED,
            "不存在的用户名登录应失败",
            "用户名或密码错误",
        )

        expect_response(
            client,
            LOGIN_REQUEST,
            {"username": missing_user},
            LOGIN_FAILED,
            "登录请求缺少 password 应失败",
            "登录请求格式不正确",
        )

        expect_response(
            client,
            LOGIN_REQUEST,
            credentials,
            LOGIN_SUCCESS,
            "正确密码登录应成功",
            "登录成功",
        )
        if verify_db_state:
            expect_online_state(username, 1, db_config_path, "登录成功后 online 应为 1")

        expect_response(
            client,
            LOGOUT_REQUEST,
            {"username": username, "password": incorrect_password},
            LOGOUT_FAILED,
            "退出登录时密码错误应失败",
            "用户名或密码错误",
        )
        if verify_db_state:
            expect_online_state(username, 1, db_config_path, "错误密码退出后 online 应仍为 1")

        expect_response(
            client,
            LOGOUT_REQUEST,
            credentials,
            LOGOUT_SUCCESS,
            "正确密码退出登录应成功",
            "退出登录成功",
        )
        if verify_db_state:
            expect_online_state(username, 0, db_config_path, "退出登录成功后 online 应为 0")

    print("PASS：所选业务异常分支均符合当前服务端响应约定。")
    print(f"测试账号：{username}（脚本不会自动删除该数据库记录）")

def main() -> int:
    parser = argparse.ArgumentParser(
        description="验证聊天室注册/登录/退出登录的业务异常分支；不会删除测试账号或自动停止 MariaDB。"
    )
    parser.add_argument("--host", default="127.0.0.1", help="服务端 IP，默认 127.0.0.1")
    parser.add_argument("--port", type=int, default=8888, help="服务端 TCP 端口，默认 8888")
    parser.add_argument("--timeout", type=float, default=8.0, help="每次 TCP 操作超时秒数，默认 8")
    parser.add_argument("--username", default=f"it{uuid.uuid4().hex[:14]}", help="测试用户名；默认自动生成")
    parser.add_argument("--password", default="TestPass123!", help="测试密码，默认 TestPass123!")
    parser.add_argument("--nickname", default="异常分支测试", help="测试昵称")
    parser.add_argument(
        "--verify-db-state",
        action="store_true",
        help="通过 mysql/mariadb CLI 验证 online=0/1；要求 config/database.conf 可读且客户端已安装",
    )
    parser.add_argument(
        "--db-config",
        type=Path,
        default=DEFAULT_DB_CONFIG,
        help=f"--verify-db-state 使用的数据库配置文件（默认 {DEFAULT_DB_CONFIG}）",
    )
    parser.add_argument(
        "--database-unavailable-only",
        action="store_true",
        help="只测数据库连接失败分支；运行前必须让当前服务端使用无效数据库凭据或确保数据库不可用",
    )
    args = parser.parse_args()

    try:
        if args.database_unavailable_only:
            run_database_unavailable(args.host, args.port, args.timeout)
        else:
            run_normal_tests(
                args.host,
                args.port,
                args.timeout,
                args.username,
                args.password,
                args.nickname,
                args.verify_db_state,
                args.db_config,
            )
    except (OSError, RuntimeError, AssertionError, json.JSONDecodeError, struct.error, subprocess.TimeoutExpired) as exc:
        raise SystemExit(f"[FAIL] {exc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
