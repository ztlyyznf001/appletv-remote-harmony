# Android UI 适配说明

日期：2026-10-13 UTC。

## 源码与范围

- 鸿蒙基线：[`285a7005b7519ba94687369fe7b428d5e41e5c09`](https://github.com/ztlyyznf001/appletv-remote-harmony/commit/285a7005b7519ba94687369fe7b428d5e41e5c09)。开始工作时重新查询 GitHub，并将本地 69 个源码文件逐一按 Git blob SHA-1 与该树核对，全部一致。
- Android 参考：[`dfb2a7c84635c1312dd5f9c3298c52e1cd6f9f7f`](https://github.com/ztlyyznf001/appletv-remote-android-community/commit/dfb2a7c84635c1312dd5f9c3298c52e1cd6f9f7f)，重点为 `RemoteScreen.kt`、`RemoteLayoutPolicy.kt`、`RemoteInteractionPolicy.kt`、`RemoteDialog.kt`。
- 用户反馈上述鸿蒙基线已完成 TV 调试。没有推断具体电视、手机、tvOS、SDK 或签名构建版本；该反馈不是本次新 UI 的真机验收。
- 保留现有发现、数字 IPv4/IPv6 与实际端口、四位 PIN、加密身份存储、生命周期、重连及八个遥控命令。没有改动这些控制器、原生协议或构建/签名配置。
- 现有鸿蒙接口尚无触摸板、长按、RTI 键盘或音量功能，本次未添加无功能按钮，也未新增这些协议。

## UI 改动

1. Android 同款炭黑底色 `#101719`、深灰面板 `#182224` / `#243133`、薄荷绿强调色 `#B8E4CA`。
2. 顶部设备入口及独立“连接”菜单；附近设备、手动地址、PIN、移除配对从长页面拆成遮罩面板。
3. 圆角方向键控制区，中心确认键；返回、主屏幕、播放/暂停操作区。使用本地 SVG 矢量图标，不依赖字符字体。
4. 普通竖屏固定底部操作；宽横屏改为右侧操作列。短屏、大字使用整体滚动；极窄屏方向键降级为按钮列表以保留点击尺寸。
5. 短暂按键请求锁定仍抑制重复点击，但不切换为断开连接的配色。
6. 面板打开时禁用底层遥控并从无障碍树隐藏；Back 关闭面板，PIN 时 Back/取消主动取消会话；移除配对仍绑定确认时的设备 ID。

## 已执行检查

- C++ 密码/编解码向量与拒绝路径，通过。
- C++ 会话测试 12 组，通过。
- 同一 C++ 测试在 ASan + UBSan 下通过，`ASAN_OPTIONS=detect_leaks=0`。初次 LeakSanitizer 因环境 ptrace 限制失败，泄漏检测未通过验收。
- 已有应用逻辑模拟：21 个生命周期/发现检查、20 个凭据存储检查，通过。
- 新增 17 个布局/页面处理器检查，通过，执行真实布局 helper 和从页面提取的非渲染处理器。
- JSON/JSON5、全部 SVG XML、INTERNET-only 权限、许可证镜像及空白检查，通过。
- 静态交互审查，并核对官方 SDK 的 UIAbilityContext 类型和无障碍属性。上述检查不是 ArkTS UI 编译。

运行：

```sh
python3 scripts/check-source.py
node tests/ui/remote-layout.test.mjs
./entry/tests/run-mocks.sh
./scripts/test-host.sh
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 ./scripts/test-host.sh
```

布局测试使用 Node 的 `stripTypeScriptTypes`，本次执行环境为 Node 24.19；需要支持该接口的 Node 版本。

## 未执行及最小复验

云端未发现 HarmonyOS SDK、DevEco、Hvigor、OHPM、HDC 或 ArkTS 编译器，因此没有生成/签名 HAP，没有新 UI 的应用运行截图，也没有声称编译、系统无障碍焦点或 IME 键盘避让已验证。Node-API 桥接测试此次缺少 Node 头文件，未重跑；历史记录保留在原验证文档中。

在已完成 TV 调试的原 DevEco 环境中应用本次 UI 改动，保留原本可用的 SDK、签名和原生依赖，依次检查：

- 编译/运行，竖屏 320/393vp、横屏、短窗口、系统大字，所有控件可到达，无系统栏遮挡。
- 设备列表、手动 IPv4/IPv6 与端口、PIN 成功/错误/取消/返回，键盘弹出后配对/取消仍可到达。
- 八个按键逐个发送，快速连点无重复并发；断开状态无法控制，输入锁定无闪白。
- 连接/PIN/按键途中切后台，返回无过期成功提示；手动断开后不自动重连。
- 移除配对确认后仅移除指定设备，取消不删除；设备变化后旧确认不能作用于新设备。
- 屏幕阅读器和硬件键盘不能进入遮罩后的遥控；关闭面板后焦点可恢复；图标和大字标签无裁切。

推荐在已调试通过的工程中先执行 `git apply --check android-ui.patch`，检查通过后执行 `git apply android-ui.patch`；补丁只涉及本文列出的 UI、检查脚本和文档。请勿用完整源码目录覆盖本地未提交的调试或签名配置。

本地源码包不是可安装包。初次源码包交付时未推送 GitHub、创建 PR、合并或发布；后续按用户授权单独推送 UI 分支并创建草稿 PR，不自动合并或发布。
