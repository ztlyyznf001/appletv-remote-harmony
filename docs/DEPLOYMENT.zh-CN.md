# 中文部署与调试步骤

## 0. 先确认本包是什么

本包是可继续开发的原生鸿蒙 PoC 源码，**不是已编译或已签名的 HAP**。本轮云端没有 Harmony SDK / DevEco / HDC，也没有用户手机和 Apple TV；不能在这里完成设备构建和互操作验收。不要把主机测试结果当成安装成功。

请先做下面第 1–4 步；任一步失败，都应保留实际错误与版本，修复后重跑，不能跳过并称为已验证。文档里的绝对路径是占位符，请替换为真实目录，不要原样复制。

## 1. 官方工具链

1. 从[华为开发者官网](https://developer.huawei.com/consumer/cn/deveco-studio/)安装与目标 SDK 匹配的 DevEco Studio。登录、许可接受和签名账号操作由用户处理。
2. 在 DevEco SDK 管理中安装 HarmonyOS 7 / API26 的 SDK，包括 ArkTS、Native、工具链与 HDC。
3. 本模板使用 `compileSdkVersion` / `targetSdkVersion` / `modelVersion` 为 `26.0.0`，使用匹配 IDE 自带的 Hvigor `6.26.4`，没有另行下载或锁定 npm 中的 Hvigor；依据[DevEco Studio 26 官方版本表](https://developer.huawei.com/consumer/en/doc/harmonyos-releases/deveco-studio-new-features-2600)。`compatibleSdkVersion` 为 `5.0.0(12)` 是接口设计下限，**未声明已测过 API12–26 的全部设备**。
4. 首次导入若 IDE 要升级配置，先对照同版本官方 Native C++ 空项目模板；保留源码，不要加入 Android 兼容模块，也不要猜测生成文件已适用于此 SDK。

此交付不包含 Hvigor wrapper 可执行脚本。让匹配版本的 DevEco 使用其官方 Hvigor，或在同版本新建空项目后复制官方生成的 wrapper 配置。不要从随机博客下载构建二进制。

## 2. 准备鸿蒙目标 OpenSSL

`entry/src/main/cpp/CMakeLists.txt` **不会**下载 OpenSSL，也不会偷偷使用主机 `/usr/lib`。必须准备 `aarch64-linux-ohos` 编译的静态 `libcrypto.a` 和匹配头文件。

- 从 [OpenSSL 官方下载页](https://www.openssl-library.org/source/)选仍受支持的 3.x 版本并验证该版本官方 SHA-256 / 签名。本轮资料核验的 3.5 LTS 补丁为 3.5.9；实际构建时重新核验安全更新。
- 用安装的 Harmony NDK Clang、sysroot 和 `--target=aarch64-linux-ohos` 交叉编译；使用 PIC 静态库，保留默认安全随机源、SHA-512、HKDF、Ed25519、X25519、ChaCha20-Poly1305、BIGNUM。
- 不可使用 Android NDK 产物、系统 Linux ARM64 库、x86_64 库或网络下载的未知 `.a/.so`。架构同为 ARM64 并不证明 ABI 一致。
- 编译过程参考[华为 NDK 构建文档](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides-V13/build-with-ndk-V13)和[OpenSSL 官方 INSTALL](https://github.com/openssl/openssl/blob/openssl-3.5.9/INSTALL.md)。本轮没有目标 SDK，不能把某组未经执行的 OpenSSL 配置参数写成已验证的一键脚本。
- 构建前缀目录须有 `include/openssl/evp.h`、`lib/libcrypto.a`。另外保留版本、源码哈希、完整 configure 命令、编译器版本、目标 triple 与完整日志。
- 用 SDK 的 `llvm-readelf` / `llvm-ar` 检查库成员确实为 AArch64，并执行目标链接检查。确认确实来自 Harmony NDK 后，才在该前缀创建 `HARMONY_OPENSSL_TARGET`，内容为 `aarch64-linux-ohos`。标记仅防误用，不是 ABI 认证。

```sh
export OHOS_NATIVE_HOME="/实际/HarmonySDK/native"
export COMPANION_HARMONY_OPENSSL_ROOT="/实际/openssl-harmony-arm64"
./scripts/check-toolchain.sh
```

在 IDE GUI 中打开项目时，它可能不继承终端环境变量。此时在 `entry/build-profile.json5` 的 `externalNativeOptions.arguments` 显式传 `-DCOMPANION_HARMONY_OPENSSL_ROOT=/实际/openssl-harmony-arm64`。不要把个人路径、证书或密钥提交到仓库。

## 3. 主机验证可先独立跑

Linux / macOS 需要 C++17、OpenSSL 3 开发头、pkg-config；本脚本不会安装缺失依赖。

```sh
./scripts/test-host.sh
SANITIZE=1 COMPANION_HOST_BUILD="$PWD/build/host-asan" \
  ./scripts/test-host.sh
```

可选 Node-API 主机验证使用本地官方 Node 源码的 `src` 头目录，须匹配实际 Node 版本：

```sh
NODE_HEADERS="/实际/node官方源码/src" ./scripts/test-node-bridge.sh
```

它用 Linux / macOS Node 运行 `.node`，只验证桥接导出、Promise、输入检查、线程与取消。**它不会编译 `.ets`，不会验证鸿蒙 Node-API ABI，也不会产出 HAP。**

本轮只实际执行了 Linux 主机脚本；macOS 路径已包含动态符号查找选项，但没有执行验证。ASan / UBSan 在当前环境通过；LeakSanitizer 受 ptrace 限制，实际测试使用 `ASAN_OPTIONS=detect_leaks=0`，不宣称泄漏检测通过。App 逻辑模拟测试另行运行 `./entry/tests/run-mocks.sh`，其边界见该目录 README。

## 4. DevEco 构建

1. 在 DevEco 打开本仓库根目录（包含 `AppScope/`、`entry/` 和 `build-profile.json5`）。这是独立鸿蒙工程。
2. 同步本地 OHPM 类型依赖，确认 `entry/src/main/cpp/types/libcompanion/index.d.ts` 被正确解析。
3. 先构建 Native 库，核对 `libcompanion.so` 为 Harmony ARM64；链接不得出现未解析 OpenSSL / Node-API 符号。
4. 再执行 ArkTS 编译与 HAP 打包。修正 SDK 实际报告的类型、权限或 API 差异后，重跑主机测试和目标构建。
5. DevEco 内建 Hvigor 的常见命令形式如下；wrapper 路径和可用参数以当前 IDE 生成模板为准：

```sh
hvigorw --mode module -p module=entry@default -p product=default assembleHap
```

6. 查看实际构建输出，不要根据文档猜 HAP 文件存在。检查包内有 `libcompanion.so`、项目 / pyatv / OpenSSL 许可，且没有调试凭据、PIN、私钥或完整日志。

目前只声明 `ohos.permission.INTERNET`，不为发现 TV 索取定位权限。若后续确实增加新接口或权限，先说明原因并相应修改验收清单。

## 5. 签名与安装

HAP 真机安装需要合适的调试签名和设备配置；本包没有签名文件，也不会自动创建长期签名密钥。由用户在官方 DevEco / 开发者账号流程配置调试签名，在手机上开启开发者选项和 USB 调试并亲自接受信任提示。参考[官方签名入门](https://developer.huawei.com/consumer/cn/develop-novice-guide/)。

```sh
hdc list targets
hdc install -r /实际构建输出/已签名的-entry.hap
```

不要安装本包中不存在的 HAP；未签名包不能视作可用交付。不要把签名证书、私钥、密码或完整配对文件回传到聊天/公开仓库。

## 6. 同网设备验证

1. 手机与 Apple TV 接入同一局域网；避免访客网或 AP 客户端隔离。打开 App 后看设备列表。
2. 点选实际电视。如果发现失败，可输入电视数字 IP 与 mDNS 当前解析/可信网络工具确认的实际 Companion 端口；不能固定填 `49152` 或其他猜测端口。
3. 点击开始配对，在电视出现 PIN 后输入四位数字。错误 PIN / 取消应回到可重试状态，不能保存半成品身份。
4. 成功保存后连接，再测试八个基础按钮；以电视画面真实响应为准。
5. 杀进程、重新打开，检查保存身份的 Pair Verify；切后台、锁屏、重开、主动断开等按[验收清单](DEVICE-ACCEPTANCE.zh-CN.md)记录。

故障记录只需要手机型号、完整 HarmonyOS 构建号、tvOS 版本、当前步骤、稳定错误码及脱敏构建错误。请勿附上 PIN、配对身份内容、私钥、未脱敏日志或真实抓包。

如果系统 HUKS 密钥丢失但仍有密文，应用会关闭存储功能，而不会自动生成新钥匙覆盖旧身份。确认无需保留后，可在系统设置清除应用数据再重新配对；这会删除全部本机配对。普通密文损坏可先使用应用里的“忘记此设备”。
