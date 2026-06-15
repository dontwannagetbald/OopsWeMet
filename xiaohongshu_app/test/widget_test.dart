import 'package:flutter_test/flutter_test.dart';

import 'package:xiaohongshu_app/main.dart';

void main() {
  testWidgets('renders onboarding screen', (WidgetTester tester) async {
    await tester.pumpWidget(const XiaohongshuApp());

    expect(find.text('[Basic Information]'), findsOneWidget);
    expect(find.text('[Personality]'), findsOneWidget);
    expect(find.text('DONE!'), findsOneWidget);
  });
}
