import 'package:flutter/material.dart';

import '../theme/brand.dart';

/// 左侧固定品牌区。
///
/// 国内大厂安装器基本都是这个结构：左边一条**纯视觉**的固定带（不放任何交互），
/// 右边才是向导内容。好处是"品牌感"和"操作区"物理分离，用户视线不会乱。
class BrandPanel extends StatelessWidget {
  const BrandPanel({super.key, this.version = '1.0.1'});

  final String version;

  static const List<String> _features = [
    '手机远程指挥电脑上的 Agent',
    '无需公网 IP，扫码即连',
    '聊天记录与密钥都留在本机',
  ];

  @override
  Widget build(BuildContext context) {
    return Container(
      width: Brand.brandPanelWidth,
      decoration: const BoxDecoration(gradient: Brand.deepGradient),
      child: Stack(
        children: [
          // 光晕：用径向渐变画，而不是再贴一张位图 —— 任意分辨率都清晰，改色也只改一个常量
          Positioned.fill(
            child: DecoratedBox(
              decoration: const BoxDecoration(gradient: Brand.glow),
            ),
          ),
          Padding(
            padding: const EdgeInsets.fromLTRB(24, 40, 24, 22),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Image.asset('assets/app_icon.png', width: 84, height: 84),
                const SizedBox(height: 20),
                const Text(
                  'LxAI',
                  style: TextStyle(
                    color: Colors.white,
                    fontSize: 30,
                    fontWeight: FontWeight.w700,
                    letterSpacing: 1.5,
                    height: 1.1,
                  ),
                ),
                const SizedBox(height: 6),
                const Text(
                  '私有 Agent 远程遥控',
                  style: TextStyle(color: Brand.sky, fontSize: 13, letterSpacing: 0.5),
                ),
                const SizedBox(height: 26),
                for (final f in _features) ...[
                  Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Container(
                        margin: const EdgeInsets.only(top: 6),
                        width: 5,
                        height: 5,
                        decoration: const BoxDecoration(
                          color: Brand.amber,
                          shape: BoxShape.circle,
                        ),
                      ),
                      const SizedBox(width: 10),
                      Expanded(
                        child: Text(
                          f,
                          style: const TextStyle(
                            color: Color(0xFFCBD5E1),
                            fontSize: 12.5,
                            height: 1.55,
                          ),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 10),
                ],
                const Spacer(),
                Text(
                  'v$version · Apache-2.0',
                  style: const TextStyle(color: Color(0xFF64809B), fontSize: 11),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
