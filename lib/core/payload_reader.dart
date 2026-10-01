import 'dart:io';

/// 定位安装素材（payload）。
///
/// 素材内容 = App 的 Release 产物 + 私有 Python 运行时 + 许可文本：
/// ```
/// payload\
/// ├── app\         ← flutter_app\build\windows\x64\runner\Release\*（LxAI.exe + data\ + dll）
/// ├── python\      ← installer\runtime\python\*（Python 3.13 embeddable + websockets）
/// ├── NOTICE / TERMS.md / PRIVACY.md
/// ```
///
/// **开发期**素材外置成目录（本类负责找它）；**M3** 会改成从安装器 exe 自身尾部读
/// 压缩包 —— 那时只换 `PayloadSource` 的实现，引擎逻辑一行都不用动。
class PayloadReader {
  PayloadReader._();

  static const List<String> _requiredMarkers = [
    r'app\LxAI.exe',
    r'python\python.exe',
  ];

  /// 依次尝试这些位置，返回第一个"看起来是完整 payload"的目录。
  ///
  /// 顺序说明：
  ///   1. `LXAI_PAYLOAD_DIR` 环境变量 —— 打包/测试时最方便，也便于指向别处的素材；
  ///   2. exe 同级目录的 `payload\` —— 未来单文件版的形态（解包到临时目录）；
  ///   3. 从 exe 所在目录**逐级向上**找 `payload\` —— 开发期直接跑
  ///      `build\windows\x64\runner\Release\lxai_setup.exe` 时能回溯到工程根；
  ///   4. 当前工作目录的 `payload\` —— `flutter run` 的情形。
  static Directory? locate() {
    final candidates = <Directory>[];

    final env = Platform.environment['LXAI_PAYLOAD_DIR'];
    if (env != null && env.trim().isNotEmpty) {
      candidates.add(Directory(env.trim()));
    }

    final exeDir = File(Platform.resolvedExecutable).parent;
    candidates.add(Directory('${exeDir.path}\\payload'));
    var cur = exeDir;
    for (var i = 0; i < 6; i++) {
      final parent = cur.parent;
      if (parent.path == cur.path) break;
      cur = parent;
      candidates.add(Directory('${cur.path}\\payload'));
    }
    candidates.add(Directory('${Directory.current.path}\\payload'));

    for (final c in candidates) {
      if (_looksComplete(c)) return c;
    }
    return null;
  }

  /// 判据：两个关键文件都在才算"完整" —— 只检查目录存在是不够的，
  /// 半成品素材会让安装过程跑到一半才失败（本项目在桥接脚本上踩过这种坑）。
  static bool _looksComplete(Directory dir) {
    if (!dir.existsSync()) return false;
    for (final m in _requiredMarkers) {
      if (!File('${dir.path}\\$m').existsSync()) return false;
    }
    return true;
  }

  /// 素材总体积（字节）—— 用来估算进度与显示"需要多少磁盘"。
  static int totalBytes(Directory payload) {
    var total = 0;
    for (final e in payload.listSync(recursive: true, followLinks: false)) {
      if (e is File) {
        try {
          total += e.lengthSync();
        } catch (_) {
          // 个别文件读不到长度不算致命，跳过
        }
      }
    }
    return total;
  }

  /// 统计文件数（进度按"已处理文件数 / 总数"推进时要用）。
  static int countFiles(Directory payload) {
    var n = 0;
    for (final e in payload.listSync(recursive: true, followLinks: false)) {
      if (e is File) n++;
    }
    return n;
  }
}
