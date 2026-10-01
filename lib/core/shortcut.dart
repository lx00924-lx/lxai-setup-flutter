import 'dart:io';

import 'install_log.dart';
import 'ps.dart';

/// 创建 / 删除 Windows 快捷方式（`.lnk`）。
///
/// **为什么走 PowerShell**：`.lnk` 是二进制格式（Shell Link Binary File Format），
/// 纯 Dart 手写容易出错；`win32` 的 `IShellLink` 要自己搭 COM vtable，代码量翻几倍。
/// 一次安装只调 2~3 次，多花的那 ~150 ms 进程启动时间完全无所谓。
/// 将来想彻底摆脱外部依赖，把这里的实现换成 IShellLink 即可 —— **接口不用变**。
class Shortcut {
  Shortcut._();

  /// 创建快捷方式。`workingDirectory` 建议设为 exe 所在目录 ——
  /// 否则从"开始菜单"启动时工作目录会是 system32，App 里用相对路径的逻辑会找不到文件。
  static Future<void> create({
    required String linkPath,
    required String target,
    String? arguments,
    String? workingDirectory,
    String? iconPath,
    String? description,
  }) async {
    final dir = File(linkPath).parent;
    if (!dir.existsSync()) dir.createSync(recursive: true);

    final sb = StringBuffer()
      ..writeln(r'$ErrorActionPreference = "Stop"')
      ..writeln('\$s = (New-Object -ComObject WScript.Shell).CreateShortcut(${Ps.q(linkPath)})')
      ..writeln('\$s.TargetPath = ${Ps.q(target)}');
    if (arguments != null) sb.writeln('\$s.Arguments = ${Ps.q(arguments)}');
    if (workingDirectory != null) sb.writeln('\$s.WorkingDirectory = ${Ps.q(workingDirectory)}');
    if (description != null) sb.writeln('\$s.Description = ${Ps.q(description)}');
    if (iconPath != null) sb.writeln('\$s.IconLocation = ${Ps.q(iconPath)}');
    // 这行是 **raw 字符串**：PowerShell 的变量就是 `$s`，不需要（也不能）加反斜杠转义
    sb.writeln(r'$s.Save()');
    sb.writeln('exit 0');

    InstallLog.write('创建快捷方式：$linkPath → $target');
    // strict：快捷方式建不上属于真失败，必须让上层知道
    await Ps.run(sb.toString(), tag: 'lnk-create');
  }

  /// 删除快捷方式（幂等）：不存在等于已经达成目标，不算错误。
  static Future<void> delete(String linkPath) async {
    final f = File(linkPath);
    if (!f.existsSync()) return;
    try {
      f.deleteSync();
      InstallLog.write('删除快捷方式：$linkPath');
    } catch (_) {
      await Ps.run(
        'Remove-Item -LiteralPath ${Ps.q(linkPath)} -Force -ErrorAction SilentlyContinue\n'
        'exit 0',
        strict: false,
        tag: 'lnk-del',
      );
    }
  }

  /// 公共（所有用户）快捷方式目录 —— 只在"为所有用户安装"时用
  static String get publicDesktop =>
      '${Platform.environment['PUBLIC'] ?? r'C:\Users\Public'}\\Desktop';

  static String get publicStartMenu =>
      '${Platform.environment['ProgramData'] ?? r'C:\ProgramData'}'
      r'\Microsoft\Windows\Start Menu\Programs';

  static String get userDesktop =>
      '${Platform.environment['USERPROFILE'] ?? ''}\\Desktop';

  static String get userStartMenu =>
      '${Platform.environment['APPDATA'] ?? ''}\\Microsoft\\Windows\\Start Menu\\Programs';
}
