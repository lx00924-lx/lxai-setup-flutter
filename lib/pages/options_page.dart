import 'dart:io';

import 'package:file_picker/file_picker.dart';
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
    required this.startMenuIcon,
    required this.autoStart,
    required this.allUsers,
    required this.createUninstaller,
    required this.onDirChanged,
    required this.onAgreedChanged,
    required this.onDesktopIconChanged,
    required this.onStartMenuIconChanged,
    required this.onAutoStartChanged,
    required this.onAllUsersChanged,
    required this.onCreateUninstallerChanged,
    required this.onBack,
    required this.onStart,
  });

  final String installDir;
  final bool agreed;
  final bool desktopIcon;
  final bool startMenuIcon;
  final bool autoStart;
  final bool allUsers;
  final bool createUninstaller;
  final ValueChanged<String> onDirChanged;
  final ValueChanged<bool> onAgreedChanged;
  final ValueChanged<bool> onDesktopIconChanged;
  final ValueChanged<bool> onStartMenuIconChanged;
  final ValueChanged<bool> onAutoStartChanged;
  final ValueChanged<bool> onAllUsersChanged;
  final ValueChanged<bool> onCreateUninstallerChanged;
  final VoidCallback onBack;
  final VoidCallback onStart;

  @override
  State<OptionsPage> createState() => _OptionsPageState();
}

class _OptionsPageState extends State<OptionsPage> {
  late final TextEditingController _dirCtrl =
      TextEditingController(text: widget.installDir);

  static String _programFilesDir() {
    final pf = Platform.environment['ProgramFiles'] ?? r'C:\Program Files';
    return '$pf\\LxAI';
  }

  @override
  void dispose() {
    _dirCtrl.dispose();
    super.dispose();
  }

  /// 「为所有用户」由外壳统一持有（安装计划要用它决定 HKLM/HKCU 与公共快捷方式），
  /// 这里只负责把变化往上抛，顺带把安装路径切到对应的默认位置。
  void _toggleAllUsers(bool v) {
    widget.onAllUsersChanged(v);
    _dirCtrl.text = v ? _programFilesDir() : _defaultUserDir();
    widget.onDirChanged(_dirCtrl.text);
  }

  static String _defaultUserDir() {
    final local = Platform.environment['LOCALAPPDATA'];
    if (local != null && local.isNotEmpty) return '$local\\Programs\\LxAI';
    return r'C:\Program Files\LxAI';
  }

  /// 「浏览」——调 Windows 自带的文件夹选择对话框。
  ///
  /// 用户取消时返回 null，什么都不改（不能把输入框清空）。
  /// 选完顺手去掉末尾反斜杠：`D:\Apps\` 和 `D:\Apps` 拼出来的子路径会差一个斜杠，
  /// 而安装计划里到处都在做字符串拼接。
  Future<void> _browse() async {
    try {
      // ⚠️ file_picker 13 起 API 变成**静态调用**（`FilePicker.getDirectoryPath`），
      //    8.x 时代的 `FilePicker.platform.getDirectoryPath` 已经没有了。
      final picked = await FilePicker.getDirectoryPath(
        dialogTitle: '选择 LxAI 的安装位置',
        // 从当前填的路径开始浏览，省得用户从头找
        initialDirectory: _dirCtrl.text.trim().isEmpty ? null : _dirCtrl.text.trim(),
      );
      if (picked == null || picked.trim().isEmpty) return; // 用户取消：保持原值
      var dir = picked.trim();
      while (dir.length > 3 && (dir.endsWith('\\') || dir.endsWith('/'))) {
        dir = dir.substring(0, dir.length - 1);
      }
      _dirCtrl.text = dir;
      widget.onDirChanged(dir);
    } catch (e) {
      // 选不出来不该让整个向导崩掉：保持原值，只记一行日志
      // ignore: avoid_print
      print('[Options] 浏览目录失败: $e');
    }
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
                onTap: _browse,
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
            value: widget.startMenuIcon,
            onChanged: widget.onStartMenuIconChanged,
            label: '创建开始菜单快捷方式',
          ),
          _SwitchRow(
            value: widget.autoStart,
            onChanged: widget.onAutoStartChanged,
            label: '开机自动启动',
          ),
          _SwitchRow(
            value: widget.allUsers,
            onChanged: _toggleAllUsers,
            label: '为所有用户安装',
            hint: '需要管理员权限，将安装到 Program Files',
          ),
          _SwitchRow(
            value: widget.createUninstaller,
            onChanged: widget.onCreateUninstallerChanged,
            label: '创建卸载程序',
            hint: '关掉后控制面板里不会出现卸载入口',
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
