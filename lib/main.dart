import 'dart:io';

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

/// 当前这个 exe 是不是"装完以后被部署到 `{app}\uninstaller\` 的那一份"。
///
/// 为什么要判断：**用户会直接双击它**。以前那份 exe 叫 `lxai_setup.exe` 且不带
/// `--uninstall`，双击的结果是弹出**安装向导**（用户实测报过："这个目录我点击是安装啊"）。
/// 所以只要发现自己住在 `uninstaller\` 里，就默认按卸载模式走 —— 双击就该是卸载。
///
/// 开发期从 `build\windows\x64\runner\Release\` 直接跑不受影响（那个目录不叫 uninstaller）。
bool _isDeployedUninstaller() {
  try {
    final dir = File(Platform.resolvedExecutable).parent.path.toLowerCase();
    return dir.endsWith(r'\uninstaller');
  } catch (_) {
    return false;
  }
}

/// LxAI 安装器入口。
///
/// 窗口策略：**无边框 + 固定尺寸 + 居中** —— 安装器不该让用户拖动/缩放（大厂都这么做，
/// 免得把向导页拉变形）。系统标题栏整条交给自绘的 `TitleBar`。
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await windowManager.ensureInitialized();

  final mode = (_hasFlag('--uninstall') || _isDeployedUninstaller())
      ? SetupMode.uninstall
      : SetupMode.install;
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
    // ⚠️ 必须在 show 之前调用：**真正去掉窗口边框**。
    //
    // `TitleBarStyle.hidden` 只是"不绘制标题栏"，窗口样式里仍然留着 WS_CAPTION 与
    // WS_THICKFRAME —— 实测外框 820×520、客户区只有 804×512，那圈 16×8 px 就是用户看到的白边；
    // 更坑的是底部边框自带 8px 的"拖拽调整大小"热区，会把按钮下半部分的点击整个吃掉
    // （光标也不变成手型）。setAsFrameless 会把样式换成无边框弹窗，两者就一致了。
    await windowManager.setAsFrameless();
    await windowManager.show();
    await windowManager.focus();
  });

  runApp(LxaiSetupApp(mode: mode, silent: silent, purgeData: purgeData));
}
