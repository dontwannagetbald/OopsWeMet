import 'package:flutter/material.dart';
import 'dart:async';
import 'dart:convert';
import 'package:web_socket_channel/web_socket_channel.dart';

void main() {
  runApp(const XiaohongshuApp());
}

class LiveLogMessage {
  const LiveLogMessage({
    required this.sender,
    required this.time,
    required this.text,
  });

  final String sender;
  final String time;
  final String text;
}

class LiveLogStore extends ChangeNotifier {
  LiveLogStore._() {
    _connect();
  }

  static final LiveLogStore instance = LiveLogStore._();

  static const String _defaultWsUrl = String.fromEnvironment(
    'WS_URL',
    defaultValue: 'ws://192.168.1.199:8765',
  );

  StreamSubscription<dynamic>? _subscription;
  Timer? _reconnectTimer;
  bool _isConnecting = false;
  bool _isConnected = false;
  String _title = '[ WAITING FOR DUEL ]';
  final List<LiveLogMessage> _messages = [];

  bool get isConnected => _isConnected;
  String get title => _title;
  List<LiveLogMessage> get messages => List.unmodifiable(_messages);

  void _connect() {
    if (_isConnecting) {
      return;
    }
    _isConnecting = true;
    try {
      final channel = WebSocketChannel.connect(Uri.parse(_defaultWsUrl));
      _subscription = channel.stream.listen(
        _onData,
        onDone: _onDisconnected,
        onError: (_) => _onDisconnected(),
      );
      channel.sink.add('DASHBOARD');
      _isConnected = true;
      _isConnecting = false;
      notifyListeners();
    } catch (_) {
      _isConnecting = false;
      _scheduleReconnect();
    }
  }

  void _onData(dynamic raw) {
    if (raw is! String) {
      return;
    }
    Map<String, dynamic> payload;
    try {
      payload = jsonDecode(raw) as Map<String, dynamic>;
    } catch (_) {
      return;
    }

    final type = payload['type'] as String?;
    if (type == 'encounter') {
      final left = _formatPersona(payload['a_persona'] as String?);
      final right = _formatPersona(payload['b_persona'] as String?);
      _title = '[ WITH $left × $right ]';
      _messages.clear();
      notifyListeners();
      return;
    }

    if (type == 'reply') {
      final sender = _formatPersona(payload['persona'] as String?);
      final text = (payload['text'] as String?)?.trim() ?? '';
      if (text.isEmpty) {
        return;
      }
      _messages.add(
        LiveLogMessage(sender: sender, time: _formatNow(), text: text),
      );
      notifyListeners();
    }
  }

  void _onDisconnected() {
    _isConnected = false;
    _subscription?.cancel();
    _subscription = null;
    notifyListeners();
    _scheduleReconnect();
  }

  void _scheduleReconnect() {
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(const Duration(seconds: 3), _connect);
  }

  String _formatPersona(String? raw) {
    final value = (raw ?? '').trim();
    if (value.isEmpty) {
      return 'UNKNOWN';
    }
    return value.replaceAll('_', ' ').toUpperCase();
  }

  String _formatNow() {
    final now = DateTime.now();
    final y = now.year.toString().padLeft(4, '0');
    final m = now.month.toString().padLeft(2, '0');
    final d = now.day.toString().padLeft(2, '0');
    final h = now.hour.toString().padLeft(2, '0');
    final min = now.minute.toString().padLeft(2, '0');
    final sec = now.second.toString().padLeft(2, '0');
    return '$y.$m.$d · $h:$min:$sec';
  }
}

class XiaohongshuApp extends StatelessWidget {
  const XiaohongshuApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'Xiaohongshu App',
      builder: (context, child) {
        final mediaQuery = MediaQuery.of(context);
        return MediaQuery(
          data: mediaQuery.copyWith(textScaler: const TextScaler.linear(1.12)),
          child: child ?? const SizedBox.shrink(),
        );
      },
      theme: ThemeData(
        scaffoldBackgroundColor: PixelPalette.orange,
        useMaterial3: false,
      ),
      home: const OnboardingPage(),
    );
  }
}

class PixelPalette {
  static const Color orange = Color(0xFFED3B0B);
  static const Color yellow = Color(0xFFFFD52C);
  static const Color nearBlack = Color(0xFF101010);
  static const Color black = Color(0xFF000000);
  static const Color white = Color(0xFFFFFFFF);
  static const Color shadowBrown = Color(0xFF8D2308);
}

class PixelFonts {
  static const String title = 'SixtyfourConvergence';
  static const String body = 'AaHuanMengKongJianXiangSuTi';
}

class OnboardingPage extends StatefulWidget {
  const OnboardingPage({super.key});

  @override
  State<OnboardingPage> createState() => _OnboardingPageState();
}

class _OnboardingPageState extends State<OnboardingPage> {
  final _nameController = TextEditingController(text: 'JERRY ZHOU');
  final _emailController = TextEditingController(text: '1234567890@GMAIL.COM');

  String _month = '06';
  String _day = '16';
  String _year = '2003';

  double _social = 0.5;
  double _speech = 0.5;
  double _movement = 0.5;
  double _thinking = 0.5;

  final List<String> _months = List.generate(
    12,
    (index) => '${index + 1}'.padLeft(2, '0'),
  );
  final List<String> _days = List.generate(
    31,
    (index) => '${index + 1}'.padLeft(2, '0'),
  );
  final List<String> _years = List.generate(40, (index) => '${2003 - index}');

  @override
  void dispose() {
    _nameController.dispose();
    _emailController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final topInset = MediaQuery.paddingOf(context).top;

    return Scaffold(
      body: Column(
        children: [
          Expanded(
            child: SingleChildScrollView(
              child: Padding(
                padding: EdgeInsets.fromLTRB(16, topInset + 4, 16, 10),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const _OnboardingHeader(),
                    const SizedBox(height: 10),
                    const _SectionTitle('[Basic Information]'),
                    const SizedBox(height: 8),
                    const _FieldChip('[NAME]'),
                    const SizedBox(height: 4),
                    _PixelTextField(controller: _nameController),
                    const SizedBox(height: 8),
                    const _FieldChip('[EMAIL]'),
                    const SizedBox(height: 4),
                    _PixelTextField(controller: _emailController),
                    const SizedBox(height: 8),
                    const _FieldChip('[AGE]'),
                    const SizedBox(height: 4),
                    Row(
                      children: [
                        Expanded(
                          child: _PixelDropdown(
                            value: _month,
                            items: _months,
                            onChanged: (value) =>
                                setState(() => _month = value!),
                          ),
                        ),
                        const SizedBox(width: 10),
                        Expanded(
                          child: _PixelDropdown(
                            value: _day,
                            items: _days,
                            onChanged: (value) => setState(() => _day = value!),
                          ),
                        ),
                        const SizedBox(width: 10),
                        Expanded(
                          flex: 2,
                          child: _PixelDropdown(
                            value: _year,
                            items: _years,
                            onChanged: (value) =>
                                setState(() => _year = value!),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 10),
                    Container(height: 4, color: PixelPalette.black),
                    const SizedBox(height: 10),
                    const _SectionTitle('[Personality]'),
                    const SizedBox(height: 6),
                    _PixelSliderBlock(
                      label: '[SOCIAL ORIENTATION]',
                      leftLabel: 'Introversion',
                      rightLabel: 'Extraversion',
                      value: _social,
                      onChanged: (value) => setState(() => _social = value),
                    ),
                    const SizedBox(height: 8),
                    _PixelSliderBlock(
                      label: '[SPEECH]',
                      leftLabel: 'Empathetic',
                      rightLabel: 'Direct',
                      value: _speech,
                      onChanged: (value) => setState(() => _speech = value),
                    ),
                    const SizedBox(height: 8),
                    _PixelSliderBlock(
                      label: '[MOVEMENT]',
                      leftLabel: 'Casual',
                      rightLabel: 'Proactive',
                      value: _movement,
                      onChanged: (value) => setState(() => _movement = value),
                    ),
                    const SizedBox(height: 8),
                    _PixelSliderBlock(
                      label: '[THINKING]',
                      leftLabel: 'Imaginative',
                      rightLabel: 'Pragmatic',
                      value: _thinking,
                      onChanged: (value) => setState(() => _thinking = value),
                    ),
                    const SizedBox(height: 10),
                    Center(
                      child: _PixelButton(
                        label: 'DONE!',
                        onPressed: () {
                          Navigator.of(context).push(
                            MaterialPageRoute<void>(
                              builder: (_) => const StatusPage(),
                            ),
                          );
                        },
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
          const _PixelNoiseFooter(),
        ],
      ),
    );
  }
}

class StatusPage extends StatefulWidget {
  const StatusPage({super.key});

  @override
  State<StatusPage> createState() => _StatusPageState();
}

class _StatusPageState extends State<StatusPage> {
  // 0: status/home, 1: log
  int _activeTab = 0;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Column(
        children: [
          Expanded(
            child: IndexedStack(
              index: _activeTab,
              children: const [_StatusTabContent(), _LogTabContent()],
            ),
          ),
          _StatusBottomNav(
            activeTab: _activeTab,
            onTapHome: () => setState(() => _activeTab = 0),
            onTapLog: () => setState(() => _activeTab = 1),
          ),
        ],
      ),
    );
  }
}

class _StatusTabContent extends StatelessWidget {
  const _StatusTabContent();

  @override
  Widget build(BuildContext context) {
    final topInset = MediaQuery.paddingOf(context).top;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: EdgeInsets.fromLTRB(16, topInset + 4, 16, 0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  _PixelIconButton(
                    icon: Icons.menu,
                    onPressed: () => Navigator.of(context).pop(),
                  ),
                  const Spacer(),
                  const Icon(
                    Icons.play_arrow,
                    color: PixelPalette.black,
                    size: 22,
                  ),
                  const SizedBox(width: 6),
                  const Text(
                    '[ME+]',
                    style: TextStyle(
                      fontFamily: PixelFonts.body,
                      color: PixelPalette.black,
                      fontSize: 22,
                    ),
                  ),
                ],
              ),
              const Align(
                alignment: Alignment.centerRight,
                child: Text(
                  '[LOG]',
                  style: TextStyle(
                    fontFamily: PixelFonts.body,
                    color: PixelPalette.shadowBrown,
                    fontSize: 20,
                  ),
                ),
              ),
              const SizedBox(height: 6),
              const _SectionTitle('[ STATUS ]'),
              const SizedBox(height: 6),
              const _StatusCard(),
              const SizedBox(height: 8),
              const _SectionTitle('[ PARALLEL-UNIVERSE ]'),
              const SizedBox(height: 6),
            ],
          ),
        ),
        const Expanded(child: _StatusScenePanel()),
      ],
    );
  }
}

class _LogTabContent extends StatefulWidget {
  const _LogTabContent();

  @override
  State<_LogTabContent> createState() => _LogTabContentState();
}

class _LogTabContentState extends State<_LogTabContent> {
  int _expandedIndex = 0;

  static const List<Map<String, String>> _logSummaries = [
    {
      'title': '[ WITH ZHANGSHENG ]',
      'time': '2026.06.02 · 16:36:24',
      'avatar': 'assets/design/pages/log/avatars/zs.png',
    },
    {
      'title': '[ WITH BELLA ]',
      'time': '2026.06.02 · 18:03:11',
      'avatar': 'assets/design/pages/log/avatars/bella.png',
    },
    {
      'title': '[ WITH MICHEALZHANG ]',
      'time': '2026.06.02 · 19:27:45',
      'avatar': 'assets/design/pages/log/avatars/unknown.png',
    },
  ];

  @override
  Widget build(BuildContext context) {
    final topInset = MediaQuery.paddingOf(context).top;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: EdgeInsets.fromLTRB(16, topInset + 4, 16, 0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  _PixelIconButton(
                    icon: Icons.menu,
                    onPressed: () => Navigator.of(context).pop(),
                  ),
                  const Spacer(),
                  const Icon(
                    Icons.play_arrow,
                    color: PixelPalette.black,
                    size: 22,
                  ),
                  const SizedBox(width: 6),
                  const Text(
                    '[LOG]',
                    style: TextStyle(
                      fontFamily: PixelFonts.body,
                      color: PixelPalette.black,
                      fontSize: 22,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 6),
              const _SectionTitle('[ 2026.06.02 ]'),
              const SizedBox(height: 6),
              AspectRatio(
                aspectRatio: 860 / 430,
                child: _PixelAssetOrPlaceholder(
                  assetPath: 'assets/design/pages/log/people.png',
                  fit: BoxFit.contain,
                  placeholder: Container(
                    decoration: BoxDecoration(
                      color: PixelPalette.orange,
                      border: Border.all(color: PixelPalette.black, width: 4),
                    ),
                    alignment: Alignment.center,
                    child: const Text(
                      'people.png',
                      style: TextStyle(
                        fontFamily: PixelFonts.body,
                        fontSize: 12,
                        color: PixelPalette.black,
                      ),
                    ),
                  ),
                ),
              ),
              const SizedBox(height: 10),
              const _SectionTitle('[ LOG ]'),
              const SizedBox(height: 4),
            ],
          ),
        ),
        Expanded(
          child: ListView.separated(
            padding: const EdgeInsets.fromLTRB(16, 0, 16, 8),
            itemCount: _logSummaries.length,
            separatorBuilder: (context, index) => const SizedBox(height: 6),
            itemBuilder: (context, index) {
              final item = _logSummaries[index];
              return _LogSummaryItem(
                isExpanded: _expandedIndex == index,
                avatarAssetPath: item['avatar']!,
                title: item['title']!,
                time: item['time']!,
                onToggle: () {
                  setState(() {
                    _expandedIndex = _expandedIndex == index ? -1 : index;
                  });
                },
              );
            },
          ),
        ),
        Image.asset(
          'assets/design/pages/log/components/bottom.png',
          width: double.infinity,
          height: 74,
          fit: BoxFit.cover,
          alignment: Alignment.topCenter,
          errorBuilder: (_, _, _) => const SizedBox(height: 74),
        ),
      ],
    );
  }
}

class _LogSummaryItem extends StatelessWidget {
  const _LogSummaryItem({
    required this.title,
    required this.time,
    required this.avatarAssetPath,
    required this.isExpanded,
    required this.onToggle,
  });

  final String title;
  final String time;
  final String avatarAssetPath;
  final bool isExpanded;
  final VoidCallback onToggle;

  @override
  Widget build(BuildContext context) {
    return Container(
      color: PixelPalette.orange,
      padding: const EdgeInsets.fromLTRB(10, 8, 10, 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              InkWell(
                onTap: onToggle,
                child: SizedBox(
                  width: 16,
                  height: 16,
                  child: _PixelAssetOrPlaceholder(
                    assetPath: isExpanded
                        ? 'assets/design/pages/log/icons/arrow_down.png'
                        : 'assets/design/pages/log/icons/arrow.png',
                    fit: BoxFit.contain,
                    placeholder: Icon(
                      isExpanded
                          ? Icons.keyboard_arrow_down
                          : Icons.chevron_right,
                      size: 14,
                      color: PixelPalette.black,
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 6),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Container(
                      color: PixelPalette.yellow,
                      padding: const EdgeInsets.symmetric(
                        horizontal: 8,
                        vertical: 3,
                      ),
                      child: Text(
                        title,
                        style: const TextStyle(
                          fontFamily: PixelFonts.body,
                          fontSize: 13,
                          color: PixelPalette.black,
                        ),
                      ),
                    ),
                    const SizedBox(height: 4),
                    Text(
                      time,
                      style: const TextStyle(
                        fontFamily: PixelFonts.body,
                        fontSize: 9,
                        color: PixelPalette.shadowBrown,
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 6),
              SizedBox(
                width: 24,
                height: 24,
                child: _PixelAssetOrPlaceholder(
                  assetPath: 'assets/design/pages/log/icons/heart.png',
                  fit: BoxFit.contain,
                  placeholder: const Icon(
                    Icons.favorite,
                    size: 18,
                    color: PixelPalette.yellow,
                  ),
                ),
              ),
              const SizedBox(width: 6),
              InkWell(
                onTap: () {
                  Navigator.of(context).push(
                    MaterialPageRoute<void>(builder: (_) => LogDetailPage()),
                  );
                },
                child: SizedBox(
                  width: 40,
                  height: 24,
                  child: _PixelAssetOrPlaceholder(
                    assetPath: 'assets/design/pages/log/icons/detail.png',
                    fit: BoxFit.contain,
                    placeholder: const Icon(
                      Icons.more_horiz,
                      size: 18,
                      color: PixelPalette.black,
                    ),
                  ),
                ),
              ),
            ],
          ),
          if (isExpanded) ...[
            const SizedBox(height: 6),
            Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                SizedBox(
                  width: 42,
                  height: 42,
                  child: _PixelAssetOrPlaceholder(
                    assetPath: avatarAssetPath,
                    fit: BoxFit.fill,
                    placeholder: Container(
                      decoration: BoxDecoration(
                        color: PixelPalette.orange,
                        border: Border.all(color: PixelPalette.black, width: 3),
                      ),
                      child: const Icon(
                        Icons.person,
                        size: 18,
                        color: PixelPalette.black,
                      ),
                    ),
                  ),
                ),
                const SizedBox(width: 8),
                const Expanded(child: _LogPreviewContent()),
              ],
            ),
          ],
        ],
      ),
    );
  }
}

class _LogPreviewContent extends StatelessWidget {
  const _LogPreviewContent();

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: const [
        _LogPreviewLine(
          'ZS · 2026.06.02 · 16:36:24',
          '“你去过武康路那个新展吗？就是那个非常艺术的展览”',
        ),
        SizedBox(height: 5),
        _LogPreviewLine('JerryZ · 2026.06.02 · 16:37:14', '“还没有，值得专门去一趟吗？”'),
        SizedBox(height: 5),
        _LogPreviewLine(
          'ZS · 2026.06.02 · 16:37:21',
          '“看情况，你喜欢那种一开始完全看不懂的东西吗”',
        ),
      ],
    );
  }
}

class _LogPreviewLine extends StatelessWidget {
  const _LogPreviewLine(this.meta, this.text);

  final String meta;
  final String text;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          meta,
          style: const TextStyle(
            fontFamily: PixelFonts.body,
            fontSize: 9,
            color: PixelPalette.shadowBrown,
          ),
        ),
        Text(
          text,
          style: const TextStyle(
            fontFamily: PixelFonts.body,
            fontSize: 10,
            color: PixelPalette.black,
          ),
          maxLines: 2,
          overflow: TextOverflow.ellipsis,
        ),
      ],
    );
  }
}

class LogDetailPage extends StatefulWidget {
  LogDetailPage({super.key}) {
    LiveLogStore.instance;
  }

  @override
  State<LogDetailPage> createState() => _LogDetailPageState();
}

class _LogDetailPageState extends State<LogDetailPage> {
  late final ScrollController _messageScrollController;
  late final LiveLogStore _store;

  @override
  void initState() {
    super.initState();
    _store = LiveLogStore.instance;
    _messageScrollController = ScrollController();
    _store.addListener(_autoScrollToLatest);
  }

  @override
  void dispose() {
    _store.removeListener(_autoScrollToLatest);
    _messageScrollController.dispose();
    super.dispose();
  }

  void _autoScrollToLatest() {
    if (!_messageScrollController.hasClients) {
      return;
    }
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!_messageScrollController.hasClients) {
        return;
      }
      _messageScrollController.animateTo(
        _messageScrollController.position.maxScrollExtent,
        duration: const Duration(milliseconds: 240),
        curve: Curves.easeOut,
      );
    });
  }

  @override
  Widget build(BuildContext context) {
    final topInset = MediaQuery.paddingOf(context).top;

    return Scaffold(
      body: Column(
        children: [
          Expanded(
            child: Padding(
              padding: EdgeInsets.fromLTRB(10, topInset + 4, 10, 12),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  InkWell(
                    onTap: () => Navigator.of(context).pop(),
                    child: ConstrainedBox(
                      constraints: const BoxConstraints(maxHeight: 220),
                      child: _PixelAssetOrPlaceholder(
                        assetPath:
                            'assets/design/pages/log_detail/components/zs.png',
                        fit: BoxFit.cover,
                        placeholder: Container(
                          constraints: const BoxConstraints(minHeight: 180),
                          decoration: BoxDecoration(
                            color: PixelPalette.orange,
                            border: Border.all(
                              color: PixelPalette.black,
                              width: 4,
                            ),
                          ),
                          alignment: Alignment.center,
                          child: const Text(
                            'Tap image to go back',
                            style: TextStyle(
                              fontFamily: PixelFonts.body,
                              fontSize: 12,
                              color: PixelPalette.black,
                            ),
                          ),
                        ),
                      ),
                    ),
                  ),
                  const SizedBox(height: 8),
                  const _SectionTitle('[ LOG ]'),
                  const SizedBox(height: 6),
                  AnimatedBuilder(
                    animation: _store,
                    builder: (context, _) => _FieldChip(_store.title),
                  ),
                  const SizedBox(height: 8),
                  Expanded(
                    child: Scrollbar(
                      controller: _messageScrollController,
                      thumbVisibility: true,
                      child: AnimatedBuilder(
                        animation: _store,
                        builder: (context, _) {
                          if (_store.messages.isEmpty) {
                            return Center(
                              child: Text(
                                _store.isConnected
                                    ? 'Waiting for new duel messages...'
                                    : 'Connecting to WebSocket...',
                                style: const TextStyle(
                                  fontFamily: PixelFonts.body,
                                  color: PixelPalette.shadowBrown,
                                  fontSize: 13,
                                ),
                              ),
                            );
                          }
                          return ListView.builder(
                            controller: _messageScrollController,
                            padding: const EdgeInsets.only(bottom: 8),
                            itemCount: _store.messages.length,
                            itemBuilder: (context, index) {
                              final item = _store.messages[index];
                              return Padding(
                                padding: const EdgeInsets.only(bottom: 14),
                                child: _LogDetailMessage(
                                  sender: item.sender,
                                  time: item.time,
                                  text: item.text,
                                ),
                              );
                            },
                          );
                        },
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
          const _PixelNoiseFooter(),
        ],
      ),
    );
  }
}

class _LogDetailMessage extends StatelessWidget {
  const _LogDetailMessage({
    required this.sender,
    required this.time,
    required this.text,
  });

  final String sender;
  final String time;
  final String text;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          '$sender · $time',
          style: const TextStyle(
            fontFamily: PixelFonts.body,
            color: PixelPalette.shadowBrown,
            fontSize: 12,
          ),
        ),
        const SizedBox(height: 4),
        Text(
          text,
          style: const TextStyle(
            fontFamily: PixelFonts.body,
            color: PixelPalette.black,
            fontSize: 15,
            height: 1.35,
          ),
        ),
      ],
    );
  }
}

class WorldPage extends StatelessWidget {
  const WorldPage({super.key});

  static const _sceneAssets = [
    'assets/design/pages/world/scenes/bedroom1.png',
    'assets/design/pages/world/scenes/bedroom2.png',
    'assets/design/pages/world/scenes/coffee_shop.png',
    'assets/design/pages/world/scenes/boss_office.png',
  ];

  @override
  Widget build(BuildContext context) {
    final topInset = MediaQuery.paddingOf(context).top;

    return Scaffold(
      body: Column(
        children: [
          Expanded(
            child: SingleChildScrollView(
              child: Padding(
                padding: EdgeInsets.fromLTRB(10, topInset + 0, 10, 56),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        _PixelIconButton(
                          icon: Icons.arrow_back_ios_new,
                          onPressed: () => Navigator.of(context).pop(),
                        ),
                        const Spacer(),
                        const Icon(
                          Icons.play_arrow,
                          color: PixelPalette.black,
                          size: 22,
                        ),
                        const SizedBox(width: 6),
                        const Text(
                          '[ WORLD ]',
                          style: TextStyle(
                            fontFamily: PixelFonts.body,
                            color: PixelPalette.black,
                            fontSize: 32,
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 12),
                    const _SectionTitle('[ SCENE ]'),
                    const SizedBox(height: 15),
                    GridView.builder(
                      padding: EdgeInsets.zero,
                      shrinkWrap: true,
                      physics: const NeverScrollableScrollPhysics(),
                      itemCount: _sceneAssets.length,
                      gridDelegate:
                          const SliverGridDelegateWithFixedCrossAxisCount(
                            crossAxisCount: 2,
                            crossAxisSpacing: 8,
                            mainAxisSpacing: 8,
                            childAspectRatio: 207 / 148,
                          ),
                      itemBuilder: (context, index) {
                        return _PixelAssetOrPlaceholder(
                          assetPath: _sceneAssets[index],
                          fit: BoxFit.fill,
                          placeholder: _WorldAssetPlaceholder(
                            label: 'SCENE ${index + 1}',
                          ),
                        );
                      },
                    ),
                    const SizedBox(height: 15),
                    const _SectionTitle('[ WAREHOUSE ]'),
                    const SizedBox(height: 15),
                    AspectRatio(
                      aspectRatio: 440 / 280,
                      child: _PixelAssetOrPlaceholder(
                        assetPath:
                            'assets/design/pages/world/gifts/WAREHOUSE.png',
                        fit: BoxFit.fill,
                        placeholder: const _WorldAssetPlaceholder(
                          label: 'WAREHOUSE',
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
          const _PixelNoiseFooter(),
        ],
      ),
    );
  }
}

class _WorldAssetPlaceholder extends StatelessWidget {
  const _WorldAssetPlaceholder({required this.label});

  final String label;

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: const Color(0xFFF2D9CA),
        border: Border.all(color: PixelPalette.black, width: 4),
      ),
      child: Center(
        child: Text(
          label,
          style: const TextStyle(
            fontFamily: PixelFonts.body,
            fontSize: 12,
            color: PixelPalette.black,
          ),
        ),
      ),
    );
  }
}

class _OnboardingHeader extends StatelessWidget {
  const _OnboardingHeader();

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        _PixelIconButton(icon: Icons.arrow_back_ios_new, onPressed: () {}),
        const Spacer(),
        const Icon(Icons.play_arrow, color: PixelPalette.black, size: 22),
        const SizedBox(width: 6),
        const Text(
          '[YOUR ID]',
          style: TextStyle(
            fontFamily: PixelFonts.body,
            color: PixelPalette.black,
            fontSize: 22,
          ),
        ),
      ],
    );
  }
}

class _PixelIconButton extends StatelessWidget {
  const _PixelIconButton({required this.icon, required this.onPressed});

  final IconData icon;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onPressed,
      child: SizedBox(
        width: 40,
        height: 40,
        child: Icon(icon, color: PixelPalette.black, size: 34),
      ),
    );
  }
}

class _SectionTitle extends StatelessWidget {
  const _SectionTitle(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: const TextStyle(
        fontFamily: PixelFonts.title,
        color: PixelPalette.black,
        fontSize: 16,
      ),
    );
  }
}

class _FieldChip extends StatelessWidget {
  const _FieldChip(this.label);

  final String label;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: const BoxDecoration(color: PixelPalette.yellow),
      child: Text(
        label,
        style: const TextStyle(
          fontFamily: PixelFonts.body,
          color: PixelPalette.black,
          fontSize: 10,
        ),
      ),
    );
  }
}

class _PixelTextField extends StatelessWidget {
  const _PixelTextField({required this.controller});

  final TextEditingController controller;

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        border: Border.all(color: PixelPalette.black, width: 4),
      ),
      child: TextField(
        controller: controller,
        style: const TextStyle(
          fontFamily: PixelFonts.body,
          color: PixelPalette.black,
          fontSize: 11,
        ),
        cursorColor: PixelPalette.black,
        decoration: const InputDecoration(
          isDense: true,
          border: InputBorder.none,
          contentPadding: EdgeInsets.symmetric(horizontal: 10, vertical: 8),
        ),
      ),
    );
  }
}

class _PixelDropdown extends StatelessWidget {
  const _PixelDropdown({
    required this.value,
    required this.items,
    required this.onChanged,
  });

  final String value;
  final List<String> items;
  final ValueChanged<String?> onChanged;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10),
      decoration: BoxDecoration(
        border: Border.all(color: PixelPalette.black, width: 4),
      ),
      child: DropdownButtonHideUnderline(
        child: DropdownButton<String>(
          value: value,
          isExpanded: true,
          icon: const Icon(
            Icons.arrow_drop_down,
            color: PixelPalette.black,
            size: 20,
          ),
          dropdownColor: PixelPalette.orange,
          style: const TextStyle(
            fontFamily: PixelFonts.body,
            color: PixelPalette.black,
            fontSize: 11,
          ),
          items: items
              .map(
                (item) => DropdownMenuItem<String>(
                  value: item,
                  child: Center(child: Text(item)),
                ),
              )
              .toList(),
          onChanged: onChanged,
        ),
      ),
    );
  }
}

class _PixelSliderBlock extends StatelessWidget {
  const _PixelSliderBlock({
    required this.label,
    required this.leftLabel,
    required this.rightLabel,
    required this.value,
    required this.onChanged,
  });

  final String label;
  final String leftLabel;
  final String rightLabel;
  final double value;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _FieldChip(label),
        const SizedBox(height: 4),
        Row(
          children: [
            Expanded(
              child: Text(
                leftLabel,
                style: const TextStyle(
                  fontFamily: PixelFonts.body,
                  color: PixelPalette.yellow,
                  fontSize: 8,
                ),
              ),
            ),
            Text(
              rightLabel,
              style: const TextStyle(
                fontFamily: PixelFonts.body,
                color: PixelPalette.yellow,
                fontSize: 8,
              ),
            ),
          ],
        ),
        const SizedBox(height: 2),
        _PixelSlider(value: value, onChanged: onChanged),
      ],
    );
  }
}

class _PixelSlider extends StatelessWidget {
  const _PixelSlider({required this.value, required this.onChanged});

  final double value;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        const divisions = 6;
        final width = constraints.maxWidth;
        final knobX = value * width;

        return GestureDetector(
          behavior: HitTestBehavior.opaque,
          onHorizontalDragUpdate: (details) {
            final local = (details.localPosition.dx / width).clamp(0.0, 1.0);
            onChanged(local);
          },
          onTapDown: (details) {
            final local = (details.localPosition.dx / width).clamp(0.0, 1.0);
            onChanged(local);
          },
          child: SizedBox(
            height: 24,
            child: Stack(
              clipBehavior: Clip.none,
              children: [
                Positioned(
                  left: 14,
                  right: 14,
                  top: 12,
                  child: Container(height: 3, color: PixelPalette.black),
                ),
                for (var i = 0; i <= divisions; i++)
                  Positioned(
                    left: (width - 14) * (i / divisions),
                    top: 7,
                    child: Container(
                      width: 14,
                      height: 14,
                      decoration: BoxDecoration(
                        color: PixelPalette.yellow,
                        border: Border.all(
                          color: PixelPalette.yellow,
                          width: 2,
                        ),
                        boxShadow: const [
                          BoxShadow(
                            color: PixelPalette.black,
                            offset: Offset(0, 0),
                            spreadRadius: -6,
                          ),
                        ],
                      ),
                      child: Center(
                        child: Container(
                          width: 4,
                          height: 4,
                          color: i == (value * divisions).round()
                              ? PixelPalette.yellow
                              : PixelPalette.orange,
                        ),
                      ),
                    ),
                  ),
                Positioned(
                  left: knobX.clamp(0.0, width - 24),
                  top: 2,
                  child: Container(
                    width: 24,
                    height: 24,
                    decoration: const BoxDecoration(
                      color: PixelPalette.yellow,
                      boxShadow: [
                        BoxShadow(
                          color: PixelPalette.black,
                          offset: Offset(4, 4),
                        ),
                      ],
                    ),
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _PixelButton extends StatelessWidget {
  const _PixelButton({required this.label, required this.onPressed});

  final String label;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onPressed,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 8),
        decoration: const BoxDecoration(
          color: PixelPalette.orange,
          boxShadow: [
            BoxShadow(color: PixelPalette.black, offset: Offset(6, 6)),
          ],
        ),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
          decoration: BoxDecoration(
            border: Border.all(color: PixelPalette.yellow, width: 3),
          ),
          child: const Text(
            'DONE!',
            style: TextStyle(
              fontFamily: PixelFonts.body,
              color: PixelPalette.yellow,
              fontSize: 18,
            ),
          ),
        ),
      ),
    );
  }
}

class _PixelNoiseFooter extends StatelessWidget {
  const _PixelNoiseFooter();

  @override
  Widget build(BuildContext context) {
    return Image.asset(
      'assets/design/pages/onboarding/components/footer_noise.png',
      width: double.infinity,
      height: 74,
      fit: BoxFit.cover,
      alignment: Alignment.topCenter,
    );
  }
}

class _StatusCard extends StatelessWidget {
  const _StatusCard();

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 84,
      width: double.infinity,
      child: _PixelAssetOrPlaceholder(
        assetPath: 'assets/design/pages/status/components/status_card.png',
        placeholder: Container(
          decoration: BoxDecoration(
            border: Border.all(color: PixelPalette.black, width: 4),
          ),
          child: Row(
            children: [
              Container(
                width: 78,
                decoration: BoxDecoration(
                  border: Border.all(color: PixelPalette.black, width: 4),
                  color: PixelPalette.orange,
                ),
                child: const Center(
                  child: Text(
                    'ICON',
                    style: TextStyle(
                      fontFamily: PixelFonts.body,
                      fontSize: 10,
                      color: PixelPalette.black,
                    ),
                  ),
                ),
              ),
              Expanded(
                child: Container(
                  color: const Color(0xFFD55435),
                  padding: const EdgeInsets.fromLTRB(6, 4, 6, 4),
                  child: const Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      _StatusText('JERRY.Z', 12, PixelPalette.yellow),
                      SizedBox(height: 2),
                      _StatusText('MBTI: ENTJ', 10, PixelPalette.yellow),
                      SizedBox(height: 2),
                      _StatusText('AGE: 23', 10, PixelPalette.yellow),
                    ],
                  ),
                ),
              ),
              Expanded(
                child: Container(
                  decoration: BoxDecoration(
                    color: const Color(0xFFDF5733),
                    border: Border(
                      left: BorderSide(color: PixelPalette.black, width: 4),
                    ),
                  ),
                  padding: const EdgeInsets.fromLTRB(6, 4, 6, 4),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        'EMOTION\nCONDITION',
                        style: TextStyle(
                          fontFamily: PixelFonts.title,
                          fontSize: 7,
                          color: PixelPalette.black,
                          height: 1.1,
                        ),
                      ),
                      const SizedBox(height: 4),
                      Container(
                        height: 24,
                        decoration: BoxDecoration(
                          border: Border.all(
                            color: PixelPalette.black,
                            width: 4,
                          ),
                        ),
                        child: Row(
                          children: List.generate(
                            7,
                            (_) => Expanded(
                              child: Container(
                                margin: const EdgeInsets.symmetric(
                                  horizontal: 2,
                                  vertical: 3,
                                ),
                                color: PixelPalette.black,
                              ),
                            ),
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _StatusScenePanel extends StatelessWidget {
  const _StatusScenePanel();

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [
        Positioned.fill(
          child: _PixelAssetOrPlaceholder(
            assetPath: 'assets/design/pages/status/components/status_scene.png',
            fit: BoxFit.fill,
            placeholder: Container(
              decoration: BoxDecoration(
                color: const Color(0xFFF2D9CA),
                border: Border.all(color: PixelPalette.black, width: 4),
              ),
              child: const Center(
                child: Text(
                  'STATUS SCENE',
                  style: TextStyle(
                    fontFamily: PixelFonts.body,
                    fontSize: 12,
                    color: PixelPalette.black,
                  ),
                ),
              ),
            ),
          ),
        ),
        Positioned(
          top: 18,
          right: 74,
          child: InkWell(
            onTap: () {
              Navigator.of(context).push(
                MaterialPageRoute<void>(builder: (_) => const WorldPage()),
              );
            },
            child: SizedBox(
              width: 46,
              height: 46,
              child: _PixelAssetOrPlaceholder(
                assetPath: 'assets/design/pages/status/icons/scene_menu.png',
                fit: BoxFit.contain,
                placeholder: Container(
                  decoration: BoxDecoration(
                    color: PixelPalette.orange,
                    border: Border.all(color: PixelPalette.black, width: 4),
                  ),
                  child: const Icon(
                    Icons.menu,
                    size: 18,
                    color: PixelPalette.black,
                  ),
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}

class _StatusBottomNav extends StatelessWidget {
  const _StatusBottomNav({
    required this.activeTab,
    required this.onTapHome,
    required this.onTapLog,
  });

  final int activeTab;
  final VoidCallback onTapHome;
  final VoidCallback onTapLog;

  @override
  Widget build(BuildContext context) {
    return Container(
      color: PixelPalette.black,
      padding: const EdgeInsets.fromLTRB(28, 10, 28, 18),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          InkWell(
            onTap: onTapHome,
            child: _BottomNavItem(
              assetPath: activeTab == 0
                  ? 'assets/design/pages/status/icons/home_active.png'
                  : 'assets/design/pages/status/icons/home_inactive.png',
              fallbackIcon: Icons.home_outlined,
              isActive: activeTab == 0,
            ),
          ),
          SizedBox(width: 78),
          InkWell(
            onTap: onTapLog,
            child: _BottomNavItem(
              assetPath: activeTab == 1
                  ? 'assets/design/pages/status/icons/log_active.png'
                  : 'assets/design/pages/status/icons/log_inactive.png',
              fallbackIcon: Icons.inventory_2_outlined,
              isActive: activeTab == 1,
            ),
          ),
        ],
      ),
    );
  }
}

class _BottomNavItem extends StatelessWidget {
  const _BottomNavItem({
    required this.assetPath,
    required this.fallbackIcon,
    this.isActive = false,
  });

  final String assetPath;
  final IconData fallbackIcon;
  final bool isActive;

  @override
  Widget build(BuildContext context) {
    final size = isActive ? const Size(132, 54) : const Size(46, 46);

    return SizedBox(
      width: size.width,
      height: size.height,
      child: _PixelAssetOrPlaceholder(
        assetPath: assetPath,
        fit: BoxFit.contain,
        placeholder: Icon(
          fallbackIcon,
          size: size.height * 0.75,
          color: isActive ? PixelPalette.orange : PixelPalette.shadowBrown,
        ),
      ),
    );
  }
}

class _PixelAssetOrPlaceholder extends StatelessWidget {
  const _PixelAssetOrPlaceholder({
    required this.assetPath,
    required this.placeholder,
    this.fit = BoxFit.cover,
  });

  final String assetPath;
  final Widget placeholder;
  final BoxFit fit;

  @override
  Widget build(BuildContext context) {
    return Image.asset(
      assetPath,
      fit: fit,
      errorBuilder: (_, _, _) => Container(
        color: Colors.transparent,
        child: Stack(
          fit: StackFit.expand,
          children: [
            placeholder,
            Align(
              alignment: Alignment.bottomCenter,
              child: Container(
                color: PixelPalette.yellow,
                padding: const EdgeInsets.all(4),
                child: Text(
                  assetPath,
                  textAlign: TextAlign.center,
                  style: const TextStyle(
                    fontFamily: PixelFonts.body,
                    fontSize: 8,
                    color: PixelPalette.black,
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _StatusText extends StatelessWidget {
  const _StatusText(this.text, this.size, this.color);

  final String text;
  final double size;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: TextStyle(
        fontFamily: PixelFonts.body,
        fontSize: size,
        color: color,
      ),
    );
  }
}
