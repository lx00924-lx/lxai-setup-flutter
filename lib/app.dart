import 'dart:io';

import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';

import 'core/install_log.dart';
import 'core/install_plan.dart';
import 'core/installer_engine.dart';
import 'core/payload_reader.dart';
import 'core/uninstaller.dart';
import 'pages/finish_page.dart';
import 'pages/options_page.dart';
import 'pages/progress_page.dart';
import 'pages/uninstall_confirm_page.dart';
import 'pages/welcome_page.dart';
import 'theme/brand.dart';
import 'widgets/brand_panel.dart';
import 'widgets/title_bar.dart';

/// 两种运行模式：装 / 卸。由 `main.dart` 读命令行决定（注册表里的卸载命令带 `--uninstall`）。
enum SetupMode { install, uninstall }

enum SetupStep { welcome, options, progress, finish, confirmUninstall }

class LxaiSetupApp extends StatelessWidget {
  const LxaiSetupApp({
    super.key,
    this.mode = SetupMode.install,
    this.silent = false,
    this.purgeData = false,
  });

  final SetupMode mode;

  /// 静默模式（`--silent`）：不等人点按钮，直接用默认配置执行，做完自动关窗。
  /// App 的自动更新链路与自动化测试都靠它。
  final bool silent;

  /// 卸载时是否连本地数据一起删（`--purge-data`）。默认 false = 保留用户数据。
  final bool purgeData;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: mode == SetupMode.uninstall ? '卸载 LxAI' : 'LxAI 安装程序',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(seedColor: Brand.primary),
        scaffoldBackgroundColor: Brand.surface,
        // Windows 上中文默认走 fallback，显式指定雅黑观感更整齐
        fontFamily: 'Microsoft YaHei',
      ),
      home: SetupShell(mode: mode, silent: silent, purgeData: purgeData),
    );
  }
}

class SetupShell extends StatefulWidget {
  const SetupShell({
    super.key,
    this.mode = SetupMode.install,
    this.silent = false,
    this.purgeData = false,
  });

  final SetupMode mode;
  final bool silent;
  final bool purgeData;

  @override
  State<SetupShell> createState() => _SetupShellState();
}

class _SetupShellState extends State<SetupShell> {
  late SetupStep _step = widget.mode == SetupMode.uninstall
      ? SetupStep.confirmUninstall
      : SetupStep.welcome;

  // ── 用户选择 ──────────────────────────────────────────────
  late String _installDir = _defaultInstallDir();
  // 选项页里由用户主动勾选；"快速安装"不走那一页，用按钮下方的小字做"点击即同意"
  bool _agreed = false;
  bool _desktopIcon = true;
  bool _autoStart = false;
  bool _allUsers = false;
  bool _runAfterInstall = true;

  /// 卸载时是否连本地数据一起删。**默认 false** —— 保留数据是安全默认，
  /// 要删必须用户在卸载确认页主动勾选（或用 `--purge-data` 显式指定）。
  late bool _purgeUserData = widget.purgeData;

  // ── 执行状态 ──────────────────────────────────────────────
  double _progress = 0;
  String _stage = '正在准备…';
  String _detail = '';
  final List<String> _log = [];
  bool _failed = false;
  String _errorMessage = '';
  UninstallReport? _uninstallReport;

  static String _defaultInstallDir() {
    final local = Platform.environment['LOCALAPPDATA'];
    if (local != null && local.isNotEmpty) return '$local\\Programs\\LxAI';
    return r'C:\Program Files\LxAI';
  }

  /// 卸载目标 = 卸载器自身所在的上一层目录。
  ///
  /// 卸载器住在 `{app}\uninstaller\`，所以 `resolvedExecutable` 的父目录的父目录就是 `{app}`。
  /// 开发期直接从 `build\...\Release\` 跑起来时结构对不上 —— 那种情况明确拒绝执行，
  /// 而不是"猜一个目录然后开删"。
  String? _resolveUninstallDir() {
    final selfDir = File(Platform.resolvedExecutable).parent; // …\uninstaller
    if (selfDir.path.toLowerCase().endsWith('uninstaller')) {
      return selfDir.parent.path;
    }
    return null;
  }

  @override
  void initState() {
    super.initState();
    if (widget.silent) {
      // 静默模式：首帧之后再开工（那时窗口已就绪，出错也看得见）
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (!mounted) return;
        if (widget.mode == SetupMode.uninstall) {
          _startUninstall();
        } else {
          _startInstall();
        }
      });
    }
  }

  // ────────────────────────────────────────────────────────────
  // 安装
  // ────────────────────────────────────────────────────────────
  Future<void> _startInstall() async {
    InstallLog.begin('安装 LxAI');
    setState(() {
      _step = SetupStep.progress;
      _progress = 0;
      _stage = '正在校验安装素材…';
      _detail = '';
      _failed = false;
      _errorMessage = '';
      _log
        ..clear()
        ..add('目标目录：$_installDir');
    });

    final payload = PayloadReader.locate();
    if (payload == null) {
      _fail('找不到安装素材（payload）。\n'
          '开发期请确认工程根目录下存在 payload\\ 目录；'
          '正式版说明安装包不完整，请重新下载。');
      return;
    }
    InstallLog.write('payload：${payload.path}');

    final plan = InstallPlan(
      installDir: _installDir,
      desktopIcon: _desktopIcon,
      startMenuIcon: true,
      autoStart: _autoStart,
      allUsers: _allUsers,
    );

    try {
      final result = await InstallerEngine.install(
        plan: plan,
        payload: payload,
        onProgress: (p) {
          if (!mounted) return;
          setState(() {
            _progress = p.value;
            _stage = p.stage;
            if (p.detail != null) _detail = p.detail!;
          });
        },
        onLog: (line) {
          InstallLog.write(line);
          if (!mounted) return;
          setState(() => _log.add(line));
        },
      );

      _log.add('安装完成：${result.fileCount} 个文件 / '
          '${(result.totalBytes / 1048576).toStringAsFixed(1)} MB');
      _log.add('快捷方式：${result.shortcuts.length} 个');

      if (_runAfterInstall) {
        try {
          await Process.start(
            plan.appExe,
            const [],
            mode: ProcessStartMode.detached,
            // ⚠️ 必须指定工作目录：不指定的话子进程继承**安装器**的当前目录，
            //    而 App 会按工作目录判断"应用目录"、把 lxai_bridge.py 释放到那里 ——
            //    实测把脚本写进了安装器工程根目录，而 {app}\lxai_bridge.py 始终是缺的。
            workingDirectory: plan.installDir,
          );
          _log.add('已启动 ${plan.appExe}');
        } catch (e) {
          _log.add('启动失败（不影响安装）：$e');
        }
      }

      if (!mounted) return;
      if (widget.silent) {
        await windowManager.close(); // 静默模式：做完就退，不留窗口
        return;
      }
      setState(() => _step = SetupStep.finish);
    } on InstallException catch (e, st) {
      InstallLog.exception('安装', e, st);
      _fail(e.message);
    } catch (e, st) {
      InstallLog.exception('安装', e, st);
      _fail('安装过程中出现意外错误：$e');
    }
  }

  // ────────────────────────────────────────────────────────────
  // 卸载
  // ────────────────────────────────────────────────────────────
  Future<void> _startUninstall() async {
    final dir = _resolveUninstallDir();
    if (dir == null) {
      _fail('这个程序不是已安装的卸载程序，无法定位安装目录。\n'
          '请从「设置 → 应用」里卸载 LxAI。');
      return;
    }

    setState(() {
      _step = SetupStep.progress;
      _progress = 0;
      _stage = '正在准备卸载…';
      _detail = '';
      _failed = false;
      _errorMessage = '';
      _log
        ..clear()
        ..add('安装目录：$dir')
        ..add(_purgeUserData
            ? '本地数据：将一并删除（你已勾选）'
            : '本地数据：保留（聊天记录、登录状态与设置）');
    });

    try {
      final report = await Uninstaller.run(
        installDir: dir,
        allUsers: _allUsers,
        purgeUserData: _purgeUserData,
        onProgress: (p) {
          if (!mounted) return;
          setState(() {
            _progress = p.value;
            _stage = p.stage;
            if (p.detail != null) _detail = p.detail!;
          });
        },
        onLog: (line) {
          InstallLog.write(line);
          if (!mounted) return;
          setState(() => _log.add(line));
        },
      );

      if (!mounted) return;
      if (widget.silent) {
        await windowManager.close();
        return;
      }
      setState(() {
        _uninstallReport = report;
        _step = SetupStep.finish;
      });
    } on InstallException catch (e, st) {
      InstallLog.exception('卸载', e, st);
      _fail(e.message);
    } catch (e, st) {
      InstallLog.exception('卸载', e, st);
      _fail('卸载过程中出现意外错误：$e');
    }
  }

  void _fail(String message) {
    if (!mounted) return;
    InstallLog.write('✗ 失败：$message');
    setState(() {
      _failed = true;
      _errorMessage = message;
      _stage = widget.mode == SetupMode.uninstall ? '卸载失败' : '安装失败';
      _log.add('✗ $message');
      _log.add('详细日志：${InstallLog.path}');
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Brand.surface,
      body: Column(
        children: [
          TitleBar(
            title: widget.mode == SetupMode.uninstall ? '卸载 LxAI' : 'LxAI 安装程序',
          ),
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
          allUsers: _allUsers,
          onDirChanged: (v) => setState(() => _installDir = v),
          onAgreedChanged: (v) => setState(() => _agreed = v),
          onDesktopIconChanged: (v) => setState(() => _desktopIcon = v),
          onAutoStartChanged: (v) => setState(() => _autoStart = v),
          onAllUsersChanged: (v) => setState(() => _allUsers = v),
          onBack: () => setState(() => _step = SetupStep.welcome),
          onStart: _startInstall,
        );
      case SetupStep.confirmUninstall:
        return UninstallConfirmPage(
          installDir: _resolveUninstallDir() ??
              (File(Platform.resolvedExecutable).parent.path),
          purgeUserData: _purgeUserData,
          onPurgeChanged: (v) => setState(() => _purgeUserData = v),
          onUninstall: _startUninstall,
          onCancel: () => windowManager.close(),
        );
      case SetupStep.progress:
        return ProgressPage(
          progress: _progress,
          stage: _stage,
          detail: _detail,
          log: _log,
          installDir: _installDir,
          failed: _failed,
          errorMessage: _errorMessage,
          onRetry: () => setState(() {
            _step = widget.mode == SetupMode.uninstall
                ? SetupStep.confirmUninstall
                : SetupStep.welcome;
          }),
          onClose: () => windowManager.close(),
        );
      case SetupStep.finish:
        return FinishPage(
          runAfterInstall: _runAfterInstall,
          onRunAfterChanged: (v) => setState(() => _runAfterInstall = v),
          onFinish: () => windowManager.close(),
          isUninstall: widget.mode == SetupMode.uninstall,
          installDir: _installDir,
          purgedUserData: _uninstallReport?.purgedUserData ?? false,
          problems: _uninstallReport?.problems ?? const [],
        );
    }
  }
}

/// 底部状态条：左边留许可/隐私入口位，右边放日志入口。
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
          _LinkText('日志：${InstallLog.path}', () {}, faint: true),
        ],
      ),
    );
  }
}

class _LinkText extends StatelessWidget {
  const _LinkText(this.text, this.onTap, {this.faint = false});

  final String text;
  final VoidCallback onTap;
  final bool faint;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: onTap,
        child: Text(
          text,
          style: TextStyle(
            fontSize: 11,
            color: faint ? Brand.textFaint : Brand.textMuted,
          ),
        ),
      ),
    );
  }
}
