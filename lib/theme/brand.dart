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
  static const Size windowSize = Size(820, 520);

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
