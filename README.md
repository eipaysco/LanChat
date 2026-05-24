# LanChat

局域网点对点通讯小工具，基于 Qt + C++17。**无第三方依赖**，只用 Qt 自带模块。

支持：
- 文字消息
- 文件发送（拖拽 / 选择，支持大文件，进度显示）
- UDP 广播自动发现局域网内对端
- 端口可配置

详细设计见 [DESIGN.md](DESIGN.md)。

> ⚠️ **关于安全**：本版本走的是明文 TCP，**不加密**。适合在可信内网使用。如果需要加密，
> 后续可以集成 OpenSSL + QSslSocket（参见 DESIGN.md 第 13 节"后续可扩展"）。

---

## 构建（Windows）

### 依赖
- Qt 5.14+ 或 Qt 6.x（已在 Qt 5.14.2 / Qt 6.11 MinGW 环境验证）
- 无其他第三方库

### 编译
```bat
:: 用对应的 Qt 命令行环境，或先运行 Qt 的 qtenv2.bat
qmake LanChat.pro
nmake          :: 或 mingw32-make
```

或者直接在 Qt Creator 里打开 `LanChat.pro`，按 Ctrl+B 构建、Ctrl+R 运行。

### 部署
```bat
windeployqt --release LanChat.exe
```

`windeployqt` 会自动复制需要的 Qt DLL，**不需要额外的 OpenSSL DLL**。

---

## 使用

1. 在两台同局域网的电脑上各启动 LanChat
2. 主窗口左侧的"已发现对端"列表会出现对方（约 3 秒内）
3. 双击对方 → 直接建立连接
4. 在右侧输入框打字回车发送；点"选文件"或**直接拖文件到聊天区**发送

### 防火墙
首次运行 Windows 防火墙会弹窗，必须勾**专用网络**放行。否则发现不到对端。

### 端口
默认 TCP 45454 / UDP 45455，在 设置 里可改。两端如果都改，须改成同样的端口。

### 不能自动发现？手动连接
菜单 → 文件 → 手动连接... → 输入对方 IP 和端口。

---

## 数据目录

`%APPDATA%\LanChat\`：
- `config.ini` — 配置（端口、机器名、保存路径等）
- `profiles\<name>\config.ini` — 使用 `--profile <name>` 启动时的独立配置

要重置程序，关掉 LanChat 后删除这个目录即可。

---

## 同机测试

在同一台机器上跑两个实例自测试：

1. 启动第一个实例：`LanChat.exe --profile first`
2. 启动第二个实例：`LanChat.exe --profile second`
3. 第一个实例保持 TCP 45454、UDP 45455（默认）
4. 第二个实例在设置里改 TCP 端口为 45464、UDP 端口为 45455，然后用同一个 profile 重启：`LanChat.exe --profile second`
5. 两个实例应当互相在对端列表里看到对方
6. 双击发送测试
