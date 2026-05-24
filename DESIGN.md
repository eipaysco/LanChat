# 局域网通讯程序 — 设计文档

一个基于 **Qt 5.14 C++** 的桌面程序，运行于 **Windows**，在同一局域网下的两台电脑之间互相发送消息和文件，并能自动发现对端。

> 📌 本版本只用 Qt 自带模块，**无第三方依赖**（不需要 OpenSSL）。代价是通讯**明文**，仅适用于可信内网。加密功能见第 13 节后续扩展。

---

## 1. 目标与范围

### 必须实现（v1.0）
- 两台局域网内电脑互相通讯
- 文字消息发送
- 文件发送（带进度显示）
- 输入对方 IP 地址的窗口（端口可配置）
- UDP 广播自动发现 — 不用输 IP，启动后能看到局域网内的其他实例
- 拖拽发文件 — 拖文件到窗口直接发

### 不在本版本范围
- 加密 / 认证（明文 TCP）
- 多人聊天（>2 人同时）
- 跨网段 / 公网穿透
- 历史消息持久化

---

## 2. 技术选型

| 项目 | 选择 | 备注 |
|------|------|------|
| Qt 版本 | **Qt 5.14** | 用户指定 |
| 语言标准 | C++17 | Qt 5.14 完全支持 |
| 目标平台 | **Windows** (x64) | Win 7+；推荐 Win 10/11 |
| 编译器 | MSVC 2017/2019 或 MinGW 7.3 | 与 Qt 5.14 安装包对应 |
| 构建系统 | qmake | Qt 5.x 主流 |
| 网络协议 | **TCP**（QTcpServer / QTcpSocket） | 可靠传输 |
| 发现协议 | UDP 广播（QUdpSocket） | 局域网内周期性 announce |
| 序列化 | QDataStream（QDataStream::Qt_5_14） | Qt 内置二进制，无外部依赖 |
| 架构 | 对等（Peer-to-Peer） | 每个实例既是服务端也是客户端 |

**无第三方依赖**：所有功能都用 Qt Network 自带模块实现，编译出来只需要 Qt 的 DLL（用 windeployqt 自动收集）。

---

## 3. 架构总览

```
┌──────────────────────────────────────────────────────────┐
│                       电脑 A                             │
│  ┌────────────────────────────────────────────────────┐  │
│  │                  MainWindow (UI)                   │  │
│  │   状态栏 | 已发现对端列表 | 聊天 | 输入 | 拖拽区  │  │
│  └────┬──────────────┬──────────────┬──────────────┬──┘  │
│       │              │              │              │     │
│  ┌────▼────┐   ┌─────▼──────┐ ┌─────▼──────────┐         │
│  │Discovery│   │ PeerServer │ │PeerConnection  │         │
│  │(QUdpSock)│  │(QTcpServer)│ │ (QTcpSocket +  │         │
│  │广播+监听  │  │   监听     │ │  组帧 + 协议)  │         │
│  └────┬─────┘  └──────┬─────┘ └──────┬─────────┘         │
│       │UDP            │TCP           │TCP                │
└───────┼───────────────┼──────────────┼──────────────────┘
        │ :45455        │ :45454       │
        ▼ 广播          ▼ 监听          ▼ 连出
   ════════════════════ 局 域 网 ══════════════════════════
        ▲               ▲              ▲
        │               │              │
┌───────┼───────────────┼──────────────┼──────────────────┐
│       │               │              │     电脑 B       │
│   (镜像结构)                                              │
└──────────────────────────────────────────────────────────┘
```

### 启动流程
1. **PeerServer** 在 TCP 端口（默认 45454，可配置）开始监听
2. **Discovery** 在 UDP 端口（默认 45455，可配置）开始广播 + 监听
3. UI 启动，主窗口显示已发现对端列表（实时刷新）
4. 用户操作：
   - 双击列表里的对端 → 连接
   - 或菜单"手动连接..." → 弹出 IP/端口输入框

---

## 4. 通讯协议（TCP）

应用层帧（frame）：

```
┌─────────────┬──────────┬─────────────────────┐
│ 帧长度 (4B) │ 类型(1B) │      载荷 (变长)    │
└─────────────┴──────────┴─────────────────────┘
  uint32 BE     uint8
```

- **帧长度**：包括"类型"+"载荷"的字节数，大端
- **类型**：
  - `0x01` HELLO — 连接建立后第一帧，交换 peerId/机器名/版本
  - `0x02` TEXT — 文字消息
  - `0x03` FILE_OFFER — 提议发送文件（含元信息）
  - `0x04` FILE_ACCEPT — 接收方同意
  - `0x05` FILE_REJECT — 接收方拒绝
  - `0x06` FILE_CHUNK — 文件数据块
  - `0x07` FILE_END — 传输完成

### 载荷编码（QDataStream，版本 `Qt_5_14`）

| 类型 | 字段 |
|------|------|
| HELLO       | `QString peerId, QString machineName, QString appVersion` |
| TEXT        | `QString message` |
| FILE_OFFER  | `QString transferId, QString fileName, qint64 fileSize` |
| FILE_ACCEPT | `QString transferId` |
| FILE_REJECT | `QString transferId, QString reason` |
| FILE_CHUNK  | `QString transferId, QByteArray data`（每块 64 KB） |
| FILE_END    | `QString transferId` |

### 接收端组帧
TCP 是字节流：
1. `QByteArray buffer`，每次 `readyRead()` 把新字节 append
2. 循环：buffer 长度 ≥ 4 → 读出 N；buffer 长度 ≥ 4+N → 截出一帧处理；否则等下次
3. 解析类型，按类型反序列化载荷

### 握手流程
1. TCP 建立连接（QTcpSocket connected 信号 / QTcpServer incomingConnection）
2. 双方各自立即发 HELLO 帧
3. 收到对端 HELLO → 记录 peerId / 机器名 → 触发 `connected()` 信号 → UI 切到"已连接"
4. 之后任意时刻可以发消息或文件

如果 10 秒内没收到对端 HELLO → 主动断开报错。

---

## 5. UDP 自动发现

### 5.1 端口与广播地址
- 默认 UDP 端口：**45455**（可配置）
- 同时发：每个网卡的子网广播地址 + `255.255.255.255` 兜底（Windows 兼容性）

### 5.2 Announce 报文（JSON）
```json
{
  "magic": "LANCHAT-v1",
  "peerId": "uuid-v4 字符串",
  "machineName": "Alice-PC",
  "tcpPort": 45454,
  "timestamp": 1716240000
}
```

- **peerId**：UUID，第一次启动时生成，存配置文件，标识"这台机器的这个程序实例"

### 5.3 广播节奏
- 启动后立刻发一次
- 之后每 3 秒（可配置）广播一次
- 退出前发 `"bye": true` 报文，让对方立刻从列表移除

### 5.4 接收侧逻辑
- 收到包 → 校验 magic → 解析
- 忽略自己（peerId 等于本机）
- 维护 `QMap<peerId, PeerInfo>`，每次收到包更新 `lastSeen`
- 定时（每秒）扫描，**超过 10 秒**没收到的对端从列表移除

### 5.5 UI 中的对端列表
- 显示：机器名 + IP + 端口
- 双击 → 用该 peer 的地址发起 TCP 连接

---

## 6. UI 设计

### 6.1 主窗口

```
┌──────────────────────────────────────────────────────────────┐
│ 文件(F)  帮助(H)                                              │
├────────────────────┬─────────────────────────────────────────┤
│ 已发现对端          │ 已连接：Bob-PC (192.168.1.42)   [断开]  │
│                    ├─────────────────────────────────────────┤
│ ● Bob-PC           │ [10:23] 我: 你好                        │
│   192.168.1.42     │ [10:23] Bob: hi                         │
│ ● Carol-PC         │                                         │
│   192.168.1.50     ├─────────────────────────────────────────┤
│                    │ 文件传输                                │
│                    │  → report.pdf 1.0 MB / 2.3 MB  (45%)    │
│                    ├─────────────────────────────────────────┤
│                    │  ┌─ 把文件拖到这里直接发送 ─┐           │
│                    │  └──────────────────────────┘           │
│                    ├─────────────────────────────────────────┤
│                    │ ┌──────────────────────┐ [发送][选文件] │
│                    │ │ 输入消息...          │                │
│ [手动连接...]      │ └──────────────────────┘                │
├────────────────────┴─────────────────────────────────────────┤
│ 本机: Alice-PC | IP: 192.168.1.10 | TCP 45454 UDP 45455      │
└──────────────────────────────────────────────────────────────┘
```

### 6.2 手动连接对话框 `ConnectDialog`
```
┌──────────────────────────────────┐
│  手动连接                         │
├──────────────────────────────────┤
│  IP 地址: [192.168.1.42       ]  │
│  端口:    [45454              ]  │
│                                  │
│        [连接]    [取消]          │
└──────────────────────────────────┘
```

### 6.3 设置对话框 `SettingsDialog`
- TCP 监听端口 / UDP 发现端口 / 广播间隔
- 机器名（显示给对端）
- 默认文件保存目录

### 6.4 文件接收提示
对端发 FILE_OFFER → QMessageBox：
- "Bob-PC 要发送 report.pdf (2.3 MB)，接收吗？"
- 接受 → QFileDialog 选保存位置 → 发 FILE_ACCEPT
- 拒绝 → 发 FILE_REJECT

---

## 7. 拖拽发文件

### 7.1 启用
DropArea 是一个独立小部件，setAcceptDrops(true)。

### 7.2 事件处理
- `dragEnterEvent`：检查 `mimeData->hasUrls()` 且都是本地文件 → 接受
- `dropEvent`：拿 url 列表，对每个文件调 `m_conn->sendFile(path)`

### 7.3 视觉反馈
拖入窗口时高亮（变色 + 提示"释放以发送 N 个文件"），离开恢复。

### 7.4 多文件
拖入多个文件 → 串行发送（一个完成再发下一个）。

---

## 8. 文件结构

```
LanChat/
├── LanChat.pro                  ← qmake 项目文件
├── DESIGN.md                    ← 本文档
├── README.md
└── src/
    ├── main.cpp                 ← 入口
    ├── Protocol.h               ← 帧类型常量
    ├── Settings.h/.cpp          ← QSettings 包装
    ├── PeerServer.h/.cpp        ← 包装 QTcpServer
    ├── PeerConnection.h/.cpp    ← 包装 QTcpSocket + 组帧 + 协议
    ├── Discovery.h/.cpp         ← UDP 广播 + 监听 + 对端表
    ├── DropArea.h/.cpp          ← 接受拖拽的小部件
    ├── ConnectDialog.h/.cpp     ← 手动输入 IP/端口
    ├── SettingsDialog.h/.cpp    ← 设置对话框
    └── MainWindow.h/.cpp        ← 主窗口
```

### 各类职责

| 类 | 职责 |
|----|------|
| `MainWindow` | UI 编排，连接 Discovery / PeerServer / PeerConnection 信号到界面 |
| `ConnectDialog` | 手动输入 IP + 端口 |
| `SettingsDialog` | 编辑配置 |
| `Discovery` | QUdpSocket 收发 announce；维护 `DiscoveredPeer` 列表；信号 `peerSeen / peerLost` |
| `PeerServer` | 监听 TCP，新连接到来时把 QTcpSocket emit 出去 |
| `PeerConnection` | 拿 QTcpSocket → 组帧 → 信号 `textReceived / fileOffered / progress / finished / disconnected / error` |
| `Settings` | QSettings + INI 后端，配置文件在 `%APPDATA%\LanChat\config.ini` |
| `DropArea` | 一个接受拖拽的 QWidget，发信号 `filesDropped(QStringList)` |

---

## 9. 关键实现要点

### 9.1 大文件分块 + 背压
- 别一次性 `readAll()`
- 用 `QFile::read(64*1024)`，每块封 FILE_CHUNK → `socket->write(frame)`
- 当 `socket->bytesToWrite() < 1 MB` 时继续写下一块（背压阈值）
- 监 `bytesWritten` 信号驱动下一块发送
- 用 `QTimer::singleShot(0, ...)` 把每块切回事件循环，UI 不卡

### 9.2 显示本机 IP
```cpp
for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
    if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
    if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
    for (const QNetworkAddressEntry& e : iface.addressEntries()) {
        if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
            ips << e.ip().toString();
    }
}
```

### 9.3 UDP 子网广播
```cpp
for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
    for (const QNetworkAddressEntry& e : iface.addressEntries()) {
        if (e.broadcast().isNull()) continue;
        udpSocket->writeDatagram(payload, e.broadcast(), udpPort);
    }
}
// 兜底全局广播
udpSocket->writeDatagram(payload, QHostAddress::Broadcast, udpPort);
```

### 9.4 配置文件
- `QSettings` + `IniFormat` + 路径 `%APPDATA%\LanChat\config.ini`
- 字段：`network/tcpPort`、`network/udpPort`、`network/broadcastIntervalMs`、`identity/machineName`、`identity/peerId`、`files/defaultSaveDir`

### 9.5 单连接策略
- 同一时刻只保持一个 PeerConnection
- 已连接时再有连接进来：直接替换旧的（v1 简化）
- 后续可改成弹窗询问

### 9.6 错误与边界
- 端口被占用 → UI 顶部日志区红字提示，让用户去设置改
- UDP 广播失败（防火墙）→ 状态栏提示，但不阻断 TCP 监听
- 传输中网络断 → 取消文件，标记不完整
- Windows 防火墙：首次启动会弹窗，需要在 README 里说明放行

---

## 10. Windows 部署

### 10.1 打包步骤
1. 用对应 MSVC/MinGW 编译 Release
2. 在 exe 目录运行 `windeployqt LanChat.exe` → 自动复制 Qt 依赖 dll
3. 把目录打包给用户（无其他 dll 依赖）

### 10.2 数据目录
- 配置：`%APPDATA%\LanChat\config.ini`
- 默认下载：`%USERPROFILE%\Downloads\LanChat\`

### 10.3 防火墙
- 首次启动 Windows Defender 防火墙会弹窗，让用户勾"专用网络"放行
- README 里写明：如果发现不到对端，先检查防火墙、再检查是否同一子网

---

## 11. 测试计划

| 场景 | 验证点 |
|------|--------|
| 同机两实例（不同 TCP 端口） | 基本连通性 |
| 两台 Win 电脑同 WiFi | 自动发现 + 列表出现 |
| 两台 Win 电脑同有线 | 自动发现可工作 |
| 关闭其中一台 | 10 秒内从列表消失（bye 包则立即消失） |
| 发短消息 | 文字正常显示 |
| 发 1 MB 文件 | MD5 一致 |
| 发 500 MB 大文件 | UI 不卡，进度平滑 |
| 传输中拔网线 | 优雅报错 |
| 改端口重启 | 新端口生效，广播内容更新 |
| 拖入 1 个文件 | 弹接受框 → 接收 OK |
| 拖入 5 个文件 | 串行发送，进度依次更新 |
| 拖入非文件（文本） | 忽略 |

---

## 12. 安全说明

**本版本是明文 TCP**。包括消息和文件都不加密，任何能抓你局域网包的人都能看到内容。

- ✅ 适用场景：内网团队、家庭、办公室局域网（你信任的范围内）
- ❌ 不适用：公共 WiFi、敌对网络、需合规的敏感数据

如果以后有 OpenSSL 可用，可以切到 TLS（QSslSocket），方案见 DESIGN-v0.md（保留的旧版加密设计）。

---

## 13. 后续可扩展

按价值优先级：

1. **TLS 加密** — 等以后能拿到 OpenSSL DLL，把 QTcpSocket 换成 QSslSocket，加自签证书 + TOFU 信任。代码改动主要在 `PeerConnection` / `PeerServer`，应用协议不变。
2. **多对端连接** — 同时和多个对端聊天
3. **文件夹传输** — 递归打包后传，对端解包
4. **断点续传** — 用 transferId + 偏移量
5. **历史记录** — SQLite 存聊天 / 文件
6. **用户名 / 头像**
7. **跨网段** — 配置文件里写入手动 peer 地址，定时尝试
8. **Linux / macOS 支持** — 代码本身基本跨平台，主要是部署脚本
