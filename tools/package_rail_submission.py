"""Stage the rail game and build sources using an explicit resource allowlist.

Never edits or deletes the development checkout. Unlisted resource files are
excluded from BOTH the executable payload and the source-code payload.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RESOURCE_LIST = ROOT / 'Build/RailSubmissionResources.json'
EXCLUDED_DIRS = {'human', 'simpleskin', 'animatedcube', 'tests', 'fence'}
EXCLUDED_FILES = {'monsterball.png', 'alarm01.wav', 'uvchecker.png'}
AUTHORING_EXTENSIONS = {'.blend', '.blend1', '.fbx', '.usdc', '.mtlx', '.log'}


def forbidden_resource(relative):
    path = Path(relative)
    lower = path.as_posix().lower()
    original = lower.removesuffix('.meta')
    return (path.parts[0].lower() in EXCLUDED_DIRS or
            Path(original).name in EXCLUDED_FILES or
            Path(original).suffix in AUTHORING_EXTENSIONS)


def safe_file(root, relative):
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        raise ValueError(f'Missing or out-of-root input: {relative}')
    return path


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def source_paths():
    # Include tracked sources and new authored sources, not editor caches,
    # personal VS settings, recordings, repository history or build products.
    paths = subprocess.check_output(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT
    ).decode('utf-8').split('\0')
    roots = {'application', 'engine', 'Build', 'lib', 'tools'}
    externals = ('externals/assimp/', 'externals/DirectXTex/', 'externals/imgui/')
    root_extensions = {'.sln', '.vcxproj', '.filters', '.cpp', '.h'}
    for relative in sorted(set(paths)):
        if not relative:
            continue
        path = Path(relative)
        if path.suffix.lower() in {'.pdb', '.obj', '.exe', '.dll', '.pyc', '.user'}:
            continue
        if '__pycache__' in path.parts:
            continue
        if (path.parts[0] in roots or relative.startswith(externals) or
                (len(path.parts) == 1 and path.suffix in root_extensions) or
                relative in {'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES_SHIPPING.md'}):
            yield relative


def stage(build, output, crystal_cavern_demo=False):
    configuration = build.name
    if configuration not in {'Development', 'Release'}:
        raise ValueError('Build folder must identify Development or Release configuration')
    resources = json.loads(RESOURCE_LIST.read_text(encoding='utf-8'))['files']
    if len(resources) != len(set(resources)):
        raise ValueError('Duplicate resource allowlist entries')
    copies = []
    for relative in resources:
        if forbidden_resource(relative):
            raise ValueError(f'Forbidden classroom resource in allowlist: {relative}')
        source = safe_file(ROOT / 'Resources', relative)
        copies.extend([(source, 'Resources/' + relative), (source, 'Source/Resources/' + relative)])
    for name in ['GE3.exe', 'dxcompiler.dll', 'dxil.dll', 'THIRD_PARTY_NOTICES.md']:
        copies.append((safe_file(build, name), name))
    for name in ['Assimp_LICENSE.txt', 'DirectXTex_LICENSE.txt', 'DearImGui_LICENSE.txt',
                 'WindowsSDK_LICENSE.rtf', 'WindowsSDK_THIRD_PARTY_NOTICES.rtf']:
        copies.append((safe_file(build, 'Licenses/' + name), 'Licenses/' + name))
    for relative in source_paths():
        copies.append((safe_file(ROOT, relative), 'Source/' + relative))
    for relative in ['docs/SubmissionPresentationAndProvenance.md']:
        copies.append((safe_file(ROOT, relative), relative))
    # Check all inputs before creating a new output. Never merge into an old
    # build directory, which might still contain excluded school assets.
    output.mkdir(parents=True, exist_ok=False)
    files = {}
    for source, relative in copies:
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        files[relative] = digest(target)
    readme = output / 'README.txt'
    standard_instructions = (
        'レールであばレール：実行ファイル・ソースコード\n\n'
        'このフォルダーの GE3.exe を起動してください。Resources と Licenses を同じ場所に保ってください。\n'
        'タイトル画面：Enterでゲーム開始、Escで終了。開始するまでゲームは進行しません。\n'
        '操作：マウスで照準、左ボタン長押しで通常射撃、右ボタン長押しでロック・離して発射。\n'
        'P：一時停止／再開。R／Enter：リトライ可能時に再試行。Alt+F4：終了。\n')
    demo_instructions = (
        'レールであばレール　光石洞窟 提出用デモ\n\n'
        'ZIPを展開し、このフォルダーの GE3.exe を起動してください。\n'
        'タイトルの「スタート」をクリックするか、Enterを押すと光石洞窟の走行が始まります。\n'
        '走行中は見せ場の結晶へ自動で通常弾を撃ちます。マウスと左クリックでも射撃できます。\n'
        'ゴール後は自動でタイトルに戻ります。Pで一時停止／再開、Alt+F4で終了できます。\n'
        'ソースからビルドしたGE3.exeでは --crystal-cavern-demo で同じデモを起動できます。\n')
    readme.write_text(
        (demo_instructions if crystal_cavern_demo else standard_instructions) +
        f'Source/ にソースコードとビルド設定があります。構成：{configuration} / x64。\n'
        '教材の人物・歩行アニメーション、起動音、ボール柄、確認用モデルは同梱していません。\n'
        '無地の共通テクスチャは Source/tools/generate_neutral_surface.py で生成できます。\n'
        'Source/Build/RailSubmissionResources.json が提出用リソースの一覧です。\n'
        '素材の出典と未確認項目は docs/SubmissionPresentationAndProvenance.md を参照してください。\n'
        'このパッケージ化は全素材の権利確認完了を意味しません。残る出典・利用条件の確認は必要です。\n'
        'プログラム説明PDF・プレイ動画・自己PRシートは、最終提出時に別途まとめてください。\n',
        encoding='utf-8-sig')
    files['README.txt'] = digest(readme)
    manifest = {'schema': 1, 'configuration': configuration + '/x64', 'files': files,
                'excludedResources': sorted(p.relative_to(ROOT / 'Resources').as_posix()
                    for p in (ROOT / 'Resources').rglob('*') if p.is_file()
                    and p.relative_to(ROOT / 'Resources').as_posix() not in resources)}
    (output / 'PACKAGE_MANIFEST.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    verify(output)


def verify(output):
    manifest = json.loads(safe_file(output, 'PACKAGE_MANIFEST.json').read_text(encoding='utf-8'))
    for relative, expected in manifest['files'].items():
        if digest(safe_file(output, relative)) != expected:
            raise ValueError(f'Changed packaged file: {relative}')
    for directory in [output / 'Resources', output / 'Source/Resources']:
        for path in directory.rglob('*'):
            if path.is_file() and forbidden_resource(path.relative_to(directory)):
                raise ValueError(f'Excluded resource leaked into package: {path}')
    print(f'PASS: {len(manifest["files"])} hashed files; excluded assets absent in runtime and source')
    return manifest


def archive(output):
    manifest = verify(output)
    target = output.with_suffix('.zip')
    # Only package the sealed manifest, not logs or caches created during play.
    with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED) as result:
        for relative in [*manifest['files'], 'PACKAGE_MANIFEST.json']:
            result.write(safe_file(output, relative), output.name + '/' + relative)
    with zipfile.ZipFile(target) as result:
        if result.testzip() is not None:
            raise ValueError('ZIP CRC validation failed')
    print(f'ZIP verified: {target}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT.parent / 'generated/outputs/Release')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--zip', action='store_true')
    parser.add_argument('--crystal-cavern-demo', action='store_true')
    args = parser.parse_args()
    if args.zip:
        archive(args.output.resolve())
    elif args.verify:
        verify(args.output.resolve())
    else:
        stage(args.build.resolve(), args.output.resolve(), args.crystal_cavern_demo)
