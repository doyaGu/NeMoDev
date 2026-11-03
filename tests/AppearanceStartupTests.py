"""Exercise appearance replacement, failed publication and startup restoration."""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import subprocess
import struct
import tempfile
import xml.etree.ElementTree as ET


@contextmanager
def help_page(directory):
    """Use a deterministic page without requiring the optional original CHM."""
    created = not directory.exists()
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / 'help.ndh'
    html = '<html><body>' + ''.join(
        '<p>Section %d: text before the reading position.</p>' % i for i in range(40))
    html += '<pre>struct</pre>' + '<p>Text after the reading position.</p>' * 40 + '</body></html>'
    data = bytearray(b'NDEVHLP5')

    def text(value):
        encoded = value.encode('utf-8')
        data.extend(struct.pack('<I', len(encoded)))
        data.extend(encoded)

    text('appearance.html')
    data.extend(struct.pack('<II', 0, 1))  # No contents tree, one topic.
    for value in ('appearance.html', 'Appearance', html, 'Appearance reading position'):
        text(value)
    with tempfile.TemporaryDirectory(prefix='.appearance-help-', dir=directory.parent) as temporary:
        backup = Path(temporary) / 'help.ndh'
        if target.exists():
            target.rename(backup)
        try:
            target.write_bytes(data)
            yield
        finally:
            target.unlink(missing_ok=True)
            if backup.exists():
                backup.rename(target)
            if created:
                directory.rmdir()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--module', type=Path, required=True)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--help-dir', type=Path, required=True)
    args = parser.parse_args()
    args.results.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nemodev-appearance-') as temporary:
        profile = Path(temporary) / '外观.json'
        profile.write_text('{"version":1}', encoding='utf-8')

        def run(name, phase, module=False, error=None):
            report = args.results / (name + '.xml')
            report.unlink(missing_ok=True)
            before = profile.read_bytes()
            env = dict(os.environ, NEMODEV_APPEARANCE_PHASE=phase,
                       NEMODEV_APPEARANCE_PROFILE=str(profile))
            command = [str(args.binary), '--editor-profile', str(profile), '--ui-tests',
                       '^appearance$' if phase == 'edit' else 'appearance_startup_state',
                       '--ui-results', str(report)]
            if module:
                command += ['--editor-module', str(args.module)]
            result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=45)
            (args.results / (name + '.log')).write_bytes(result.stdout)
            if error:
                if result.returncode != 1 or error not in result.stdout.decode(errors='replace') or profile.read_bytes() != before:
                    raise RuntimeError(name + ': invalid appearance did not preserve the profile\n' + result.stdout.decode(errors='replace'))
                return
            if result.returncode or not report.is_file():
                raise RuntimeError(name + ': ' + result.stdout.decode(errors='replace'))
            cases = [c for c in ET.parse(report).getroot().iter('testcase') if c.find('skipped') is None]
            if len(cases) != (8 if phase == 'edit' else 1) or any(c.get('status') != 'Success' for c in cases):
                raise RuntimeError(name + ': appearance assertions failed\n' + result.stdout.decode(errors='replace'))
            data = json.loads(profile.read_text(encoding='utf-8'))
            (args.results / (name + '.json')).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
            return data

        with help_page(args.help_dir):
            run('editing', 'edit', module=True)
        profile.write_text('{"version":1}', encoding='utf-8')
        saved = run('save', 'save', module=True)
        assert {p['kind'] for p in saved['providers']} == {'theme', 'font', 'image'}
        restored = run('restore', 'restored')
        assert restored['providers'] == saved['providers']
        restored['modules'] = [{'id': 'example.appearance', 'enabled': False, 'path': str(args.module)}]
        profile.write_text(json.dumps(restored), encoding='utf-8')
        missing = run('missing-provider', 'missing')
        assert missing['providers'] == saved['providers']
        missing['modules'][0]['enabled'] = True
        profile.write_text(json.dumps(missing), encoding='utf-8')
        returned = run('provider-returned', 'restored')
        for kind, provider, failure in [('theme', 'example.cycle', 'Theme inheritance cycle'),
                                        ('font', 'example.invalid-font', 'Cannot load font'),
                                        ('theme', 'example.invalid-layout', 'Invalid page geometry')]:
            broken = json.loads(json.dumps(returned))
            next(p for p in broken['providers'] if p['kind'] == kind)['provider'] = provider
            profile.write_text(json.dumps(broken), encoding='utf-8')
            run('invalid-layout' if provider == 'example.invalid-layout' else 'invalid-' + kind, 'restored', error=failure)
        profile.write_text(json.dumps({'version': 1, 'modules': [{'id': 'appearance.classic', 'enabled': False}]}), encoding='utf-8')
        run('builtin-disabled', 'basic')
    print('Appearance editing, publication rollback, restoration, missing modules and invalid-resource protection passed')


if __name__ == '__main__':
    main()
