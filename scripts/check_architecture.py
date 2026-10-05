"""Check explicit source boundaries and the application entry contract."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
PUBLIC_MODULES = {
    "credential_protection": "platform",
    "build_version": "ui",
    "pdf": "core", "mindmap": "core", "pdf_storage": "platform", "mindmap_storage": "platform", "automation": "ui", "office_ai": "ui", "pdf_export": "ui",
    "app": "app", "ai_island_app": "app", "ui": "ui", "ai_island": "ui", "core": "core", "text": "core",
    "platform": "platform", "text_storage": "platform", "image_decode": "platform", "embedded_font": "platform", "markdown": "core",
    "presentation": "core", "presentation_animation": "core", "presentation_geometry": "core", "presentation_storage": "platform", "presentation_media": "platform",
    "word": "core", "word_storage": "platform", "office_package": "core", "spreadsheet": "core", "spreadsheet_storage": "platform",
    "office_progress": "platform",
}
ALLOWED = {
    "main": {"app"},
    "app": {"app", "ui"},
    "ui": {"ui", "platform", "core"},
    "platform": {"platform", "core"},
    "core": {"core"},
}
VENDOR_OWNERS = {"mirrorfly_md4c": {"core"}, "mirrorfly_pugixml": {"core"}, "mirrorfly_miniz": {"platform"}, "mirrorfly_pdfium": {"platform"}}


def check_build_dependencies(errors):
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    modules = {f"mirrorfly_{name}": name for name in ("app", "ui", "platform", "core")}
    modules["MirrorflyOffice"] = "main"
    modules["MirrorflyAiIsland"] = "main"
    modules["mirrorfly_ai_island_app"] = "app"
    modules["mirrorfly_ai_island_ui"] = "ui"
    graph = {module: set() for module in ALLOWED}
    for target, body in re.findall(r"target_link_libraries\(\s*(\w+)\s+([^)]*)\)", cmake, re.S):
        module = modules.get(target)
        if not module:
            continue
        for dependency in re.findall(r"[A-Za-z_][\w:]*", body):
            if dependency in VENDOR_OWNERS and module not in VENDOR_OWNERS[dependency]:
                errors.append(f"CMake: vendor dependency escaped its owner: {target} -> {dependency}")
            if dependency in modules:
                destination = modules[dependency]
                graph[module].add(destination)
                if destination not in ALLOWED[module]:
                    errors.append(f"CMake: illegal dependency {target} -> {dependency}")
            if module in {"core", "app", "main"} and dependency.startswith("Qt6::"):
                errors.append(f"CMake: Qt linked outside UI/platform: {target} -> {dependency}")

    def visit(module, active, finished):
        if module in active:
            errors.append(f"CMake: cyclic module dependency through {module}")
            return
        if module in finished:
            return
        active.add(module)
        for dependency in graph[module]:
            visit(dependency, active, finished)
        active.remove(module)
        finished.add(module)

    finished = set()
    for module in graph:
        visit(module, set(), finished)


def check():
    errors = []
    check_build_dependencies(errors)
    private_headers = {}
    for path in (ROOT / "src").rglob("*.hpp"):
        if "include" not in path.parts:
            private_headers.setdefault(path.name, set()).add(path.relative_to(ROOT / "src").parts[0])
    main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
    public_app = (ROOT / "src/app/include/mirrorfly/app.hpp").read_text(encoding="utf-8")
    entry_names = re.findall(r"\b(?:int|void)\s+(mirrorfly_\w+)\s*\(", public_app)
    entry_call = r"(?:" + "|".join(map(re.escape, entry_names)) + r")\((?:argc, argv)?\);"
    main_without_comments = re.sub(r"//[^\n]*", "", main)
    expected = re.compile(
        r"#include <mirrorfly/app\.hpp>\s+"
        r"int main\(int argc, char\* argv\[\]\)\s*\{\s*"
        r"(?:" + entry_call + r"\s*)*return " + entry_call + r"\s*\}\s*"
    )
    if not expected.fullmatch(main_without_comments):
        errors.append("Application main may call multiple public app interfaces, without local logic.")
    island_main = (ROOT / "src/ai_island_main.cpp").read_text(encoding="utf-8")
    island_entry = re.compile(
        r"#include <mirrorfly/ai_island_app\.hpp>\s+"
        r"int main\(int argc, char\* argv\[\]\)\s*\{\s*"
        r"return mirrorfly_run_ai_island\(argc, argv\);\s*\}\s*"
    )
    if not island_entry.fullmatch(island_main):
        errors.append("AI island main must call only its public app interface.")

    for path in (ROOT / "src").rglob("*"):
        if path.suffix not in {".cpp", ".hpp"}:
            continue
        content = path.read_text(encoding="utf-8")
        relative = path.relative_to(ROOT)
        module = path.relative_to(ROOT / "src").parts[0]
        module = "main" if path.parent == ROOT / "src" else module
        if "\t" in content:
            errors.append(f"{relative}: tab indentation is forbidden.")
        for number, line in enumerate(content.splitlines(), 1):
            if line.strip() and (len(line) - len(line.lstrip(" "))) % 4:
                errors.append(f"{relative}:{number}: indentation must use four-space units.")
        for include in re.findall(r"^#include\s+[<\"]([^>\"]+)[>\"]", content, re.M):
            if include.endswith(".cpp") or "../" in include:
                errors.append(f"{relative}: implementation-path include {include}")
            if include.startswith("mirrorfly/"):
                name = Path(include).stem
                dependency = PUBLIC_MODULES.get(name)
                if dependency not in ALLOWED[module]:
                    errors.append(f"{relative}: illegal public dependency {include}")
            if module in {"core", "app", "main"} and include.startswith("Q"):
                errors.append(f"{relative}: Qt escaped the framework boundary.")
            owners = private_headers.get(Path(include).name)
            if owners and module not in owners:
                errors.append(f"{relative}: another module's private header: {include}")
        if "include" in path.parts and re.search(r"\bQ[A-Z]\w+", content):
            errors.append(f"{relative}: public module interface exposes a Qt type.")

    for path in (ROOT / "ui").rglob("*.qml"):
        content = path.read_text(encoding="utf-8")
        if "\t" in content:
            errors.append(f"{path.relative_to(ROOT)}: tab indentation is forbidden.")
        for number, line in enumerate(content.splitlines(), 1):
            if line.strip() and (len(line) - len(line.lstrip(" "))) % 4:
                errors.append(f"{path.relative_to(ROOT)}:{number}: indentation must use four-space units.")
        if path.name != "Main.qml" and re.search(r"\b(?:appBridge|textEditor|editorTools|presentation|spreadsheet|wordEditor|pdfEditor|mindmapEditor|pdfExporter|automationBridge)\s*\.", content):
            errors.append(f"{path.relative_to(ROOT)}: page bypasses the composition root context boundary.")

    for path in (ROOT / "tests").glob("*.cpp"):
        content = path.read_text(encoding="utf-8")
        if not re.search(r"\bint main\([^)]*\)\s*\{\s*return \w+\([^;{}]*\);\s*\}", content):
            errors.append(f"{path.relative_to(ROOT)}: test main must only call its runner interface.")

    if errors:
        print("\n".join(errors))
        return 1
    print("Module graph, private headers, framework isolation, QML boundaries and main contract: passed.")
    return 0


if __name__ == "__main__":
    sys.exit(check())
