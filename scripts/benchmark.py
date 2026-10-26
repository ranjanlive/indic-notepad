"""Visible release-app warm startup and idle benchmark; never touches the normal profile."""
from pathlib import Path
import ctypes,json,subprocess,time,statistics,platform,winreg,argparse
import psutil
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--samples',type=int,default=5);p.add_argument('--binary',type=Path,default=ROOT/'source_code/build/indic-notepad.exe');a=p.parse_args()
    if not 1<=a.samples<=10:raise ValueError('Use 1–10 samples')
    user32=ctypes.WinDLL('user32',use_last_error=True);user32.WaitForInputIdle.argtypes=[ctypes.c_void_p,ctypes.c_uint32];user32.WaitForInputIdle.restype=ctypes.c_uint32
    samples=[]
    for n in range(a.samples):
        profile=ROOT/'local-work'/f'benchmark-{time.time_ns()}'
        start=time.perf_counter();process=subprocess.Popen([str(a.binary.resolve()),'--data-dir',str(profile),'--benchmark-ms','8500'])
        owned=psutil.Process(process.pid);created=owned.create_time()
        ready=user32.WaitForInputIdle(int(process._handle),10000)
        startup=(time.perf_counter()-start)*1000
        if ready!=0:raise RuntimeError('App did not reach input-idle readiness')
        time.sleep(1);memory=owned.memory_info();full=owned.memory_full_info();t0=time.perf_counter();cpu0=owned.cpu_times();connections=len(owned.net_connections(kind='inet'));children=len(owned.children())
        time.sleep(5);cpu1=owned.cpu_times();elapsed=time.perf_counter()-t0
        cpu=((cpu1.user+cpu1.system)-(cpu0.user+cpu0.system))/elapsed*100
        code=process.wait(timeout=15)
        if code:raise RuntimeError(f'Benchmark app exited {code}')
        samples.append({'sample':n+1,'startup_input_idle_ms':round(startup,2),'working_set_bytes':memory.rss,'private_bytes':getattr(memory,'private',None),'private_working_set_bytes':full.uss,'idle_cpu_percent_one_core':round(cpu,4),'observed_inet_connections_at_sample':connections,'child_processes_at_sample':children,'normal_exit':code==0,'pid_identity_exited':not psutil.pid_exists(process.pid) or psutil.Process(process.pid).create_time()!=created})
        print(f'Sample {n+1}/{a.samples}: {startup:.1f} ms, private working set {full.uss/1048576:.2f} MiB, idle CPU {cpu:.4f}%',flush=True)
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as key:cpu_name=winreg.QueryValueEx(key,'ProcessorNameString')[0].strip()
    ordered=sorted(s['startup_input_idle_ms'] for s in samples)
    report={'version':'0.1.0-beta.1','measured_date':'2026-10-26','method':'Warm, visible release binary; Popen to WaitForInputIdle; synthetic 10 KiB UTF-8 buffer, one tab, minimal UI. No OS/file caches purged. First-paint responsiveness proxy, not keystroke latency. Each isolated profile auto-exits normally.','hardware':{'windows':platform.version(),'architecture':platform.machine(),'cpu':cpu_name,'logical_cpus':psutil.cpu_count(),'physical_ram_bytes':psutil.virtual_memory().total,'system_dpi':user32.GetDpiForSystem(),'storage':'Not characterized by this harness; no SSD/cold-start claim.','antivirus':'Existing host protection unchanged; state not controlled.'},'sample_count':len(samples),'startup_p50_ms':statistics.median(ordered),'startup_p95_nearest_rank_ms':ordered[max(0,__import__('math').ceil(len(ordered)*.95)-1)],'samples':samples,'not_measured':['True cold launch after reboot/security scans','Typing latency and scrolling FPS','100/150/200 percent DPI matrix','Clean Windows 10 and 11 machines','Peak memory during large edits']}
    report['last_sample_native_loop']=json.loads((profile/'native-loop.json').read_text(encoding='utf-8'))
    (ROOT/'docs/verification/benchmark.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
