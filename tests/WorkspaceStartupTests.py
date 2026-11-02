"""Restore workspace instances, missing providers and failed state across processes."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--document', type=Path, required=True)
    args = parser.parse_args()
    args.results.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nemodev-workspace-') as temporary:
        profile = Path(temporary) / '中文 workspace.json'
        profile.write_text('{"version":1}', encoding='utf-8')
        instance = 0

        def run(name, phase, preserve=False):
            before = profile.read_bytes()
            report = args.results / (name + '.xml')
            report.unlink(missing_ok=True)
            log = args.results / (name + '.log')
            env = dict(os.environ, NEMODEV_WORKSPACE_PHASE=phase)
            if phase == 'reopen':
                env['NEMODEV_WORKSPACE_INSTANCE'] = str(instance)
            command = [str(args.binary), '--editor-profile', str(profile),
                       '--ui-tests', 'workspace_startup_state', '--ui-results', str(report)]
            if phase == 'opening':
                command.append(str(args.document))
            try:
                result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, timeout=45)
            except subprocess.TimeoutExpired as failure:
                log.write_bytes(failure.stdout or b'')
                raise RuntimeError(name + ': native test exceeded 45 seconds') from failure
            log.write_bytes(result.stdout)
            (args.results / (name + '.json')).write_bytes(profile.read_bytes())
            if result.returncode or not report.is_file():
                raise RuntimeError(name + ': ' + result.stdout.decode(errors='replace'))
            cases = [c for c in ET.parse(report).getroot().iter('testcase')
                     if c.find('skipped') is None]
            if len(cases) != 1 or cases[0].get('status') != 'Success':
                raise RuntimeError(name + ': workspace UI assertions failed')
            if preserve and profile.read_bytes() != before:
                raise RuntimeError(name + ': failed restore overwrote the original profile')
            return json.loads(profile.read_text(encoding='utf-8'))

        opened = run('first-start-with-document', 'opening')
        assert any(v['role'] == 'main.layout' for v in opened['workspace']['views'])
        profile.write_text('{"version":1}', encoding='utf-8')
        saved = run('save', 'save')
        secondary = next(v for v in saved['workspace']['views'] if v['role'] == 'test.secondary')
        layouts = [v for v in saved['workspace']['views'] if v['type'] == 'layout.viewport']
        assert len(layouts) == 2 and len({v['instance'] for v in layouts}) == 2
        layout = next(v for v in layouts if v['role'] == 'test.secondary-layout')
        assert next(s['value'] for s in layout['settings'] if s['key'] == 'tool') == 28
        assert layout['tool'] == 'layout.pan'
        assert next(v for v in layouts if v['role'] == 'main.layout')['tool'] == 'layout.select'
        pan = next(t for t in saved['toolbar-items'] if t['id'] == 'test.startup-pan')
        assert pan['location'] == 'layout.top' and pan['action'] == 'layout.pan'
        assert pan['position'] == [375, 1] and pan['size'] == [100, 19]
        assert any(b['action'] == 'layout.pan' and b['chord'] == 'Ctrl+K' for b in saved['bindings'])
        assert next(s['value'] for s in secondary['settings'] if s['key'] == 'zoom') == .5
        assert all(s['key'] not in ('document', 'session')
                   for v in saved['workspace']['views'] for s in v['settings'])
        instance = secondary['instance']
        assert saved['workspace']['focused'] == instance
        assert '[Docking][Data]' in saved['workspace']['layout']
        restored = run('restore', 'restored')
        assert next(v for v in restored['workspace']['views']
                    if v['role'] == 'test.secondary-layout')['instance'] == layout['instance']
        assert next(v for v in restored['workspace']['views']
                    if v['role'] == 'test.secondary')['instance'] == instance
        assert next(t for t in restored['toolbar-items'] if t['id'] == 'test.startup-pan') == pan

        disabled = copy.deepcopy(restored)
        disabled['modules'] = [{'id': 'schematic.views', 'enabled': False},
                               {'id': 'runtime.views', 'enabled': False}]
        profile.write_text(json.dumps(disabled), encoding='utf-8')
        missing = run('missing-provider', 'missing')
        assert next(v for v in missing['workspace']['views']
                    if v['instance'] == instance)['settings'] == secondary['settings']
        missing = run('close-missing-provider', 'close-missing')
        assert not next(v for v in missing['workspace']['views']
                        if v['instance'] == instance)['open']
        for module in missing['modules']:
            module['enabled'] = True
        profile.write_text(json.dumps(missing), encoding='utf-8')
        run('provider-returned', 'reopen')

        corrupt = copy.deepcopy(restored)
        next(v for v in corrupt['workspace']['views']
             if v['role'] == 'test.secondary')['state-version'] = 999
        profile.write_text(json.dumps(corrupt), encoding='utf-8')
        run('bad-view-version', 'failed', preserve=True)
        run('discard-unrestored', 'recover')

        corrupt = copy.deepcopy(restored)
        corrupt['workspace']['layout'] = ('[Docking][Data]\n'
                                          'DockNode ID=0x1 Parent=0x2\n'
                                          'DockNode ID=0x2 Parent=0x1\n')
        profile.write_text(json.dumps(corrupt), encoding='utf-8')
        run('bad-dock-tree', 'failed', preserve=True)
    print('Workspace instances, layout, missing/returning providers and failed restore recovery passed')


if __name__ == '__main__':
    main()
