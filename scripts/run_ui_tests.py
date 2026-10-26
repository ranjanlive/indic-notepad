"""Run the real native test executable visibly in an isolated, owned profile."""
from pathlib import Path
import subprocess,sys,json,time,psutil
ROOT=Path(__file__).resolve().parents[1]
def main():
    test=ROOT/'local-work'/f'ui-smoke-{time.time_ns()}'
    exe=ROOT/'source_code/build/notepad-ui-tests.exe'
    process=subprocess.Popen([str(exe),'--smoke-test',str(test)])
    identity={'pid':process.pid,'creation_unix':psutil.Process(process.pid).create_time(),'executable':str(exe),'test_dir':str(test)}
    (ROOT/'local-work/owned-ui-test.json').write_text(json.dumps(identity,indent=2),encoding='utf-8')
    try:code=process.wait(timeout=45)
    except subprocess.TimeoutExpired:
        owned=psutil.Process(process.pid)
        if owned.create_time()==identity['creation_unix'] and Path(owned.exe()).resolve()==exe.resolve():owned.terminate();process.wait(timeout=10)
        raise RuntimeError('Native test timed out; its isolated owned process was closed.')
    report=test/'ui-smoke.json'
    if report.exists():
        data=json.loads(report.read_text(encoding='utf-8'));print(json.dumps(data,indent=2));
        if code==0 and data['passed']:(ROOT/'docs/verification/native-ui-smoke.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
    if code:raise SystemExit(code)
if __name__=='__main__':main()
