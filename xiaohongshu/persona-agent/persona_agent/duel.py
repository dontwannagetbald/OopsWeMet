"""两个人设对话：自动检测 1 块或 2 块 M5，单板/双板自适应。"""

from __future__ import annotations

import contextlib
from dataclasses import dataclass, field
from glob import glob
import sys
import termios
import time

import serial

from persona_agent.agents import AgentPool
from persona_agent.config import (
    duel_persona_a_id,
    duel_persona_b_id,
    serial_baudrate,
    serial_port,
)
from persona_agent.hardware import (
    drain_serial,
    infer_persona_from_line,
)
from persona_agent.registry import PersonaRegistry
from persona_agent.sessions import Session
from persona_agent.vocalcn_stream import reset_m5, speak, synthesize_pcm_bytes
from persona_agent.ws_transport import WsTransport


def _log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


OPENING_STAGE_WAIT_SECONDS = 10.2
LEAVE_STAGE_WAIT_SECONDS = 21.0

EMOTION_KEYWORDS: tuple[tuple[str, tuple[str, ...]], ...] = (
    (
        "angry",
        (
            "生气",
            "愤怒",
            "火大",
            "恼火",
            "讨厌",
            "烦死",
            "可恶",
            "混蛋",
            "闭嘴",
            "滚开",
            "气死",
            "不爽",
            "angry",
            "mad",
            "hate",
        ),
    ),
    (
        "happy",
        (
            "开心",
            "高兴",
            "快乐",
            "兴奋",
            "惊喜",
            "喜欢",
            "爱你",
            "太棒",
            "真好",
            "幸福",
            "好耶",
            "哈哈",
            "嘿嘿",
            "happy",
            "glad",
            "love",
        ),
    ),
    (
        "sad",
        (
            "难过",
            "伤心",
            "失落",
            "沮丧",
            "委屈",
            "害怕",
            "孤单",
            "遗憾",
            "失望",
            "想哭",
            "哭了",
            "寂寞",
            "sad",
            "upset",
            "lonely",
        ),
    ),
)


@dataclass
class Board:
    port: str
    persona_id: str | None
    ser: serial.Serial


@dataclass
class DuelLayout:
    """硬件布局：单板共用一口，双板各一口。"""

    mode: str  # "single" | "dual"
    pid_a: str
    pid_b: str
    speaker_serial: dict[str, serial.Serial] = field(default_factory=dict)
    broadcast_serials: list[serial.Serial] = field(default_factory=list)
    ws: WsTransport | None = None
    ws_personas: set[str] = field(default_factory=set)

    def close(self) -> None:
        seen: set[int] = set()
        for ser in self.broadcast_serials:
            fd = id(ser)
            if fd in seen:
                continue
            seen.add(fd)
            try:
                ser.close()
            except Exception:
                pass
        if self.ws is not None:
            self.ws.stop()

    def send_to_all(self, cmd: str) -> None:
        ws_sent = False
        if self.ws is not None:
            for persona in self.ws_personas:
                ws_sent = self.ws.send_board(persona, cmd) or ws_sent
        if ws_sent:
            return
        for ser in self.broadcast_serials:
            drain_serial(ser, timeout=0.05)
            ser.write((cmd + "\n").encode("utf-8"))
            ser.flush()
            _log(f"  → {ser.port}: {cmd[:70]}…")


def _open_serial(port: str) -> serial.Serial:
    _log(f"打开串口 {port} @ {serial_baudrate()}")
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = serial_baudrate()
    ser.timeout = 1
    ser.rtscts = False
    ser.dsrdtr = False
    ser.rts = False
    ser.dtr = False
    ser.open()
    with contextlib.suppress(Exception):
        attrs = termios.tcgetattr(ser.fileno())
        attrs[2] &= ~termios.HUPCL
        termios.tcsetattr(ser.fileno(), termios.TCSANOW, attrs)
    with contextlib.suppress(Exception):
        ser.setRTS(False)
    with contextlib.suppress(Exception):
        ser.setDTR(False)
    with contextlib.suppress(Exception):
        ser.reset_input_buffer()
    with contextlib.suppress(Exception):
        ser.reset_output_buffer()
    return ser


def _candidate_ports() -> list[str]:
    seen: set[str] = set()
    ports: list[str] = []
    for pattern in (
        "/dev/tty.usbmodem*",
        "/dev/tty.wchusbserial*",
        "/dev/tty.usbserial*",
        "/dev/cu.usbmodem*",
        "/dev/cu.wchusbserial*",
        "/dev/cu.usbserial*",
    ):
        for port in sorted(glob(pattern)):
            if port not in seen:
                seen.add(port)
                ports.append(port)
    explicit = serial_port()
    if explicit and explicit not in seen:
        ports.insert(0, explicit)
    return ports


def _read_persona(ser: serial.Serial, timeout: float = 6.0) -> str | None:
    """读满 timeout；固件每 2s 上报 PERSONA，也认 Self: 行。"""
    deadline = time.perf_counter() + timeout
    selected: str | None = None

    while time.perf_counter() < deadline:
        if not ser.in_waiting:
            time.sleep(0.05)
            continue

        raw = ser.readline()
        if not raw:
            continue

        line = raw.decode("utf-8", errors="replace").strip()
        if line:
            _log(f"  {ser.port}: {line}")

        pid = infer_persona_from_line(line)
        if pid:
            selected = pid

    return selected


def _assign_speaker_serials(
    boards: list[Board],
    pid_a: str,
    pid_b: str,
) -> dict[str, serial.Serial]:
    """按板上报的身份绑定音频口，避免按端口顺序绑错。"""
    required = [pid_a, pid_b]
    mapping: dict[str, serial.Serial] = {}
    matched: list[Board] = []

    for board in boards:
        if (
            board.persona_id
            and board.persona_id in required
            and board.persona_id not in mapping
        ):
            mapping[board.persona_id] = board.ser
            matched.append(board)

    unknown = [b for b in boards if b not in matched]
    missing = [p for p in required if p not in mapping]
    for board, pid in zip(unknown, missing):
        mapping[pid] = board.ser
        _log(f"{board.port} 身份未知，兜底绑定音频 → {pid}")

    for pid, ser in mapping.items():
        _log(f"音频出口: {pid} → {ser.port}")

    return mapping


def _scan_boards() -> list[Board]:
    boards: list[Board] = []

    for port in _candidate_ports():
        try:
            ser = _open_serial(port)
        except serial.SerialException as exc:
            _log(f"跳过串口 {port}: {exc}")
            continue

        time.sleep(1.0)
        pid = _read_persona(ser)
        if pid:
            _log(f"{port} 上报身份 → {pid}")
        else:
            _log(f"{port} 未上报 PERSONA（仍可用）")
        boards.append(Board(port=port, persona_id=pid, ser=ser))

    return boards


def open_duel_layout(pid_a: str, pid_b: str) -> DuelLayout:
    """
    自动布局：
    - WebSocket 两块板已上线 → 双板 WS 模式
    - 扫到 ≥2 块板 → 双板（各一口，DUEL 广播到两块）
    - 扫到 1 块板 → 单板（同一块显示+播音）
    - 扫到 0 块 → 尝试 SERIAL_PORT 单板兜底
    """
    ws = WsTransport()
    if ws.start():
        _log("等待 M5 WebSocket 注册（最多 8s）…")
        online = ws.wait_for_boards({pid_a, pid_b}, timeout=8.0)
        if {pid_a, pid_b}.issubset(online):
            _log(f"双板 WS 模式: {', '.join(sorted(online))}")
            return DuelLayout(
                mode="dual-ws",
                pid_a=pid_a,
                pid_b=pid_b,
                ws=ws,
                ws_personas={pid_a, pid_b},
            )
        _log(
            "WebSocket 未等到两块板，回退串口扫描 "
            f"(online: {', '.join(sorted(online)) or 'none'})"
        )

    boards = _scan_boards()
    required = {pid_a, pid_b}

    if len(boards) >= 2:
        active = boards[:2]
        for extra in boards[2:]:
            extra.ser.close()

        speaker = _assign_speaker_serials(active, pid_a, pid_b)
        ports = ", ".join(f"{b.port}({b.persona_id or '?'})" for b in active)
        _log(f"双板模式: {ports}")
        return DuelLayout(
            mode="dual",
            pid_a=pid_a,
            pid_b=pid_b,
            speaker_serial=speaker,
            broadcast_serials=[b.ser for b in active],
            ws=ws,
        )

    if len(boards) == 1:
        ser = boards[0].ser
        _log(f"单板模式: {boards[0].port}（两人共用）")
        return DuelLayout(
            mode="single",
            pid_a=pid_a,
            pid_b=pid_b,
            speaker_serial={pid_a: ser, pid_b: ser},
            broadcast_serials=[ser],
            ws=ws,
        )

    # 0 块：尝试配置的默认串口
    port = serial_port()
    try:
        ser = _open_serial(port)
    except serial.SerialException as exc:
        raise RuntimeError(
            "未检测到 M5 串口。请插板后重试，或设置 SERIAL_PORT。"
        ) from exc

    time.sleep(1.0)
    pid = _read_persona(ser, timeout=2.0)
    if pid:
        _log(f"{port} 上报身份 → {pid}（单板模式）")
    else:
        _log(f"{port} 未上报 PERSONA，按单板模式使用")

    return DuelLayout(
        mode="single",
        pid_a=pid_a,
        pid_b=pid_b,
        speaker_serial={pid_a: ser, pid_b: ser},
        broadcast_serials=[ser],
        ws=ws,
    )


def _safe_line(text: str) -> str:
    return " ".join(text.replace("\r", "\n").splitlines()).strip()


def _emotion_state_for_reply(text: str) -> str:
    normalized = _safe_line(text).lower()
    if not normalized:
        return "chat"

    best_state = "chat"
    best_score = 0

    for state, keywords in EMOTION_KEYWORDS:
        score = sum(1 for keyword in keywords if keyword in normalized)
        if score > best_score:
            best_state = state
            best_score = score

    return best_state


def _encounter_stage_text(label_a: str, label_b: str) -> str:
    return f"{label_a}遇见{label_b}"


def _leave_stage_text(label_a: str) -> str:
    return f"谈话结束，{label_a}准备回家。"


def send_duel_scene(
    ser: serial.Serial,
    left_id: str,
    right_id: str,
    speaker_id: str,
    state: str,
    _display_name: str,
    text: str,
) -> None:
    screen = _safe_line(text)
    msg = f"DUEL|{left_id}|{right_id}|{speaker_id}|{state}|{screen}\n"
    ser.write(msg.encode("utf-8"))
    ser.flush()
    _log(f"  → {ser.port}: DUEL|{speaker_id}|{state}|{screen[:36]}…")


def duel_scene_command(
    left_id: str,
    right_id: str,
    speaker_id: str,
    state: str,
    _display_name: str,
    text: str,
) -> str:
    screen = _safe_line(text)
    return f"DUEL|{left_id}|{right_id}|{speaker_id}|{state}|{screen}"


def send_duel_stage(
    ser: serial.Serial,
    state: str,
    next_state: str,
    left_id: str,
    right_id: str,
    text: str,
) -> None:
    screen = _safe_line(text)
    msg = f"DUELSTAGE|{state}|{next_state}|{left_id}|{right_id}|{screen}\n"
    ser.write(msg.encode("utf-8"))
    ser.flush()
    _log(f"  → {ser.port}: DUELSTAGE|{state}|{next_state}|{screen[:36]}…")


def duel_stage_command(
    state: str,
    next_state: str,
    left_id: str,
    right_id: str,
    text: str,
) -> str:
    screen = _safe_line(text)
    return f"DUELSTAGE|{state}|{next_state}|{left_id}|{right_id}|{screen}"


def broadcast_stage(
    layout: DuelLayout | None,
    state: str,
    next_state: str,
    left_id: str,
    right_id: str,
    text: str,
    text_right: str | None = None,
    *,
    use_hardware: bool,
) -> None:
    if (
        not use_hardware or
        layout is None
    ):
        return

    right_text = text_right or text

    if layout.mode == "dual-ws" and layout.ws is not None:
        layout.ws.send_board(
            left_id,
            duel_stage_command(
                state,
                next_state,
                left_id,
                right_id,
                text,
            ),
        )
        layout.ws.send_board(
            right_id,
            duel_stage_command(
                state,
                next_state,
                right_id,
                left_id,
                right_text,
            ),
        )
        layout.ws.broadcast(
            {
                "type": "stage",
                "state": state,
                "next_state": next_state,
                "left": left_id,
                "right": right_id,
                "text": text,
                "text_right": right_text,
            }
        )
        return

    if layout.mode == "dual":
        left_ser = layout.speaker_serial.get(left_id)
        right_ser = layout.speaker_serial.get(right_id)

        if left_ser is not None and right_ser is not None:
            send_duel_stage(
                left_ser,
                state,
                next_state,
                left_id,
                right_id,
                text,
            )

            if right_ser is not left_ser:
                send_duel_stage(
                    right_ser,
                    state,
                    next_state,
                    right_id,
                    left_id,
                    right_text,
                )
            return

    cmd = duel_stage_command(
        state,
        next_state,
        left_id,
        right_id,
        text,
    )
    layout.send_to_all(cmd)
    if layout.ws is not None:
        layout.ws.broadcast(
            {
                "type": "stage",
                "state": state,
                "next_state": next_state,
                "left": left_id,
                "right": right_id,
                "text": text,
            }
        )


def deliver_turn(
    layout: DuelLayout | None,
    left_id: str,
    right_id: str,
    persona_id: str,
    display_name: str,
    reply: str,
    *,
    use_hardware: bool,
) -> None:
    _log(f">>> 轮到 {display_name} [{persona_id}]")

    if not use_hardware or layout is None:
        print(f"[{display_name}] {reply}\n", flush=True)
        return

    state = _emotion_state_for_reply(reply)

    cmd = duel_scene_command(
        left_id,
        right_id,
        persona_id,
        state,
        display_name,
        reply,
    )
    layout.send_to_all(cmd)
    if layout.ws is not None:
        layout.ws.broadcast(
                {
                    "type": "reply",
                    "persona": persona_id,
                    "state": state,
                    "text": reply,
                }
            )

    time.sleep(0.05)

    if (
        layout.ws is not None and
        persona_id in layout.ws_personas
    ):
        _log(f"  通过 WebSocket 播放 [{persona_id}]")
        pcm_bytes, sample_rate = synthesize_pcm_bytes(
            reply,
            persona_id=persona_id,
        )
        if not pcm_bytes:
            _log(f"  无音频样本，跳过 [{persona_id}]")
            _log(f"<<< {display_name} 发言结束\n")
            return

        sent = False
        for attempt in range(2):
            if attempt > 0:
                _log(
                    f"  WebSocket 音频重试 [{persona_id}] "
                    f"({attempt + 1}/2)，等待板子重连…"
                )
                online = layout.ws.wait_for_boards(
                    {persona_id},
                    timeout=8.0,
                )
                if persona_id not in online:
                    _log(f"  板子仍未重连，跳过本轮音频: {persona_id}")
                    continue
                layout.ws.send_board(
                    persona_id,
                    cmd,
                )
                time.sleep(0.05)

            if layout.ws.send_audio(
                persona_id,
                pcm_bytes,
                sample_rate,
            ):
                sent = True
                break

            _log(f"  WebSocket 音频发送失败: {persona_id}")
            time.sleep(0.4)

        if not sent:
            _log(f"  跳过本轮 WebSocket 音频: {persona_id}")
            _log(f"<<< {display_name} 发言结束\n")
            return

        audio_seconds = len(pcm_bytes) / 2 / sample_rate
        if not layout.ws.wait_audio_done(
            persona_id,
            timeout=max(20.0, audio_seconds + 8.0),
        ):
            _log(f"  等待 M5 播放完成超时，继续流程: {persona_id}")
            layout.ws.wait_for_boards({persona_id}, timeout=3.0)

        _log(f"<<< {display_name} 发言结束\n")
        return

    speaker_ser = layout.speaker_serial.get(persona_id)
    if speaker_ser is None:
        _log(f"  WS 显示已发送，跳过串口音频 [{persona_id}]")
        _log(f"<<< {display_name} 发言结束\n")
        return

    _log(f"  从 {speaker_ser.port} 播放 [{persona_id}]")

    try:
        reset_m5()
        speak(reply, speaker_ser, persona_id=persona_id)
    except serial.SerialException as exc:
        _log(f"speak 失败，0.5s 后重试: {exc}")
        time.sleep(0.5)
        drain_serial(speaker_ser, timeout=0.3)
        send_duel_scene(
            speaker_ser,
            left_id,
            right_id,
            persona_id,
            state,
            display_name,
            reply,
        )
        time.sleep(0.05)
        reset_m5()
        speak(reply, speaker_ser, persona_id=persona_id)

    for ser in layout.broadcast_serials:
        drain_serial(ser, timeout=0.1)

    _log(f"<<< {display_name} 发言结束\n")


def _wait_remaining_stage_time(
    stage_started_at: float,
    wait_seconds: float,
) -> None:
    remaining = wait_seconds - (time.perf_counter() - stage_started_at)
    if remaining > 0:
        time.sleep(remaining)


def run_duel(
    persona_a: str | None = None,
    persona_b: str | None = None,
    rounds: int = 6,
    topic: str | None = None,
    *,
    use_hardware: bool = True,
    layout: DuelLayout | None = None,
    close_layout: bool = True,
) -> None:
    registry = PersonaRegistry()
    pool = AgentPool(registry)

    pid_a = persona_a or duel_persona_a_id()
    pid_b = persona_b or duel_persona_b_id()

    try:
        info_a = registry.get(pid_a)
        info_b = registry.get(pid_b)
    except KeyError as e:
        print(f"错误：{e}", file=sys.stderr)
        sys.exit(1)

    agent_a = pool.get(pid_a)
    agent_b = pool.get(pid_b)
    session_a = Session(id=f"duel-{pid_a}", persona_id=pid_a)
    session_b = Session(id=f"duel-{pid_b}", persona_id=pid_b)

    label_a = info_a.name
    label_b = info_b.name
    message = topic or f"{label_b}，我想跟你聊聊。"

    if use_hardware:
        reset_m5()
        if layout is None:
            layout = open_duel_layout(pid_a, pid_b)
        mode_label = "双板" if layout.mode == "dual" else "单板"
        if layout.mode == "dual-ws":
            mode_label = "双板 WebSocket"
        print(f"双 Persona 对话（{mode_label}）")
    else:
        print("双 Persona 对话（仅终端）")

    print(f"  A: {label_a} [{pid_a}]")
    print(f"  B: {label_b} [{pid_b}]")
    print(f"  轮数: {rounds}\n")

    try:
        opening_stage_started_at = time.perf_counter()
        broadcast_stage(
            layout,
            "outdoor",
            "chat",
            pid_a,
            pid_b,
            _encounter_stage_text(label_a, label_b),
            _encounter_stage_text(label_b, label_a),
            use_hardware=use_hardware,
        )
        if layout and layout.ws is not None:
            layout.ws.broadcast(
                {
                    "type": "encounter",
                    "a_persona": pid_a,
                    "b_persona": pid_b,
                    "affinity": 100,
                    "mode": layout.mode,
                }
            )

        for index in range(1, rounds + 1):
            print(f"—— 第 {index}/{rounds} 轮 ——", flush=True)

            if index == 1:
                _log(f"[LLM] 转场期间预生成 {label_a} 首句回复…")
            else:
                _log(f"[LLM] 生成 {label_a} 回复…")

            reply_a = agent_a.reply(
                session_a,
                f"{label_b}：{message}",
            )

            if use_hardware and index == 1:
                _wait_remaining_stage_time(
                    opening_stage_started_at,
                    OPENING_STAGE_WAIT_SECONDS,
                )

            print(f"[{index}. {label_a}]\n{reply_a}\n", flush=True)
            deliver_turn(
                layout, pid_a, pid_b, pid_a, label_a, reply_a,
                use_hardware=use_hardware,
            )

            _log(f"[LLM] 生成 {label_b} 回复…")
            reply_b = agent_b.reply(session_b, f"{label_a}：{reply_a}")
            print(f"[{index}. {label_b}]\n{reply_b}\n", flush=True)
            deliver_turn(
                layout, pid_a, pid_b, pid_b, label_b, reply_b,
                use_hardware=use_hardware,
            )

            message = reply_b
    finally:
        broadcast_stage(
            layout,
            "leave",
            "idle",
            pid_a,
            pid_b,
            _leave_stage_text(label_a),
            _leave_stage_text(label_b),
            use_hardware=use_hardware,
        )
        if use_hardware:
            time.sleep(LEAVE_STAGE_WAIT_SECONDS)

        if layout and layout.ws is not None:
            layout.ws.broadcast(
                {
                    "type": "leave",
                    "reason": "duel finished",
                }
            )

        if layout is not None and close_layout:
            layout.close()
        reset_m5()

    print("对话结束。")


def run_hardware_smoke() -> None:
    """不调用模型，验证单板/双板 DUEL 显示与轮流音频。"""
    registry = PersonaRegistry()
    pid_a = duel_persona_a_id()
    pid_b = duel_persona_b_id()
    label_a = registry.get(pid_a).name
    label_b = registry.get(pid_b).name

    reset_m5()
    layout = open_duel_layout(pid_a, pid_b)

    try:
        broadcast_stage(
            layout,
            "outdoor",
            "chat",
            pid_a,
            pid_b,
            _encounter_stage_text(label_a, label_b),
            _encounter_stage_text(label_b, label_a),
            use_hardware=True,
        )
        time.sleep(OPENING_STAGE_WAIT_SECONDS)

        deliver_turn(
            layout, pid_a, pid_b, pid_a, label_a,
            "你好小红，硬件 smoke 测试开始。",
            use_hardware=True,
        )
        deliver_turn(
            layout, pid_a, pid_b, pid_b, label_b,
            "你好张总，收到，切换正常。",
            use_hardware=True,
        )
    finally:
        broadcast_stage(
            layout,
            "leave",
            "idle",
            pid_a,
            pid_b,
            _leave_stage_text(label_a),
            _leave_stage_text(label_b),
            use_hardware=True,
        )
        time.sleep(LEAVE_STAGE_WAIT_SECONDS)
        layout.close()
        reset_m5()

    print(f"硬件 smoke test 结束（{layout.mode}）。")


if __name__ == "__main__":
    run_duel()
