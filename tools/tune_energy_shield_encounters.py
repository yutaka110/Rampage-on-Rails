"""Introduce the shield variant sparsely, preserving encounter populations."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PLACEMENTS={'short_intercept-member-0','short-exit-guard-member-0'}

def main():
    path=ROOT/'Resources/courses/ShortCompleteRun.course'
    rows=[]
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        p=line.split('|')
        if p[0]=='enemy_placement' and p[1] in PLACEMENTS:p[2]='short_shield_scout'
        rows.append('|'.join(p))
    path.write_text('\n'.join(rows)+'\n',encoding='utf-8')

if __name__=='__main__':main()
