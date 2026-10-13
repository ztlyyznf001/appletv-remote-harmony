# 鸿蒙 PoC 第三方来源与许可

项目原创代码沿用原项目 MIT，版权 `2026 ztlyyznf001 and contributors`。完整文本保留于 `licenses/LICENSE-project.txt`。第三方内容不因放入此目录而改标为项目 MIT。

## Companion 互操作参考

协议字段、认证流程及测试设计参考原 Android community 仓库固定 v0.2.3 / main 基线，以及 pyatv 固定提交 `b277a4c8222ecdcbaab8a24e3e713ca44765adb4`：

- <https://github.com/postlund/pyatv/tree/b277a4c8222ecdcbaab8a24e3e713ca44765adb4/pyatv/protocols/companion>
- <https://github.com/postlund/pyatv/blob/b277a4c8222ecdcbaab8a24e3e713ca44765adb4/pyatv/auth/hap_srp.py>
- <https://github.com/postlund/pyatv/blob/b277a4c8222ecdcbaab8a24e3e713ca44765adb4/pyatv/support/opack.py>

pyatv：MIT，Copyright (c) 2020 Pierre Ståhl，完整文本 `licenses/LICENSE-pyatv.txt`。没有打包 pyatv Python 运行时、Apple 二进制、反编译伪代码、真实抓包或用户配对凭据。

## 密码运行库

OpenSSL 3 使用 Apache-2.0。主机验证实际链接环境自带的 OpenSSL 3.5.7；它只说明这次测试的版本，**不是推荐把过期补丁版本发布到设备**。2026-10-12 查阅的官方 3.5 LTS 下载为 3.5.9：

- <https://www.openssl-library.org/source/>
- <https://github.com/openssl/openssl/tree/openssl-3.5.9>
- <https://github.com/openssl/openssl/blob/openssl-3.5.9/LICENSE.txt>

本包保留官方 3.5.9 `LICENSE.txt` 原文于 `licenses/LICENSE-OpenSSL-3.5.9.txt`，另保留通用 Apache-2.0 全文。**本源码交付没有捆绑 OpenSSL 源码或 ARM64 库**。设备构建须记录实际版本、来源、哈希、编译选项，并随应用保留该版本许可证及适用版权/NOTICE；版本变动时同步文件，不可仅沿用本次文件名声称完成依赖审计。

## 构建和测试工具

DevEco Studio、Harmony SDK、Hvigor、OHPM 由用户通过华为官方渠道安装并按其许可使用；本包不复制 SDK、签名材料或这些工具。

可选 Node-API 主机测试使用官方 Node.js v24.19.0 头文件（源码提交 `cdc1b38d40cb567b7ad0b39c86addf830a0af0ae`），没有把 Node 运行时或头文件打入应用。Node 的项目许可见 <https://github.com/nodejs/node/blob/v24.19.0/LICENSE>。

主机 C++ 测试只依赖系统编译器、OpenSSL 和 POSIX socket，没有新增测试框架依赖。密码参考向量来自 RFC8032 / RFC8439 等公开标准；夹具均为合成数据。

MIT/Apache 许可不等于 Apple 协议兼容认证，也不替代应用分发所需的其他审核。
