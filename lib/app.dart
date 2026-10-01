import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';

import 'pages/finish_page.dart';
import 'pages/options_page.dart';
import 'pages/progress_page.dart';
import 'pages/welcome_page.dart';
import 'theme/brand.dart';
import 'widgets/brand_panel.dart';
import 'widgets/title_bar.dart';

/// 安装器外壳：无边框窗口 + 四步向导的状态机。
///
/// M0 阶段**只做壳**：真正的解压/写文件/注册表在 M1/M2 接进 `core/`，
/// 接口就是 `_startFakeInstall()` 里那段注释描述的形状（换成引擎的进度回调即可）。
enum SetupStep { welcome, options, progress, finish }

class LxaiSetupApp extends StatelessWidget {
  const LxaiSetupApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'LxAI 安装程序',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(seedColor: Brand.primary),
        scaffoldBackgroundColor: Brand.surface,
        // Windows 上中文默认走 fallback，显式指定雅黑观感更整齐
        fontFamily: 'Microsoft YaHei',
      ),
      home: const SetupShell(),
    );
  }
}

class SetupShell extends StatefulWidget {
  const SetupShell({super.key});

  @override
  State<SetupShell> createState() => _SetupShellState();
}

class _SetupShellState extends State<SetupShell> {
  SetupStep _step = SetupStep.welcome;

  late String _installDir = _defaultInstallDir();
  bool _agreed = false;
  bool _desktopIcon = true;
  bool _autoStart = false;
  bool _runAfterInstall = true;

  double _progress = 0;
  String _stage = '正在准备…';
  final List<String> _log = [];
  Timer? _ticker;

  static String _defaultInstallDir() {
    final local = Platform.environment['LOCALAPPDATA'];
    if (local != null && local.isNotEmpty) return '$local\\Programs\\LxAI';
    return r'C:\Program Files\LxAI';
  }

  @override
  void dispose() {
    _ticker?.cancel();
    super.dispose();
  }

  /// M0 的**假进度**：按时间推进、按区间切换阶段文案，用来看观感。
  ///
  /// M1 接真引擎时把这里换掉即可，形态是：
  /// ```dart
  /// await InstallerEngine.run(
  ///   plan: InstallPlan(dir: _installDir, desktopIcon: _desktopIcon, ...),
  ///   onProgress: (stage, value, line) => setState(() { _stage = stage; _progress = value; if (line != null) _log.add(line); }),
  /// );
  /// ```
  void _startInstall() {
    _ticker?.cancel();
    setState(() {
      _step = SetupStep.progress;
      _progress = 0;
      _stage = '正在解压主程序…';
      _log
        ..clear()
        ..add('目标目录：$_installDir');
    });
    _ticker = Timer.periodic(const Duration(milliseconds: 90), (t) {
      if (!mounted) return;
      setState(() {
        _progress += _progress < 0.5 ? 0.011 : 0.005;
        if (_progress >= 1) {
          _progress = 1;
          _stage = '安装完成';
          _log.add('全部完成');
          t.cancel();
          Future.delayed(const Duration(milliseconds: 450), () {
            if (mounted) setState(() => _step = SetupStep.finish);
          });
          return;
        }
        final s = _stageFor(_progress);
        if (s != _stage) {
          _stage = s;
          _log.add(s);
        }
      });
    });
  }

  String _stageFor(double p) {
    if (p < 0.35) return '正在解压主程序…';
    if (p < 0.62) return '正在配置运行环境…';
    if (p < 0.82) return '正在创建快捷方式…';
    if (p < 0.95) return '正在写入卸载信息…';
    return '正在整理…';
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Brand.surface,
      body: Column(
        children: [
          const TitleBar(),
          Expanded(
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                const BrandPanel(),
                Expanded(
                  child: AnimatedSwitcher(
                    duration: const Duration(milliseconds: 260),
                    switchInCurve: Curves.easeOutCubic,
                    switchOutCurve: Curves.easeIn,
                    transitionBuilder: (child, anim) => FadeTransition(
                      opacity: anim,
                      child: SlideTransition(
                        position: Tween<Offset>(
                          begin: const Offset(0.035, 0),
                          end: Offset.zero,
                        ).animate(anim),
                        child: child,
                      ),
                    ),
                    child: KeyedSubtree(key: ValueKey(_step), child: _buildPage()),
                  ),
                ),
              ],
            ),
          ),
          const _Footer(),
        ],
      ),
    );
  }

  Widget _buildPage() {
    switch (_step) {
      case SetupStep.welcome:
        return WelcomePage(
          onQuickInstall: _startInstall,
          onCustomInstall: () => setState(() => _step = SetupStep.options),
        );
      case SetupStep.options:
        return OptionsPage(
          installDir: _installDir,
          agreed: _agreed,
          desktopIcon: _desktopIcon,
          autoStart: _autoStart,
          onDirChanged: (v) => setState(() => _installDir = v),
          onAgreedChanged: (v) => setState(() => _agreed = v),
          onDesktopIconChanged: (v) => setState(() => _desktopIcon = v),
          onAutoStartChanged: (v) => setState(() => _autoStart = v),
          onBack: () => setState(() => _step = SetupStep.welcome),
          onStart: _startInstall,
        );
      case SetupStep.progress:
        return ProgressPage(
          progress: _progress,
          stage: _stage,
          log: _log,
          installDir: _installDir,
        );
      case SetupStep.finish:
        return FinishPage(
          runAfterInstall: _runAfterInstall,
          onRunAfterChanged: (v) => setState(() => _runAfterInstall = v),
          onFinish: () => windowManager.close(),
        );
    }
  }
}

/// 底部状态条：左边留许可/隐私入口位，右边放安装位置提示。
class _Footer extends StatelessWidget {
  const _Footer();

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 34,
      padding: const EdgeInsets.symmetric(horizontal: 16),
      decoration: const BoxDecoration(
        color: Brand.surfaceAlt,
        border: Border(top: BorderSide(color: Brand.border)),
      ),
      child: Row(
        children: [
          const Text(
            '© 2026 lx00924-lx · Apache-2.0',
            style: TextStyle(fontSize: 11, color: Brand.textFaint),
          ),
          const Spacer(),
          _LinkText('用户协议', () {}),
          const SizedBox(width: 12),
          _LinkText('隐私政策', () {}),
        ],
      ),
    );
  }
}

class _LinkText extends StatelessWidget {
  const _LinkText(this.text, this.onTap);

  final String text;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: onTap,
        child: Text(
          text,
          style: const TextStyle(fontSize: 11, color: Brand.textMuted),
        ),
      ),
    );
  }
}
