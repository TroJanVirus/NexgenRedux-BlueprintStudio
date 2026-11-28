# Device Manager — first remote development integration

Open Scenes & Cinematics > Build & Package > Device Manager. On a trusted isolated LAN, run a matching Redux console runtime with an empty media/debug folder. Enter the console IPv4 address and Attach (TCP 4711). A single debugger owns the connection; disconnect VS Code first. This is an unauthenticated development protocol: do not expose it to the Internet or enable it in shipping builds.

The native editor uses nonblocking sockets and a bounded handshake. Pause, continue, step over/into/out, top script stack location and latest log reuse the existing debugger protocol. Closing Studio disconnects; Back keeps the connection open. The device panel polls a read-only telemetry extension once per second: device platform, physical and free memory as reported by the platform, tracked engine allocations and peak, tagged allocations, last frame FPS/draws and active voices. Older runtimes can attach but may not report telemetry. FPS and draws remain last-reported values while paused. No CPU utilization or GPU time is inferred. The physical platform's memory counter semantics still require real Xbox verification.

Not yet implemented: automatic asset cooking/FTP installation, remote game launch, source hash matching and node breakpoints, pin watches, live graph flow, actor property editing, streamed or captured viewport, live reload controls. A connected runtime need not be running the current project; script location is displayed without falsely mapping it to an unrelated graph. No real-console run has been verified.

## Prepare and deploy a project (command-line helper)

Use the native Export project command first, extract the matching original Xbox runtime, and use the Redux cook executable for your desktop. The optional Python 3.10+ helper stages only the new project media through the Xbox cooker, merges it into a fresh copy of the runtime, and creates the media/screen-ID marker. It creates media/debug and media/dev by default, or omits them with --release. The runtime still contains its bundled screens. It does not set a video mode or claim a standalone shipping game.

```
python engine/tools/blueprint/deploy_xbox.py --export path/to/bp-export --runtime path/to/Nexgen-xbox-og --cook path/to/cook.exe --output path/to/PreviewTitle --start Screen
```

Add `--ftp-host 192.168.1.50 --ftp-user xbox --remote /E/Games/PreviewTitle` to upload to a **new** directory using the dashboard's FTP server (password prompted, not saved). The parent Games directory must already exist. The helper validates portable FATX names, refuses conflicts, creates directories, uploads the executable last, and verifies file sizes. It cannot prove file contents match or launch the XBE; use the dashboard to launch manually. If interrupted, do not launch the incomplete directory. No files are deleted remotely. Use a trusted LAN. This command-line helper is not yet integrated with the native Device Manager.
