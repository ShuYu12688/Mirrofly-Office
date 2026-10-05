# Third-party components

This source distribution contains Mirrorfly Office source, selected third-party
sources, headers, resources and regression fixtures. It does not contain Qt SDKs,
Qt runtime binaries, Microsoft runtime binaries or the PDFium DLL/import library.
References below to portable-package contents describe optional future binary
packaging, not files bundled in this source archive.

Mirrorfly original work is covered by the root LICENSE. Its non-commercial and
source-disclosure conditions do not replace or restrict any third-party license.
See COMMERCIAL.md for separate commercial authorization, and QT-LICENSING.md for
Qt-specific rights, the audited version and redistribution requirements.

Qt 6.8.3 is dynamically linked using the applicable LGPL-3.0-only option.
Qt and its bundled dependencies retain their original copyrights and licenses.
The source package includes unmodified LGPLv3/GPLv3 texts and a selected module
license record in licenses/qt/. Future binary packages must additionally retain
their actual component notices, license texts and corresponding-source provision.
Dynamic linking alone does not establish complete LGPL compliance.

- Qt licensing: https://doc.qt.io/qt-6.8/licensing.html
- Qt third-party attributions: https://doc.qt.io/qt-6.8/third-party-libraries.html
- Matching source: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/
- LGPL obligations: https://www.qt.io/development/open-source-lgpl-obligations

Optional Windows binary packages use Visual C++ runtime DLLs from the builder's
licensed Visual Studio redistributable directory. That redistribution is subject
to Microsoft's terms; no such binaries are included here.

- Microsoft guidance: https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files
- SPDX texts: https://github.com/spdx/license-list-data/tree/v3.27.0/text

The Logo/ assets were supplied by 镜蝶科技 and remain unchanged. Company marks
retain their respective ownership and are not a grant of trademark endorsement.
System fonts are used and are not redistributed.

Plain-text Unicode validation and conversion use UTF8-CPP v4.1.1 by Nemanja
Trifunovic and contributors, under the Boost Software License 1.0 (BSL-1.0).
The unmodified header-only source and license are in `third_party/utfcpp/`;
the codec includes them privately and exposes only Mirrorfly function interfaces.
Source revision and per-file SHA-256 hashes are recorded in its `ORIGIN.md`.
The portable package includes the license and provenance in `licenses/utfcpp/`.

- [UTF8-CPP GitHub source](https://github.com/nemtrif/utfcpp)
- [Pinned v4.1.1 commit](https://github.com/nemtrif/utfcpp/tree/819011bb01628fe1aa2f1da9f2c842a48fd5680b)
- [Qt 6.8.3 QSaveFile implementation](https://github.com/qt/qtbase/blob/v6.8.3/src/corelib/io/qsavefile.cpp)

Markdown visual editing reuses the QTextDocument Markdown reader already present in
Qt 6.8.3 Gui. Its bundled MD4C 0.5.2 parser is by Martin Mitas, under the MIT
license. No additional Markdown engine or browser runtime is redistributed.
The matching Qt SBOM, QT_ATTRIBUTIONS.txt and licenses/texts/MIT.txt preserve
the parser's notices and terms. Mirrorfly uses only Qt public interfaces for
document editing and Markdown serialization; the core data layer remains Qt-free.

- [MD4C source release](https://github.com/mity/md4c/releases/tag/release-0.5.2)
- [Matching Qt MD4C attribution](https://github.com/qt/qtbase/blob/v6.8.3/src/3rdparty/md4c/qt_attribution.json)
- [Matching Qt Markdown reader](https://github.com/qt/qtbase/blob/v6.8.3/src/gui/text/qtextmarkdownimporter.cpp)

PPTX preset geometry definitions use the unmodified XML resource from Apache POI
5.4.1, under the Apache License 2.0. The source artifact, SHA-256, original
LICENSE and NOTICE are in `third_party/poi/`; notices are deployed in
`licenses/poi/`. Only geometry data is embedded; no Java runtime or POI bytecode
is included. The guide evaluator and rendering adapter are separate C++ code.

PPTX XML parsing uses pugixml 1.15 by Arseny Kapoulkine, under the MIT license,
pinned to commit `ee86beb30e4973f5feffe3ce63bfa4fbadf72f38`. The unmodified
upstream source and license are in `third_party/pugixml/`. It is compiled as a
private static dependency of the Qt-free core, with XPath support disabled.
Only Mirrorfly data structures and functions appear in the core public API.
The package includes its license and provenance in `licenses/pugixml/`.

- [Pinned pugixml source](https://github.com/zeux/pugixml/tree/ee86beb30e4973f5feffe3ce63bfa4fbadf72f38)
- [pugixml MIT license](https://github.com/zeux/pugixml/blob/ee86beb30e4973f5feffe3ce63bfa4fbadf72f38/LICENSE.md)

PPTX ZIP reading and writing use miniz 3.1.2, under the MIT license, pinned to commit
`77d0dce8627735138c51770d1799a1ef48f2117d`. Its notices credit RAD Game Tools,
Valve Software, Rich Geldreich and Tenacious Software LLC. The unmodified
upstream source and license are in `third_party/miniz/`; `miniz_export.h` is a
small project-owned configuration header for static linking. The library is a
private static dependency of the platform layer. File-based miniz I/O, time
APIs and zlib compatibility APIs are disabled; ZIP writing and deflate are enabled.
Mirrorfly processes bounded package data in memory without extracting archive
members to disk, and saves through Qt's atomic file interface. The package includes the license and provenance in
`licenses/miniz/`.

- [Pinned miniz source](https://github.com/richgel999/miniz/tree/77d0dce8627735138c51770d1799a1ef48f2117d)
- [miniz MIT license](https://github.com/richgel999/miniz/blob/77d0dce8627735138c51770d1799a1ef48f2117d/LICENSE)

Both dependencies are built from the fixed source revisions above; no separate
prebuilt parser binaries or upstream build scripts are downloaded or run.
Their `ORIGIN.md` files record the source revisions and per-file SHA-256 hashes.
PPTX font names are resolved against installed system fonts, with reported
substitutions when needed; neither presentation fonts nor font files are bundled.

The Mirrorfly product mark in assets/mirrorfly-mark.svg is an original vector
asset for this project. The company Logo/ files remain separate and unchanged.
The splash layout references the general art-and-information composition of
IDE launch screens; no JetBrains graphics or logos are included.

PDF viewing and page/annotation editing use the Windows x64 PDFium distribution
from bblanchon/pdfium-binaries, pinned to chromium/8057 (155.0.8057.0), without
V8. Mirrorfly calls only the public C API from the platform layer; no PDFium
headers or handles appear in the core interface. This dependency is a prebuilt
DLL, unlike the source-built XML and ZIP dependencies described above.
The source archive origin and hash are recorded in `third_party/pdfium/ORIGIN.md`.
The portable package includes `pdfium.dll`, its LICENSE and provenance under
`licenses/pdfium/`, and every bundled dependency notice under
`licenses/pdfium/dependencies/`. Preserve those files together when updating
or distributing the preview. PDFium's own license and the bundled component
licenses apply separately; see the included notices for their respective terms.

- [Pinned binary distribution](https://github.com/bblanchon/pdfium-binaries/releases/tag/chromium%2F8057)
- [PDFium source project](https://pdfium.googlesource.com/pdfium/)

FreeMind `.mm` support implements a bounded XML subset in Mirrorfly's own core
using pugixml. No FreeMind executable, library or artwork is redistributed.

## MD4C

Markdown link recognition privately uses MD4C 0.5.2 and its HTML entity table,
Copyright (c) 2016-2024 Martin Mitáš, under the MIT license. The source range
callback patch does not change the Markdown grammar. Source, pinned archive
checksum, patch and license are in `third_party/md4c/`; release packages retain
the license, provenance and patch in `licenses/md4c/`.

- Upstream: https://github.com/mity/md4c/tree/release-0.5.2
