"""Create unsigned Windows portable/installer artifacts with full dependency notices."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,zipfile,os
ROOT=Path(__file__).resolve().parents[1];VERSION='0.1.0-beta.1'
def main():
    p=argparse.ArgumentParser();p.add_argument('--makensis',type=Path);a=p.parse_args()
    build=ROOT/'source_code/build';out=ROOT/'local-work/native';stage=ROOT/'local-work/package-stage'
    stage.mkdir(parents=True,exist_ok=True);(stage/'licenses').mkdir(exist_ok=True);out.mkdir(parents=True,exist_ok=True)
    for source,name in [(build/'indic-notepad.exe','indic-notepad.exe'),(ROOT/'LICENSE','LICENSE'),(ROOT/'THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md'),(ROOT/'docs/HELP.md','HELP.md')]:shutil.copyfile(source,stage/name)
    cached={}
    for row in (build/'CMakeCache.txt').read_text(encoding='utf-8').splitlines():
        if '=' in row and ':' in row.split('=',1)[0]:
            key,value=row.split('=',1);cached[key.split(':')[0]]=value
    for dep,license,target in [('scintilla','License.txt','Scintilla.txt'),('lexilla','License.txt','Lexilla.txt'),('pcre2','LICENCE.md','PCRE2.md')]:
        source=Path(cached.get('FETCHCONTENT_SOURCE_DIR_'+dep.upper()) or build/f'_deps/{dep}-src')
        shutil.copyfile(source/license,stage/'licenses'/target)
    (stage/'README.txt').write_text('Indic NotePad '+VERSION+' | From IndicStudio\n\nWindows 10/11 x64. Unsigned beta. No WebView, Java or separate VC runtime install.\n\nPORTABLE ZIP: unzip the whole archive, then run indic-notepad.exe. The included\nportable.mode marker keeps settings/recovery in its adjacent data directory.\nINSTALLER: per-user installation; standard profile in LocalAppData.\n\nRead HELP.md for file safety, recovery privacy, limits and keyboard shortcuts.\nMIT source: https://github.com/ranjanlive/indic-notepad\nRelease: https://github.com/ranjanlive/indic-notepad/releases/tag/v'+VERSION+'\n',encoding='utf-8')
    archive=out/f'indic-notepad-{VERSION}-windows-x64-portable.zip'
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for file in sorted(stage.rglob('*')):
            if file.is_file():z.write(file,file.relative_to(stage).as_posix())
        z.writestr('portable.mode','Indic NotePad portable profile marker\n')
    nsis=a.makensis or Path(os.environ['LOCALAPPDATA'])/'tauri/NSIS/makensis.exe'
    if not nsis.exists():
        found=shutil.which('makensis')
        if not found:raise RuntimeError('NSIS 3 is required for the per-user installer. Pass --makensis.')
        nsis=Path(found)
    installer=out/f'indic-notepad-{VERSION}-windows-x64-setup.exe'
    subprocess.run([str(nsis),'/V2',f'/DSTAGE={stage}',f'/DOUTPUT={installer}',str(ROOT/'scripts/installer.nsi')],check=True)
    artifacts=[{'file':file.name,'bytes':file.stat().st_size,'sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'platform':'windows','architecture':'x64','signing':'unsigned'} for file in [archive,installer]]
    (out/'SHA256SUMS.txt').write_text(''.join(f'{item["sha256"]}  {item["file"]}\n' for item in artifacts),encoding='utf-8')
    (out/'build-windows-x64.json').write_text(json.dumps({'version':VERSION,'artifacts':artifacts,'runtime':'C++20 native Win32; static Scintilla, selected Lexilla lexers, PCRE2 and MSVC CRT; system DLLs only','source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip() if (ROOT/'.git/refs/heads/main').exists() else None},indent=2)+'\n',encoding='utf-8')
    print(json.dumps(artifacts,indent=2))
if __name__=='__main__':main()
