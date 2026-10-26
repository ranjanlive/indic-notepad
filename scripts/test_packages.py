"""Test only newly built owned packages; refuse existing installation identities."""
from pathlib import Path
import hashlib,json,subprocess,time,zipfile,winreg,os
ROOT=Path(__file__).resolve().parents[1];VERSION='0.1.0-beta.1'
KEY=r'Software\Microsoft\Windows\CurrentVersion\Uninstall\IndicStudio.IndicNotePad.Beta'
def exists_key():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,KEY):return True
    except FileNotFoundError:return False
def main():
    root=ROOT/'local-work'/f'package-smoke-{time.time_ns()}';root.mkdir(parents=True)
    shortcut=Path(os.environ['APPDATA'])/'Microsoft/Windows/Start Menu/Programs/IndicStudio/Indic NotePad Beta.lnk'
    if exists_key() or shortcut.exists():raise RuntimeError('Existing Beta install identity found; test refused to overwrite it.')
    out=ROOT/'local-work/native';archive=out/f'indic-notepad-{VERSION}-windows-x64-portable.zip'
    portable=root/'portable'
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            destination=(portable/name).resolve()
            if not destination.is_relative_to(portable.resolve()):raise RuntimeError('Archive traversal')
        z.extractall(portable)
    expected=hashlib.sha256((ROOT/'source_code/build/indic-notepad.exe').read_bytes()).hexdigest()
    assert hashlib.sha256((portable/'indic-notepad.exe').read_bytes()).hexdigest()==expected
    required=['portable.mode','licenses/Scintilla.txt','licenses/Lexilla.txt','licenses/PCRE2.md','HELP.md','LICENSE']
    assert all((portable/p).is_file() for p in required)
    subprocess.run([str(portable/'indic-notepad.exe'),'--data-dir',str(root/'portable-benchmark'),'--benchmark-ms','1000'],check=True,timeout=15)
    install=root/'installed';installer=out/f'indic-notepad-{VERSION}-windows-x64-setup.exe'
    # NSIS's /D= value must be the unquoted final tail; all components are owned
    # fixed literal paths. No shell is involved.
    command=subprocess.list2cmdline([str(installer),'/S'])+' /D='+str(install)
    subprocess.run(command,check=True,timeout=30)
    assert (install/'indic-notepad.exe').is_file() and exists_key() and shortcut.is_file()
    assert not (install/'portable.mode').exists()
    assert hashlib.sha256((install/'indic-notepad.exe').read_bytes()).hexdigest()==expected
    subprocess.run([str(install/'indic-notepad.exe'),'--data-dir',str(root/'installed-benchmark'),'--benchmark-ms','1000'],check=True,timeout=15)
    sentinel=install/'data/keep.txt';sentinel.parent.mkdir();sentinel.write_text('Preserve user data.\n',encoding='utf-8')
    subprocess.run([str(install/'uninstall.exe'),'/S'],check=True,timeout=30)
    deadline=time.monotonic()+20
    while time.monotonic()<deadline and ((install/'indic-notepad.exe').exists() or exists_key() or shortcut.exists()):time.sleep(.1)
    assert not (install/'indic-notepad.exe').exists() and not exists_key() and not shortcut.exists()
    assert sentinel.read_text(encoding='utf-8')=='Preserve user data.\n'
    report={'passed':True,'version':VERSION,'checks':['portable ZIP path/notice/marker presence','portable executable hash equals tested build','portable executable launches and closes normally','per-user silent install into fresh owned directory','installed executable hash equals tested build','installer omits portable marker','installed executable launches and closes normally','uninstall removes own executable/Start-menu shortcut/Beta registry identity','uninstall preserves an adjacent user-data sentinel'],'limits':'Existing Windows host, silent installer flow; not clean Windows 10/11 machines or every interactive installer page. Portable marker profile behavior is separately verified in visible UI.'}
    (ROOT/'docs/verification/package-smoke.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
