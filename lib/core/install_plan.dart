/// 安装计划：界面上收集到的所有选择，打包交给引擎执行。
///
/// 刻意做成纯数据（没有逻辑、没有 IO），这样引擎可以脱离界面被测试，
/// 也方便以后加"静默安装"模式（命令行参数直接构造一个 InstallPlan）。
class InstallPlan {
  const InstallPlan({
    required this.installDir,
    this.desktopIcon = true,
    this.startMenuIcon = true,
    this.autoStart = false,
    this.allUsers = false,
    this.createUninstaller = true,
    this.appName = 'LxAI',
  });

  /// 目标安装目录（绝对路径，不含末尾反斜杠）
  final String installDir;

  /// 桌面快捷方式 / 开始菜单快捷方式
  final bool desktopIcon;
  final bool startMenuIcon;

  /// 开机自启（写 HKCU\...\Run）
  final bool autoStart;

  /// 为所有用户安装（提权后为 true，注册表改写 HKLM、快捷方式写公共目录）
  final bool allUsers;

  /// 是否把安装器自身复制成 `{app}\uninstall.exe`
  final bool createUninstaller;

  final String appName;

  // ── 派生路径 ─────────────────────────────────────────────
  String get appExe => '$installDir\\$appName.exe';
  String get uninstallExe => '$installDir\\uninstall.exe';
  String get pythonDir => '$installDir\\python';

  /// 反斜杠转成正斜杠，便于与 Dart 的 p.join 混用
  String get normalizedDir => installDir.replaceAll('\\', '/');

  Map<String, Object?> toJson() => {
        'installDir': installDir,
        'desktopIcon': desktopIcon,
        'startMenuIcon': startMenuIcon,
        'autoStart': autoStart,
        'allUsers': allUsers,
        'createUninstaller': createUninstaller,
        'appName': appName,
      };

  static InstallPlan fromJson(Map<String, Object?> j) => InstallPlan(
        installDir: (j['installDir'] ?? '') as String,
        desktopIcon: (j['desktopIcon'] ?? true) as bool,
        startMenuIcon: (j['startMenuIcon'] ?? true) as bool,
        autoStart: (j['autoStart'] ?? false) as bool,
        allUsers: (j['allUsers'] ?? false) as bool,
        createUninstaller: (j['createUninstaller'] ?? true) as bool,
        appName: (j['appName'] ?? 'LxAI') as String,
      );
}

/// 引擎向外汇报进度的形状 —— 界面只认这三个字段。
class InstallProgress {
  const InstallProgress({
    required this.stage,
    required this.value,
    this.detail,
  });

  /// 阶段文案（显示在进度条上方），如「正在解压主程序…」
  final String stage;

  /// 0.0 ~ 1.0
  final double value;

  /// 一行可选的细节（进"详细信息"日志）
  final String? detail;

  @override
  String toString() => '$stage (${(value * 100).toStringAsFixed(0)}%)';
}

/// 引擎执行失败时抛这个，带上"用户看得懂"的原因。
class InstallException implements Exception {
  InstallException(this.message, {this.cause});

  final String message;
  final Object? cause;

  @override
  String toString() => cause == null ? message : '$message（$cause）';
}
