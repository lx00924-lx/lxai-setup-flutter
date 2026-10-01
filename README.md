# LxAI Windows 安装器（自研 · Flutter 自绘）

替代 LxAI 项目里那套 Inno Setup 安装器 —— 后者只能换向导图，版式改不动，做不出自定义界面。

**当前状态：M0 已完成** —— 无边框窗口 + 四步向导 + 全自绘界面；安装逻辑尚未接入
（进度条是假 Timer，真实引擎见下方里程碑）。

## 跑起来

```powershell
flutter run -d windows              # 开发（热重载）
flutter build windows --release     # 产物：build\windows\x64\runner\Release\lxai_setup.exe
```

## 结构

| 路径 | 职责 |
| :--- | :--- |
| `lib/main.dart` | 入口：无边框窗口（820×520、固定尺寸、居中、系统标题栏交给自绘） |
| `lib/app.dart` | 四步状态机（欢迎 / 选项 / 进度 / 完成）—— **将来唯一接安装引擎的地方** |
| `lib/theme/brand.dart` | 全部配色与尺寸 token；改观感只改这里，页面里不写死颜色 |
| `lib/widgets/` | 自绘标题栏、左侧品牌区、主次按钮 |
| `lib/pages/` | 四个向导页 |
| `assets/` | 品牌图标（由 LxAI 的 `tools/make_icons.py` 生成）与功能截图 |

## 里程碑

- [x] **M0** 无边框窗口 + 四步跳转 + 假进度
- [ ] **M1** payload 解压 + 写文件 + 建快捷方式
- [ ] **M2** 注册表卸载项 + `--uninstall` 模式
- [ ] **M3** 单文件打包 + 提权分支
- [ ] **M4** 覆盖安装（清占用进程）+ 开机自启
- [ ] **M5** 代码签名 + 对接 App 自更新

设计稿与踩坑记录在 `..\lxai-installer-research\` 的 `笔记-04-自研安装器方案.md`、`笔记-05-M0原型说明.md`。

## ⚠️ 开发期注意

自研安装器与机器上已装的 **Inno 版 LxAI 是同一个 App**。测试真安装逻辑时，
请用独立的注册表键与安装目录（例如 `LxAI-Dev` / `%LOCALAPPDATA%\Programs\LxAI-Dev`），
不要覆盖正式安装的那一份 —— 否则会出现两个卸载项、文件互相覆盖。

## 许可

Apache-2.0（与 LxAI 项目一致）。