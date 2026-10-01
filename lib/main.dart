import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';

import 'app.dart';
import 'theme/brand.dart';

/// LxAI 安装器入口。
///
/// 窗口策略：**无边框 + 固定尺寸 + 居中** —— 安装器不该让用户拖动/缩放（大厂都这么做，
/// 免得把向导页拉变形）。系统标题栏整条交给自绘的 `TitleBar`。
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await windowManager.ensureInitialized();

  const options = WindowOptions(
    size: Brand.windowSize,
    minimumSize: Brand.windowSize,
    maximumSize: Brand.windowSize,
    center: true,
    title: 'LxAI 安装程序',
    titleBarStyle: TitleBarStyle.hidden,
    backgroundColor: Colors.white,
  );

  await windowManager.waitUntilReadyToShow(options, () async {
    await windowManager.show();
    await windowManager.focus();
  });

  runApp(const LxaiSetupApp());
}
