import 'dart:convert';
import 'dart:io';

import 'install_log.dart';

/// PowerShell 执行结果
class PsResult {
  const PsResult({required this.exitCode, required this.stdout, required this.stderr});

  final int exitCode;
  final String stdout;
  final String stderr;

  bool get ok => exitCode == 0;
}

/// 统一的 PowerShell 执行入口。
///
/// ## 为什么不用 `-Command "<多行脚本>"`
/// 实测踩了两个坑（2026-10-02，卸载流程卡在 35%）：
///
///  1. **退出码语义**：`powershell.exe -Command` 的退出码取决于**最后一条命令是否成功**（`$?`）。
///     删一个本来就不存在的注册表值（哪怕带了 `-ErrorAction SilentlyContinue`）也会让 `$?` 变 false、
///     进程以 **1** 退出。于是"本来就没这项、删掉正好"被误判成致命错误，整个卸载在第 3 步中断 ——
///     现象是注册表和快捷方式都删了、文件一个没动。
///  2. 多行脚本作为**单个命令行参数**传递时，换行与引号的转义规则很难一次写对。
///
/// 所以改成：**把脚本写成临时 `.ps1`，再用 `-File` 执行** ——
/// 旧版 Inno 安装器里也是这个做法（`SaveStringsToFile` + `Exec`），实测可靠。
///
/// ## strict 的取舍
/// - `strict: true`（默认）—— 写入类操作（卸载项、快捷方式）：失败必须让上层知道；
/// - `strict: false` —— **幂等删除**类操作（删注册表值/项）：目标状态是"不存在"，
///   本来就不存在等于已经达成，不该当错误。
class Ps {
  Ps._();

  static int _seq = 0;

  /// PowerShell 单引号字符串字面量（唯一转义规则：单引号写两遍）
  static String q(String s) => "'${s.replaceAll("'", "''")}'";

  static Future<PsResult> run(
    String body, {
    bool strict = true,
    String tag = 'cmd',
  }) async {
    final file = File('${Directory.systemTemp.path}\\lxai-$tag-${pid}-${_seq++}.ps1');
    // ⚠️ 必须带 UTF-8 BOM：本机是 Windows PowerShell 5.1，
    // 它读无 BOM 的 UTF-8 会按系统 ANSI（936/GBK）解码 —— 脚本里只要出现中文路径就会乱码。
    const bom = [0xEF, 0xBB, 0xBF];
    file.writeAsBytesSync([...bom, ...utf8.encode(body)], flush: true);

    try {
      final r = await Process.run(
        'powershell.exe',
        ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', file.path],
        runInShell: false,
      );
      final out = '${r.stdout}'.trim();
      final err = '${r.stderr}'.trim();
      InstallLog.write('[ps:$tag] exit=${r.exitCode}'
          '${out.isEmpty ? '' : ' out=$out'}'
          '${err.isEmpty ? '' : ' err=$err'}');

      if (strict && r.exitCode != 0) {
        throw ProcessException(
          'powershell.exe',
          [tag],
          err.isEmpty ? '退出码 ${r.exitCode}' : err,
          r.exitCode,
        );
      }
      return PsResult(exitCode: r.exitCode, stdout: out, stderr: err);
    } finally {
      try {
        if (file.existsSync()) file.deleteSync();
      } catch (_) {
        // 临时文件删不掉无所谓，系统会清
      }
    }
  }
}
