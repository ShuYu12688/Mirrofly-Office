# 构建说明

## Windows 桌面端

已验证的基础工具链：Windows x64、MSVC x64、Qt 6.8.3 `msvc2022_64`、CMake 3.21+
和 Ninja。请安装 Visual Studio C++ 桌面开发工具、Windows SDK 和 PowerShell 7。
Qt 的 MSVC 工具包和 MinGW 工具包不能混用。

所需 Qt 组件为 Core、Concurrent、Gui、Network、Qml、Quick、QuickControls2、Svg、
Widgets；隔离测试还需要 Test。QML 使用 Dialogs、Effects、Layouts 和 Window。
Qt SDK 须自行从官方渠道获取，使用条件见 [QT-LICENSING.md](QT-LICENSING.md)。

在项目根目录打开 PowerShell 7，确保 `cmake`、`ninja` 和 `ctest` 已加入 PATH：

```powershell
pwsh -File ./scripts/fetch-pdfium.ps1
pwsh -File ./scripts/build.ps1 -QtRoot 'C:/Qt/6.8.3/msvc2022_64' -SkipTests
$env:PATH = 'C:/Qt/6.8.3/msvc2022_64/bin;' + $env:PATH
ctest --test-dir build --output-on-failure -C Release -E '^ai_island_hidden_launch$'
```

将示例 Qt 路径替换为自己的安装目录。构建脚本可以自动查找 MSVC；如查找失败，
传入 `-VsDevCmd '<Visual Studio>/Common7/Tools/VsDevCmd.bat'`。
公开副本的构建脚本默认测试也排除了 `ai_island_hidden_launch`；该用例会启动实际
AI 岛进程，应用交互验收由维护者单独进行。其余测试是独立开发者测试，不替代 GUI 验收。

PDFium 固定版本为 `chromium/8057`（Windows x64），下载时核验来源记录中的 SHA-256。
源码包已保留匹配的头文件和第三方许可证；下载脚本只补齐 DLL 和导入库。
也可以在 CMake 配置时用 `-DMIRRORFLY_PDFIUM_ROOT='<依赖目录>'` 指向含 `include`、
`bin`、`lib`、`LICENSE`、`ORIGIN.md` 和 `licenses` 的同版本目录。
脚本不会执行下载的文件，GitHub 下载不可达时可用 `-ArchivePath '<已下载的 tgz>'`。

## 只构建核心层

在 MSVC x64 Developer PowerShell 中运行（其他编译器需自行验证）：

```powershell
cmake -S . -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DMIRRORFLY_BUILD_UI=OFF -DBUILD_TESTING=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

该模式不需要 Qt 或 PDFium，不产生完整桌面程序。

## 静态检查与兼容性

Python 脚本使用 Python 3；C++ 格式检查固定使用 clang-format 22.1.3。

```powershell
python scripts/check_architecture.py
python scripts/check_quality.py --clang-format clang-format --qmllint 'C:/Qt/6.8.3/msvc2022_64/bin/qmllint.exe'
```

独立 Office 文件读回检查还使用 `python-docx`、`openpyxl`、`python-pptx`、`Pillow` 和 `pypdf`。
先完成完整构建及上述 CTest，再安装到自己的 Python 虚拟环境并执行：

```powershell
python -m pip install python-docx openpyxl python-pptx Pillow pypdf
python scripts/check_quality.py --clang-format clang-format --qmllint 'C:/Qt/6.8.3/msvc2022_64/bin/qmllint.exe' --interop
```

这些 Python 包仅用于开发检查，不是桌面程序运行时依赖。
外部真实文件的兼容性只能按实际样本判断，测试通过不代表支持所有 Office 特性。

## 可选的便携包

本次发布只提供源码，不提供安装程序。仓库保留打包脚本供后续使用；公开副本默认输出到
项目内 `dist/MirrorflyOffice-<版本>`，同名目录存在时停止。脚本不会自动上传任何内容。

完成构建、验收并满足依赖再分发条件后，可运行：

```powershell
pwsh -File ./scripts/package-windows.ps1 -QtRoot 'C:/Qt/6.8.3/msvc2022_64'
```

生成目录包含依赖和许可文件，但**脚本完成不等于已经履行全部再分发义务**。
公开发布 Qt 二进制前，须完成对应 Qt 源码提供安排，并保留所需声明和替换能力；
不能只依赖未来可能失效的上游源码链接。请按实际组件核对 [QT-LICENSING.md](QT-LICENSING.md)。
MSVC 运行库再分发也须遵守自己的 Visual Studio 许可。
