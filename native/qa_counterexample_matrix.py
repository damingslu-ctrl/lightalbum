"""Counter-example proof for the 2.8.0 gate matrix (native/gate_matrix.ps1).

Discipline: an assertion nobody has seen go red is a hypothesis, not a fact.
Each case below injects ONE concrete mistake into the real gate table, runs the
real LA_GATE_MATRIX_QA through the real runner, and asserts the runner reports
RED. The table is restored afterwards, and a final green run proves the restore.

Run from the native/ directory:
    python qa_counterexample_matrix.py

Exit code 0 = all counter-examples proven non-vacuous.

History: the first version of the gate's E6 rule was a RATIO check ("fast must
stay under half the suite"). Two of the five original cases stayed GREEN against
it - moving a 3640 ms map gate into the fast layer gave 10/30, still "under half".
A ratio rule cannot see membership decay. The rule was rewritten as a membership
rule (measured-cost budget + a core-invariant gate list), and the cases below are
what forced that rewrite. Keep them when touching gate_matrix.ps1.
"""
import io
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RUNNER = 'qa_runner.ps1'
MATRIX = 'gate_matrix.ps1'

CASES = [
    ('CE1 a core invariant gate silently leaves the PR layer',
     '  @{n="LA_THUMB_CACHE_QA";  png=$false; tier="fast"; measuredMs=201},',
     '  @{n="LA_THUMB_CACHE_QA";  png=$false; measuredMs=201},'),

    ('CE2 a duplicate gate name -> must be rejected',
     '  @{n="LA_LOG_SMOKE";       png=$false; tier="fast"; measuredMs=120},',
     '  @{n="LA_LOG_SMOKE";       png=$false; tier="fast"; measuredMs=120},\n'
     '  @{n="LA_LOG_SMOKE";       png=$false; tier="fast"; measuredMs=120},'),

    ('CE3 an env-gated gate dragged into the fast layer',
     '  @{n="LA_SCAN_SMOKE";      png=$false; need=@("LA_SCAN_ROOT","LA_SCAN_DB"); measuredMs=0},',
     '  @{n="LA_SCAN_SMOKE";      png=$false; need=@("LA_SCAN_ROOT","LA_SCAN_DB"); tier="fast"; measuredMs=0},'),

    ('CE4 an expensive gate demoted into the PR layer',
     '  @{n="LA_MAP_A_QA";        png=$true; measuredMs=3640},',
     '  @{n="LA_MAP_A_QA";        png=$true; tier="fast"; measuredMs=3640},'),

    ('CE5 an unknown tier value is smuggled in',
     '  @{n="LA_HOME_SMOKE";      png=$false; tier="fast"; measuredMs=194},',
     '  @{n="LA_HOME_SMOKE";      png=$false; tier="fas"; measuredMs=194},'),

    # CE6 is a REAL corruption that happened while writing the measuredMs column:
    # an annotation pass appended inside a QUOTED scriptArgs string, so the key
    # vanished and the release self-test silently got a different argument.
    ('CE6 measuredMs swallowed into a quoted scriptArgs string',
     '  @{n="LA_RELEASE_QA";      png=$false; kind="ps1"; script="release.ps1"; scriptArgs="-SelfTest"; measuredMs=3135}',
     '  @{n="LA_RELEASE_QA";      png=$false; kind="ps1"; script="release.ps1"; scriptArgs="-SelfTest; measuredMs=3135"}'),
]


def read(p):
    return io.open(p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


def run_gate():
    r = subprocess.run(
        ['powershell', '-ExecutionPolicy', 'Bypass', '-File', RUNNER,
         '-Only', 'LA_GATE_MATRIX_QA',
         '-Result', os.path.join(os.environ.get('TEMP', '.'), 'qa_client', 'ce_matrix.json')],
        capture_output=True, text=True, timeout=180, cwd=HERE)
    return r.returncode, (r.stdout or '') + (r.stderr or '')


def main():
    orig = read(MATRIX)
    rc, out = run_gate()
    print('baseline rc=%d  (must be 0)' % rc)
    if rc != 0:
        print(out)
        return 1

    verdicts = []
    try:
        for name, old, new in CASES:
            if orig.count(old) != 1:
                print('%-62s ANCHOR MISS (gate table changed?)' % name)
                verdicts.append((name, False))
                continue
            write(MATRIX, orig.replace(old, new))
            rc, out = run_gate()
            red = (rc != 0) and ('FAIL' in out)
            print('%-62s rc=%d  -> %s' % (name, rc, 'RED (good)' if red else '!! STILL GREEN'))
            verdicts.append((name, red))
    finally:
        write(MATRIX, orig)
        print('restored %s' % MATRIX)

    rc, _ = run_gate()
    print('restored rc=%d  (must be 0 again)' % rc)

    bad = [n for n, r in verdicts if not r]
    print('\nCOUNTER-EXAMPLES: %d/%d turned the gate RED'
          % (len(verdicts) - len(bad), len(verdicts)))
    if bad or rc != 0:
        for n in bad:
            print('  NOT PROVEN:', n)
        return 1
    print('LA_GATE_MATRIX_QA assertions are non-vacuous.')
    return 0


if __name__ == '__main__':
    sys.exit(main())