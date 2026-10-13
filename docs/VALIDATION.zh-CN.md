# 验证记录

执行日期：2026-10-12 UTC。源码基线：main `dfb2a7c84635c1312dd5f9c3298c52e1cd6f9f7f`，改动仅在 `harmony/`。原始交付时本地工作分支为 `harmony/poc-discovery-pairing-remote`，当时没有 push、PR、release 或签名操作。后续 GitHub 源码提交见下方记录。

## 已通过的主机检查

### C++ 密码与编解码：7 组

使用 GCC 14.2.0、C++17、`-Wall -Wextra -Wpedantic -Werror`、环境内 OpenSSL 3.5.7。

1. RFC8032 Ed25519 公钥/签名；篡改消息、伪造签名、长度错误拒绝。
2. RFC8439 ChaCha20-Poly1305 向量；AAD/tag 篡改、短 tag、nonce 长度、空载荷与计数字节序。
3. 独立 HKDF-SHA512 预期值、SHA512、RFC7748 X25519、非法共享密钥输入。
4. 独立生成的 HAP SRP-3072 A/K/M1/M2 固定夹具与无效公钥/proof 拒绝。
5. OPACK Kotlin 已知字节、64 位整数、不合法字典/引用/长度、深度和引用放大限制。
6. TLV8 大于 255 字节的分片、合并、空字段、截断与输出上限。
7. 固定随机种子的 10,000 组 OPACK / TLV 畸形输入 smoke 检查。

固定夹具不是用户抓包。SRP/HKDF 固定值独立于被测 C++ 客户端生成；完整 loopback 仍共享部分密码和 codec helper，不能称为完全独立的协议实现认证。

### C++ TCP 会话：12 组

- 独立 server SRP 计算、Pair Setup → Pair Verify → 会话 → 八个按钮
- 凭据格式与边界
- 伪造 SRP proof
- 错 TV 身份、错误 Ed25519 签名、Pair Verify AEAD 篡改
- 帧头 / tag 篡改及畸形长度
- 无关帧洪流、部分帧的总超时
- 阻塞 I/O 的显式取消与过期 epoch
- 旧 epoch 操作不会关闭随后建立的新连接，空闲取消清理状态
- 32 轮并发取消与新 epoch 立即连接的压力回归
- 拒绝命令后的尽力释放
- down/up 同批写入、乱序回复、等待回复期间取消
- 数字 IPv4 / IPv6 与动态端口

这些测试只绑定本机 loopback，不访问用户网络或电视。

### 内存错误检查

上述两组 C++ 可执行程序在 AddressSanitizer + UndefinedBehaviorSanitizer 下通过。当前执行环境 ptrace 限制使 LeakSanitizer 无法运行，实际设置 `ASAN_OPTIONS=detect_leaks=0`；**泄漏检测未通过验收，也没有被声称已运行成功**。

### Node-API 桥接主机检查

官方 Node v24.19.0 headers，源码提交 `cdc1b38d40cb567b7ad0b39c86addf830a0af0ae`；Linux `.node` 编译与 8 组 smoke 检查通过：导出表、端口、凭据、PIN、未连接拒绝、事件循环响应/显式取消、排队任务取消、幂等断开。

另有一项 Worker 终止的有界回归检查：只要求原生总期限内能退出，不要求特定平台必须等待十秒，也不把较早退出视作失败。

额外观察：在本机已连接 socket 上，`beginPairing` 等待第一条认证回复时强制 terminate Node Worker，测得约 10 秒后随该请求超时退出；cleanup hook 没有提前中断它。这个时间只适用于该探针，**不是所有高层操作的统一十秒期限**；`connect` / `finishPairing` 有多个各自限时的阶段。这与显式 `cancel()` 的及时唤醒不同。ArkUI 正常后台/销毁回调显式调用 cancel；Harmony 运行时突然终止行为仍待真机验证。

### App 逻辑模拟：41 项

`entry/tests/run-mocks.sh` 直接读取实现的 `.ets`，在 Node24 中剥除类型并注入替身：

- 21 项生命周期、发现、输入格式、重复点击、过期异步结果、有限重连、端口刷新、认证失败不重试、确认目标绑定
- 20 项加密信封、随机 nonce、目标 AAD、篡改拒绝、输入缓冲清理、取消/flush 回滚、密钥丢失、损坏记录清理与失败关闭

HUKS 替身用 Node AES-GCM 检查信封逻辑；它不能证明鸿蒙 HUKS 的参数或返回格式在设备上正确。Node 剥除类型不是 ArkTS 编译。

### 静态检查

- 新配置 JSON/JSON5 的语法检查通过（JSON5 仅去除独立注释后解析，不代表 SDK schema 校验）
- Shell 语法检查通过
- Git 空白检查通过
- 新内容保持在 `harmony/`，原 Android 源码、Gradle 与 CI 未改动
- 原创代码 MIT，pyatv MIT 与 OpenSSL Apache-2.0 许可分别保留

## 明确未运行

- CMake 本身在本轮云端不可用；直接编译器命令通过不等于 CMake 配置已执行
- HarmonyOS API26 Native/ArkTS 编译、Hvigor/OHPM 构建、目标 OpenSSL 交叉链接
- HAP 打包、签名、安装、UI 布局、无障碍/屏幕尺寸测试
- 真实 HUKS、Preferences、mDNS、系统权限、锁屏与进程回收
- 真实 Apple TV PIN 配对、重连与电视画面按键效果
- LeakSanitizer、完整安全审计、兼容认证、上架审核
- 原 Android 全量回归未重跑；其代码没有改动

因此交付状态是：**主机验证过的原生协议源码 + 鸿蒙平台接入模板；尚未完成鸿蒙 SDK 编译和真机 PoC 验收。**

## 再现

```sh
./scripts/test-host.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 \
  COMPANION_HOST_BUILD="$PWD/build/host-asan" ./scripts/test-host.sh
NODE_HEADERS="/实际/node-v24.19.0/src" ./scripts/test-node-bridge.sh
./entry/tests/run-mocks.sh
```

交付的 `validation/` 保存最终命令摘要与输出。后续代码修改后必须重跑受影响检查，再更新此记录；不要沿用旧 PASS。

## 2026-10-13 GitHub 源码提交复核

- 目标是现有 community 仓库的新分支与草稿 PR，只增加 `harmony/`；不合并、不发布 Release、不签名。
- 原始交付源码标识：`fdb6a883b2f000bb0b125a3385845075c532c30f`；GitHub 提交将另有 SHA。
- 本次重新运行主机 C++ 密码/编解码及 12 组 TCP 会话测试、41 项 App mock 和静态检查，均通过。
- Node-API 桥接与 Worker teardown 的结果沿用 2026-10-12 记录；本次环境未找到 Node headers，未重跑。
- Shell 脚本以可执行文件提交；打包产物、构建目录、凭据不提交。
- 上方鸿蒙 SDK 编译、HAP、HUKS、真实 Apple TV 和 LeakSanitizer 等未验证边界继续有效。源码上传不代表完成鸿蒙设备验收。

## 2026-10-13 独立仓库迁移

用户选择独立公开仓库 `appletv-remote-harmony` 后，将 community 提交 `805b26780e0508ae636af28537f1f0598d08a862` 的 `harmony/` 内容移至仓库根目录，并更新当前运行路径、根 README 和 MIT LICENSE。上方 community PR 记录保留为历史来源；不合并该 PR。

原始 `validation/` 日志中的 `harmony/` 路径属于历史执行目录。当前重现命令使用上方的新路径。本仓库未配置 GitHub Actions CI，Android CI 的成功结果不作为鸿蒙验收。SDK、HAP、签名与真机状态仍为未验证。

## 2026-10-13 Android UI 适配

用户反馈上午的独立仓库基线 `285a7005` 已完成 TV 调试。本次 UI 更改的主机回归、17 项页面/布局检查以及尚未完成的 SDK 编译/真机复验，见 [UI 适配与验收](UI-ADAPTATION.zh-CN.md)。历史记录中的未验证项描述其当时执行边界，不覆盖或否定用户后续调试反馈。
