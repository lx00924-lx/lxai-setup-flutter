import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';

import '../theme/brand.dart';

/// 自绘标题栏 —— 无边框窗口下代替系统标题栏。
///
/// 为什么要自己画：`window_manager` 的 `TitleBarStyle.hidden` 把系统标题栏整条去掉后，
/// **拖拽、最小化、关闭都得自己补**。这正是"大厂那种自定义界面"的第一块砖。
///
/// ⚠️ 两个已知代价（M0 先记着，后面补）：
///   1. 无边框后 `Alt+空格` 的系统菜单也没了 —— 键盘可达性要在正文里自己兜；
///   2. Win10 上窗口是直角（Win11 系统自带圆角），要圆角得调 DwmSetWindowAttribute。
class TitleBar extends StatelessWidget {
  const TitleBar({super.key, this.title = 'LxAI 安装程序'});

  final String title;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      // 整条标题栏都能拖着走窗口
      onPanStart: (_) => windowManager.startDragging(),
      child: Container(
        height: Brand.titleBarHeight,
        decoration: const BoxDecoration(gradient: Brand.deepGradient),
        child: Row(
          children: [
            const SizedBox(width: 16),
            Image.asset('assets/app_icon.png', width: 22, height: 22),
            const SizedBox(width: 10),
            Text(
              title,
              style: const TextStyle(
                color: Colors.white,
                fontSize: 13.5,
                fontWeight: FontWeight.w600,
                letterSpacing: 0.2,
              ),
            ),
            const Spacer(),
            _WindowButton(
              icon: Icons.remove,
              tooltip: '最小化',
              onTap: () => windowManager.minimize(),
            ),
            _WindowButton(
              icon: Icons.close,
              tooltip: '关闭',
              danger: true,
              onTap: () => windowManager.close(),
            ),
          ],
        ),
      ),
    );
  }
}

/// 标题栏右侧的最小化/关闭按钮（带 hover 反馈，这是"像大厂"的细节之一）。
class _WindowButton extends StatefulWidget {
  const _WindowButton({
    required this.icon,
    required this.tooltip,
    required this.onTap,
    this.danger = false,
  });

  final IconData icon;
  final String tooltip;
  final VoidCallback onTap;
  final bool danger;

  @override
  State<_WindowButton> createState() => _WindowButtonState();
}

class _WindowButtonState extends State<_WindowButton> {
  bool _hover = false;

  @override
  Widget build(BuildContext context) {
    final Color bg = _hover
        ? (widget.danger ? const Color(0xFFE11D48) : Colors.white24)
        : Colors.transparent;
    return Tooltip(
      message: widget.tooltip,
      waitDuration: const Duration(milliseconds: 500),
      child: MouseRegion(
        cursor: SystemMouseCursors.click,
        onEnter: (_) => setState(() => _hover = true),
        onExit: (_) => setState(() => _hover = false),
        child: GestureDetector(
          onTap: widget.onTap,
          child: AnimatedContainer(
            duration: const Duration(milliseconds: 120),
            width: 46,
            height: Brand.titleBarHeight,
            color: bg,
            child: Icon(widget.icon, size: 17, color: Colors.white),
          ),
        ),
      ),
    );
  }
}
