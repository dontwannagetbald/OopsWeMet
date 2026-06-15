# 小红书风格像素 RPG 社交 App

Flutter 客户端，支持 WebSocket 实时对话，可在 iPhone 上调试。

## 技术栈总览

| 层级 | 选型 | 作用 |
|------|------|------|
| UI 框架 | **Flutter 3.44** | 跨平台，iOS/Android |
| 状态管理 | **Riverpod 3** | 实时消息、多页面状态同步 |
| 路由 | **go_router** | 声明式导航（初始化 / 状态 / 世界 / 日志 / 对话详情） |
| 实时通信 | **web_socket_channel** | WebSocket 收发，驱动对话列表更新 |
| REST API | **dio** | 登录、用户资料、历史记录拉取 |
| 本地存储 | **hive** + **shared_preferences** | 聊天记录缓存、用户配置 |
| JSON | **json_annotation** + **json_serializable** | 消息/用户模型序列化 |
| 图片 | **cached_network_image** | 头像、礼物、场景图 |
| 字体 | **google_fonts** | 像素风字体（如 Press Start 2P） |
| 工具 | **intl** / **uuid** / **equatable** / **logger** / **connectivity_plus** | 时间格式、ID、模型比较、日志、网络状态 |

## 项目结构（推荐）

```
lib/
├── main.dart                 # 入口，ProviderScope + GoRouter
├── app.dart                  # MaterialApp、主题（橙黑像素风）
├── core/
│   ├── config/               # API / WebSocket 地址
│   ├── theme/                # 颜色、像素字体、组件样式
│   ├── network/
│   │   ├── dio_client.dart   # HTTP
│   │   └── ws_client.dart    # WebSocket 连接、重连、心跳
│   └── utils/
├── data/
│   ├── models/               # ChatMessage, UserProfile, Encounter...
│   ├── repositories/         # chat_repository, user_repository
│   └── local/                # Hive boxes
├── features/
│   ├── onboarding/           # [YOUR ID] 初始化页
│   ├── status/               # [ME+] 状态页
│   ├── world/                # [WORLD] 场景与仓库
│   ├── log/                  # [LOG] 列表与对话详情
│   └── chat/                 # WebSocket 逻辑、消息 Provider
└── shared/widgets/           # 像素按钮、滑块、头像格
```

## WebSocket 数据流

```
后端 WS ──► WsClient ──► ChatRepository ──► chatMessagesProvider
                                              │
                                              ▼
                                    LogDetailPage (ListView)
```

- 收到新消息 → `ref.invalidate` / `state = [...state, msg]` → UI 自动滚动到底部
- 断线 → `connectivity_plus` + 指数退避重连

## 页面与路由

| 路由 | 页面 | 说明 |
|------|------|------|
| `/onboarding` | 初始化 | 姓名、邮箱、年龄、性格滑块 |
| `/status` | [ME+] | 头像、MBTI、情绪条、平行宇宙场景 |
| `/world` | [WORLD] | 场景网格、礼物仓库 |
| `/log` | [LOG] | 遭遇列表、会话摘要 |
| `/log/:id` | 对话详情 | 文本日志格式 + **WebSocket 实时追加** |

## 环境准备（iPhone 真机调试）

1. **Flutter SDK**（已安装于 `~/development/flutter`）  
   在 `~/.zshrc` 中加入：
   ```bash
   export PATH="$HOME/development/flutter/bin:$PATH"
   ```

2. **Xcode**（当前未完整安装）  
   - 从 App Store 安装 Xcode  
   - 执行：
     ```bash
     sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
     sudo xcodebuild -runFirstLaunch
     ```
   - 打开 Xcode → Settings → Accounts，登录 Apple ID（免费账号即可真机调试）

3. **CocoaPods**（iOS 依赖）
   ```bash
   sudo gem install cocoapods
   ```

4. **运行**
   ```bash
   cd /Users/root1/workspace/xiaohongshu_app
   flutter pub get
   flutter devices          # 确认 iPhone 已连接
   flutter run -d <device_id>
   ```

## 常用命令

```bash
flutter pub get
dart run build_runner build --delete-conflicting-outputs   # 生成 JSON / Riverpod 代码
flutter analyze
flutter run
```

## 配置 WebSocket 地址

在 `lib/core/config/env.dart`（待创建）中设置：

```dart
const wsBaseUrl = 'wss://your-api.example.com/ws';
const apiBaseUrl = 'https://your-api.example.com';
```

## 设计参考

`assets/images/design_reference.png`
