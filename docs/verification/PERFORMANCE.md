# Measured performance · Windows host

26 October 2026 · 0.1.0-beta.1 release build · 5 sequential visible samples.

| Measurement | Observed |
|---|---|
| Warm startup, process creation to Windows input-idle p50 / p95 | 143.4 / 231.9 ms |
| Private working set (USS) | 21.72–21.76 MiB |
| Total working set | 53.52–53.56 MiB |
| Private bytes / commit | 37.31–37.36 MiB |
| Idle CPU, percent of one logical core | 1.87–4.99% |
| Idle CPU, divided by 12 logical CPUs | 0.156–0.416% |
| App children / observed Internet sockets at sample points | 0 / 0 |
| Normal self-exit and owned PID gone | All samples |

Host: Windows build 10.0.26300, 12th Gen Intel(R) Core(TM) i5-12450H,
15.73 GiB physical RAM.
Minimal view, synthetic 10 KiB UTF-8 buffer, one tab. Display API reported
96 system DPI; no full scaling matrix was exercised.
Storage and antivirus state were not controlled; system caches were preserved.
No true cold-start, typing-latency, scrolling-FPS or large-edit peak-memory claim.

The near-zero idle CPU planning target is not fully met under this measurement;
it is reported as observed, not rounded to zero. Diagnostic counters recorded
2 main-window paints,
2 Scintilla UI updates and
1 one-shot app timer in the last sample;
there is no continuous main-window repaint/polling loop in that run.
No sockets at sample points and absence of networking code are not a packet-trace
certification of the operating system. Full raw values/method are in benchmark.json.

The portable ZIP size is measured separately in the release receipt. These are
one-host beta results; clean Windows 10/11 and independent accessibility/IME/RTL
and cold-start testing remain pending. The full original performance target set
is not presented as achieved.
