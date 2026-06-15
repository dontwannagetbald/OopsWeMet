"""小红咖啡店独白：固定台词 + coffee_shop 场景。"""

from __future__ import annotations

import sys
import time

import serial

from persona_agent.config import serial_baudrate, serial_port
from persona_agent.hardware import drain_serial
from persona_agent.vocalcn_stream import reset_m5, speak, synthesize_pcm_bytes
from persona_agent.ws_transport import WsTransport


PERSONA_ID = "xiao_hong"
STATE = "coffee"

COFFEE_LINES: tuple[str, ...] = (
    "这杯冰美式看起来很适合我。",
    "冷，黑，且不主动解释自己。",
    "店员问我要不要加热。",
    "我沉默了三秒。企鹅不接受这种挑衅。",
    "旁边那块蛋糕一直在看我。",
    "感觉它比我更清楚我今天不会减脂。",
    "我打开电脑准备写作业。",
    "然后开始研究杯垫为什么比我的方案更有设计感。",
    "企鹅今日占领地：",
    "靠窗第二张桌子。咖啡归我，发呆也归我。",
)


def _log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def _screen_text(text: str) -> str:
    return " ".join(text.replace("\r", "\n").splitlines()).strip()


def _coffee_command(text: str) -> str:
    return f"TXT|{STATE}|{_screen_text(text)}"


def _play_ws(ws: WsTransport) -> bool:
    if not ws.start():
        return False

    try:
        _log("等待小红 WebSocket 上线（最多 8s）…")
        online = ws.wait_for_boards({PERSONA_ID}, timeout=8.0)
        if PERSONA_ID not in online:
            _log("WebSocket 没等到小红，准备回退串口。")
            return False

        for index, line in enumerate(COFFEE_LINES, start=1):
            _log(f"[{index}/{len(COFFEE_LINES)}] 小红 coffee")
            if not ws.send_board(PERSONA_ID, _coffee_command(line)):
                raise RuntimeError("WebSocket 屏幕发送失败")

            pcm_bytes, sample_rate = synthesize_pcm_bytes(
                line,
                persona_id=PERSONA_ID,
            )
            if not pcm_bytes:
                time.sleep(1.6)
                continue

            if not ws.send_audio(PERSONA_ID, pcm_bytes, sample_rate):
                raise RuntimeError("WebSocket 音频发送失败")

            audio_seconds = len(pcm_bytes) / 2 / sample_rate
            ws.wait_audio_done(
                PERSONA_ID,
                timeout=max(20.0, audio_seconds + 8.0),
            )

        return True
    finally:
        ws.stop()


def _open_serial() -> serial.Serial:
    port = serial_port()
    _log(f"打开串口 {port} @ {serial_baudrate()}")
    ser = serial.Serial(
        port=port,
        baudrate=serial_baudrate(),
        timeout=1,
        rtscts=False,
        dsrdtr=False,
    )
    ser.setRTS(False)
    ser.setDTR(False)
    return ser


def _play_serial() -> None:
    ser = _open_serial()
    try:
        for index, line in enumerate(COFFEE_LINES, start=1):
            _log(f"[{index}/{len(COFFEE_LINES)}] 小红 coffee")
            drain_serial(ser, timeout=0.1)
            ser.write((_coffee_command(line) + "\n").encode("utf-8"))
            ser.flush()
            time.sleep(0.15)
            reset_m5()
            speak(line, ser, persona_id=PERSONA_ID)
    finally:
        ser.close()


def run() -> None:
    ws = WsTransport()
    if _play_ws(ws):
        return

    _play_serial()


if __name__ == "__main__":
    run()
