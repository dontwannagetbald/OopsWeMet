"""BLE proximity encounter monitor for M5 WebSocket boards."""

from __future__ import annotations

import sys
import time

from persona_agent.duel import DuelLayout, run_duel
from persona_agent.ws_transport import WsEncounterEvent, WsTransport


def _log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def _pair_key(a: str, b: str) -> tuple[str, str]:
    return tuple(sorted((a, b)))


def run_encounter_monitor(
    *,
    rounds: int = 5,
    cooldown: float = 30.0,
    online_fallback_delay: float = 5.0,
) -> None:
    ws = WsTransport()
    if not ws.start():
        raise RuntimeError("WebSocket 启动失败，请确认 8765 端口没有被占用。")

    _log("相遇监听已启动：等待 M5 上报 ENCOUNTER|本机人格|对方人格|RSSI")
    if online_fallback_delay > 0:
        _log(
            "BLE 未上报时启用在线 fallback："
            f"两块板同时在线 {online_fallback_delay:.1f}s 后自动开始"
        )
    last_finished: dict[tuple[str, str], float] = {}
    online_since: dict[tuple[str, str], float] = {}
    fallback_triggered: set[tuple[str, str]] = set()

    try:
        while True:
            event = ws.wait_encounter(timeout=1.0)
            if event is None:
                if online_fallback_delay > 0:
                    fallback = _online_fallback_event(
                        ws,
                        online_since,
                        fallback_triggered,
                        online_fallback_delay,
                    )
                    if fallback is None:
                        continue
                    event = fallback
                else:
                    continue

            persona_a, persona_b = _ordered_personas(event)
            key = _pair_key(persona_a, persona_b)
            now = time.time()
            last = last_finished.get(key, 0.0)

            if now - last < cooldown:
                remain = int(cooldown - (now - last))
                _log(
                    f"跳过重复相遇 {persona_a}<->{persona_b}，冷却剩余 {remain}s"
                )
                continue

            online = ws.wait_for_boards({persona_a, persona_b}, timeout=8.0)
            if not {persona_a, persona_b}.issubset(online):
                _log(
                    "收到相遇，但两块板还没同时在线："
                    f"{persona_a}, {persona_b}; online={', '.join(sorted(online)) or 'none'}"
                )
                continue

            _log(
                "开始自动 duel："
                f"python -m persona_agent duel --persona-a {persona_a} "
                f"--persona-b {persona_b} --rounds {rounds} "
                f"(rssi={event.rssi}, source={event.source})"
            )

            layout = DuelLayout(
                mode="dual-ws",
                pid_a=persona_a,
                pid_b=persona_b,
                ws=ws,
                ws_personas={persona_a, persona_b},
            )

            try:
                run_duel(
                    persona_a=persona_a,
                    persona_b=persona_b,
                    rounds=rounds,
                    use_hardware=True,
                    layout=layout,
                    close_layout=False,
                )
            except Exception as exc:
                _log(
                    f"自动 duel 失败 {persona_a}<->{persona_b}: {exc}"
                )
                ws.drain_encounters()
                continue

            last_finished[key] = time.time()
            ws.drain_encounters()
    finally:
        ws.stop()


def _ordered_personas(event: WsEncounterEvent) -> tuple[str, str]:
    # Prefer the reporting board as persona-a so the log mirrors the physical trigger.
    persona_a = event.persona_a
    persona_b = event.persona_b
    if persona_a == persona_b:
        return persona_a, persona_b
    return persona_a, persona_b


def _online_fallback_event(
    ws: WsTransport,
    online_since: dict[tuple[str, str], float],
    fallback_triggered: set[tuple[str, str]],
    delay: float,
) -> WsEncounterEvent | None:
    online = sorted(p for p in ws.boards if p and p != "unknown")
    if len(online) < 2:
        online_since.clear()
        fallback_triggered.clear()
        return None

    preferred = ("zhang_zong", "xiao_hong")
    if set(preferred).issubset(online):
        persona_a, persona_b = preferred
    else:
        persona_a, persona_b = online[:2]

    key = _pair_key(persona_a, persona_b)
    now = time.time()
    started = online_since.setdefault(key, now)

    for stale in list(online_since):
        if stale != key:
            online_since.pop(stale, None)
            fallback_triggered.discard(stale)

    if key in fallback_triggered:
        return None

    if now - started < delay:
        return None

    fallback_triggered.add(key)
    _log(
        "未收到 BLE ENCOUNTER，但两块板已同时在线，"
        f"用 fallback 触发：{persona_a}<->{persona_b}"
    )
    return WsEncounterEvent(
        persona_a=persona_a,
        persona_b=persona_b,
        rssi=-40,
        source="online-fallback",
        received_at=now,
    )
