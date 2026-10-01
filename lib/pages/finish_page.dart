import 'package:flutter/material.dart';

import '../theme/brand.dart';
import '../widgets/buttons.dart';

/// 第 4 步 · 完成页。
///
/// 大厂在这里的套路很固定：一个"成功"符号 + 一句确认 + 一两个勾选项 + 一个主按钮。
/// 勾选项放在这里而不是选项页，是因为"装完要不要跑"此刻用户才真的关心。
class FinishPage extends StatelessWidget {
  const FinishPage({
    super.key,
    required this.runAfterInstall,
    required this.onRunAfterChanged,
    required this.onFinish,
    this.launchOnFinish = false,
  });

  final bool runAfterInstall;
  final ValueChanged<bool> onRunAfterChanged;
  final VoidCallback onFinish;

  /// M1 接引擎后由外部告知是否真的拉起了 App（M0 只切窗口）
  final bool launchOnFinish;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(40, 52, 40, 28),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 成功符号：渐变圆 + 白勾，比一个 Icons.check_circle 更有"自己的品牌"
          Container(
            width: 62,
            height: 62,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              gradient: Brand.primaryGradient,
              boxShadow: [
                BoxShadow(
                  color: Brand.primary.withValues(alpha: 0.28),
                  blurRadius: 20,
                  offset: const Offset(0, 6),
                ),
              ],
            ),
            child: const Icon(Icons.check_rounded, color: Colors.white, size: 34),
          ),
          const SizedBox(height: 22),
          const Text(
            '安装完成',
            style: TextStyle(
              fontSize: 23,
              fontWeight: FontWeight.w700,
              color: Brand.textMain,
            ),
          ),
          const SizedBox(height: 10),
          Text(
            launchOnFinish
                ? 'LxAI 已启动，正在后台就绪。'
                : 'LxAI 已安装到你的电脑，可以开始远程连接了。',
            style: const TextStyle(fontSize: 13.5, color: Brand.textMuted, height: 1.8),
          ),
          const SizedBox(height: 26),
          _CheckRow(
            value: runAfterInstall,
            onChanged: onRunAfterChanged,
            label: '立即运行 LxAI',
          ),
          const SizedBox(height: 6),
          const Text(
            '卸载时会保留你的聊天记录与登录状态。',
            style: TextStyle(fontSize: 11.5, color: Brand.textFaint),
          ),
          const Spacer(),
          Row(
            children: [
              PrimaryButton(
                label: '完成',
                onTap: onFinish,
                width: 120,
              ),
            ],
          ),
        ],
      ),
    );
  }
}

/// 复选框一行（自绘，保持与整体风格一致；Material 默认样式在这里偏"系统"。）
class _CheckRow extends StatelessWidget {
  const _CheckRow({required this.value, required this.onChanged, required this.label});

  final bool value;
  final ValueChanged<bool> onChanged;
  final String label;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: () => onChanged(!value),
        behavior: HitTestBehavior.opaque,
        child: Row(
          children: [
            AnimatedContainer(
              duration: const Duration(milliseconds: 130),
              width: 17,
              height: 17,
              decoration: BoxDecoration(
                color: value ? Brand.primary : Colors.white,
                borderRadius: BorderRadius.circular(4),
                border: Border.all(
                  color: value ? Brand.primary : const Color(0xFFCBD5E1),
                  width: 1.4,
                ),
              ),
              child: value
                  ? const Icon(Icons.check_rounded, size: 13, color: Colors.white)
                  : null,
            ),
            const SizedBox(width: 9),
            Text(
              label,
              style: const TextStyle(fontSize: 13, color: Brand.textMain),
            ),
          ],
        ),
      ),
    );
  }
}
