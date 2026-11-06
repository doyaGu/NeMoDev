"""Run module lifecycle checks through production editor startup and shutdown."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--module', type=Path, required=True)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--case', choices=['DisabledExecution', 'ExternalModule', 'DisabledExternal', 'StartRefusal', 'StartException'], required=True)
    args = parser.parse_args()
    args.results.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='nemodev-module-lifecycle-') as temporary:
        root = Path(temporary)
        profile, trace = root / 'profile.json', root / 'lifecycle.txt'
        report = args.results / (args.case + '.xml')
        report.unlink(missing_ok=True)
        modules = []
        case = 'external_module_uses_all_frame_phases'
        options = ['--editor-module', str(args.module.resolve())]
        if args.case == 'DisabledExecution':
            modules = [{'id': 'runtime.execution', 'enabled': False}, {'id': 'runtime.views', 'enabled': False}]
            options = []
            case = 'disabled_execution_does_not_bind_debugger'
        elif args.case == 'DisabledExternal':
            modules = [{'id': 'example.lifecycle', 'enabled': False, 'path': str(args.module.resolve())}]
            case = 'disabled_external_module_does_not_run'
        profile.write_text(json.dumps({'version': 1, 'modules': modules}), encoding='utf-8')
        original = profile.read_bytes()
        environment = {key: value for key, value in os.environ.items() if not key.startswith('NEMODEV_LIFECYCLE_')}
        environment['NEMODEV_LIFECYCLE_REPORT'] = str(trace)
        if args.case in ('StartRefusal', 'StartException'):
            environment['NEMODEV_LIFECYCLE_START_MODE'] = 'refuse' if args.case == 'StartRefusal' else 'throw'
        command = [str(args.binary.resolve()), '--editor-profile', str(profile), '--ui-tests', case,
                   '--ui-results', str(report), *options]
        result = subprocess.run(command, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        (args.results / (args.case + '.log')).write_bytes(result.stdout)
        if args.case in ('StartRefusal', 'StartException'):
            assert result.returncode == 1 and b'Module start failed: example.lifecycle' in result.stdout, result.stdout.decode(errors='replace')
            assert profile.read_bytes() == original, 'failed startup changed the saved profile'
            assert list(map(int, trace.read_text().split())) == [1, 0, 0, 0, 1, 0], 'failed startup did not stop and release its module once'
        else:
            assert result.returncode == 0, result.stdout.decode(errors='replace')
            selected = [case for case in ET.parse(report).getroot().iter('testcase') if case.find('skipped') is None]
            assert len(selected) == 1 and selected[0].get('status') == 'Success', 'module startup assertion failed'
            if args.case == 'ExternalModule':
                values = list(map(int, trace.read_text().split()))
                assert values[0] == 1 and all(value > 0 for value in values[1:4]) and values[4:] == [1, 1], 'module stopped after losing its document or did not release its data'
            else:
                assert not trace.exists(), 'disabled external module ran its entry'
        if trace.exists():
            (args.results / (args.case + '.txt')).write_bytes(trace.read_bytes())
    print(args.case + ': startup, frame phases, stop and release verified')


if __name__ == '__main__':
    main()
