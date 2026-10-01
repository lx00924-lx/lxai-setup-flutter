import 'package:flutter/material.dart';

import '../theme/brand.dart';

/// 主按钮：蓝色渐变 + 圆角 + hover 抬升。
///
/// 单独抽出来是因为向导四页里它出现四次（快速安装 / 开始安装 / 立即体验 / 完成），
/// 观感要完全一致 —— 大厂的"精致感"基本都来自这种一致性。
class PrimaryButton extends StatefulWidget {
  const PrimaryButton({
    super.key,
    required this.label,
    required this.onTap,
    this.icon,
    this.enabled = true,
    this.width = 132,
  });

  final String label;
  final VoidCallback onTap;
  final IconData? icon;
  final bool enabled;
  final double width;

  @override
  State<PrimaryButton> createState() => _PrimaryButtonState();
}

class _PrimaryButtonState extends State<PrimaryButton> {
  bool _hover = false;

  @override
  Widget build(BuildContext context) {
    final bool on = widget.enabled;
    return MouseRegion(
      cursor: on ? SystemMouseCursors.click : SystemMouseCursors.forbidden,
      onEnter: (_) => setState(() => _hover = true),
      onExit: (_) => setState(() => _hover = false),
      child: GestureDetector(
        onTap: on ? widget.onTap : null,
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 140),
          width: widget.width,
          height: 42,
          decoration: BoxDecoration(
            gradient: on ? Brand.primaryGradient : null,
            color: on ? null : const Color(0xFFE2E8F0),
            borderRadius: BorderRadius.circular(Brand.radius),
            boxShadow: on && _hover
                ? [
                    BoxShadow(
                      color: Brand.primary.withValues(alpha: 0.32),
                      blurRadius: 16,
                      offset: const Offset(0, 5),
                    ),
                  ]
                : null,
          ),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              if (widget.icon != null) ...[
                Icon(widget.icon, size: 17, color: on ? Colors.white : Brand.textFaint),
                const SizedBox(width: 7),
              ],
              Text(
                widget.label,
                style: TextStyle(
                  fontSize: 13.5,
                  fontWeight: FontWeight.w600,
                  color: on ? Colors.white : Brand.textFaint,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// 次级按钮：白底 + 描边（"自定义安装"、"上一步"用）。
class GhostButton extends StatefulWidget {
  const GhostButton({
    super.key,
    required this.label,
    required this.onTap,
    this.width = 108,
  });

  final String label;
  final VoidCallback onTap;
  final double width;

  @override
  State<GhostButton> createState() => _GhostButtonState();
}

class _GhostButtonState extends State<GhostButton> {
  bool _hover = false;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      onEnter: (_) => setState(() => _hover = true),
      onExit: (_) => setState(() => _hover = false),
      child: GestureDetector(
        onTap: widget.onTap,
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 140),
          width: widget.width,
          height: 42,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: _hover ? const Color(0xFFF1F5F9) : Colors.white,
            borderRadius: BorderRadius.circular(Brand.radius),
            border: Border.all(
              color: _hover ? Brand.primary.withValues(alpha: 0.5) : Brand.border,
            ),
          ),
          child: Text(
            widget.label,
            style: TextStyle(
              fontSize: 13.5,
              fontWeight: FontWeight.w500,
              color: _hover ? Brand.primary : Brand.textMain,
            ),
          ),
        ),
      ),
    );
  }
}

/// 纯文字链接（"上一步"、协议链接）。
class LinkButton extends StatelessWidget {
  const LinkButton(this.label, this.onTap, {super.key, this.color});

  final String label;
  final VoidCallback onTap;
  final Color? color;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: onTap,
        child: Text(
          label,
          style: TextStyle(
            fontSize: 13,
            color: color ?? Brand.primary,
            decoration: TextDecoration.underline,
            decorationColor: (color ?? Brand.primary).withValues(alpha: 0.4),
          ),
        ),
      ),
    );
  }
}
