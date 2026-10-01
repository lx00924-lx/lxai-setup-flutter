import 'package:flutter/material.dart';

import '../theme/brand.dart';
import '../widgets/buttons.dart';

/// 第 3 步 · 进度页（安装与卸载共用）。
///
/// 契约就三个字段：`progress` / `stage` / `log` —— 引擎按这个形状回调，
/// 界面完全不关心背后是在拷文件还是在删注册表。
class ProgressPage extends StatefulWidget {
  const ProgressPage({
    super.key,
    required this.progress,
    required this.stage,
    required this.log,
    required this.installDir,
    this.detail = '',
    this.failed = false,
    this.errorMessage = '',
    this.onRetry,
    this.onClose,
  });

  final double progress;
  final String stage;
  final List<String> log;
  final String installDir;

  /// 一行细节（正在处理的文件名等），显示在进度条下方
  final String detail;

  final bool failed;
  final String errorMessage;
  final VoidCallback? onRetry;
  final VoidCallback? onClose;

  @override
  State<ProgressPage> createState() => _ProgressPageState();
}

class _ProgressPageState extends State<ProgressPage> {
  bool _showLog = false;

  @override
  Widget build(BuildContext context) {
    final int percent = (widget.progress * 100).clamp(0, 100).round();

    return Padding(
      padding: const EdgeInsets.fromLTRB(40, 40, 40, 26),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              Expanded(
                child: Row(
                  children: [
                    if (widget.failed)
                      const Padding(
                        padding: EdgeInsets.only(right: 7, bottom: 2),
                        child: Icon(Icons.error_outline_rounded,
                            size: 19, color: Color(0xFFDC2626)),
                      ),
                    Expanded(
                      child: Text(
                        widget.stage,
                        style: TextStyle(
                          fontSize: 14.5,
                          fontWeight: FontWeight.w600,
                          color: widget.failed
                              ? const Color(0xFFDC2626)
                              : Brand.textMain,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
              Text(
                '$percent',
                style: TextStyle(
                  fontSize: 30,
                  fontWeight: FontWeight.w700,
                  color: widget.failed ? Brand.textFaint : Brand.primary,
                  height: 1.0,
                ),
              ),
              const Padding(
                padding: EdgeInsets.only(bottom: 3, left: 2),
                child: Text('%', style: TextStyle(fontSize: 13, color: Brand.textMuted)),
              ),
            ],
          ),
          const SizedBox(height: 16),

          // 自绘进度条
          ClipRRect(
            borderRadius: BorderRadius.circular(5),
            child: Container(
              height: 8,
              color: const Color(0xFFE9EEF4),
              child: AnimatedFractionallySizedBox(
                duration: const Duration(milliseconds: 160),
                curve: Curves.easeOut,
                alignment: Alignment.centerLeft,
                widthFactor: widget.progress.clamp(0.0, 1.0),
                heightFactor: 1,
                child: Container(
                  decoration: BoxDecoration(
                    gradient: widget.failed
                        ? const LinearGradient(
                            colors: [Color(0xFFDC2626), Color(0xFFF87171)])
                        : Brand.primaryGradient,
                  ),
                ),
              ),
            ),
          ),
          const SizedBox(height: 12),

          // 失败时把原因摆在最显眼的位置 —— 用户最需要的是"为什么"
          if (widget.failed)
            Container(
              width: double.infinity,
              padding: const EdgeInsets.fromLTRB(12, 11, 12, 11),
              decoration: BoxDecoration(
                color: const Color(0xFFFEF2F2),
                borderRadius: BorderRadius.circular(Brand.radius),
                border: Border.all(color: const Color(0xFFFECACA)),
              ),
              child: Text(
                widget.errorMessage,
                style: const TextStyle(
                    fontSize: 12.5, color: Color(0xFF991B1B), height: 1.7),
              ),
            )
          else ...[
            Text(
              '安装位置：${widget.installDir}',
              style: const TextStyle(fontSize: 11.5, color: Brand.textFaint),
            ),
            if (widget.detail.isNotEmpty) ...[
              const SizedBox(height: 5),
              Text(
                widget.detail,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(fontSize: 11, color: Brand.textFaint),
              ),
            ],
          ],

          const SizedBox(height: 18),

          // 日志：默认折叠
          InkWell(
            onTap: () => setState(() => _showLog = !_showLog),
            child: Row(
              children: [
                Icon(
                  _showLog ? Icons.keyboard_arrow_down : Icons.keyboard_arrow_right,
                  size: 18,
                  color: Brand.textMuted,
                ),
                const SizedBox(width: 4),
                Text(
                  _showLog ? '隐藏详细信息' : '显示详细信息',
                  style: const TextStyle(fontSize: 12, color: Brand.textMuted),
                ),
              ],
            ),
          ),
          if (_showLog) ...[
            const SizedBox(height: 8),
            Expanded(
              child: Container(
                width: double.infinity,
                padding: const EdgeInsets.all(10),
                decoration: BoxDecoration(
                  color: const Color(0xFF0F172A),
                  borderRadius: BorderRadius.circular(Brand.radiusSm),
                ),
                child: ListView(
                  reverse: true,
                  children: [
                    for (final line in widget.log.reversed)
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 1.5),
                        child: Text(
                          line,
                          style: TextStyle(
                            fontFamily: 'Consolas',
                            fontSize: 11.5,
                            color: line.startsWith('✗')
                                ? const Color(0xFFFCA5A5)
                                : const Color(0xFF94E2D5),
                            height: 1.5,
                          ),
                        ),
                      ),
                  ],
                ),
              ),
            ),
          ] else
            const Spacer(),

          const SizedBox(height: 6),
          if (widget.failed)
            Row(
              children: [
                GhostButton(label: '关闭', width: 88, onTap: widget.onClose ?? () {}),
                const Spacer(),
                PrimaryButton(
                  label: '重试',
                  icon: Icons.refresh_rounded,
                  onTap: widget.onRetry ?? () {},
                  width: 118,
                ),
              ],
            )
          else
            const Text(
              '过程中请不要关闭本窗口',
              style: TextStyle(fontSize: 11.5, color: Brand.textFaint),
            ),
        ],
      ),
    );
  }
}
