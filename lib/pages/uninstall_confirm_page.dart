import 'package:flutter/material.dart';

import '../theme/brand.dart';
import '../widgets/buttons.dart';

/// 卸载模式的第一个页面：确认 + **说清楚删什么、留什么**，并让用户自己选要不要连数据一起清。
///
/// 大厂的卸载确认页都会明确告知数据去向 —— 用户最怕的就是"卸个软件把聊天记录也带走了"。
/// 所以这里的默认是**保留数据**，"连数据一起清"必须用户主动勾选，且勾选后立刻变成醒目警告。
class UninstallConfirmPage extends StatelessWidget {
  const UninstallConfirmPage({
    super.key,
    required this.installDir,
    required this.purgeUserData,
    required this.onPurgeChanged,
    required this.onUninstall,
    required this.onCancel,
  });

  final String installDir;

  /// 是否连本地数据（聊天记录 / 登录状态 / 设置）一起删
  final bool purgeUserData;
  final ValueChanged<bool> onPurgeChanged;

  final VoidCallback onUninstall;
  final VoidCallback onCancel;

  static const List<(IconData, String)> _willRemove = [
    (Icons.folder_delete_outlined, '程序文件与内置运行环境'),
    (Icons.link_off_rounded, '桌面与开始菜单快捷方式'),
    (Icons.app_registration_rounded, '注册表中的卸载信息'),
  ];

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(40, 34, 40, 26),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 44,
                height: 44,
                decoration: BoxDecoration(
                  color: const Color(0xFFFEF3C7),
                  borderRadius: BorderRadius.circular(12),
                ),
                child: const Icon(Icons.report_problem_rounded,
                    color: Color(0xFFD97706), size: 24),
              ),
              const SizedBox(width: 13),
              const Expanded(
                child: Text(
                  '确定要卸载 LxAI 吗？',
                  style: TextStyle(
                    fontSize: 20,
                    fontWeight: FontWeight.w700,
                    color: Brand.textMain,
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 14),
          Text(
            installDir,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(fontSize: 11.5, color: Brand.textFaint),
          ),
          const SizedBox(height: 18),

          const _SectionLabel('将会删除'),
          const SizedBox(height: 6),
          for (final (icon, text) in _willRemove)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 2.5),
              child: Row(
                children: [
                  Icon(icon, size: 15, color: Brand.textMuted),
                  const SizedBox(width: 9),
                  Text(text, style: const TextStyle(fontSize: 12.5, color: Brand.textMain)),
                ],
              ),
            ),

          const SizedBox(height: 18),
          const _SectionLabel('本地数据'),
          const SizedBox(height: 7),

          // 复选框：默认不勾 = 保留数据
          _PurgeCheckBox(
            value: purgeUserData,
            onChanged: onPurgeChanged,
            label: '同时删除本地数据',
            hint: '聊天记录、登录状态与全部设置',
          ),
          const SizedBox(height: 10),

          Container(
            width: double.infinity,
            padding: const EdgeInsets.fromLTRB(12, 10, 12, 10),
            decoration: BoxDecoration(
              color: purgeUserData ? const Color(0xFFFEF2F2) : const Color(0xFFF0FDF4),
              borderRadius: BorderRadius.circular(Brand.radius),
              border: Border.all(
                color: purgeUserData ? const Color(0xFFFECACA) : const Color(0xFFBBF7D0),
              ),
            ),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Icon(
                  purgeUserData
                      ? Icons.warning_amber_rounded
                      : Icons.verified_user_outlined,
                  size: 17,
                  color: purgeUserData ? const Color(0xFFDC2626) : const Color(0xFF16A34A),
                ),
                const SizedBox(width: 9),
                Expanded(
                  child: Text(
                    purgeUserData
                        ? '本地数据将被永久删除且无法恢复（含聊天记录、登录状态与所有设置）。'
                        : '聊天记录、登录状态与你的全部设置都会保留，重新安装后自动恢复。',
                    style: TextStyle(
                      fontSize: 12.5,
                      height: 1.6,
                      color: purgeUserData
                          ? const Color(0xFF991B1B)
                          : const Color(0xFF166534),
                    ),
                  ),
                ),
              ],
            ),
          ),

          const Spacer(),
          Row(
            children: [
              GhostButton(label: '取消', width: 92, onTap: onCancel),
              const Spacer(),
              PrimaryButton(
                label: '开始卸载',
                icon: Icons.delete_outline_rounded,
                onTap: onUninstall,
                width: 136,
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _SectionLabel extends StatelessWidget {
  const _SectionLabel(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: const TextStyle(
        fontSize: 12.5,
        fontWeight: FontWeight.w600,
        color: Brand.textMain,
      ),
    );
  }
}

/// 复选框：主标签 + 一行灰色说明（自绘，保持与整体风格一致）
class _PurgeCheckBox extends StatelessWidget {
  const _PurgeCheckBox({
    required this.value,
    required this.onChanged,
    required this.label,
    required this.hint,
  });

  final bool value;
  final ValueChanged<bool> onChanged;
  final String label;
  final String hint;

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
                color: value ? const Color(0xFFDC2626) : Colors.white,
                borderRadius: BorderRadius.circular(4),
                border: Border.all(
                  color: value ? const Color(0xFFDC2626) : const Color(0xFFCBD5E1),
                  width: 1.4,
                ),
              ),
              child: value
                  ? const Icon(Icons.check_rounded, size: 13, color: Colors.white)
                  : null,
            ),
            const SizedBox(width: 9),
            Text(label, style: const TextStyle(fontSize: 12.5, color: Brand.textMain)),
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                hint,
                style: const TextStyle(fontSize: 11, color: Brand.textFaint),
                overflow: TextOverflow.ellipsis,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
