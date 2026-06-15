import struct
import sys
import time
from datetime import datetime
from pathlib import Path
import pypinyin
import serial
import wave
import re


def _log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def _tick(label: str, t0: float, indent: int = 0) -> float:
    now = time.perf_counter()
    prefix = "  " * indent
    _log(f"{prefix}[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] {label} (+{now - t0:.3f}s)")
    return now

# =========================
# 配置
# =========================
SERIAL_PORT = "/dev/cu.usbmodem1101"
BAUDRATE = 2000000

PACKAGE_DIR = Path(__file__).parent
RESOURCE_DIR = PACKAGE_DIR / "pinyin_zhangzong"
VOICE_PACK_DIRS = {
    "zhang_zong": PACKAGE_DIR / "pinyin_zhangzong",
    "xiao_hong": PACKAGE_DIR / "pinyin_xiaohong",
}

# =========================
# 拼音资源
# =========================
characters = [
    'b', 'p', 'm', 'f', 'd', 't', 'n', 'l',
    'g', 'k', 'h', 'j', 'q', 'x',
    'zh', 'ch', 'sh', 'r',
    'z', 'c', 's', 'y', 'w',

    'a', 'o', 'e', 'i', 'u', 'ü',
    'ai', 'ei', 'ui', 'ao', 'ou',
    'iu', 'ie', 've', 'er',

    'an', 'en', 'in', 'un', 'ün',
    'ang', 'eng', 'ing', 'ong',

    'uang', 'uan', 'uai',
    'uo', 'ua', 'ia',
    'ian', 'iang', 'iong', 'iao'
]

yun_mu_list = [
    'a', 'o', 'e', 'i', 'u', 'ü',
    'ai', 'ei', 'ui', 'ao', 'ou',
    'iu', 'ie', 've', 'er',

    'an', 'en', 'in', 'un', 'ün',
    'ang', 'eng', 'ing', 'ong',

    'uang', 'uan', 'uai',
    'uo', 'ua', 'ia',
    'ian', 'iang', 'iong', 'iao'
]

# =========================
# 基础 wav 参数
# =========================
sample_file = wave.open(
    str(RESOURCE_DIR / "a.wav"),
    "rb"
)

sampleRate = sample_file.getframerate()
sample_file.close()
OUTPUT_SAMPLE_RATE = 16000

# =========================
# 加载资源
# =========================
def _read_wav_samples(wav_path: Path) -> tuple[list[int], int]:
    file = wave.open(
        str(wav_path),
        "rb"
    )

    samples = file.readframes(
        file.getnframes()
    )

    sw = file.getsampwidth()
    channels = file.getnchannels()
    sample_rate = file.getframerate()
    frame_width = (
        sw * channels
    )
    pcm = []

    for i in range(
        0,
        len(samples),
        frame_width
    ):
        pcm.append(
            struct.unpack(
                "<h",
                samples[i:i + sw]
            )[0]
        )

    file.close()
    return pcm, sample_rate


def trim_pack_sample(
    samples: list[int],
    sample_rate: int
) -> list[int]:
    if not samples:
        return samples

    peak = max(
        abs(s)
        for s in samples
    )

    if peak <= 0:
        return []

    threshold = max(
        80,
        int(peak * 0.04)
    )

    start = 0
    while (
        start < len(samples) and
        abs(samples[start]) < threshold
    ):
        start += 1

    end = len(samples) - 1
    while (
        end > start and
        abs(samples[end]) < threshold
    ):
        end -= 1

    pad = int(
        sample_rate * 0.003
    )
    start = max(
        0,
        start - pad
    )
    end = min(
        len(samples) - 1,
        end + pad
    )

    return samples[
        start:
        end + 1
    ]


def resize_sample(
    samples: list[int],
    target_len: int
) -> list[int]:
    if (
        not samples or
        target_len <= 0
    ):
        return samples

    if len(samples) == target_len:
        return samples

    resized = []

    for i in range(target_len):
        src_pos = i * (
            len(samples) - 1
        ) / max(
            1,
            target_len - 1
        )
        left = int(src_pos)
        right = min(
            left + 1,
            len(samples) - 1
        )
        frac = src_pos - left
        resized.append(
            int(
                samples[left] * (1.0 - frac) +
                samples[right] * frac
            )
        )

    return resized


def normalize_peak(
    samples: list[int],
    target_peak: int = 12000
) -> list[int]:
    if not samples:
        return samples

    peak = max(
        abs(s)
        for s in samples
    )

    if peak <= 0:
        return samples

    gain = (
        target_peak / peak
    )

    return [
        max(
            -32768,
            min(
                32767,
                int(s * gain)
            )
        )
        for s in samples
    ]


INITIAL_PHONEMES = {
    "b", "p", "m", "f",
    "d", "t", "n", "l",
    "g", "k", "h",
    "j", "q", "x",
    "zh", "ch", "sh", "r",
    "z", "c", "s",
    "y", "w",
}


def target_xiaohong_len(
    phoneme: str,
    sample_rate: int
) -> int:
    if phoneme in INITIAL_PHONEMES:
        seconds = 0.055
    elif phoneme in {"ang", "eng", "ing", "ong", "uang", "iang"}:
        seconds = 0.18
    elif phoneme in {"an", "en", "in", "un", "vn", "uan", "ian"}:
        seconds = 0.16
    else:
        seconds = 0.14

    return int(
        sample_rate * seconds
    )


def target_xiaohong_peak(
    phoneme: str
) -> int:
    if phoneme in INITIAL_PHONEMES:
        return 7000
    return 14000


def preprocess_voice_sample(
    persona_id: str,
    phoneme: str,
    samples: list[int],
    sample_rate: int
) -> list[int]:
    if persona_id != "xiao_hong":
        return samples

    active = trim_pack_sample(
        samples,
        sample_rate
    )

    target_len = target_xiaohong_len(
        phoneme,
        sample_rate
    )

    return normalize_peak(
        resize_sample(
            active,
            target_len
        ),
        target_peak=target_xiaohong_peak(
            phoneme
        )
    )


def load_voice_pack(
    persona_id: str,
    folder: Path
) -> dict:
    sounds = {}
    sample_rate = sampleRate

    print(f"Loading voice pack {persona_id} from {folder}...")

    for wav_path in folder.glob("*.wav"):
        pcm, sample_rate = _read_wav_samples(
            wav_path
        )
        sounds[wav_path.stem] = preprocess_voice_sample(
            persona_id,
            wav_path.stem,
            pcm,
            sample_rate
        )

    print(f"Voice pack {persona_id} ready ({len(sounds)} phonemes @ {sample_rate} Hz).")

    return {
        "sounds": sounds,
        "sample_rate": sample_rate,
    }


voice_packs = {}

for persona_id, folder in VOICE_PACK_DIRS.items():
    if folder.exists():
        voice_packs[persona_id] = load_voice_pack(
            persona_id,
            folder
        )

characters_sound = voice_packs.get(
    "zhang_zong",
    {"sounds": {}}
)["sounds"]

# =========================
# 拼音处理
# =========================
def get_pinyin(text):

    text = re.sub(
        r"[a-zA-Z]",
        " ",
        text
    )

    return pypinyin.slug(
        text,
        separator=" "
    )


def split_pinyin(py):

    if py in yun_mu_list:
        return None, py

    if py.startswith("zh"):
        return "zh", py[2:]

    if py.startswith("ch"):
        return "ch", py[2:]

    if py.startswith("sh"):
        return "sh", py[2:]

    sheng_mu = py[0]
    yun_mu = py[1:]

    # ü 特殊处理
    if py in [
        "ju", "qu", "xu", "yu"
    ]:
        yun_mu = "ü"

    elif py in [
        "jue", "que",
        "xue", "yue"
    ]:
        yun_mu = "ve"

    elif py in [
        "jun", "qun",
        "xun", "yun"
    ]:
        yun_mu = "ün"

    return sheng_mu, yun_mu


def resolve_phoneme(
    sounds: dict,
    phoneme: str | None
) -> str | None:
    if not phoneme:
        return None

    candidates = [
        phoneme
    ]

    aliases = {
        "ü": ["v"],
        "ün": ["vn"],
        "iu": ["iou"],
        "iong": ["ong"],
    }

    candidates.extend(
        aliases.get(
            phoneme,
            []
        )
    )

    for candidate in candidates:
        if candidate in sounds:
            return candidate

    return None


# =========================
# PCM 合成
# =========================
secondPosition = 0.5
connectionPosition = 0.005

connectionSamples = round(
    connectionPosition *
    sampleRate
)


def handle_input(
    text,
    persona_id: str = "zhang_zong"
):

    t0 = time.perf_counter()
    voice_pack = voice_packs.get(
        persona_id,
        voice_packs.get(
            "zhang_zong",
            {
                "sounds": characters_sound,
                "sample_rate": sampleRate,
            }
        )
    )
    sounds = voice_pack["sounds"]
    pack_sample_rate = voice_pack["sample_rate"]

    py_list = get_pinyin(text).split()
    t0 = _tick(f"handle_input[{persona_id}]: 转拼音 ({len(py_list)} 音节)", t0, indent=2)

    audio = []
    last_is_py = False
    connection_samples = round(
        connectionPosition *
        pack_sample_rate
    )

    for py in py_list:
        if not py:
            continue

        if not py[0].isalpha():

            blank_num = int(
                pack_sample_rate * 0.15
            )

            audio.extend(
                [0] * blank_num
            )

            last_is_py = False
            continue

        sheng_mu, yun_mu = split_pinyin(py)

        resolved_yun_mu = resolve_phoneme(
            sounds,
            yun_mu
        )

        if resolved_yun_mu is None:
            print(
                "missing yunmu:",
                yun_mu
            )
            continue

        sheng_samples = []
        resolved_sheng_mu = resolve_phoneme(
            sounds,
            sheng_mu
        )

        if (
            resolved_sheng_mu
        ):
            sheng_samples = (
                sounds[
                    resolved_sheng_mu
                ]
            )

        yun_samples = (
            sounds[
                resolved_yun_mu
            ]
        )

        yun_pos = 0

        if sheng_samples:
            yun_pos = int(
                len(
                    sheng_samples
                ) *
                secondPosition
            )

        word = []

        total_len = (
            yun_pos +
            len(yun_samples)
        )

        for i in range(total_len):

            a = 0
            b = 0

            if (
                sheng_samples and
                i < len(sheng_samples)
            ):
                a = sheng_samples[i]

            if (
                i >= yun_pos and
                i - yun_pos <
                len(yun_samples)
            ):
                b = yun_samples[
                    i - yun_pos
                ]

            word.append(a + b)

        # overlap 拼接
        if (
            last_is_py and
            len(audio) >
            connection_samples
        ):

            overlap = min(
                connection_samples,
                len(word)
            )

            start = (
                len(audio) -
                overlap
            )

            for j in range(overlap):

                mixed = (
                    audio[start + j]
                    + word[j]
                )

                audio[start + j] = max(
                    -32768,
                    min(32767, mixed)
                )

            audio.extend(
                word[overlap:]
            )

        else:
            audio.extend(word)

        last_is_py = True

    _tick(f"handle_input[{persona_id}]: PCM 合成完成 ({len(audio)} samples, {len(audio)/pack_sample_rate:.2f}s 音频)", t0, indent=2)
    target_peak_by_persona = {
        "zhang_zong": 17000,
        "xiao_hong": 16000,
    }

    target_peak = target_peak_by_persona.get(
        persona_id
    )

    if target_peak:
        audio = normalize_peak(
            audio,
            target_peak=target_peak
        )

    return audio, pack_sample_rate


def resample_samples(samples, source_rate, target_rate):
    if source_rate == target_rate or not samples:
        return samples

    target_len = max(
        1,
        int(len(samples) * target_rate / source_rate)
    )

    resampled = []

    for i in range(target_len):
        src_pos = i * source_rate / target_rate
        left = int(src_pos)
        right = min(
            left + 1,
            len(samples) - 1
        )
        frac = src_pos - left
        value = int(
            samples[left] * (1.0 - frac) +
            samples[right] * frac
        )
        resampled.append(value)

    return resampled


def samples_to_pcm_bytes(
    samples,
    source_rate: int,
    output_rate: int = OUTPUT_SAMPLE_RATE,
) -> tuple[bytes, int]:
    samples = resample_samples(
        samples,
        source_rate,
        output_rate
    )

    pcm_bytes = b"".join(
        struct.pack(
            "<h",
            max(
                -32768,
                min(32767, s)
            )
        )
        for s in samples
    )

    return pcm_bytes, output_rate


def synthesize_pcm_bytes(
    text: str,
    persona_id: str = "zhang_zong",
) -> tuple[bytes, int]:
    if not text:
        return b"", OUTPUT_SAMPLE_RATE

    samples, source_rate = handle_input(
        text,
        persona_id=persona_id
    )

    if not samples:
        return b"", OUTPUT_SAMPLE_RATE

    return samples_to_pcm_bytes(
        samples,
        source_rate,
        OUTPUT_SAMPLE_RATE
    )

# =========================
# M5 串口音频
# =========================
class M5SerialAudioOutput:

    def __init__(self, ser):

        self.ser = ser
        _log("M5 connected")

    def play_raw(
        self,
        samples,
        source_rate=sampleRate
    ):

        t0 = time.perf_counter()
        CHUNK = 64
        pcm_bytes, output_rate = samples_to_pcm_bytes(
            samples,
            source_rate,
            OUTPUT_SAMPLE_RATE
        )
        t0 = _tick(f"play_raw: PCM 打包 ({len(pcm_bytes)} bytes)", t0, indent=2)

        # 协议头
        self.ser.write(
            b"PC2" +
            struct.pack(
                "<I",
                output_rate
            ) +
            struct.pack(
                "<I",
                len(pcm_bytes)
            )            
        )
        self.ser.flush()
        t0 = _tick(f"play_raw: 发送 PCM 协议头 ({output_rate} Hz)", t0, indent=2)

        ready_deadline = time.time() + 15.0
        got_ready = False
        while time.time() < ready_deadline:
            if not self.ser.in_waiting:
                time.sleep(0.02)
                continue
            raw = self.ser.readline()
            if not raw:
                continue
            text = raw.decode("utf-8", errors="replace").strip()
            if text:
                _log(f"  M5: {text}")
            if "PCM READY" in text:
                got_ready = True
                break
        if not got_ready:
            raise serial.SerialException(
                "M5 did not become ready for PCM data (timeout 15s; "
                "ensure TXT finished on device before PCM)"
            )

        sent = 0

        while sent < len(pcm_bytes):

            chunk = pcm_bytes[
                sent:
                sent + CHUNK
            ]

            self.ser.write(chunk)
            self.ser.flush()
            time.sleep(0.003)

            sent += len(chunk)

        self.ser.flush()
        _tick(f"play_raw: 串口发送音频数据 ({len(pcm_bytes)} bytes)", t0, indent=2)

        from persona_agent.hardware import wait_playback_done

        wait_playback_done(self.ser, len(pcm_bytes), output_rate)
        return len(pcm_bytes), output_rate

    def close(self):
        self.ser.close()


# =========================
# 单例
# =========================
_m5 = None


def get_m5(ser):

    global _m5

    if _m5 is None:
        _m5 = M5SerialAudioOutput(ser)

    return _m5


def reset_m5() -> None:
    global _m5
    _m5 = None


# =========================
# 外部调用函数
# =========================
def speak(
    text: str,
    ser,
    persona_id: str = "zhang_zong"
) -> dict[str, float]:
    """合成并播放；返回各阶段耗时（秒）。"""
    timings: dict[str, float] = {}
    if not text:
        return timings

    t_round = time.perf_counter()
    t0 = time.perf_counter()
    samples, source_rate = handle_input(
        text,
        persona_id=persona_id
    )

    timings["synthesis"] = time.perf_counter() - t0

    if not samples:
        _tick("speak: 无音频样本，跳过", t_round, indent=1)
        return timings

    t0 = time.perf_counter()
    m5 = get_m5(ser)
    timings["m5_init"] = time.perf_counter() - t0

    t0 = time.perf_counter()
    m5.play_raw(
        samples,
        source_rate
    )
    timings["serial_play"] = time.perf_counter() - t0

    timings["speak_total"] = time.perf_counter() - t_round
    _tick(f"speak 合计 ({timings['speak_total']:.3f}s)", t_round, indent=1)
    return timings
