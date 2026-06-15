# Persona Agent

从 `skills/<persona_id>/` 动态加载人设，支持 HTTP 服务、终端对话、硬件串口选人设、双人对话。

## 目录

```
persona-agent/
├── skills/              人设库（每人设一目录 + meta.json）
├── persona_agent/       Python 包
│   ├── registry.py      人设注册表
│   ├── server.py        HTTP API
│   ├── bridge.py        串口 + API（硬件 PERSONA|id）
│   └── duel.py          双人对话
└── serve.sh
```

## 安装

```bash
cd persona-agent
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env   # 填入 OPENAI_API_KEY
```

## 用法

### 列出可用人设

```bash
python -m persona_agent list
```

### 终端对话（指定人设）

```bash
python -m persona_agent chat --persona zhang_zong
# 运行中可用 /persona xiao_hong 切换
```

### HTTP 服务

```bash
./serve.sh
curl http://127.0.0.1:8765/v1/personas
curl -s http://127.0.0.1:8765/v1/chat \
  -H 'Content-Type: application/json' \
  -d '{"persona_id":"zhang_zong","message":"你好"}'
```

### 硬件桥接（串口选人设）

设备发送 `PERSONA|zhang_zong` 或 `SEL|xiao_hong` 后，再输入用户话：

```bash
python -m persona_agent bridge
```

### 双人对话（自动单板 / 双板）

- **插 1 块 M5** → 单板模式：同一块板显示 DUEL 场景 + 轮流播音
- **插 2 块 M5** → 双板模式：两块同屏显示，各自播对应 agent 的音频

```bash
python -m persona_agent duel --persona-a zhang_zong --persona-b xiao_hong --rounds 2

# 先测 LLM 对话逻辑（不占用串口）
python -m persona_agent duel --rounds 1 --no-hardware

# 不测 LLM，只测硬件 DUEL + 音频
python -m persona_agent duel --hardware-smoke
```

## 环境变量

| 变量 | 说明 |
|------|------|
| `DEFAULT_PERSONA_ID` | 默认人设，如 `zhang_zong` |
| `PERSONA_SKILLS_DIR` | skills 根目录，默认 `skills` |
| `DUEL_PERSONA_A` / `DUEL_PERSONA_B` | duel 默认两人设 |
| `AGENT_HOST` / `AGENT_PORT` | HTTP 监听 |
| `SERIAL_PORT` | M5 串口 |

## 新增人设

1. 在 `skills/` 下新建目录，如 `skills/my_hero/`
2. 添加 `meta.json`：`{"name":"显示名","slug":"my_hero"}`
3. 添加 `SKILL.md` 和/或 `assets/*.md`
4. `python -m persona_agent list` 确认出现后即可用
