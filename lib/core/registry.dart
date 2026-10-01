import 'install_log.dart';
import 'ps.dart';

/// 注册表读写：卸载项、开机自启。
///
/// 卸载项位置（Windows「应用和功能」读的就是这里）：
/// ```
/// HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\{AppId}   ← 仅当前用户安装
/// HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\{AppId}   ← 为所有用户安装
/// ```
///
/// **删除一律是幂等的**：目标状态是"不存在"，所以"本来就没有"等于已经达成，
/// 绝不能让它变成错误（这正是 2026-10-02 卸载卡死的根因，详见 `ps.dart` 的注释）。
class Registry {
  Registry._();

  /// 和旧版 Inno 安装器**同一个 GUID** —— 语义上它们是同一个应用，
  /// 这样"装过旧版 → 卸掉 → 装新版"在系统看来是连续的（不会出现两个 LxAI）。
  static const String appId = '{8CC1E567-9691-46FF-914C-B7A24B230C39}';

  static const String runValueName = 'LxAI';

  static String _hive(bool allUsers) => allUsers ? 'HKLM:' : 'HKCU:';

  static String uninstallKeyPath(bool allUsers) =>
      '${_hive(allUsers)}\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\$appId';

  static String runKeyPath(bool allUsers) =>
      '${_hive(allUsers)}\\Software\\Microsoft\\Windows\\CurrentVersion\\Run';

  // ────────────────────────────────────────────────────────────
  // 卸载项
  // ────────────────────────────────────────────────────────────

  /// 写入卸载项。字段名与 Inno 版保持兼容（`DisplayName` / `UninstallString` …），
  /// 这样控制面板与各类"软件管家"都能正常识别。
  static Future<void> writeUninstallEntry({
    required String displayName,
    required String displayVersion,
    required String publisher,
    required String installLocation,
    required String uninstallString,
    required String displayIcon,
    required int estimatedSizeKb,
    required bool allUsers,
    String urlInfoAbout = '',
    String? installDate,
  }) async {
    final key = uninstallKeyPath(allUsers);
    final d = installDate ?? _todayStamp();

    final sb = StringBuffer()
      ..writeln(r'$ErrorActionPreference = "Stop"')
      ..writeln('\$k = ${Ps.q(key)}')
      ..writeln('if (-not (Test-Path -LiteralPath \$k)) { New-Item -Path \$k -Force | Out-Null }');

    void setStr(String name, String value) =>
        sb.writeln('New-ItemProperty -LiteralPath \$k -Name ${Ps.q(name)} '
            '-Value ${Ps.q(value)} -PropertyType String -Force | Out-Null');

    void setDword(String name, int value) =>
        sb.writeln('New-ItemProperty -LiteralPath \$k -Name ${Ps.q(name)} '
            '-Value $value -PropertyType DWord -Force | Out-Null');

    setStr('DisplayName', displayName);
    setStr('DisplayVersion', displayVersion);
    setStr('Publisher', publisher);
    setStr('InstallLocation', installLocation);
    setStr('UninstallString', uninstallString);
    setStr('QuietUninstallString', uninstallString);
    setStr('DisplayIcon', displayIcon);
    setStr('InstallDate', d);
    if (urlInfoAbout.isNotEmpty) setStr('URLInfoAbout', urlInfoAbout);
    // 控制面板里不显示"修改/修复"按钮 —— 我们没实现这两个动作
    setDword('NoModify', 1);
    setDword('NoRepair', 1);
    setDword('EstimatedSize', estimatedSizeKb);
    // 标记来源，便于以后排查"这个卸载项是谁写的"
    setStr('LxAIInstaller', 'lxai-setup-flutter');
    sb.writeln('exit 0');

    InstallLog.write('写卸载项：$key');
    await Ps.run(sb.toString(), tag: 'reg-write');
  }

  /// 删除卸载项（幂等）。两个 hive 都会被卸载流程调用，装的时候用的哪个不重要 ——
  /// 重要的是别留下孤儿项。
  static Future<void> deleteUninstallEntry(bool allUsers) async {
    final key = uninstallKeyPath(allUsers);
    InstallLog.write('删卸载项：$key');
    await Ps.run(
      'Remove-Item -LiteralPath ${Ps.q(key)} -Recurse -Force -ErrorAction SilentlyContinue\n'
      'exit 0',
      strict: false,
      tag: 'reg-del-uninstall',
    );
  }

  // ────────────────────────────────────────────────────────────
  // 开机自启
  // ────────────────────────────────────────────────────────────

  static Future<void> setRunAtStartup({
    required String command,
    required bool allUsers,
  }) async {
    final key = runKeyPath(allUsers);
    InstallLog.write('写开机自启：$key');
    await Ps.run(
      '${r'$ErrorActionPreference = "Stop"'}'
      '\n\$k = ${Ps.q(key)}'
      '\nif (-not (Test-Path -LiteralPath \$k)) { New-Item -Path \$k -Force | Out-Null }'
      '\nNew-ItemProperty -LiteralPath \$k -Name ${Ps.q(runValueName)} '
      '-Value ${Ps.q(command)} -PropertyType String -Force | Out-Null'
      '\nexit 0',
      tag: 'reg-run-set',
    );
  }

  /// 删除开机自启项（幂等）——
  /// 用户从没开过这个开关时这一项根本不存在，删它必须是"成功"而不是"失败"。
  static Future<void> removeRunAtStartup({required bool allUsers}) async {
    final key = runKeyPath(allUsers);
    InstallLog.write('删开机自启：$key');
    await Ps.run(
      'Remove-ItemProperty -LiteralPath ${Ps.q(key)} -Name ${Ps.q(runValueName)} '
      '-Force -ErrorAction SilentlyContinue\n'
      'exit 0',
      strict: false,
      tag: 'reg-run-del',
    );
  }

  // ────────────────────────────────────────────────────────────
  /// 查卸载项是否还在（卸载后自检用）
  static Future<bool> uninstallEntryExists(bool allUsers) async {
    final r = await Ps.run(
      'if (Test-Path -LiteralPath ${Ps.q(uninstallKeyPath(allUsers))}) { "yes" } else { "no" }',
      strict: false,
      tag: 'reg-exists',
    );
    return r.stdout.contains('yes');
  }

  static String _todayStamp() {
    final n = DateTime.now();
    return '${n.year}${n.month.toString().padLeft(2, '0')}${n.day.toString().padLeft(2, '0')}';
  }
}
