"""Exercise composition entry points and Help state through real editor restarts."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--module', type=Path, required=True)
    parser.add_argument('--help-dir', type=Path, required=True)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--mode', choices=('files', 'help'), required=True)
    args = parser.parse_args()
    args.results.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nemodev-finish-') as temporary:
        root = Path(temporary)
        profile = root / '中文 profile.json'
        profile.write_text('{"version":1}', encoding='utf-8')

        def run(name, phase, *files):
            report = args.results / (name + '.xml')
            report.unlink(missing_ok=True)
            case = ('composition_entrypoints_share_open_and_failure_state' if args.mode == 'files'
                    else 'help_cross_startup_and_external_provider')
            command = [str(args.binary.resolve()), '--editor-profile', str(profile), '--editor-module',
                       str(args.module.resolve()), '--ui-tests', case, '--ui-results', str(report.resolve()), *map(str, files)]
            env = dict(os.environ, NEMODEV_FINISH_PHASE=phase, NEMODEV_FINISH_ROOT=str(root))
            result = subprocess.run(command, env=env, cwd=root, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=45)
            (args.results / (name + '.log')).write_bytes(result.stdout)
            if result.returncode or not report.is_file():
                raise RuntimeError(name + ': ' + result.stdout.decode(errors='replace'))
            selected = [c for c in ET.parse(report).getroot().iter('testcase') if c.get('status') != 'Unknown']
            if len(selected) != 1 or selected[0].get('status') != 'Success':
                raise RuntimeError(name + ': native UI assertions failed')
            data = json.loads(profile.read_text(encoding='utf-8'))
            (args.results / (name + '.json')).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
            return data

        if args.mode == 'files':
            run('create-fixtures', 'files-save')
            cmo = root / '中文 scene.cmo'
            run('startup-cmo', 'files-startup-cmo', cmo)
            run('startup-nmo', 'files-startup-nmo', '--', '--runtime.nmo')
            run('open-drop-dialog-cmo', 'files-entry-cmo', cmo)
            run('open-drop-dialog-nmo', 'files-entry-nmo', '--', '--runtime.nmo')
        else:
            # Use the real compiler; the original installed help is restored even
            # when a native process or an assertion fails.
            sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
            from HelpContent import compile_help
            from ExtractHelp import decode
            source = root / 'help'
            source.mkdir()
            (source / 'home.html').write_text('<h1>Home</h1><a href="topic.html">Topic</a>', encoding='utf-8')
            (source / 'topic.html').write_text('<h1>Topic</h1>' + ''.join(
                f'<p>Saved reading position paragraph {i}</p>' for i in range(120)), encoding='utf-8')
            compile_help(source, [], 'home.html', decode)
            archive = args.help_dir / 'help.ndh'
            existed, directory_existed = archive.exists(), args.help_dir.exists()
            original = archive.read_bytes() if existed else None
            args.help_dir.mkdir(parents=True, exist_ok=True)
            fixture = (source / 'help.ndh').read_bytes()
            try:
                archive.write_bytes(fixture)
                saved = run('help-save', 'help-save')
                view = next(v for v in saved['workspace']['views'] if v['type'] == 'help.reference')
                settings = view['settings']
                assert next(v['value'] for v in settings if v['key'] == 'current') == 'topic.html'
                native = run('help-native-restore', 'help-native')
                native_view = next(v for v in native['workspace']['views'] if v['type'] == 'help.reference')
                assert native_view['settings'] == settings
                external = json.loads(profile.read_text(encoding='utf-8'))
                external['providers'].append({'kind': 'view', 'id': 'help.reference', 'provider': 'example.help'})
                profile.write_text(json.dumps(external), encoding='utf-8')
                archive.unlink()
                replaced = run('help-external-without-builtin-book', 'help-external')
                restored_view = next(v for v in replaced['workspace']['views'] if v['type'] == 'help.reference')
                assert restored_view['settings'] == settings
                archive.write_bytes(fixture)
                replaced['providers'] = [v for v in replaced['providers'] if not
                    (v['kind'] == 'view' and v['id'] == 'help.reference')]
                profile.write_text(json.dumps(replaced), encoding='utf-8')
                returned = run('help-builtin-returned', 'help-native')
                returned_view = next(v for v in returned['workspace']['views'] if v['type'] == 'help.reference')
                assert returned_view['settings'] == settings
            finally:
                if existed:
                    archive.write_bytes(original)
                else:
                    archive.unlink(missing_ok=True)
                    if not directory_existed:
                        args.help_dir.rmdir()
    print(args.mode + ': real startup, shared commands, state restoration and provider checks passed')


if __name__ == '__main__':
    main()
