import 'dart:io';

import 'install_log.dart';
import 'install_plan.dart';
import 'payload_reader.dart';
import 'ps.dart';
import 'registry.dart';
import 'shortcut.dart';

/// 安装结果（做完之后回给界面，用于完成页与日志）
class InstallResult {
  const InstallResult({
    required this.installDir,
    required this.fileCount,
    required this.totalBytes,
    required this.shortcuts,
  });

  final String installDir;
  final int fileCount;
  final int totalBytes;
  final List<String> shortcuts;
}

/// 安装引擎：把 payload 铺到目标目录，并登记快捷方式与卸载项。
///
/// 设计原则（照 LxAI 项目 AGENTS.md 的教训）：
///  1. **先校验再动手** —— 素材不完整、磁盘不够、目录不可写，都在拷第一个文件之前报错；
///  2. **失败要说人话** —— 所有异常包成 `InstallException`，界面直接显示；
///  3. **只碰自己该碰的** —— 结束进程只按 `ExecutablePath` 落在目标目录里过滤，
///     绝不动用户自己环境里的同名进程；用户数据（Documents 里的 Hive）从不触碰。
class InstallerEngine {
  InstallerEngine._();

  // 进度权重：拷文件是绝对大头
  static const double _pValidate = 0.03;
  static const double _pKill = 0.06;
  static const double _pCopyEnd = 0.80;
  static const double _pShortcut = 0.86;
  static const double _pRegistry = 0.94;

  static Future<InstallResult> install({
    required InstallPlan plan,
    required Directory payload,
    void Function(InstallProgress)? onProgress,
    void Function(String)? onLog,
  }) async {
    final prog = onProgress ?? (_) {};
    final log = onLog ?? (_) {};

    // ── 1) 校验 ──────────────────────────────────────────────
    prog(const InstallProgress(stage: '正在校验安装素材…', value: 0.005));
    if (!payload.existsSync()) {
      throw InstallException('找不到安装素材目录', cause: payload.path);
    }
    final totalFiles = PayloadReader.countFiles(payload);
    final totalBytes = PayloadReader.totalBytes(payload);
    if (totalFiles == 0 || totalBytes == 0) {
      throw InstallException('安装素材是空的，安装包可能已损坏', cause: payload.path);
    }
    log('素材：$totalFiles 个文件 / ${_mb(totalBytes)} MB');
    log('目标目录：${plan.installDir}');

    // 磁盘空间：素材大小 × 1.15 留点余量
    final need = (totalBytes * 1.15).round();
    final free = _freeSpaceOf(plan.installDir);
    if (free != null && free < need) {
      throw InstallException(
        '目标磁盘空间不足：需要约 ${_mb(need)}，可用 ${_mb(free)}',
      );
    }
    prog(InstallProgress(stage: '正在准备安装目录…', value: _pValidate));

    // 创建目标目录（顺带验证可写）
    try {
      Directory(plan.installDir).createSync(recursive: true);
    } on FileSystemException catch (e) {
      throw InstallException('无法创建安装目录（可能没有权限）', cause: e.message);
    }

    // ── 2) 结束占用目标目录的进程（覆盖安装时的关键一步）──────
    prog(InstallProgress(stage: '正在检查文件占用…', value: 0.04));
    final killed = await killProcessesIn(plan.installDir);
    if (killed > 0) log('已结束 $killed 个占用安装目录的进程');

    // ── 3) 铺文件 ────────────────────────────────────────────
    var copied = 0;
    var copiedFiles = 0;
    final entries = payload.listSync().whereType<FileSystemEntity>().toList()
      ..sort((a, b) => a.path.compareTo(b.path));

    for (final e in entries) {
      final name = _basename(e.path);
      // 目录映射：app → {app}、python → {app}\python、其它目录原样、文件 → {app}
      final String destRoot;
      if (e is Directory) {
        destRoot = name == 'app' ? plan.installDir : '${plan.installDir}\\$name';
      } else {
        destRoot = plan.installDir;
      }

      final files = <File>[];
      if (e is File) {
        files.add(e);
      } else if (e is Directory) {
        files.addAll(e.listSync(recursive: true, followLinks: false).whereType<File>());
      }

      for (final f in files) {
        final rel = e is Directory ? f.path.substring(e.path.length + 1) : name;
        final dest = File('$destRoot\\$rel');
        try {
          if (!dest.parent.existsSync()) dest.parent.createSync(recursive: true);
          await f.copy(dest.path);
        } on FileSystemException catch (err) {
          throw InstallException('写入文件失败：$rel', cause: err.message);
        }
        copiedFiles++;
        copied += await f.length();
        prog(InstallProgress(
          stage: _copyStageFor(name),
          value: _pKill + (_pCopyEnd - _pKill) * (totalBytes == 0 ? 1 : copied / totalBytes),
          detail: rel,
        ));
      }
    }
    log('已写入 $copiedFiles 个文件 / ${_mb(copied)} MB');

    // ── 4) 快捷方式 ──────────────────────────────────────────
    prog(InstallProgress(stage: '正在创建快捷方式…', value: _pCopyEnd));
    final shortcuts = <String>[];
    // 图标来源优先用"按内容哈希命名的独立 ico"（理由见 _installIcon），拿不到才回退 exe 内嵌图标
    final iconRef = await _installIcon(plan, log);
    if (plan.desktopIcon) {
      final p = plan.allUsers
          ? '${Shortcut.publicDesktop}\\${plan.appName}.lnk'
          : '${Shortcut.userDesktop}\\${plan.appName}.lnk';
      await Shortcut.create(
        linkPath: p,
        target: plan.appExe,
        workingDirectory: plan.installDir,
        iconPath: iconRef,
        description: '${plan.appName} —— 远程指挥电脑上的私有 Agent',
      );
      shortcuts.add(p);
    }
    if (plan.startMenuIcon) {
      final base = plan.allUsers ? Shortcut.publicStartMenu : Shortcut.userStartMenu;
      final p = '$base\\${plan.appName}\\${plan.appName}.lnk';
      await Shortcut.create(
        linkPath: p,
        target: plan.appExe,
        workingDirectory: plan.installDir,
        iconPath: iconRef,
        description: '${plan.appName}',
      );
      shortcuts.add(p);
    }
    log('快捷方式：${shortcuts.length} 个');

    // 通知 Windows 刷新图标缓存。
    //
    // 为什么必须做：快捷方式的图标**不是嵌进去的**，而是按"exe 路径 + 索引"动态读取并缓存。
    // 覆盖安装时 exe 路径没变、图标却换过了，桌面与开始菜单往往会继续显示旧图标
    // （实测：exe 里的图标已经是新的，桌面还是旧的；连同"托盘是新的、桌面是旧的"这种
    //  不一致现象都是它造成的）。刷一下就好，代价只有一次约 50ms 的进程调用。
    if (shortcuts.isNotEmpty) {
      try {
        await Process.run('ie4uinit.exe', ['-show'], runInShell: false);
        InstallLog.write('已通知 shell 刷新图标缓存');
      } catch (e) {
        InstallLog.write('刷新图标缓存失败（不影响安装）：$e');
      }
    }

    prog(InstallProgress(stage: '正在登记卸载信息…', value: _pShortcut));

    // ── 5) 注册表：卸载项 +（可选）开机自启 ───────────────────
    final sizeKb = (copied / 1024).round();
    await Registry.writeUninstallEntry(
      displayName: '${plan.appName} ${_versionOf(plan)}',
      displayVersion: _versionOf(plan),
      publisher: 'lx00924-lx',
      installLocation: '${plan.installDir}\\',
      uninstallString: '"${uninstallerPath(plan)}" --uninstall',
      displayIcon: plan.appExe,
      estimatedSizeKb: sizeKb,
      allUsers: plan.allUsers,
      urlInfoAbout: 'https://github.com/lx00924-lx/flutter-app',
    );
    if (plan.autoStart) {
      await Registry.setRunAtStartup(
        command: '"${plan.appExe}" --minimized',
        allUsers: plan.allUsers,
      );
      log('已登记开机自启');
    }
    prog(InstallProgress(stage: '正在放置卸载程序…', value: _pRegistry));

    // ── 6) 把安装器自身放成卸载器 ────────────────────────────
    if (plan.createUninstaller) {
      final n = await _placeUninstaller(plan);
      log('卸载程序：${plan.installDir}\\uninstaller（$n 个文件）');
    }

    prog(const InstallProgress(stage: '安装完成', value: 1.0));
    return InstallResult(
      installDir: plan.installDir,
      fileCount: copiedFiles,
      totalBytes: copied,
      shortcuts: shortcuts,
    );
  }

  // ────────────────────────────────────────────────────────────
  // 内部实现
  // ────────────────────────────────────────────────────────────

  static String _copyStageFor(String entryName) {
    switch (entryName) {
      case 'app':
        return '正在解压主程序…';
      case 'python':
        return '正在配置运行环境…';
      default:
        return '正在写入文件…';
    }
  }

  /// 结束"可执行文件位于 [dir] 下"的进程。
  ///
  /// 为什么要做：覆盖安装时 App 本体常驻托盘、不响应系统的关闭请求，
  /// 文件被占用会让安装/卸载中途失败（旧版 Inno 安装器实测卡在 RestartManager 上、退出码 5）。
  ///
  /// ⚠️ 两个必须注意的点（都是实测踩出来的）：
  ///  1. **过滤条件是 `ExecutablePath` 落在目标目录里**，不是按进程名 ——
  ///     用户自己环境里的同名程序一根汗毛都不能碰；
  ///  2. **必须把调用者自己排除掉**：卸载器就住在 `{app}\uninstaller\` 里，
  ///     不排除的话它会把自己也 kill 掉（自杀式清理）。
  static Future<int> killProcessesIn(String dir) async {
    final r = await Ps.run(
      r'$ErrorActionPreference = "SilentlyContinue"' '\n'
      '\$app = ${Ps.q(dir)}\n'
      // ⚠️ $pid 这里要的是 **Dart 的进程 PID**（传给 PowerShell 当"别杀自己"的排除项）。
      //    千万别写成 PowerShell 的 $PID —— 那会变成 powershell.exe 自己的 PID，等于没排除。
      '\$self = $pid\n'
      r'$n = 0' '\n'
      'Get-CimInstance Win32_Process |\n'
      // ⚠️ 这一行必须用**双引号**的 raw 字符串。
      //    路径通配符在 PowerShell 里要带单引号（`'\*'`），而 Dart 的单引号 raw 字符串
      //    写不出内层单引号 —— Dart 不支持 SQL 那种 `''` 转义，`r'...''...'` 会被当成
      //    三个相邻字符串拼接，结果把引号丢掉，生成 `($app + \*)` 让 PowerShell 报错。
      r"  Where-Object { $_.ProcessId -ne $self -and $_.ExecutablePath -and ($_.ExecutablePath -like ($app + '\*')) } |" '\n'
      r'  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue; $n++ }' '\n'
      r'Write-Output $n' '\n'
      'exit 0',
      strict: false,
      tag: 'kill-appdir',
    );
    final n = int.tryParse(r.stdout.trim()) ?? 0;
    InstallLog.write('结束占用进程：目录=$dir 结果=${r.stdout.trim()}（退出码 ${r.exitCode}）');
    return n;
  }

  /// 把安装器自身（exe + 同目录运行时）复制成 `{app}\uninstaller\`。
  ///
  /// 为什么要连运行时一起复制：Flutter 的 Windows exe **不能单独运行**，
  /// 必须连带 `data\app.so`、`flutter_windows.dll` 等；而 `{app}\data\` 里装的是
  /// **App 自己**的 Dart 代码，卸载器放进去会启动成 App。所以只能自带一份，代价约 20 MB。
  ///
  /// 复制完把 exe **改名成 `uninstall.exe`**：以前保持原名 `lxai_setup.exe`，
  /// 用户进这个目录看到"setup"会以为它是安装器 —— 实测就有人双击它，
  /// 结果弹出**安装向导**（没带 `--uninstall` 就是安装模式）。名字得说人话。
  static Future<int> _placeUninstaller(InstallPlan plan) async {
    final self = File(Platform.resolvedExecutable);
    final selfDir = self.parent;
    final target = Directory('${plan.installDir}\\uninstaller');
    if (target.existsSync()) {
      // 覆盖安装：先清掉旧的卸载器目录，避免新旧文件混在一起
      try {
        target.deleteSync(recursive: true);
      } catch (_) {}
    }
    target.createSync(recursive: true);

    var count = 0;
    for (final e in selfDir.listSync()) {
      final name = _basename(e.path);
      // 开发期 exe 旁边可能躺着 payload\ 或源码目录，别一起搬过去
      if (name == 'payload' || name == 'installer' || name.endsWith('.pdb')) continue;

      if (e is File) {
        await e.copy('${target.path}\\$name');
        count++;
      } else if (e is Directory && name == 'data') {
        count += await _copyDir(e, Directory('${target.path}\\data'));
      }
    }

    // 改名成 uninstall.exe：让"这个目录里的哪个文件是卸载器"一眼可见。
    // 失败也不致命（下面注册表用的是最终名，所以这里失败要回退成原名）。
    try {
      final from = File('${target.path}\\${_basename(self.path)}');
      final to = File('${target.path}\\$kUninstallerExeName');
      if (from.existsSync()) {
        if (to.existsSync()) to.deleteSync();
        from.renameSync(to.path);
      }
    } catch (e) {
      InstallLog.write('卸载器改名失败（继续用原名）：$e');
    }

    return count;
  }

  /// 部署后的卸载器文件名。注册表与"自动进卸载模式"的判定都用它。
  static const String kUninstallerExeName = 'uninstall.exe';

  /// 部署后的卸载器绝对路径。
  static String uninstallerPath(InstallPlan plan) =>
      '${plan.installDir}\\uninstaller\\$kUninstallerExeName';

  static Future<int> _copyDir(Directory from, Directory to) async {
    var n = 0;
    for (final e in from.listSync(recursive: true, followLinks: false)) {
      if (e is! File) continue;
      final rel = e.path.substring(from.path.length + 1);
      final dest = File('${to.path}\\$rel');
      if (!dest.parent.existsSync()) dest.parent.createSync(recursive: true);
      await e.copy(dest.path);
      n++;
    }
    return n;
  }

  /// 把 App 自带的图标装成 `{app}\app_icon_<内容哈希>.ico`，返回快捷方式要用的 `路径,0`。
  ///
  /// **为什么不用现成的 `{app}\LxAI.exe,0`**：Windows 的图标缓存是按**来源路径**索引的 ——
  /// 路径不变、exe 里的图标却换过了时，shell 会继续显示缓存里的旧图。实测现象就是
  /// 「右键属性里是新的、桌面上还是旧的」，非得清缓存或重启 explorer 才恢复
  ///（而这正是用户报的"托盘是新的、桌面是旧的"）。
  ///
  /// 按内容哈希命名之后，图标一变路径就变（`app_icon_a1b2c3d4.ico` → `app_icon_5e6f7a8b.ico`），
  /// 缓存必然 miss —— **不需要任何清缓存/重启资源管理器的操作**就能立刻显示新图标；
  /// 图标没变时路径也不变，缓存照常复用，不会平白多出文件。
  ///
  /// 顺带清掉历史版本的图标副本，避免安装目录里越堆越多。
  static Future<String> _installIcon(InstallPlan plan, void Function(String) log) async {
    final src = File('${plan.installDir}\\app_icon.ico');
    if (!src.existsSync()) {
      log('未找到 app_icon.ico，快捷方式回退为使用 exe 内嵌图标');
      return '${plan.appExe},0';
    }
    final hash = _fnv1aHex(await src.readAsBytes());
    final dst = File('${plan.installDir}\\app_icon_$hash.ico');
    if (!dst.existsSync()) {
      await src.copy(dst.path);
    }
    for (final f in Directory(plan.installDir).listSync()) {
      if (f is! File) continue;
      final name = _basename(f.path);
      if (name.startsWith('app_icon_') &&
          name.endsWith('.ico') &&
          name != 'app_icon_$hash.ico') {
        try {
          f.deleteSync();
        } catch (_) {}
      }
    }
    log('快捷方式图标：app_icon_$hash.ico');
    return '${dst.path},0';
  }

  /// FNV-1a 32 位哈希：零依赖、够快，用途只是"内容变则名字变"，不需要密码学强度
  static String _fnv1aHex(List<int> data) {
    var h = 0x811c9dc5;
    for (final b in data) {
      h ^= b;
      h = (h * 0x01000193) & 0xFFFFFFFF;
    }
    return h.toRadixString(16).padLeft(8, '0');
  }

  /// 目标盘的可用空间（拿不到就返回 null，不做硬性阻拦）
  static int? _freeSpaceOf(String dir) {
    try {
      var d = dir;
      while (d.isNotEmpty && !Directory(d).existsSync()) {
        final parent = File(d).parent.path;
        if (parent == d) break;
        d = parent;
      }
      final r = Process.runSync('powershell.exe', [
        '-NoProfile',
        '-NonInteractive',
        '-Command',
        '(Get-PSDrive -Name ((Get-Item -LiteralPath ' +
            "'${d.replaceAll("'", "''")}'" +
            ').PSDrive.Name)).Free',
      ]);
      return int.tryParse('${r.stdout}'.trim());
    } catch (_) {
      return null;
    }
  }

  static String _versionOf(InstallPlan plan) {
    // 版本号从"安装器自身"的版本资源读；开发期取不到就退回常量。
    // （M3 接 CI 时改成构建期注入，见 笔记-04 §7）
    final m = RegExp(r'(\d+\.\d+\.\d+)').firstMatch(Platform.resolvedExecutable);
    return m?.group(1) ?? '1.0.1';
  }

  static String _basename(String path) {
    final i = path.lastIndexOf(Platform.pathSeparator);
    return i < 0 ? path : path.substring(i + 1);
  }

  static String _mb(int bytes) => (bytes / 1048576).toStringAsFixed(1);
}
