import 'package:flutter/material.dart';

/// 品牌视觉 token。
///
/// 配色与 App、图标生成器（`tools/make_icons.py`）保持**同一套**：
/// 主色来自 App 界面里已在用的 #0284C7，深藏青/琥珀/天蓝来自图标本身。
/// 改这里 = 改整个安装器的观感，别在页面里写死颜色。
class Brand {
  Brand._();

  /// 主色（主按钮、进度条、强调文字）
  static const Color primary = Color(0xFF0284C7);
  static const Color primaryDark = Color(0xFF0369A1);

  /// 深藏青：标题栏与左侧品牌区底色（与图标背景同源）
  static const Color deepA = Color(0xFF0C1D32);
  static const Color deepB = Color(0xFF133351);

  /// 图标里那两个点缀色
  static const Color amber = Color(0xFFFBBF24);
  static const Color sky = Color(0xFF7DD3FC);

  /// 文本三档
  static const Color textMain = Color(0xFF0F172A);
  static const Color textMuted = Color(0xFF64748B);
  static const Color textFaint = Color(0xFF94A3B8);

  /// 面与线
  static const Color surface = Colors.white;
  static const Color surfaceAlt = Color(0xFFF8FAFC);
  static const Color border = Color(0xFFE2E8F0);
  static const Color success = Color(0xFF16A34A);

  static const double radius = 10;
  static const double radiusSm = 6;
  static const double titleBarHeight = 48;
  static const double brandPanelWidth = 248;

  /// 窗口尺寸（国内大厂安装器常见区间：别做全屏，也别小到内容挤）
  ///
  /// ⚠️ 高度 620 是被**选项页的实际内容**顶上去的，不是随手写的：
  /// 标题栏占 48，剩下的才是内容区；而"协议 + 安装位置 + 5 个附加任务开关 + 按钮行"
  /// 实测需要 520 出头。原先 520 高时最后两个开关和「开始安装」被挤出可视区 ——
  /// 中间虽然做了滚动，但**首次安装的人不知道要滚**，等于按钮不存在（用户实测反馈）。
  /// 所以宁可窗口高一点，也要让所有选项一屏可见；滚动只作为"以后再加选项"的兜底。
  ///
  /// 再加附加任务时：每行约 41px，先确认 620 还放得下，放不下就同步调大这里。
  static const Size windowSize = Size(820, 620);

  static const LinearGradient deepGradient = LinearGradient(
    begin: Alignment.topLeft,
    end: Alignment.bottomRight,
    colors: [deepA, deepB],
  );

  static const LinearGradient primaryGradient = LinearGradient(
    begin: Alignment.centerLeft,
    end: Alignment.centerRight,
    colors: [primary, Color(0xFF38BDF8)],
  );

  /// 品牌区那团光晕（径向渐变，比贴一张位图省事且任意分辨率都清晰）
  static const RadialGradient glow = RadialGradient(
    center: Alignment(0.1, -0.15),
    radius: 0.9,
    colors: [Color(0x337DD3FC), Color(0x140284C7), Color(0x00000000)],
  );
}
