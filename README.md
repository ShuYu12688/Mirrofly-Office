
# Mirrorfly Office
<img width="1260" height="820" alt="df02cb6d54da9a5bdd59b11c0adf6ce1" src="https://github.com/user-attachments/assets/4e295cc3-edca-4cf2-a23b-d55548aec6a9" />


一个使用 C++17 和 Qt 6 编写的轻量级桌面办公项目，作者：舒宇。

当前版本：**1.1.10beta**。完整桌面端目前以 **Windows x64** 为构建和验证目标。
项目处于持续优化阶段，重点是文件兼容性、编辑稳定性和 AI 办公任务的可靠交付。

## 可以做什么
<img width="1260" height="820" alt="34e92b36c64798a41a734b24465cee58" src="https://github.com/user-attachments/assets/082737b7-53aa-45cc-a031-e3c611826a7a" />
- Word 文档、电子表格、演示文稿的读取、显示及已实现范围内的编辑和保存。
<img width="1260" height="820" alt="7f7f14bbc3d66a3a47f58c86e07ee02b" src="https://github.com/user-attachments/assets/eaca8f14-11fa-4428-a2b5-864189a54733" />

- PDF 查看及已实现的页面、批注操作。
- 文本、Markdown 和思维导图编辑。
- 通过公开的文档操作接口执行 AI 办公任务，并校验实际保存结果。
<img width="1831" height="893" alt="c3d72e54ede74bb97ddd71f63f1b77bf" src="https://github.com/user-attachments/assets/511200c7-636b-47d4-a03e-0bc7b0da33d6" />


这是开发中的 Beta 项目，不承诺完整兼容 Microsoft Office，也不承诺无损处理所有复杂文件。
字体、嵌入对象、动画和复杂排版等能力，以当前代码、测试和具体文件验证结果为准。
重要文件请保留原件，先在副本上验证。

## 源码和构建

本目录是源码发布包，包含代码、资源、测试、依赖来源及许可说明。
不含安装程序、可执行文件、Qt SDK、PDFium DLL、本机配置或历史构建目录。

先阅读 [构建说明](BUILDING.md)。PDFium Windows x64 依赖可通过
`scripts/fetch-pdfium.ps1` 下载，脚本固定版本并校验 SHA-256。
只构建核心层时不需要 Qt 或 PDFium。

## 结构与开发规范

依赖方向：`main → app → ui → core/platform`，平台层可以依赖核心层。

| 目录 | 内容 |
| --- | --- |
| `src/core` | Qt 无关的文档模型、事务与 AI 合约 |
| `src/platform` | 文件、图片、PDFium 等平台适配 |
| `src/ui` | Qt 会话桥、绘制与异步协调 |
| `src/app` | 应用装配和公开启动接口 |
| `ui` | QML 页面和组件 |
| `config` | 主题及生成配置 |
| `tests` | 单元测试、隔离测试和兼容性样本 |
| `third_party` | 第三方源码、头文件、许可及来源记录 |

模块之间只通过公开接口交换数据。C++ 使用四个空格和 Allman 大括号，主题集中在
`config/theme.json`。详见 [开发规范](DEVELOPMENT.md) 和 [贡献说明](CONTRIBUTING.md)。

AI 功能需要自行配置服务；本仓库不提供密钥或服务额度。调用外部模型时，相关输入可能
发送到所配置的服务，请按该服务条款和自己的数据权限使用。

## 许可与商业授权

本项目采用自定义的 **Mirrorfly 源码公开许可证 1.0**，完整条款见 [LICENSE](LICENSE)。
由于限制商业使用，这属于 **source-available（源码公开）**，不是 OSI 定义的开源许可。

- 非商业使用可以按许可学习、运行和修改。
- 分发本项目或修改、衍生版本，以及提供基于它的网络服务时，需要公开对应源码。
- 仅在本地进行的私人非商业修改，不要求公开。
- 商业使用须事先取得作者书面授权，公开源码本身不能免除商业授权要求。
- 普通办公文档、用户数据和独立开发的无关程序，不会因此被要求公开。

商业授权参考报价为 **人民币 4,000 元/年**，最终范围、价格和期限以双方书面协议为准。
这不是自动授权，也不包含 Qt 商业许可证、外部 AI 额度或未约定的支持服务。
联系方法及说明见 [商业授权](COMMERCIAL.md)。

Qt、PDFium 等第三方组件保持各自原有许可证，不受本项目新增的商业限制覆盖。
请同时阅读 [第三方声明](THIRD_PARTY_NOTICES.md) 和 [Qt 许可说明](QT-LICENSING.md)。

## 反馈

欢迎在仓库 Issues 提交可复现问题。请注明版本、操作系统、文件类型、操作步骤和预期结果，
并使用脱敏样本；不要公开 API Key、私人文档或其他人的个人资料。
