import 'dart:io';

import 'package:flutter/material.dart';

import '../theme/brand.dart';
import '../widgets/buttons.dart';

/// 第 2 步 · 安装选项（"自定义安装"进来的那一页）。
///
/// 三块内容照国内大厂的顺序排：**协议 → 安装位置 → 附加任务**。
/// 协议没勾时主按钮置灰 —— 这是唯一一处"必须拦住用户"的地方。
class OptionsPage extends StatefulWidget {
  const OptionsPage({
    super.key,
    required this.installDir,
    required this.agreed,
    required this.desktopIcon,
    required this.autoStart,
    required this.onDirChanged,
    required this.onAgreedChanged,
    required this.onDesktopIconChanged,
    required this.onAutoStartChanged,
    required this.onBack,
    required this.onStart,
  });

  final String installDir;
  final bool agreed;
  final bool desktopIcon;
  final bool autoStart;
  final ValueChanged<String> onDirChanged;
  final ValueChanged<bool> onAgreedChanged;
  final ValueChanged<bool> onDesktopIconChanged;
  final ValueChanged<bool> onAutoStartChanged;
  final VoidCallback onBack;
  final VoidCallback onStart;

  @override
  State<OptionsPage> createState() => _OptionsPageState();
}

class _OptionsPageState extends State<OptionsPage> {
  late final TextEditingController _dirCtrl =
      TextEditingController(text: widget.installDir);

  /// 「为所有用户」是 M0 的界面占位：真正提权在 M3 做
  /// （勾了之后用 ShellExecuteW(runas) 带 /ALLUSERS 重启自己，见 笔记-04 §5）
  bool _allUsers = false;

  static String _programFilesDir() {
    final pf = Platform.environment['ProgramFiles'] ?? r'C:\Program Files';
    return '$pf\\LxAI';
  }

  @override
  void dispose() {
    _dirCtrl.dispose();
    super.dispose();
  }

  void _toggleAllUsers(bool v) {
    setState(() => _allUsers = v);
    // 安装位置跟着权限模式走，避免出现"选了 Program Files 却按用户目录装"的错位
    _dirCtrl.text = v ? _programFilesDir() : _defaultUserDir();
    widget.onDirChanged(_dirCtrl.text);
  }

  static String _defaultUserDir() {
    final local = Platform.environment['LOCALAPPDATA'];
    if (local != null && local.isNotEmpty) return '$local\\Programs\\LxAI';
    return r'C:\Program Files\LxAI';
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(40, 34, 40, 26),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text(
            '安装选项',
            style: TextStyle(
              fontSize: 20,
              fontWeight: FontWeight.w700,
              color: Brand.textMain,
            ),
          ),
          const SizedBox(height: 6),
          const Text(
            '选择安装位置与附加任务，也可以直接下一步',
            style: TextStyle(fontSize: 12.5, color: Brand.textMuted),
          ),
          const SizedBox(height: 20),

          // ── 协议 ──────────────────────────────────────────────
          Row(
            children: [
              _CheckBox(
                value: widget.agreed,
                onChanged: widget.onAgreedChanged,
              ),
              const SizedBox(width: 9),
              const Text('我已阅读并同意', style: TextStyle(fontSize: 12.5, color: Brand.textMain)),
              LinkButton('《用户协议》', () {}),
              const Text('与', style: TextStyle(fontSize: 12.5, color: Brand.textMain)),
              LinkButton('《隐私政策》', () {}),
            ],
          ),
          const SizedBox(height: 20),

          // ── 安装位置 ──────────────────────────────────────────
          const _SectionLabel('安装位置'),
          const SizedBox(height: 8),
          Row(
            children: [
              Expanded(
                child: SizedBox(
                  height: 38,
                  child: TextField(
                    controller: _dirCtrl,
                    onChanged: widget.onDirChanged,
                    style: const TextStyle(fontSize: 12.5, color: Brand.textMain),
                    decoration: InputDecoration(
                      isDense: true,
                      contentPadding:
                          const EdgeInsets.symmetric(horizontal: 11, vertical: 10),
                      filled: true,
                      fillColor: Colors.white,
                      enabledBorder: OutlineInputBorder(
                        borderRadius: BorderRadius.circular(Brand.radiusSm),
                        borderSide: const BorderSide(color: Brand.border),
                      ),
                      focusedBorder: OutlineInputBorder(
                        borderRadius: BorderRadius.circular(Brand.radiusSm),
                        borderSide: const BorderSide(color: Brand.primary, width: 1.4),
                      ),
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 8),
              GhostButton(
                label: '浏览',
                width: 68,
                // M0 占位：真正的目录选择在 M1 接 FilePicker / 自绘目录树
                onTap: () {},
              ),
            ],
          ),
          const SizedBox(height: 22),

          // ── 附加任务 ──────────────────────────────────────────
          const _SectionLabel('附加任务'),
          const SizedBox(height: 4),
          _SwitchRow(
            value: widget.desktopIcon,
            onChanged: widget.onDesktopIconChanged,
            label: '创建桌面快捷方式',
          ),
          _SwitchRow(
            value: widget.autoStart,
            onChanged: widget.onAutoStartChanged,
            label: '开机自动启动',
          ),
          _SwitchRow(
            value: _allUsers,
            onChanged: _toggleAllUsers,
            label: '为所有用户安装',
            hint: '需要管理员权限，将安装到 Program Files',
          ),

          const Spacer(),
          Row(
            children: [
              GhostButton(label: '上一步', width: 96, onTap: widget.onBack),
              const Spacer(),
              PrimaryButton(
                label: '开始安装',
                icon: Icons.download_rounded,
                enabled: widget.agreed,
                onTap: widget.onStart,
                width: 142,
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

class _CheckBox extends StatelessWidget {
  const _CheckBox({required this.value, required this.onChanged});

  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: () => onChanged(!value),
        child: AnimatedContainer(
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
      ),
    );
  }
}

class _SwitchRow extends StatelessWidget {
  const _SwitchRow({
    required this.value,
    required this.onChanged,
    required this.label,
    this.hint,
  });

  final bool value;
  final ValueChanged<bool> onChanged;
  final String label;
  final String? hint;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 1),
      child: Row(
        children: [
          Text(label, style: const TextStyle(fontSize: 12.5, color: Brand.textMain)),
          const SizedBox(width: 8),
          if (hint != null)
            Expanded(
              child: Text(
                hint!,
                style: const TextStyle(fontSize: 11, color: Brand.textFaint),
                overflow: TextOverflow.ellipsis,
              ),
            )
          else
            const Spacer(),
          Transform.scale(
            scale: 0.82,
            child: Switch(
              value: value,
              onChanged: onChanged,
              activeThumbColor: Colors.white,
              activeTrackColor: Brand.primary,
            ),
          ),
        ],
      ),
    );
  }
}
