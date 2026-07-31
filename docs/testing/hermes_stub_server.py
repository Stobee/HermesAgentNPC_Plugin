#!/usr/bin/env python3
"""Hermes UE5 클라이언트 수동 검증용 스텁 서버.

실제 LLM 도 추론도 없다. 프로토콜(ue5-socket-protocol.md)만 흉내내서
클라이언트의 각 경로를 의도적으로 자극하는 것이 목적이다.

프레이밍: 4바이트 big-endian 길이 프리픽스 + UTF-8 JSON 바디
          (Plugins/.../Protocol/HermesFrameCodec.h 와 동일)

사용법:
    py docs/testing/hermes_stub_server.py --scenario happy
    py docs/testing/hermes_stub_server.py --scenario session_taken_over
    py docs/testing/hermes_stub_server.py --list

시나리오는 --list 로 확인한다. 기본 포트는 8770 (UHermesSettings::Port 기본값).
"""

from __future__ import annotations

import argparse
import json
import socket
import struct
import sys
import threading
import time
import uuid

MAX_BODY = 1024 * 1024  # 1 MiB — 클라이언트의 FHermesFrameCodec::MaxBodySize 와 동일


# ---------------------------------------------------------------- 프레이밍

def send_frame(conn: socket.socket, obj: dict) -> None:
    body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
    conn.sendall(struct.pack(">I", len(body)) + body)
    print(f"  --> {json.dumps(obj, ensure_ascii=False)}", flush=True)


def send_raw(conn: socket.socket, payload: bytes) -> None:
    """프레이밍을 일부러 어길 때 쓴다(bad_frame 시나리오)."""
    conn.sendall(payload)
    print(f"  --> (raw {len(payload)} bytes)", flush=True)


class FrameReader:
    """부분 수신을 견디는 누적 리더. 클라이언트의 FFrameAccumulator 대응."""

    def __init__(self) -> None:
        self.buf = bytearray()

    def feed(self, data: bytes) -> None:
        self.buf.extend(data)

    def pop(self) -> dict | None:
        if len(self.buf) < 4:
            return None
        (length,) = struct.unpack(">I", self.buf[:4])
        if length == 0 or length > MAX_BODY:
            raise ValueError(f"protocol violation: length={length}")
        if len(self.buf) < 4 + length:
            return None
        body = bytes(self.buf[4 : 4 + length])
        del self.buf[: 4 + length]
        return json.loads(body.decode("utf-8"))


# ---------------------------------------------------------------- 상태

class Session:
    """이 스텁이 기억하는 최소한의 것. 발급한 신원과 대화 카운터뿐."""

    def __init__(self) -> None:
        self.issued: dict[str, str] = {}  # player_id -> session_token
        self.lock = threading.Lock()

    def issue(self) -> tuple[str, str]:
        pid = f"p-{uuid.uuid4().hex[:12]}"
        tok = uuid.uuid4().hex
        with self.lock:
            self.issued[pid] = tok
        return pid, tok

    def validate(self, pid: str, tok: str) -> bool:
        with self.lock:
            return bool(pid) and self.issued.get(pid) == tok


SESSION = Session()


# ---------------------------------------------------------------- 시나리오

def handle_identify(conn: socket.socket, msg: dict, *, force_error: str | None) -> bool:
    """identify 에 응답한다. 계속 진행할지 여부를 돌려준다."""
    version = msg.get("protocol_version")
    if version != 2:
        send_frame(conn, {
            "type": "error", "code": "unsupported_version",
            "message": f"protocol_version must be 2, got {version!r}",
        })
        return False

    if force_error:
        send_frame(conn, {
            "type": "error", "code": force_error,
            "message": f"stub server forcing {force_error}",
        })
        return False

    pid = msg.get("player_id", "")
    tok = msg.get("session_token", "")

    if pid or tok:
        # 둘 중 하나만 온 경우도 not_authorized 다 (프로토콜 §5).
        if not SESSION.validate(pid, tok):
            send_frame(conn, {
                "type": "error", "code": "not_authorized",
                "message": "stored credentials did not validate",
            })
            return False
    else:
        pid, tok = SESSION.issue()

    send_frame(conn, {
        "type": "identified", "ok": True,
        "player_id": pid, "session_token": tok,
        "chat_id": f"conv-{pid}",
    })
    return True


def stream_reply(conn: socket.socket, chat_id: str, text: str, *, delay: float = 0.35) -> None:
    """chat_delta 로 조금씩 보내고 chat_response 로 정본을 확정한다."""
    chunks = [text[i : i + 6] for i in range(0, len(text), 6)]
    for seq, chunk in enumerate(chunks):
        send_frame(conn, {"type": "chat_delta", "id": chat_id, "seq": seq, "text": chunk})
        time.sleep(delay)
    send_frame(conn, {"type": "chat_response", "id": chat_id, "text": text})


def serve(conn: socket.socket, addr, scenario: str) -> None:
    print(f"[+] connected from {addr} (scenario={scenario})", flush=True)
    reader = FrameReader()
    identified = False
    chat_count = 0
    action_seq = 0

    # identify 전에 무엇이 오든 not_identified 로 되돌리는 시나리오
    force_identify_error = {
        "session_taken_over": "session_taken_over",
        "unsupported_version_error": "unsupported_version",
        "not_authorized": "not_authorized",
        "rate_limited_identify": "rate_limited",
    }.get(scenario)

    conn.settimeout(1.0)
    try:
        while True:
            try:
                data = conn.recv(65536)
            except socket.timeout:
                continue
            if not data:
                print("[-] peer closed", flush=True)
                return
            reader.feed(data)

            while True:
                msg = reader.pop()
                if msg is None:
                    break
                mtype = msg.get("type", "")
                print(f"  <-- {json.dumps(msg, ensure_ascii=False)}", flush=True)

                if mtype == "identify":
                    if not handle_identify(conn, msg, force_error=force_identify_error):
                        # 프로토콜 §5: 이 코드들은 연결을 닫는다.
                        time.sleep(0.2)
                        print("[-] closing after fatal error", flush=True)
                        return
                    identified = True

                    if scenario == "bad_frame":
                        # 길이 0 프레임은 클라이언트가 프레이밍 위반으로 처리한다.
                        send_frame(conn, {
                            "type": "error", "code": "bad_frame",
                            "message": "stub server forcing bad_frame",
                        })
                        time.sleep(0.2)
                        return

                elif mtype == "chat":
                    chat_count += 1
                    cid = msg.get("id", "")

                    if scenario == "chat_timeout":
                        print("  (chat_timeout: 응답을 보내지 않는다. "
                              "ChatResponseTimeoutSeconds 후 클라이언트가 턴을 실패시켜야 한다)",
                              flush=True)
                        continue

                    if scenario == "server_busy":
                        send_frame(conn, {
                            "type": "error", "code": "server_busy", "id": cid,
                            "message": "inference queue full",
                        })
                        continue

                    if scenario == "error_without_id":
                        send_frame(conn, {
                            "type": "error", "code": "internal_error",
                            "message": "no id on purpose -- no turn may be failed",
                        })
                        continue

                    if scenario == "stale_delta":
                        # 이전 턴의 id 로 델타를 보낸다. 클라이언트는 무시해야 한다.
                        send_frame(conn, {
                            "type": "chat_delta", "id": "c-0000-stale",
                            "seq": 0, "text": "이 텍스트는 화면에 나오면 안 된다",
                        })
                        time.sleep(0.3)

                    if scenario == "interleaved_turns" and chat_count >= 2:
                        # 규약 위반: 두 턴의 델타를 섞어 보낸다.
                        send_frame(conn, {"type": "chat_delta", "id": cid, "seq": 0, "text": "A조각"})
                        send_frame(conn, {"type": "chat_delta", "id": "c-9999", "seq": 0, "text": "B조각"})
                        send_frame(conn, {"type": "chat_response", "id": cid, "text": "A만 보여야 한다"})
                        continue

                    threading.Thread(
                        target=stream_reply,
                        args=(conn, cid, f"[{chat_count}] 알겠어요. 지금 처리할게요."),
                        daemon=True,
                    ).start()

                    if scenario == "move_to":
                        action_seq += 1
                        aid = f"act-{action_seq:04d}"
                        time.sleep(1.2)
                        send_frame(conn, {
                            "type": "action_request", "id": aid,
                            "command": "move_to",
                            "params": {"location": {"x": 500.0, "y": 500.0, "z": 100.0}},
                        })

                    if scenario == "unprompted_speech":
                        # id 없는 chat_response. 프로토콜 §4.4 에서 id 는
                        # optional 이고, 없는 것은 서버가 먼저 거는 말이다
                        # (action_event 이후의 "도착했어요").
                        time.sleep(1.0)
                        send_frame(conn, {
                            "type": "chat_response",
                            "text": "그러고 보니 방금 도착했어요.",
                        })

                    if scenario == "unknown_command":
                        action_seq += 1
                        send_frame(conn, {
                            "type": "action_request", "id": f"act-{action_seq:04d}",
                            "command": "self_destruct", "params": {},
                        })

                elif mtype == "ping":
                    if scenario == "silent_after_identify":
                        # 여기서 pong 을 돌려주면 그것이 수신 신호가 되어
                        # LastRecvTime 이 갱신되고 사망 판정이 영영 나지 않는다.
                        # "침묵"은 아무것도 보내지 않는다는 뜻이다.
                        print("  (silent_after_identify: pong 을 보내지 않는다)", flush=True)
                        continue
                    send_frame(conn, {"type": "pong", "id": msg.get("id", "")})

                elif mtype == "pong":
                    pass  # 우리가 보낸 ping 의 응답

                elif mtype == "action_result":
                    if not msg.get("ok"):
                        print(f"  (action failed: {msg.get('error')!r})", flush=True)
                    elif "arrived" in (msg.get("result") or {}):
                        print("  !! action_result 에 arrived 가 있다 -- 프로토콜 §4.5 위반",
                              flush=True)

                elif mtype == "action_event":
                    print(f"  (action_event: {msg.get('event')} for {msg.get('id')})", flush=True)

                else:
                    send_frame(conn, {
                        "type": "error", "code": "unknown_type",
                        "message": f"unrecognized type {mtype!r}",
                    })

    except ValueError as exc:
        print(f"[!] {exc}", flush=True)
    except ConnectionResetError:
        print("[-] connection reset by peer", flush=True)
    finally:
        conn.close()


SCENARIOS = {
    "happy": "정상 흐름. 신원 발급, chat_delta 스트리밍 후 chat_response.",
    "move_to": "chat 마다 move_to action_request 를 보낸다. action_result{started} 와 이후 action_event{completed} 를 확인.",
    "unknown_command": "화이트리스트에 없는 command 를 보낸다. ok=false 회신을 확인.",
    "chat_timeout": "chat 에 아무 응답도 하지 않는다. ChatResponseTimeoutSeconds 후 UI 가 실패를 표시해야 한다.",
    "server_busy": "chat 의 id 를 달아 server_busy 를 보낸다. 타임아웃을 기다리지 않고 즉시 실패해야 한다 (Task 13e).",
    "error_without_id": "id 없는 internal_error 를 보낸다. 어떤 턴도 실패하지 않아야 한다 (Task 13e).",
    "stale_delta": "이전 턴의 id 로 델타를 보낸다. 화면에 나오지 않아야 한다 (Task 13 상관 규칙).",
    "unprompted_speech": "id 없는 chat_response 를 보낸다. 자발 발화이므로 표시되어야 한다 (§4.4).",
    "interleaved_turns": "두 번째 chat 부터 두 턴의 델타를 섞어 보낸다. 다른 턴 조각이 섞이지 않아야 한다 (§4.9).",
    "silent_after_identify": "identify 후 침묵한다. PeerTimeoutSeconds 후 사망 판정과 재연결이 일어나야 한다 (Task 12).",
    "session_taken_over": "identify 에 session_taken_over 로 답하고 닫는다. 재연결이 멈춰야 한다 (Task 13d Step 5).",
    "unsupported_version_error": "unsupported_version 으로 답하고 닫는다. 재연결이 멈춰야 한다 (Task 13d).",
    "not_authorized": "not_authorized 로 답하고 닫는다. 자격 증명을 버리고 신규 발급을 요청해야 한다.",
    "bad_frame": "identify 후 bad_frame 을 보내고 닫는다. 평소대로 백오프 재연결해야 한다.",
}


def main() -> int:
    ap = argparse.ArgumentParser(description="Hermes UE5 클라이언트 수동 검증용 스텁 서버")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8770)
    ap.add_argument("--scenario", default="happy", choices=sorted(SCENARIOS))
    ap.add_argument("--once", action="store_true",
                    help="한 연결만 처리하고 종료한다(재연결 정지 확인에 유용).")
    ap.add_argument("--list", action="store_true", help="시나리오 목록을 출력하고 종료한다.")
    args = ap.parse_args()

    if args.list:
        width = max(len(k) for k in SCENARIOS)
        for name in sorted(SCENARIOS):
            print(f"  {name.ljust(width)}  {SCENARIOS[name]}")
        return 0

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(4)
    print(f"[*] listening on {args.host}:{args.port} -- scenario '{args.scenario}'", flush=True)
    print(f"[*] {SCENARIOS[args.scenario]}", flush=True)
    print("[*] Ctrl+C to stop", flush=True)

    try:
        while True:
            conn, addr = srv.accept()
            serve(conn, addr, args.scenario)
            if args.once:
                print("[*] --once given, exiting", flush=True)
                return 0
    except KeyboardInterrupt:
        print("\n[*] stopped", flush=True)
        return 0
    finally:
        srv.close()


if __name__ == "__main__":
    sys.exit(main())
