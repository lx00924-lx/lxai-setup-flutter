import 'package:flutter/material.dart';

import '../theme/brand.dart';
import '../widgets/buttons.dart';

/// 第 1 步 · 欢迎页。
///
/// 结构照国内大厂的规矩：**左对齐大标题 + 一句话价值主张 + 三个卖点 + 两个按钮**
/// （一个"无脑下一步"的主按钮 + 一个"我想自己选"的次按钮）。
class WelcomePage extends StatelessWidget {
  const WelcomePage({
    super.key,
    required this.onQuickInstall,
    required this.onCustomInstall,
  });

  final VoidCallback onQuickInstall;
  final VoidCallback onCustomInstall;

  static const List<(IconData, String, String)> _highlights = [
    (Icons.bolt_rounded, '一键安装', '内置运行环境\n无需另装 Python'),
    (Icons.shield_moon_rounded, '数据本机', '聊天记录与密钥\n只留在你的电脑'),
    (Icons.link_rounded, '免公网 IP', '反向长连接\n扫码即可远程'),
  ];

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(40, 44, 40, 28),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text(
            '欢迎使用 LxAI 安装向导',
            style: TextStyle(
              fontSize: 25,
              fontWeight: FontWeight.w700,
              color: Brand.textMain,
              height: 1.25,
            ),
          ),
          const SizedBox(height: 12),
          const Text(
            '轻量原生客户端，用来远程指挥你电脑上的私有 Agent。\n全程约 20 秒，装完即可扫码连接。',
            style: TextStyle(fontSize: 13.5, color: Brand.textMuted, height: 1.9),
          ),
          const SizedBox(height: 30),
          Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              for (var i = 0; i < _highlights.length; i++) ...[
                Expanded(
                  child: _Highlight(
                    icon: _highlights[i].$1,
                    title: _highlights[i].$2,
                    desc: _highlights[i].$3,
                  ),
                ),
                if (i != _highlights.length - 1) const SizedBox(width: 10),
              ],
            ],
          ),
          const Spacer(),
          Row(
            children: [
              PrimaryButton(
                label: '快速安装',
                icon: Icons.bolt_rounded,
                onTap: onQuickInstall,
                width: 142,
              ),
              const SizedBox(width: 12),
              GhostButton(label: '自定义安装', onTap: onCustomInstall, width: 120),
            ],
          ),
          const SizedBox(height: 12),
          const Text(
            '快速安装将使用默认位置，并创建桌面快捷方式',
            style: TextStyle(fontSize: 11.5, color: Brand.textFaint),
          ),
        ],
      ),
    );
  }
}

class _Highlight extends StatelessWidget {
  const _Highlight({required this.icon, required this.title, required this.desc});

  final IconData icon;
  final String title;
  final String desc;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.fromLTRB(12, 13, 10, 13),
      decoration: BoxDecoration(
        color: Brand.surfaceAlt,
        borderRadius: BorderRadius.circular(Brand.radius),
        border: Border.all(color: Brand.border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, size: 19, color: Brand.primary),
          const SizedBox(height: 9),
          Text(
            title,
            style: const TextStyle(
              fontSize: 12.5,
              fontWeight: FontWeight.w600,
              color: Brand.textMain,
            ),
          ),
          const SizedBox(height: 5),
          Text(
            desc,
            style: const TextStyle(fontSize: 11, color: Brand.textMuted, height: 1.6),
          ),
        ],
      ),
    );
  }
}
