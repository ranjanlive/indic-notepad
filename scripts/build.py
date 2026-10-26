"""Build/test the native app with two workers; discover installed MSVC securely."""
from pathlib import Path
import os,subprocess,json,argparse,shutil
ROOT=Path(__file__).resolve().parents[1]
def environment():
    if os.environ.get('VSCMD_ARG_TGT_ARCH')=='x64':return os.environ.copy(),shutil.which('cmake')
    vswhere=Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    instances=json.loads(subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-format','json'],text=True))
    if not instances:raise RuntimeError('Install MSVC C++ x64 Build Tools and a Windows SDK.')
    vs=Path(instances[0]['installationPath']); bat=vs/'VC/Auxiliary/Build/vcvars64.bat'
    comspec=os.environ.get('COMSPEC','cmd.exe')
    raw=subprocess.check_output(f'"{comspec}" /d /s /c ""{bat}" >nul && set"',text=True)
    env=os.environ.copy()
    for row in raw.splitlines():
        if '=' in row and not row.startswith('='):
            k,v=row.split('=',1);env[k]=v
    tools=vs/'Common7/IDE/CommonExtensions/Microsoft/CMake'
    env['PATH']=str(tools/'CMake/bin')+';'+str(tools/'Ninja')+';'+env.get('Path',env.get('PATH',''))
    return env,str(tools/'CMake/bin/cmake.exe')
def main():
    p=argparse.ArgumentParser();p.add_argument('--local-dependencies',action='store_true');p.add_argument('--configure-only',action='store_true');a=p.parse_args()
    env,cmake=environment();build=ROOT/'source_code/build';cmd=[cmake,'-S',str(ROOT/'source_code'),'-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release']
    if a.local_dependencies:
        for name,folder in [('scintilla','scintilla'),('lexilla','lexilla'),('pcre2','pcre2-10.49')]:cmd.append(f'-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={ROOT/"local-work/dependencies"/folder}')
    subprocess.run(cmd,env=env,check=True)
    if a.configure_only:return
    subprocess.run([cmake,'--build',str(build),'--parallel','2'],env=env,check=True)
    subprocess.run([str(Path(cmake).with_name('ctest.exe')),'--test-dir',str(build),'--output-on-failure'],env=env,check=True)
if __name__=='__main__':main()
