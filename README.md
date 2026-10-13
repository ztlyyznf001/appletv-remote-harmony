# Apple TV Remote for HarmonyOS · 原生鸿蒙遥控器

这是独立的 **ArkUI / ArkTS + C++** 工程源码。目标是 HarmonyOS 7.0 手机上的“发现 → 四位 PIN 配对 → 基础遥控”。它不是 APK 转换，也不是可安装的 HAP 成品。

**验证状态：** 用户于 2026-10-13 反馈，当日上午版本（`285a7005`）已完成 TV 调试；未提供具体机型、构建版本或逐项验收记录。本次在该源码基线上适配 Android UI。云端没有 DevEco Studio、HarmonyOS SDK、Hvigor、OHPM 或 HDC，因此本次 UI 修改尚未完成鸿蒙编译与真机视觉/交互复验。主机 C++ / Node-API 测试的实际结果见 [验证记录](docs/VALIDATION.zh-CN.md)，不代表 ArkTS 编译成功。没有创建签名凭据、发布版本或修改 Android 构建流程。

## 范围

- Android 同款深色/薄荷绿视觉、顶部设备选择、方向键和底部操作区；窄屏/横屏/大字适配。
- ArkUI 设备列表；mDNS `_companion-link._tcp`，解析实际 IP 和端口；手动 IP + 端口后备入口。
- HAP SRP-3072 / SHA-512 配对、服务端 proof、Ed25519 身份、X25519 Pair Verify、HKDF-SHA512、ChaCha20-Poly1305。
- OPACK / TLV8、TCP 流分帧、独立收发 nonce、XID 匹配、整次请求期限、取消与失败关闭。
- 方向、选择、返回、Home、播放/暂停；一次点击对应按下和释放。
- 配对秘密经 HUKS AES-GCM 密文落盘；广播信息不作为可信身份。
- 前台生命周期、有限重连、主动断开抑制重连；过期异步结果不更新界面。

不含触控、长按、自动键盘/RTI、Siri、音量/CEC、后台常驻、AppGallery 发布。按钮成功表示协议请求得到了成功回复，不表示已读取电视画面确认。

## 入口

- [本次 Android UI 适配与验收](docs/UI-ADAPTATION.zh-CN.md)

- [中文部署与调试](docs/DEPLOYMENT.zh-CN.md)
- [架构与安全边界](docs/ARCHITECTURE.zh-CN.md)
- [验证记录](docs/VALIDATION.zh-CN.md)
- [真机验收清单](docs/DEVICE-ACCEPTANCE.zh-CN.md)
- [第三方来源与许可](THIRD_PARTY_NOTICES.md)

主机测试（已有 C++17、OpenSSL 3 开发头与 pkg-config）：

```sh
./scripts/test-host.sh
SANITIZE=1 ./scripts/test-host.sh
```

可选 Node-API 桥接主机测试需要官方 Node headers，命令见部署文档。这些脚本不下载软件、不访问电视、不签名、不发布。

## 仓库与源码来源

当前独立仓库：<https://github.com/ztlyyznf001/appletv-remote-harmony>。在 DevEco 中直接打开本仓库根目录。

本仓库只包含鸿蒙工程、主机测试与文档，不含 Android 工程。GitHub 目前未配置 CI；下面的主机检查需本地运行，不能替代 SDK 和真机验收。

原始源码基线：

- 原仓库：<https://github.com/ztlyyznf001/appletv-remote-android-community>
- 固定 main：`dfb2a7c84635c1312dd5f9c3298c52e1cd6f9f7f`
- Android v0.2.3：`e8762843fbde6b619df0a5a5928413404d9428d4`
- 原始交付只在 Android 仓库增加 `harmony/`；现将该目录内容移到独立仓库根目录。原 Android 源码、Gradle 和发布流程保持不变。
- 迁移来源提交：`805b26780e0508ae636af28537f1f0598d08a862`；历史草稿：[community PR #4](https://github.com/ztlyyznf001/appletv-remote-android-community/pull/4)。
- 项目原创代码为 [MIT](LICENSE)；第三方许可另见 [来源与许可](THIRD_PARTY_NOTICES.md)。

本项目不是 Apple 官方应用，基于开源互操作资料；协议可能随 tvOS 变化。保留项目 MIT 和 pyatv MIT 声明。OpenSSL 运行库需另外从官方来源构建，不随这个源码包分发目标二进制。
