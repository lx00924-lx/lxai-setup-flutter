import 'dart:io';

/// 安装/卸载的执行轨迹落盘：`%TEMP%\lxai-setup.log`。
///
/// **为什么必须有**：安装器是运行在别人机器上、会失败的程序。GUI 一旦停在错误页，
/// 光看界面根本不知道卡在哪一步 —— 第一次实测卸载就吃了这个亏：注册表和快捷方式都删掉了、
/// 文件一个没动、VBS 也没生成，只能靠猜。有了这份日志，出错时直接看最后几行。
///
/// 每次开始安装/卸载会先清空，只保留本次运行的记录（不滚历史，避免文件越来越大）。
class InstallLog {
  InstallLog._();

  static final List<String> _lines = [];
  static String? _cachedPath;

  /// 日志文件路径（也显示给用户，方便反馈问题时一起发过来）
  static String get path =>
      _cachedPath ??= '${Directory.systemTemp.path}\\lxai-setup.log';

  /// 开始新一轮：清空
  static void begin(String title) {
    _lines.clear();
    write('════════ $title ════════');
    write('时间：${DateTime.now().toIso8601String()}');
    write('可执行文件：${Platform.resolvedExecutable}');
    write('工作目录：${Directory.current.path}');
    write('进程 PID：$pid');
  }

  static void write(String line) {
    final ts = DateTime.now().toIso8601String().substring(11, 23);
    _lines.add('[$ts] $line');
    try {
      File(path).writeAsStringSync('${_lines.join('\n')}\n', flush: true);
    } catch (_) {
      // 写日志失败绝不影响主流程（比如临时目录不可写）
    }
  }

  /// 记异常：**连堆栈一起**，否则只知道"出错了"不知道"错在哪一行"
  static void exception(String where, Object e, StackTrace st) {
    write('✗ 异常 @ $where');
    write('    类型: ${e.runtimeType}');
    write('    信息: $e');
    var i = 0;
    for (final l in st.toString().split('\n')) {
      if (i++ >= 10) break;
      write('    $l');
    }
  }
}
