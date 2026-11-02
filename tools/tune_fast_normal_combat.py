"""Preserve readable combat time after promoting the ordinary travel speed."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WAVES = {'short_fire', 'short_lock', 'short_intercept', 'short_finale', 'short-exit-guard'}
# Attack lead is in seconds, so it survives both E and reward speed changes.
# Fixed batteries already include this lead in their forward trigger distance.
ATTACK_LEADS = {
    'route_scout': 1.35, 'route_sniper': 2.0, 'short_sniper': 2.4,
    'short_interceptor': 1.55, 'short_turret_predictive': .90,
}


def main():
    course = ROOT/'Resources/courses/ShortCompleteRun.course'
    rows = []
    for line in course.read_text(encoding='utf-8-sig').splitlines():
        p = line.split('|')
        if p[0] == 'wave' and p[1] in WAVES:
            p[4] = '60.000000'
        elif p[0] == 'encounter_beat' and p[4] in WAVES:
            p[7:12] = ['60.000000', '0.600000', '1.200000', '0.950000',
                        '1.450000' if p[4] == 'short_finale' else '1.650000']
        elif p[0] == 'enemy_placement' and p[4] in WAVES:
            p[16] = '110.000000'
        rows.append('|'.join(p))
    course.write_text('\n'.join(rows)+'\n', encoding='utf-8')
    for actor, lead in ATTACK_LEADS.items():
        path = ROOT/'Resources/courses/actors'/f'{actor}.actor'
        rows = []
        for line in path.read_text(encoding='utf-8-sig').splitlines():
            p = line.split('|')
            if p[0] == 'behavior':
                p[5] = f'{lead:.2f}'
            rows.append('|'.join(p))
        path.write_text('\n'.join(rows)+'\n', encoding='utf-8')
    from tune_energy_shield_encounters import main as tune_shields
    tune_shields()


if __name__ == '__main__':
    main()
