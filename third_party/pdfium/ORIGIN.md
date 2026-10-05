# PDFium binary origin

- Upstream: https://github.com/bblanchon/pdfium-binaries
- Release: `chromium/8057` (`155.0.8057.0`)
- Asset: `pdfium-win-x64.tgz`
- Asset size: `3821600` bytes
- SHA-256: `e307d519e42f2e69b1b531f0c2a32dffcdf3891ec0eba60328ba51a57cec01ed`
- Retrieved through the GitHub release asset API on 2026-09-15.

The archive was inspected before extraction. It contains the public headers,
Windows x64 import library and DLL, upstream license, and bundled dependency
licenses. Mirrorfly Office uses only PDFium's public C API.

## Source-only export

The source-only export omits bin/pdfium.dll, lib/pdfium.dll.lib and the upstream
fpdfview.h.orig backup. All active headers and license notices are unchanged.
Run scripts/fetch-pdfium.ps1 to obtain only the pinned DLL/import library after
SHA-256 verification. It does not overwrite the retained headers or licenses.
