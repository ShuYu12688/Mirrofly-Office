"""Repeatable source and QML checks; never starts the application."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(args):
    subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang-format', default='clang-format')
    parser.add_argument('--qmllint', default='qmllint')
    parser.add_argument('--build-dir', default='build')
    parser.add_argument('--core-only', action='store_true')
    parser.add_argument('--interop', action='store_true')
    args = parser.parse_args()
    run([sys.executable, 'scripts/check_architecture.py'])
    version = subprocess.check_output([args.clang_format, '--version'], text=True)
    if not re.search(r'\b22\.1\.3\b', version):
        raise RuntimeError('Use clang-format 22.1.3 to keep local and CI formatting identical.')
    sources = sorted(p for folder in ('src', 'tests') for p in (ROOT / folder).rglob('*')
                     if p.suffix in ('.cpp', '.hpp'))
    for offset in range(0, len(sources), 40):
        run([args.clang_format, '--dry-run', '--Werror', *sources[offset:offset + 40]])
    cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
    for variable, values, body in re.findall(r'foreach\((\w+)\s+([^)]*)\)(.*?)endforeach\(\)', cmake, re.S):
        cmake += ''.join(body.replace('${' + variable + '}', value) for value in values.split())
    missing = [str(p.relative_to(ROOT)) for p in (ROOT / 'tests').glob('*.cpp')
               if 'tests/' + p.name not in cmake]
    if missing:
        raise RuntimeError('Test sources absent from CMake: ' + ', '.join(missing))
    print(f'Format: {len(sources)} C++ files; all test sources registered.', flush=True)
    if not args.core_only:
        pages = sorted(p for p in (ROOT / 'ui').rglob('*.qml') if p.name != 'Main.qml')
        run([args.qmllint, '-I', args.build_dir, '-W', '0', *pages])
        composition = ROOT / 'ui/Main.qml'
        result = subprocess.run([args.qmllint, '-I', args.build_dir, '--json', '-', str(composition)],
                                cwd=ROOT, capture_output=True, text=True, encoding='utf-8')
        report = json.loads(result.stdout)
        contexts = set(re.findall(r'setContextProperty\("(\w+)"',
                                 (ROOT / 'src/ui/src/runtime.cpp').read_text(encoding='utf-8')))
        lines = composition.read_text(encoding='utf-8').splitlines()
        accepted = 0
        for file in report['files']:
            for warning in file['warnings']:
                line = lines[warning['line'] - 1]
                token = line[warning['column'] - 1:warning['column'] - 1 + warning['length']]
                if (warning['id'] == 'unqualified' and warning['type'] == 'warning'
                        and token in contexts):
                    accepted += 1
                else:
                    raise RuntimeError('Unexpected Main.qml diagnostic: ' + json.dumps(warning))
        if result.returncode and not accepted:
            raise RuntimeError('qmllint failed: ' + result.stderr)
        print(f'QML: {len(pages)} pages/components clean; Main has {accepted} known injected-context references.')
    if args.interop:
        if args.core_only or args.build_dir != 'build':
            raise RuntimeError('Interop fixtures currently use the full build/ directory.')
        table_fixture = ROOT / 'build' / 'presentation-table-structure.pptx'
        transition_fixture = ROOT / 'build' / 'presentation-transition.pptx'
        group_fixture = ROOT / 'build' / 'presentation-group-layer.pptx'
        theme_fixture = ROOT / 'build' / 'presentation-theme-state.pptx'
        storage_test = ROOT / 'build' / ('mirrorfly_presentation_storage_tests.exe'
                                           if os.name == 'nt' else 'mirrorfly_presentation_storage_tests')
        environment = os.environ.copy()
        environment['MIRRORFLY_PPTX_TABLE_STRUCTURE_OUTPUT'] = str(table_fixture)
        environment['MIRRORFLY_PPTX_TRANSITION_OUTPUT'] = str(transition_fixture)
        environment['MIRRORFLY_PPTX_GROUP_LAYER_OUTPUT'] = str(group_fixture)
        environment['MIRRORFLY_PPTX_AUTHORED_THEME_OUTPUT'] = str(theme_fixture)
        environment['PATH'] = str(Path(args.qmllint).resolve().parent) + os.pathsep + environment.get('PATH', '')
        subprocess.run([str(storage_test)], cwd=ROOT, env=environment, check=True)
        run([sys.executable, 'tests/fixtures/verify_word_interop.py'])
        run([sys.executable, 'tests/fixtures/verify_word_lines.py'])
        run([sys.executable, 'tests/fixtures/verify_word_lists.py'])
        run([sys.executable, 'tests/fixtures/verify_word_tabs.py'])
        run([sys.executable, 'tests/fixtures/verify_word_unicode.py'])
        run([sys.executable, 'tests/fixtures/verify_spreadsheet_interop.py'])
        run([sys.executable, 'tests/fixtures/verify_presentation_interop.py'])
        run([sys.executable, 'tests/fixtures/verify_presentation_patterns.py', 'build'])
        run([sys.executable, 'scripts/check_office_ai_contract.py'])
    print('Quality checks passed.')


if __name__ == '__main__':
    main()
