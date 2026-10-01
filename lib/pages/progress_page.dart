import 'package:flutter/material.dart';

import '../theme/brand.dart';

/// 第 3 步 · 安装进度页。
///
/// M0 用的是外壳传进来的假进度；M1 接真引擎后这里一行都不用改 ——
/// 契约就是 `(progress, stage, log)`，引擎按同样的形状回调即可。
class ProgressPage extends StatefulWidget {
  const ProgressPage({
    super.key,
    required this.progress,
    required this.stage,
    required this.log,
    required this.installDir,
  });

  final double progress;
  final String stage;
  final List<String> log;
  final String installDir;

  @override
  State<ProgressPage> createState() => _ProgressPageState();
}

class _ProgressPageState extends State<ProgressPage> {
  bool _showLog = false;

  @override
  Widget build(BuildContext context) {
    final int percent = (widget.progress * 100).clamp(0, 100).round();

    return Padding(
      padding: const EdgeInsets.fromLTRB(40, 44, 40, 26),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              Expanded(
                child: Text(
                  widget.stage,
                  style: const TextStyle(
                    fontSize: 14.5,
                    fontWeight: FontWeight.w600,
                    color: Brand.textMain,
                  ),
                ),
              ),
              Text(
                '$percent',
                style: const TextStyle(
                  fontSize: 30,
                  fontWeight: FontWeight.w700,
                  color: Brand.primary,
                  height: 1.0,
                ),
              ),
              const Padding(
                padding: EdgeInsets.only(bottom: 3, left: 2),
                child: Text(
                  '%',
                  style: TextStyle(fontSize: 13, color: Brand.textMuted),
                ),
              ),
            ],
          ),
          const SizedBox(height: 16),
          // 自绘进度条：内层宽度随进度走，外面套一段短动画让推进不那么"跳"
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
                  decoration: const BoxDecoration(gradient: Brand.primaryGradient),
                ),
              ),
            ),
          ),
          const SizedBox(height: 14),
          Text(
            '安装位置：${widget.installDir}',
            style: const TextStyle(fontSize: 11.5, color: Brand.textFaint),
          ),
          const SizedBox(height: 22),
          // 日志：默认折叠，展开后等宽字体滚到底（"大厂感"来自这种"有细节但不喧宾夺主"）
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
                          style: const TextStyle(
                            fontFamily: 'Consolas',
                            fontSize: 11.5,
                            color: Color(0xFF94E2D5),
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
          const Text(
            '安装期间请不要关闭本窗口',
            style: TextStyle(fontSize: 11.5, color: Brand.textFaint),
          ),
        ],
      ),
    );
  }
}
