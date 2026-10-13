# 鸿蒙触摸板补齐

日期：2026-10-13 UTC。

## 基线与保留项

- 本次开始时重新读取 GitHub，PR #1 已由用户合并。当前 `main` 是 [`b8e94dae6c062cb06c9de0d80c75c2b2f0aec701`](https://github.com/ztlyyznf001/appletv-remote-harmony/commit/b8e94dae6c062cb06c9de0d80c75c2b2f0aec701)，本地所有上游文件逐一通过 Git blob SHA-1 核对。
- 保留用户后续本地编译 / 启动修复：SDK 的 `node_api.h`、组件 `inputEnabled` 属性、页面安全区背景、加载完成后设置系统栏及异常处理；不覆盖签名或本地依赖配置。
- Android 参考为 [`dfb2a7c84635c1312dd5f9c3298c52e1cd6f9f7f`](https://github.com/ztlyyznf001/appletv-remote-android-community/commit/dfb2a7c84635c1312dd5f9c3298c52e1cd6f9f7f) 的 `CompanionClient.kt`、`TouchGesturePump.kt`、`InputControls.kt`、`SelectHoldPolicy.kt` 及 `MainActivity.kt`。
- 既有配对、凭据存储、发现、按键和手动断开禁止重连的语义不变。没有键盘 / RTI、Siri、音量功能。

## 实际实现

1. 默认显示触摸板，可切换方向键。保持原三个操作键及设备面板；方向键模式提供可聚焦的无障碍操作按钮。
2. Pair Verify 后通过现有加密传输发送 `_touchStart`，协商 double 类型的 1000 × 1000 表面，然后启动远程会话。
3. 触摸发送真正的 `_hidT` 事件（`_t=1`），而非将滑动模拟成方向键。包含 `_tPh=1/3/4`（BEGIN/MOVE/END）、`_tFg=1`、`_cx/_cy` 和 `_ns`。坐标按实际区域归一化并限制为整数 0–1000；时间由原生单调时钟提供，不使用 Unix 时间。
4. UI 最多每 16 ms 提交一次移动；发送队列仅保留最新未发送 MOVE，保留 BEGIN / END 与一次 HOLD，最多五个待发样本、一个 Promise 执行中。松开位置总会作为 END 提交，不因节流丢失。
5. 小于 500 ms 且最大移动小于 8 vp 的轻点，在 END 发送后才发 SELECT。静止 500 ms 触发 SELECT-down，实际 down 完成后保持至少 1000 ms 再释放，对齐 Android；取消不等待最短保持时间。指针历史点也参与最大位移判定，避免同帧来回滑动误触确认。
6. 多指、系统 Cancel、组件移除 / 禁用 / 尺寸变化、面板 / 模式切换、10 秒接触超时均取消；取消不会补发轻点。父级滚动手势在触摸板占用输入时被拒绝，触摸板外仍可滚动。
7. 连接代次与 pump 身份在每次异步返回后核对。离开前台或断开立即废弃队列和计时器、取消旧原生任务并关闭传输，旧输入不会在重连后的设备上重放。

## 释放与成功的边界

- 仍连接时的指针 / 面板 / 模式取消会发送 END 和必要的 SELECT-up。
- 后台 / 断开保持原来的立即取消与关 socket 行为；网络失败、系统退出或已关闭 socket 后，不能保证电视收到释放。不会为补发释放而重新连接。
- 原生独立 `disconnect()` 在尚可用连接上尽力发送 SELECT-up / END，全部清理合用 200 ms 预算；不等待回复。应用强制取消后，这个清理不意味着释放已送达。
- `_hidT` 是无回复事件。Promise 完成仅表示本机成功写入，不代表电视确认移动。需要真实电视检查拖动速度、长按行为与断网恢复。

## 已执行及未执行验证

主机检查执行真实 ArkTS 非 UI 代码及组件处理器、编译 C++ 与 Node-API，使用加密的本机假电视对端核对协议。不是 ArkUI 编译、渲染或实际电视测试。

具体最终结果见 [验证记录](VALIDATION.zh-CN.md)。可复现命令：

```sh
python3 scripts/check-source.py
node tests/ui/remote-layout.test.mjs
node tests/ui/touch-contact.test.mjs
./entry/tests/run-mocks.sh
./scripts/test-host.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 ./scripts/test-host.sh
NODE_HEADERS=/path/to/official/node-v24.19.0/src ./scripts/test-node-bridge.sh
git diff --check
```

新增 ArkUI API 依据：官方 [TouchEvent](https://github.com/openharmony/docs/blob/master/en/application-dev/reference/apis-arkui/arkui-ts/ts-universal-events-touch.md)（vp 坐标、历史点）、[BaseEvent 与手势判断](https://github.com/openharmony/docs/blob/master/en/application-dev/reference/apis-arkui/arkui-ts/ts-gesture-customize-judge.md)（纳秒时间戳、`onGestureJudgeBegin`）。这不替代目标 SDK 编译。

云端 `check-toolchain.sh` 仍报告缺少 Hvigor、OHPM、HDC、官方 Harmony NDK 和目标依赖配置。本次没有生成或签名 HAP，没有运行截图。此前用户完成的上游 SDK 构建 / 启动验收不会自动覆盖这些新增文件。

请在原先已验证的 DevEco 工程中按 [真机增量清单](DEVICE-ACCEPTANCE.zh-CN.md#触摸板增量验收本次尚未真机执行) 检查：编译、四向 / 快慢滑动、单次轻点、长按释放、多指取消、旋转、面板 / 模式切换、后台 / 断网 / 重连、短屏 / 横屏 / 大字 / 屏幕阅读器。没有合并、发布或 AppGallery 操作。
