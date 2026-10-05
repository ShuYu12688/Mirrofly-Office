"""Check the interface inventory emitted by the invisible composition test."""

import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def main():
    contract = json.loads((ROOT / 'build/office-ai-contract.json').read_text(encoding='utf-8'))
    assert contract['ok'] and contract['contractVersion'] == 1
    assert set(contract['modules']) == {'app', 'text', 'word', 'sheets', 'slides', 'pdf', 'mindmap', 'export', 'images'}
    for module, actions in contract['modules'].items():
        assert len({action['name'] for action in actions}) == len(actions), module
        for action in actions:
            assert action['available'], (module, action['name'])
            assert len(action['args']) == len(action['parameters']), action
            assert all(p['name'] and not re.fullmatch(r'arg\d+', p['name'])
                       for p in action['parameters']), action
            assert action['resultType'] in {
                'accepted_status', 'pending_status', 'boolean', 'integer',
                'number', 'string', 'object', 'array', 'json_value'}, action
            assert (action['completion'] == 'pending') == (action['resultType'] == 'pending_status'), action
            metadata = action['metadata']
            assert metadata['effect'] in {'read', 'session', 'document', 'navigation', 'create', 'save', 'export'}
            assert action['mutating'] == (metadata['effect'] != 'read'), action
            assert metadata['completion'] == ('observeState' if action['completion'] == 'pending' else 'immediate')
            evidence = {'read': 'observation', 'session': 'sessionState', 'document': 'acceptedEdit',
                        'navigation': 'verifiedNavigation', 'create': 'createdDocument',
                        'save': 'verifiedFile', 'export': 'verifiedFile'}
            assert metadata['evidence'] == evidence[metadata['effect']], action
            assert isinstance(metadata['aiPermitted'], bool), action
    word = (ROOT / 'src/ui/src/word_document.cpp').read_text(encoding='utf-8')
    word = word.split('bool format_word_document(', 1)[1].split('\n    bool ', 1)[0]
    formats = set(re.findall(r'action == "(\w+)"', word))
    assert formats == set(contract['schemas']['word']['formats']), 'Word format schema drift'
    adapter = (ROOT / 'src/ui/src/presentation_edit_adapter.cpp').read_text(encoding='utf-8')
    edits = set(re.findall(r'action == QStringLiteral\("(\w+)"\)', adapter))
    bridge = (ROOT / 'src/ui/src/presentation_bridge.cpp').read_text(encoding='utf-8')
    bridge = bridge.split('bool PresentationBridge::applyEdit(', 1)[1]
    bridge = bridge.split('static const std::set<QString> actions{', 1)[1].split('};', 1)[0]
    accepted = set(re.findall(r'QStringLiteral\("(\w+)"\)', bridge))
    described = {key for key, value in contract['schemas']['slides'].items() if isinstance(value, dict)}
    assert edits == accepted == described, 'PPTX edit schema drift'
    assert all(field['name'] != 'document' for field in contract['stateFields']['slides'])
    counts = ', '.join(f'{module}={len(actions)}' for module, actions in contract['modules'].items())
    print(f'AI interface inventory: {counts}; Word formats={len(formats)}, PPTX edits={len(edits)}; passed.')


if __name__ == '__main__':
    main()
