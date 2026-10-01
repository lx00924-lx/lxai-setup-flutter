import 'package:flutter/material.dart';
import 'package:win32/win32.dart';
import 'package:window_manager/window_manager.dart';

import 'app.dart';
import 'theme/brand.dart';

/// 读 Windows 的命令行。
///
/// ⚠️ Flutter 的 `main()` **收不到 exe 的命令行参数**（Dart 侧拿到的永远是空列表），
/// 所以这里直接向系统要 `GetCommandLineW()`。
///
/// 支持的开关：
///   `--uninstall`    卸载模式（注册表里登记的卸载命令就是它）
///   `--silent`       静默执行：不等人点按钮，用默认配置做完就关窗（供 App 自更新与自动化测试）
///   `--purge-data`   卸载时连本地数据（聊天记录/登录状态/设置）一起删 ——
///                    **默认不动用户数据**，必须显式带上这个开关才会删
bool _hasFlag(String flag) {
  try {
    return GetCommandLine().toDartString().contains(flag);
  } catch (_) {
    return false; // 拿不到就当没有这个开关
  }
}

/// LxAI 安装器入口。
///
/// 窗口策略：**无边框 + 固定尺寸 + 居中** —— 安装器不该让用户拖动/缩放（大厂都这么做，
/// 免得把向导页拉变形）。系统标题栏整条交给自绘的 `TitleBar`。
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await windowManager.ensureInitialized();

  final mode = _hasFlag('--uninstall') ? SetupMode.uninstall : SetupMode.install;
  final silent = _hasFlag('--silent');
  final purgeData = _hasFlag('--purge-data');

  final options = WindowOptions(
    size: Brand.windowSize,
    minimumSize: Brand.windowSize,
    maximumSize: Brand.windowSize,
    center: true,
    title: mode == SetupMode.uninstall ? '卸载 LxAI' : 'LxAI 安装程序',
    titleBarStyle: TitleBarStyle.hidden,
    backgroundColor: Colors.white,
  );

  await windowManager.waitUntilReadyToShow(options, () async {
    await windowManager.show();
    await windowManager.focus();
  });

  runApp(LxaiSetupApp(mode: mode, silent: silent, purgeData: purgeData));
}
