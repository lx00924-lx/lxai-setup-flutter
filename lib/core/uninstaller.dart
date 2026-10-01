import 'dart:io';

import 'install_log.dart';
import 'install_plan.dart';
import 'installer_engine.dart';
import 'registry.dart';
import 'shortcut.dart';

/// 卸载结果：做了多少事、以及**哪些事没做成**。
///
/// 为什么要有 `problems` 而不是"失败就抛异常"：卸载是**一串互相独立的清理动作**
/// （杀进程 / 删快捷方式 / 删注册表 / 删文件 / 删数据）。其中任何一项失败都不该让其余项不做 ——
/// 2026-10-02 的实测事故就是反例：删一个本来不存在的注册表值报了错，
/// 结果快捷方式之后的**所有清理全部没执行**，用户看到的是"卸载失败"，程序还留在磁盘上。
class UninstallReport {
  const UninstallReport({
    required this.removedFiles,
    required this.removedBytes,
    required this.removedLinks,
    required this.purgedUserData,
    required this.problems,
  });

  final int removedFiles;
  final int removedBytes;
  final int removedLinks;
  final bool purgedUserData;

  /// 没做成的步骤（空 = 完全干净）
  final List<String> problems;

  bool get clean => problems.isEmpty;
}

/// 卸载：把安装时铺出去的东西收回来。
///
/// | 安装时 | 卸载时 |
/// | :--- | :--- |
/// | 铺文件到 `{app}` | 删整个 `{app}` |
/// | 桌面 / 开始菜单快捷方式 | 删（用户 + 公共两处都查） |
/// | 注册表卸载项（HKCU 或 HKLM） | 两个 hive 都删，不看装的时候用的哪个 |
/// | `Run` 开机自启项 | 两个 hive 都删（幂等，本来没有也算成功） |
/// | 用户数据（文档目录里的 Hive） | **默认保留**；只有用户主动勾选「同时清理本地数据」才删 |
///
/// **自删除**：卸载器自己就在 `{app}\uninstaller\` 里，Windows 不允许删正在运行的 exe。
/// 做法是删完全部内容后，用一个**隐藏的 VBScript** 延迟 1.5 秒把 `{app}` 整个目录树删掉 ——
/// `wscript.exe` 本身无窗口，所以用户看不到黑框一闪。
class Uninstaller {
  Uninstaller._();

  /// 用户数据文件名（桌面 App 的 Hive 数据库，位于「文档」目录）。
  /// **刻意只列这三个**：文档目录是用户的私人地盘，宁可漏删也不能误删。
  static const List<String> userDataFiles = [
    'messages_box.hive',
    'sessions_box.hive',
    'settings_box.hive',
  ];

  static Future<UninstallReport> run({
    required String installDir,
    required bool allUsers,
    bool purgeUserData = false,
    void Function(InstallProgress)? onProgress,
    void Function(String)? onLog,
  }) async {
    final prog = onProgress ?? (_) {};
    final log = onLog ?? (_) {};
    final problems = <String>[];

    InstallLog.begin('卸载 LxAI');
    InstallLog.write('参数：installDir=$installDir allUsers=$allUsers purgeUserData=$purgeUserData');

    if (installDir.trim().isEmpty) {
      throw InstallException('卸载目录为空，拒绝执行');
    }
    // 保命检查：绝不允许对盘根或系统目录执行递归删除
    final normalized = installDir.replaceAll('/', '\\').replaceAll(RegExp(r'\\+$'), '');
    final lower = normalized.toLowerCase();
    if (RegExp(r'^[a-z]:$').hasMatch(lower) ||
        lower == r'c:\windows' ||
        lower == r'c:\program files' ||
        lower == r'c:\program files (x86)') {
      throw InstallException('卸载目录看起来是系统目录，已拒绝执行', cause: normalized);
    }

    final sw = Stopwatch()..start();

    // ── 1) 结束占用进程（App 本体 + 它拉起的桥接）─────────────
    prog(const InstallProgress(stage: '正在关闭正在运行的程序…', value: 0.05));
    await _guard('结束占用进程', problems, log, () async {
      final killed = await InstallerEngine.killProcessesIn(normalized);
      log(killed > 0 ? '已结束 $killed 个进程' : '没有需要结束的进程');
      // 给被杀的进程一点时间真正退出，否则后面删文件会大面积失败
      if (killed > 0) await Future.delayed(const Duration(milliseconds: 1200));
    });

    // ── 2) 快捷方式 ──────────────────────────────────────────
    prog(const InstallProgress(stage: '正在删除快捷方式…', value: 0.15));
    var removedLinks = 0;
    final appName = _appNameOf(normalized);
    final links = <String>[
      '${Shortcut.userDesktop}\\$appName.lnk',
      '${Shortcut.publicDesktop}\\$appName.lnk',
      '${Shortcut.userStartMenu}\\$appName\\$appName.lnk',
      '${Shortcut.publicStartMenu}\\$appName\\$appName.lnk',
      // 旧版 Inno 安装器留下的"卸载 LxAI"入口，也一并扫掉
      '${Shortcut.userStartMenu}\\$appName\\卸载 $appName.lnk',
      '${Shortcut.publicStartMenu}\\$appName\\卸载 $appName.lnk',
    ];
    for (final l in links) {
      await _guard('删除快捷方式 $l', problems, log, () async {
        if (File(l).existsSync()) {
          await Shortcut.delete(l);
          log('已删除快捷方式：$l');
          removedLinks++;
        }
      });
    }
    // 快捷方式所在的文件夹（空了就删，非空说明用户自己放了东西，留着）
    for (final d in [
      '${Shortcut.userStartMenu}\\$appName',
      '${Shortcut.publicStartMenu}\\$appName',
    ]) {
      await _guard('删除开始菜单文件夹 $d', problems, log, () async {
        final dir = Directory(d);
        if (dir.existsSync() && dir.listSync().isEmpty) {
          dir.deleteSync();
          log('已删除开始菜单文件夹：$d');
        }
      });
    }

    // ── 3) 注册表（两个 hive 都清）────────────────────────────
    prog(const InstallProgress(stage: '正在清理注册表…', value: 0.35));
    for (final all in [false, true]) {
      await _guard('删除注册表卸载项（allUsers=$all）', problems, log, () async {
        await Registry.deleteUninstallEntry(all);
      });
      await _guard('删除开机自启项（allUsers=$all）', problems, log, () async {
        await Registry.removeRunAtStartup(allUsers: all);
      });
    }
    log('注册表卸载项与开机自启已清理');

    // ── 4) 删除安装目录内容（保留卸载器自身，最后交给延迟脚本）──
    prog(const InstallProgress(stage: '正在删除程序文件…', value: 0.45));
    var removedFiles = 0;
    var removedBytes = 0;
    await _guard('删除程序文件', problems, log, () async {
      final root = Directory(normalized);
      if (!root.existsSync()) {
        log('安装目录本来就不存在：$normalized');
        return;
      }
      final selfDir = File(Platform.resolvedExecutable).parent.path.toLowerCase();
      final entries = root.listSync().toList();
      for (var i = 0; i < entries.length; i++) {
        final e = entries[i];
        // 跳过卸载器自己所在的那一层（它正被运行占用）
        if (e.path.toLowerCase() == selfDir) {
          log('跳过卸载器自身目录（稍后由清理脚本删除）');
          continue;
        }
        try {
          if (e is Directory) {
            for (final f in e.listSync(recursive: true, followLinks: false)) {
              if (f is File) {
                removedFiles++;
                try {
                  removedBytes += f.lengthSync();
                } catch (_) {}
              }
            }
            e.deleteSync(recursive: true);
          } else if (e is File) {
            removedFiles++;
            try {
              removedBytes += e.lengthSync();
            } catch (_) {}
            e.deleteSync();
          }
        } catch (err) {
          // 被占用/权限不足的文件删不掉不阻断 —— 最后的延迟脚本会再扫一遍
          InstallLog.write('删不掉（交给清理脚本）：${e.path} → $err');
          log('跳过（稍后重试）：${e.path.split('\\').last}');
        }
        prog(InstallProgress(
          stage: '正在删除程序文件…',
          value: 0.45 + 0.35 * (i + 1) / entries.length,
          detail: e.path,
        ));
      }
    });

    // ── 5) 用户数据（默认保留，只有用户勾了才删）───────────────
    prog(const InstallProgress(stage: '正在处理本地数据…', value: 0.82));
    var purged = false;
    if (purgeUserData) {
      await _guard('清理本地数据', problems, log, () async {
        final docs = '${Platform.environment['USERPROFILE'] ?? ''}\\Documents';
        var n = 0;
        for (final name in userDataFiles) {
          for (final suffix in ['', '.lock']) {
            final f = File('$docs\\$name$suffix');
            if (f.existsSync()) {
              f.deleteSync();
              n++;
              log('已删除本地数据：$name$suffix');
            }
          }
        }
        purged = true;
        log(n > 0 ? '本地数据已清理（$n 个文件）' : '本地数据本来就不存在');
      });
    } else {
      log('本地数据（聊天记录 / 登录状态 / 设置）按你的选择保留');
    }

    // ── 6) 安排自删除：整个 {app} 目录树 ──────────────────────
    prog(const InstallProgress(stage: '正在完成清理…', value: 0.92));
    await _guard('安排延迟清理', problems, log, () async {
      await _scheduleSelfDestruct(normalized);
      log('已安排延迟清理（约 1.5 秒后移除剩余文件）');
    });

    prog(const InstallProgress(stage: '卸载完成', value: 1.0));
    sw.stop();
    log('用时 ${(sw.elapsedMilliseconds / 1000).toStringAsFixed(1)} 秒 · '
        '删除 $removedFiles 个文件 / ${(removedBytes / 1048576).toStringAsFixed(1)} MB · '
        '快捷方式 $removedLinks 个');
    if (problems.isNotEmpty) {
      log('⚠ 有 ${problems.length} 项没做成，已记录在日志：${InstallLog.path}');
      for (final p in problems) {
        log('   · $p');
      }
    }
    InstallLog.write('卸载结束：problems=${problems.length}');

    return UninstallReport(
      removedFiles: removedFiles,
      removedBytes: removedBytes,
      removedLinks: removedLinks,
      purgedUserData: purged,
      problems: problems,
    );
  }

  /// 把每一步单独包起来：出错只记录 + 继续，绝不让一步失败掐断整条清理链。
  static Future<void> _guard(
    String name,
    List<String> problems,
    void Function(String) log,
    Future<void> Function() body,
  ) async {
    try {
      await body();
    } catch (e, st) {
      InstallLog.exception(name, e, st);
      problems.add('$name：$e');
      log('✗ $name 失败（继续下一步）：$e');
    }
  }

  /// 用隐藏的 VBScript 延迟删除整个安装目录（含卸载器自己）。
  ///
  /// 为什么不用 `cmd /c rmdir`：`Process.start` 在 Windows 上会给控制台程序开一个窗口，
  /// 用户会看到黑框一闪；而 VBS 走 `FileSystemObject`，既不闪窗，也顺带绕开了
  /// 路径里 `&`、`^` 之类字符在 cmd 里的转义问题。
  static Future<void> _scheduleSelfDestruct(String installDir) async {
    final tmp = Directory.systemTemp.path;
    final vbs = File('$tmp\\lxai-uninstall-cleanup.vbs');
    final escaped = installDir.replaceAll('"', '""');
    vbs.writeAsStringSync(
      'Set fso = CreateObject("Scripting.FileSystemObject")\r\n'
      'WScript.Sleep 1500\r\n'
      'On Error Resume Next\r\n'
      'fso.DeleteFolder "$escaped", True\r\n'
      'On Error GoTo 0\r\n',
    );
    InstallLog.write('自删除脚本：${vbs.path}');
    await Process.start(
      'wscript.exe',
      [vbs.path],
      mode: ProcessStartMode.detached,
    );
  }

  static String _appNameOf(String installDir) {
    final i = installDir.lastIndexOf('\\');
    final name = i < 0 ? installDir : installDir.substring(i + 1);
    // "LxAI-Dev" 这类测试目录名，取回正式产品名
    return name.startsWith('LxAI') ? 'LxAI' : name;
  }
}
