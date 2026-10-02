# LxAI Windows 安装器（自研 · Flutter 自绘）

替代 LxAI 项目里那套 Inno Setup 安装器。

**当初为什么换掉 Inno（2026-10-04 修正措辞）**：先前这里写的是"后者只能换向导图，
版式改不动"——这话**不准确**。旧的那份 `.iss` 其实有 `[Code]` 段，用 Pascal Script
往安装页上动态贴了图片+标题+描述做幻灯片，不只换了图。
真正的限制是：**只能在 `WizardForm` 既定版式里做加法**，按钮行、页面框架这些改不动；
要再往下走就得上第三方皮肤 DLL（botva2 / InnoUI 那类），属于不受支持的注入式做法
（未签名 DLL、非官方 API、跨 Inno 版本与 DPI 都脆）。自研 Flutter 是为了**用正常 UI
框架拿到完整控制权**，代价是 ~28 MB 的 Flutter 运行时、且做不成单文件。

**当前状态：M0 / M1 / M2 / M4 已完成** —— 无边框四步向导 + 真实安装引擎
（解包素材、写文件、建快捷方式、写注册表卸载项、清占用进程、覆盖安装、开机自启）
+ 完整卸载流程（含 VBS 延迟自毁）。
**未完成：M3（单文件打包 / 提权分支）与 M5（代码签名 / 对接自更新）**，见下方「里程碑」与
「单文件安装包」两节。

## 跑起来

```powershell
flutter run -d windows              # 开发（热重载）
flutter build windows --release     # 产物：build\windows\x64\runner\Release\lxai_setup.exe
powershell -File tool\build-payload.ps1   # 把 App 产物 + 私有 Python 铺进 payload\
```

**开发期必须把 `payload\` 和 exe 放在一起**（引擎会从 exe 所在目录逐级向上找它），
所以现在还不是"一个 exe 拷到哪都能装"的形态 —— 原因与出路见「单文件安装包」一节。

## 结构

| 路径 | 职责 |
| :--- | :--- |
| `lib/main.dart` | 入口：无边框窗口（820×620、固定尺寸、居中、系统标题栏交给自绘）；命令行开关解析 |
| `lib/app.dart` | 四步状态机（欢迎 / 选项 / 进度 / 完成）+ 卸载确认；**唯一接安装引擎的地方** |
| `lib/theme/brand.dart` | 全部配色与**窗口尺寸** token；改观感只改这里，页面里不写死颜色 |
| `lib/core/installer_engine.dart` | 安装引擎：铺文件 → 建快捷方式 → 注册表 → 部署卸载器 |
| `lib/core/uninstaller.dart` | 卸载流程：删文件、清注册表/快捷方式、VBS 延迟自毁；用户数据默认保留 |
| `lib/core/registry.dart` · `shortcut.dart` · `ps.dart` | 注册表、快捷方式、PowerShell 调用（均走 `.ps1` + `-File`） |
| `lib/pages/` | 欢迎 / 选项 / 进度 / 完成 / 卸载确认 |
| `lib/widgets/` | 自绘标题栏、左侧品牌区、主次按钮 |
| `tool/build-payload.ps1` | 组装 `payload\`（App 产物 + 私有 Python 运行时 + 许可文本 + 图标） |
| `assets/` | 品牌图标（由 LxAI 的 `tools/make_icons.py` 生成）与功能截图 |

## 里程碑

- [x] **M0** 无边框窗口 + 四步跳转
- [x] **M1** payload 解压 + 写文件 + 建快捷方式
- [x] **M2** 注册表卸载项 + `--uninstall` 模式（部署为 `{app}\uninstaller\uninstall.exe`）
- [ ] **M3** 单文件打包 + 提权分支 ← **两件事都还没做，见下节**
- [x] **M4** 覆盖安装（清占用进程）+ 开机自启
- [ ] **M5** 代码签名 + 对接 App 自更新

设计稿与踩坑记录在 `..\lxai-installer-research\` 的 `笔记-04-自研安装器方案.md`、`笔记-05-M0原型说明.md`。

## 单文件安装包（M3）：实测结论与待选项

> 这一节是**实测**出来的，不是推测。重做之前先看完，能省掉一轮试错。

### 结论：Flutter 应用做不到"单个 exe 直接跑"

试过的做法：把素材压成 zip 追加到安装器 exe 尾部（Windows 加载 PE 只认头部结构、
尾部多余字节会忽略 —— Inno / NSIS 就是这么干的），运行时按偏移自读自解。
脚本产出了 25.2 MB 的单文件、大小校验与尾部魔数都正确。

**结果：双击起不来。** 二分测试直接指向根因：

| 测试 | 结果 |
| :--- | :--- |
| **原始 exe（完全没动过）单独拷到别的目录** | **✘ 起不来** |
| 追加 1KB / 1MB / 25MB | ✘ 起不来 |

**问题不在追加，而在于 Flutter 的 exe 本来就不能单独运行** —— 它必须和
`flutter_windows.dll`（20.8 MB）、`data\app.so`、各插件 DLL 待在一起。
所以"让 exe 读自己尾巴解包"这条路逻辑上就不成立：**Flutter 引擎还没启动，谁来解包。**

### 代价：任何"保留 Flutter 向导 + 单文件"的方案都是 ~44 MB

| 组成 | 大小 |
| :--- | ---: |
| 安装器自身的 Flutter 运行时 | 28 MB |
| 素材 payload | 56 MB → 压后 25 MB |
| 自解压引导器（原生） | ~5 MB |
| **合计** | **≈ 44 MB** |

对比：旧 Inno 包 `LxAI-Setup-1.0.1.exe` 是 **22 MB**（`lzma2/max` + solid）。
贵的这一倍，全是那个自定义 Flutter 向导的运行时。

### 待选的四条路

| 方案 | 单文件 | 体积 | UI 自定义 | 工作量 |
| :--- | :---: | ---: | :--- | :--- |
| **Inno Setup + 皮肤** | ✓ | ~22 MB | 强（国内大厂安装器多数就是这么做的） | 中 |
| **NSIS + 插件** | ✓ | ~22 MB | 强 | 中 |
| **原生 C++/Win32** | ✓ | ~1-2 MB | 完全自由 | 高（现有引擎逻辑需移植） |
| **Dart CLI 引导器 + 本工程** | ✓ | ~44 MB | 本工程的 UI 原样保留 | 低 |

两条补充事实：

- **「不用 Flutter」不等于「放弃自定义界面」**：微信/QQ 那类大图背景 + 无边框 +
  自定义按钮的安装器，绝大多数是 Inno Setup + 皮肤插件（botva2 / InnoUI 之类）做的。
  代价是那类皮肤库多为第三方个人项目，不是官方 API。
- **Inno 原生就在 `{app}` 放 `unins000.exe`**（装完目录里那个卸载 exe），
  不用像本工程现在这样自己部署一份到 `{app}\uninstaller\`。

### 另一件没做的事：提权

「为所有用户安装」开关目前**只改了安装路径与注册表分支（HKLM）**，
**没有实际做 UAC 提权**（没有 `ShellExecuteEx` + `runas`）。所以非管理员勾它，
写 `Program Files` / HKLM 会失败。要做 M3 时一并补上。

## ⚠️ 开发期注意

自研安装器与机器上装的 LxAI 是**同一个 App**（共用卸载项 GUID
`{8CC1E567-9691-46FF-914C-B7A24B230C39}`，所以自研版能原地覆盖 Inno 版）。
测试真安装逻辑时如需隔离，请用独立的注册表键与安装目录（例如
`LxAI-Dev` / `%LOCALAPPDATA%\Programs\LxAI-Dev`），避免影响正式安装的那一份。

## 许可

Apache-2.0（与 LxAI 项目一致）。
