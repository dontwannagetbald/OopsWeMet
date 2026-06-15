"""WebSocket transport for M5 boards and the browser dashboard."""

from __future__ import annotations

import asyncio
import base64
import contextlib
import json
import os
import queue
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Any


def _log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


@dataclass(frozen=True)
class WsEncounterEvent:
    persona_a: str
    persona_b: str
    rssi: int
    source: str
    received_at: float


@dataclass
class WsTransport:
    host: str = field(
        default_factory=lambda: os.getenv("M5_WS_HOST", "0.0.0.0").strip()
    )
    port: int = field(
        default_factory=lambda: int(os.getenv("M5_WS_PORT", "8765"))
    )
    boards: dict[str, Any] = field(default_factory=dict)
    dashboards: set[Any] = field(default_factory=set)
    _loop: asyncio.AbstractEventLoop | None = None
    _server: Any = None
    _thread: threading.Thread | None = None
    _audio_done: dict[str, threading.Event] = field(default_factory=dict)
    _encounters: queue.Queue[WsEncounterEvent] = field(default_factory=queue.Queue)

    def start(self) -> bool:
        try:
            import websockets  # noqa: F401
        except ImportError:
            _log("WebSocket disabled: pip install websockets")
            return False

        if self._thread and self._thread.is_alive():
            return True

        ready = threading.Event()

        def runner() -> None:
            loop = asyncio.new_event_loop()
            self._loop = loop
            asyncio.set_event_loop(loop)
            try:
                loop.run_until_complete(self._start_async())
            except Exception as exc:
                _log(f"WebSocket server failed: {exc}")
                ready.set()
                return
            ready.set()
            loop.run_forever()
            pending = [
                task for task in asyncio.all_tasks(loop)
                if not task.done()
            ]
            for task in pending:
                task.cancel()
            if pending:
                with contextlib.suppress(Exception):
                    loop.run_until_complete(
                        asyncio.gather(
                            *pending,
                            return_exceptions=True,
                        )
                    )
            with contextlib.suppress(Exception):
                loop.run_until_complete(loop.shutdown_asyncgens())
            loop.close()

        self._thread = threading.Thread(target=runner, daemon=True)
        self._thread.start()
        ready.wait(timeout=3.0)
        return self._server is not None

    async def _start_async(self) -> None:
        import websockets

        self._server = await websockets.serve(
            self._handler,
            self.host,
            self.port,
        )
        _log(f"WebSocket server on ws://{self.host}:{self.port}")

    def stop(self) -> None:
        if not self._loop:
            return
        fut = asyncio.run_coroutine_threadsafe(self._stop_async(), self._loop)
        with contextlib.suppress(Exception):
            fut.result(timeout=3.0)
        self._loop.call_soon_threadsafe(self._loop.stop)

    async def _stop_async(self) -> None:
        if self._server:
            self._server.close()
            await self._server.wait_closed()
            self._server = None
        for ws in list(self.boards.values()):
            with contextlib.suppress(Exception):
                await ws.close()
        for ws in list(self.dashboards):
            with contextlib.suppress(Exception):
                await ws.close()
        self.boards.clear()
        self.dashboards.clear()

    async def _handler(self, ws: Any) -> None:
        persona: str | None = None
        is_dashboard = False
        try:
            async for raw in ws:
                msg = str(raw).strip()
                if msg.startswith("HELLO|"):
                    persona = msg[6:].strip() or "unknown"
                    self.boards[persona] = ws
                    _log(
                        f"[ws] board online: {persona} "
                        f"(online: {', '.join(self.boards) or 'none'})"
                    )
                    await self.broadcast_status()
                    continue
                if msg.startswith("PERSONA|"):
                    new_persona = msg[8:].strip() or persona or "unknown"
                    if persona and self.boards.get(persona) is ws:
                        self.boards.pop(persona, None)
                    persona = new_persona
                    self.boards[persona] = ws
                    await self.broadcast_status()
                    continue
                if msg.startswith("DASHBOARD"):
                    is_dashboard = True
                    self.dashboards.add(ws)
                    await self.broadcast_status()
                    continue
                if msg.startswith("AUDIO_DONE|"):
                    done_persona = msg[11:].strip() or persona or "unknown"
                    event = self._audio_done.setdefault(
                        done_persona,
                        threading.Event(),
                    )
                    event.set()
                    _log(f"[ws] audio done: {done_persona}")
                    continue
                if msg.startswith("ENCOUNTER|"):
                    event = self._parse_encounter(msg, persona)
                    if event is not None:
                        self._encounters.put(event)
                        _log(
                            "[ws] encounter: "
                            f"{event.persona_a}<->{event.persona_b} "
                            f"rssi={event.rssi} source={event.source}"
                        )
                        await self._broadcast_async(
                            {
                                "type": "encounter_detected",
                                "a_persona": event.persona_a,
                                "b_persona": event.persona_b,
                                "rssi": event.rssi,
                                "source": event.source,
                            }
                        )
                    continue
        except Exception:
            pass
        finally:
            if persona and self.boards.get(persona) is ws:
                self.boards.pop(persona, None)
                _log(f"[ws] board offline: {persona}")
            if is_dashboard:
                self.dashboards.discard(ws)
            await self.broadcast_status()

    def wait_for_boards(
        self,
        personas: set[str],
        timeout: float = 8.0,
    ) -> set[str]:
        deadline = time.perf_counter() + timeout
        while time.perf_counter() < deadline:
            online = set(self.boards)
            if personas.issubset(online):
                return online
            time.sleep(0.1)
        return set(self.boards)

    def send_board(self, persona: str, cmd: str) -> bool:
        if not self._loop:
            return False
        fut = asyncio.run_coroutine_threadsafe(
            self._send_board_async(persona, cmd),
            self._loop,
        )
        try:
            return bool(fut.result(timeout=3.0))
        except Exception as exc:
            _log(f"[ws] send failed -> {persona}: {exc}")
            return False

    def send_audio(
        self,
        persona: str,
        pcm_bytes: bytes,
        sample_rate: int,
        *,
        chunk_size: int = 4096,
    ) -> bool:
        if not pcm_bytes:
            return True

        event = self._audio_done.setdefault(
            persona,
            threading.Event(),
        )
        event.clear()

        if not self.send_board(
            persona,
            f"PCMWSBEGIN|{sample_rate}|{len(pcm_bytes)}",
        ):
            return False

        for offset in range(0, len(pcm_bytes), chunk_size):
            chunk = pcm_bytes[offset:offset + chunk_size]
            payload = base64.b64encode(chunk).decode("ascii")
            if not self.send_board(persona, f"PCMWSCHUNK|{payload}"):
                return False

        return self.send_board(persona, "PCMWSEND")

    def wait_audio_done(self, persona: str, timeout: float = 20.0) -> bool:
        event = self._audio_done.setdefault(
            persona,
            threading.Event(),
        )
        return event.wait(timeout=timeout)

    def wait_encounter(
        self,
        timeout: float = 1.0,
    ) -> WsEncounterEvent | None:
        try:
            return self._encounters.get(timeout=timeout)
        except queue.Empty:
            return None

    def drain_encounters(self) -> None:
        while True:
            try:
                self._encounters.get_nowait()
            except queue.Empty:
                return

    def _parse_encounter(
        self,
        msg: str,
        source_persona: str | None,
    ) -> WsEncounterEvent | None:
        parts = msg.split("|")
        if len(parts) < 4:
            return None

        persona_a = (parts[1].strip() or source_persona or "unknown")
        persona_b = parts[2].strip()
        if not persona_b:
            return None

        try:
            rssi = int(parts[3])
        except ValueError:
            rssi = -127

        return WsEncounterEvent(
            persona_a=persona_a,
            persona_b=persona_b,
            rssi=rssi,
            source=source_persona or persona_a,
            received_at=time.time(),
        )

    async def _send_board_async(self, persona: str, cmd: str) -> bool:
        ws = self.boards.get(persona)
        if not ws:
            return False
        try:
            await ws.send(cmd)
            _log(f"  -> {persona} via WS: {cmd[:60]}")
            return True
        except Exception as exc:
            _log(f"[ws] send failed -> {persona}: {exc}")
            self.boards.pop(persona, None)
            await self.broadcast_status()
            return False

    def broadcast(self, obj: dict[str, Any]) -> None:
        if not self._loop:
            return
        asyncio.run_coroutine_threadsafe(
            self._broadcast_async(obj),
            self._loop,
        )

    async def broadcast_status(self) -> None:
        await self._broadcast_async(
            {
                "type": "status",
                "peers": [
                    {
                        "id": persona,
                        "persona": persona,
                        "name": f"WS-{persona}",
                        "state": "online",
                        "rssi": -40,
                    }
                    for persona in sorted(self.boards)
                ],
            }
        )

    async def _broadcast_async(self, obj: dict[str, Any]) -> None:
        if not self.dashboards:
            return
        msg = json.dumps(obj, ensure_ascii=False)
        dead = []
        for ws in list(self.dashboards):
            try:
                await ws.send(msg)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.dashboards.discard(ws)
