import 'package:flutter/material.dart';

import '../theme/brand.dart';
import '../widgets/buttons.dart';

/// 第 4 步 · 完成页（安装与卸载共用）。
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
    this.isUninstall = false,
    this.installDir = '',
    this.purgedUserData = false,
    this.problems = const [],
  });

  final bool runAfterInstall;
  final ValueChanged<bool> onRunAfterChanged;
  final VoidCallback onFinish;

  /// 是否真的拉起了 App（由外壳在执行后告知）
  final bool launchOnFinish;

  /// 卸载模式：文案与勾选项都不同
  final bool isUninstall;

  final String installDir;

  /// 卸载时是否连本地数据一起删了（决定提示文案）
  final bool purgedUserData;

  /// 没做成的清理步骤（空 = 完全干净）
  final List<String> problems;

  @override
  Widget build(BuildContext context) {
    final String title = isUninstall ? '卸载完成' : '安装完成';
    final String subtitle = isUninstall
        ? (purgedUserData
            ? 'LxAI 已从你的电脑移除，本地数据也已一并清理。'
            : 'LxAI 已从你的电脑移除，程序文件、快捷方式与注册表信息都已清理干净。')
        : (launchOnFinish
            ? 'LxAI 已启动，正在后台就绪。'
            : 'LxAI 已安装到你的电脑，可以开始远程连接了。');

    return Padding(
      padding: const EdgeInsets.fromLTRB(40, 44, 40, 26),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 成功符号：渐变圆 + 白勾，比一个 Icons.check_circle 更有"自己的品牌"
          Container(
            width: 56,
            height: 56,
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
            child: const Icon(Icons.check_rounded, color: Colors.white, size: 31),
          ),
          const SizedBox(height: 18),
          Text(
            title,
            style: const TextStyle(
              fontSize: 22,
              fontWeight: FontWeight.w700,
              color: Brand.textMain,
            ),
          ),
          const SizedBox(height: 9),
          Text(
            subtitle,
            style: const TextStyle(fontSize: 13, color: Brand.textMuted, height: 1.75),
          ),
          const SizedBox(height: 20),

          // 卸载模式：数据去向说明
          if (isUninstall && !purgedUserData)
            const _Notice(
              icon: Icons.verified_user_outlined,
              text: '你的聊天记录、登录状态与全部设置都保留着，重新安装 LxAI 后会自动恢复。',
              fg: Color(0xFF166534),
              bg: Color(0xFFF0FDF4),
              border: Color(0xFFBBF7D0),
            ),

          // 有没做成的步骤：如实告知，不粉饰
          if (problems.isNotEmpty) ...[
            const SizedBox(height: 10),
            _Notice(
              icon: Icons.warning_amber_rounded,
              text: '有 ${problems.length} 项没能完成，可能被占用或权限不足：\n'
                  '${problems.take(3).map((p) => '· $p').join('\n')}'
                  '${problems.length > 3 ? '\n· …' : ''}',
              fg: const Color(0xFF991B1B),
              bg: const Color(0xFFFEF2F2),
              border: const Color(0xFFFECACA),
            ),
          ],

          if (!isUninstall) ...[
            const SizedBox(height: 6),
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
          ],

          const Spacer(),
          Row(
            children: [
              PrimaryButton(label: '完成', onTap: onFinish, width: 116),
            ],
          ),
        ],
      ),
    );
  }
}

class _Notice extends StatelessWidget {
  const _Notice({
    required this.icon,
    required this.text,
    required this.fg,
    required this.bg,
    required this.border,
  });

  final IconData icon;
  final String text;
  final Color fg;
  final Color bg;
  final Color border;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.fromLTRB(12, 10, 12, 10),
      decoration: BoxDecoration(
        color: bg,
        borderRadius: BorderRadius.circular(Brand.radius),
        border: Border.all(color: border),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 17, color: fg),
          const SizedBox(width: 9),
          Expanded(
            child: Text(
              text,
              style: TextStyle(fontSize: 12.5, color: fg, height: 1.65),
            ),
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
