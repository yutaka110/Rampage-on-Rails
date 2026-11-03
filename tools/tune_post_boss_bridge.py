"""Retain the opt-in boss-relative bridge prototype during course regeneration."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MARKER='post-boss-white-bridge'
def main():
    path=ROOT/'Resources/courses/ShortCompleteRun.course'
    rows=[line for line in path.read_text(encoding='utf-8-sig').splitlines()
          if not (line.startswith('event|') and line.split('|')[3]==MARKER)]
    rows.append('event|3350|setpiece|post-boss-white-bridge|boss_relative|post-boss-white-bridge|1|0')
    path.write_text('\n'.join(rows)+'\n',encoding='utf-8')
if __name__=='__main__':main()
