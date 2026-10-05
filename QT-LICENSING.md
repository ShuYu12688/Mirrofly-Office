# Qt 许可与第三方边界

本源码包不分发 Qt SDK、Qt DLL 或 Qt 源码。项目使用 Qt 6.8.3 动态库，采用其适用的
LGPL-3.0-only 选项。Mirrorfly 的自定义许可和商业报价均不覆盖 Qt 本身。

## 本版本核对范围

以下按 CMake、QML 导入以及本机 Qt 6.8.3 SDK 的 SPDX 清单核对；
清单中的完整表达式保存在 [模块记录](licenses/qt/MODULES.json)。

| Qt SDK 模块 | 本项目直接使用或部署相关组件 |
| --- | --- |
| qtbase | Core、Concurrent、Gui、Network、Widgets；Test 为开发测试用途 |
| qtdeclarative | Qml、Quick、QuickControls2、QuickDialogs2、QuickLayouts、QuickEffects 等 |
| qtsvg | Svg，以及部署时使用的 SVG 支持 |
| qtshadertools | 着色器相关构建及运行支持；区分库和构建工具 |

上述库的记录具有 LGPL 选项。SDK 中另有使用 `GPL-3.0-only WITH Qt-GPL-exception-1.0`
的工具，工具许可不能直接当作应用运行库许可。此记录不是所有未来发布包的完整清单。
新增模块、插件、静态链接或升级 Qt 后必须重新核对，尤其不能默认 GPL-only 模块也支持 LGPL。

## 分发含 Qt 的程序时

保留实际组件的版权、第三方声明，以及 LGPLv3 和 GPLv3 全文；明确告知用户使用 Qt。
提供实际使用版本的完整对应 Qt 源码及修改，或采用许可证允许且确实能履行的其他方式。
允许替换、重新链接并运行修改后的 LGPL 库，允许为调试库修改而进行必要逆向工程；
适用时提供安装信息。动态链接是工程措施，不能替代这些义务。

本项目 [LICENSE](LICENSE) 明确保留上述第三方权利。购买本项目商业授权不会免除它们。
能满足 LGPL 条件时，商业应用不因此自动需要 Qt 商业许可证；需要 Qt 商业许可时须另向
Qt 权利人获取。自定义项目许可不能覆盖 GPL-only 组件要求，须先解决许可兼容性再分发。

本文件是工程记录，不是法律意见或完整的合规认证；实际义务以各组件许可正文为准。

## 原始资料

- [Qt 6.8 官方许可说明](https://doc.qt.io/qt-6.8/licensing.html)
- [Qt 官方 LGPL 义务说明](https://www.qt.io/development/open-source-lgpl-obligations)
- [Qt 6.8.3 对应源码](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/)
- [LGPLv3 全文](licenses/qt/LGPL-3.0-only.txt)
- [GPLv3 全文](licenses/qt/GPL-3.0-only.txt)

上述许可文本原样来自 SPDX license-list-data v3.27.0，本源码包没有修改标准许可正文。
模块记录只选取 SDK 中相关条目的名称和许可表达式，不包含个人目录或机器信息。
