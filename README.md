# Desktop Toggler

**English** | [فارسی](README.fa.md)

Double-click an **empty spot on your Windows desktop** to instantly hide or show
the desktop icons *and* the taskbar together — a one-gesture "clean desktop" for
screenshots, screen recordings and presentations.

> 🤖 **Made with AI.** This project, including its source code, build setup and
> documentation, was written by an AI assistant (**Claude**, by Anthropic) in a
> conversation with a human maintainer. The maintainer tested it on Windows and
> reported that it works, but there is no automated test suite. Please read the
> code (it is a single, commented file) before you run it, like you would with any
> third-party program. See [AI-generated code notice](#ai-generated-code-notice).

## Features

- **Double-click empty desktop space** → toggle *clean mode*:
  - ON: desktop icons hidden + taskbar auto-hide turned on
  - OFF: icons shown + taskbar auto-hide restored to what it was before
- **Invisible**: no window, no taskbar button, no system-tray icon.
- **Right-click the desktop** → `Enable/Disable Desktop Toggler` turns the
  feature on or off. The choice is saved and survives restarts.
- **Starts with Windows**, no administrator rights needed (everything is stored
  per-user).
- **Self-contained installer and uninstaller in one EXE.** The uninstaller removes
  *every* version of the app (processes, startup entries, menu entries, settings,
  files).
- Works with multiple monitors (every taskbar is handled), per-monitor DPI aware.
- Single small static executable, no runtime to install. No network access.

## Install

1. Download `DesktopToggler.exe` from the [Releases](../../releases) page (or build it
   yourself, see [Build](#build)).
2. Run it once. It will:
   1. remove any older version of this app,
   2. copy itself to `%LocalAppData%\DesktopToggler\`,
   3. register autostart, a desktop right-click entry and an *Apps & features* entry,
   4. start in the background.

If the app is already installed, running the EXE asks: **Yes** = clean reinstall,
**No** = uninstall everything, **Cancel** = do nothing.

> The EXE is **not code-signed**, so Windows SmartScreen may warn on first run
> (*More info → Run anyway*), and some antivirus products are suspicious of any
> program that installs a global mouse hook. Compare the SHA-256 in
> `SHA256SUMS.txt` with the file you downloaded, or build from source.

## Use

| Action | Result |
|---|---|
| Double-click an empty area of the desktop | Toggle clean mode |
| Double-click an icon | Normal behaviour (does *not* toggle) |
| Right-click desktop → *Enable/Disable Desktop Toggler* | Turn the feature on/off |

On Windows 11 the entry is under **Show more options** (this applies to every
third-party context-menu item).

The taskbar usually takes a moment to slide away after auto-hide is switched on and
reappears instantly when it is switched off. That timing belongs to Windows
Explorer, not to this app.

## Uninstall

Any of these removes **all versions** of the app:

- *Settings → Apps → Desktop Toggler → Uninstall*
- Run `DesktopToggler-Uninstall.exe` (or any copy of the EXE whose file name contains
  `uninstall`)
- Command line: `DesktopToggler.exe --uninstall` (add `--silent` for no dialogs)

If the app was left in clean mode, the uninstaller first restores your icons and
taskbar. The installed EXE cannot delete itself while running, so a hidden
`cmd.exe` removes the leftover folder a couple of seconds after the uninstaller exits.
Copies of the EXE that you saved elsewhere (e.g. *Downloads*) must be deleted by hand.

### Command line

| Argument | Meaning |
|---|---|
| *(none)* | Installed copy: run in the background. Other copy: install / reinstall |
| `--install` | Force install (reinstall without asking) |
| `--uninstall` | Remove every version |
| `--silent` | Suppress all dialogs |
| `--toggle-enabled` | Internal: used by the context-menu entry |

## How it works

1. **Install.** The EXE copies itself to `%LocalAppData%\DesktopToggler\` and writes
   a few per-user registry keys (see [Registry and files](#registry-and-files)).
2. **Listener.** At login the installed copy starts, creates an invisible
   *message-only* window and installs a **low-level mouse hook** (`WH_MOUSE_LL`).
3. **Double-click detection.** The hook recognises a double-click with the same rules
   Windows uses (`GetDoubleClickTime`, `SM_CXDOUBLECLK/SM_CYDOUBLECLK`). A 700 ms
   cool-down after each detected double-click stops a fast extra click from firing a
   second toggle that would undo the first. The hook itself does no real work: it only
   hands the click position to a worker thread, because Windows disconnects hooks
   that are slow.
4. **Is it the empty desktop?** The worker asks which window is under the cursor
   and walks up to its top-level ancestor. If that is the desktop (`Progman` or
   `WorkerW`) the click counts. If the icon list (`SysListView32`) was hit, an
   `LVM_HITTEST` is sent to Explorer's list view to see whether the cursor is on an
   icon or on empty space. When the icons are hidden the list is invisible, so a
   click on the desktop is empty space by definition.
5. **Hide/show icons.** Explorer's own *View → Show desktop icons* command is sent:
   `WM_COMMAND` with command id `0x7402` to the desktop's `SHELLDLL_DefView`
   window. Success is verified by checking whether the desktop list view became
   (in)visible. If the command had no observable effect, the list view is shown or hidden
   directly as a fallback, so the visible result is always right.
6. **Taskbar.** Auto-hide is switched with the documented AppBar API
   (`SHAppBarMessage` with `ABM_GETSTATE` / `ABM_SETSTATE`) on the main taskbar and
   every secondary-monitor taskbar. The previous auto-hide setting is remembered and
   restored when leaving clean mode.
7. **Enable/Disable.** The context-menu entry runs `DesktopToggler.exe --toggle-enabled`.
   That short-lived process posts a private window message to the running listener,
   which flips the setting, rewrites the menu label, and installs or removes the
   mouse hook. Disabled means the hook is not installed at all.

## Code walkthrough

All logic is in [`src/DesktopToggler.cpp`](src/DesktopToggler.cpp) (plain Win32 API,
C++17, no external libraries).

| Area | Functions |
|---|---|
| Entry / modes | `wWinMain`, `EnableDpiAwareness` — parses arguments; the file name containing `uninstall` also selects uninstall mode |
| Mouse hook | `MouseProc` (double-click timing, cool-down, busy guard), `InstallHook`, `RemoveHook` |
| Worker | `WorkerProc` → `HandleDoubleClick` → `HitTestDesktopIcon` (cross-process `LVM_HITTEST` using `VirtualAllocEx`/`WriteProcessMemory`) |
| Desktop icons | `FindDefView`, `FindDesktopListView`, `AreIconsHidden`, `SendIconsCommand`, `SetIconsHidden` |
| Taskbar | `GetTaskbarAutoHide`, `SetAutoHideOne`, `SetTaskbarAutoHide` |
| State | `ToggleCleanMode` (decides direction, remembers the previous auto-hide setting) |
| Enable/disable | `UpdateContextMenu`, `ToggleEnabledFeature`, `HelperToggleEnabled`, `WndProc`, `RunListener` |
| Installer | `DoInstall`, `RegisterUninstallEntry`, `RunInstallerInteractive` |
| Uninstaller | `DoUninstall`, `RemoveAllVersions`, `StopAllInstances`, `FindTrace*`, `DeleteTree`, `ScheduleDelayedDelete`, `RestoreDesktopIfWeChangedIt` |
| Diagnostics | `Logf` (size-capped log file) |

Design notes:

- **"All versions" cleanup** matches by the prefix `DesktopToggle` (process names,
  startup entries, menu keys, settings keys, install folders). Startup entries are also
  matched by their *command line*, so a renamed entry is still found. Stubborn old
  processes that ignore `WM_CLOSE` are terminated after a short wait.
- **State is not trusted blindly.** After the first successful, observed toggle the
  app reads the real icon state before each toggle; before that it uses its own flag.
- **No admin rights**: only `HKCU` and `%LocalAppData%` are touched, and the manifest
  requests `asInvoker`. (Running elevated would actually get in the way of talking to
  the non-elevated Explorer.)
- **Per-monitor DPI awareness** (manifest) keeps screen coordinates consistent with
  Explorer's, which matters for the icon hit test on scaled displays.

### Registry and files

| Location | Purpose |
|---|---|
| `HKCU\Software\DesktopToggler` | `Enabled`, `PrevAutoHide`, `CleanActive` |
| `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` → `DesktopToggler` | Autostart |
| `HKCU\Software\Classes\DesktopBackground\Shell\DesktopToggler` (+ `\command`) | Desktop right-click entry |
| `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopToggler` | *Apps & features* entry |
| `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced\HideIcons` | Read only, as a fallback when Explorer is not ready yet |
| `%LocalAppData%\DesktopToggler\DesktopToggler.exe` | Installed program |
| `%LocalAppData%\DesktopToggler\log.txt` | Diagnostic log (max 128 KB) |

## Build

Requirements: a Windows x64 target; any of the options below.

### GitHub Actions (no local tools)

The workflow in [`.github/workflows/build.yml`](.github/workflows/build.yml) builds on
every push and pull request:

1. Open **Actions → Build → (latest run) → Artifacts → DesktopToggler** to download
   the EXEs.
2. To publish a release, push a tag:
   ```
   git tag v2.0.0
   git push origin v2.0.0
   ```
   The workflow then builds and attaches `DesktopToggler.exe`,
   `DesktopToggler-Uninstall.exe` and `SHA256SUMS.txt` to a new GitHub Release.

### Linux / macOS / WSL (mingw-w64 cross compiler)

```bash
sudo apt-get install g++-mingw-w64-x86-64   # Debian / Ubuntu
bash build.sh                               # output in dist/
```

`build.sh` also fails if the EXE depends on anything other than standard Windows system DLLs.

### Windows (MSVC, "x64 Native Tools Command Prompt")

```bat
cd src
rc DesktopToggler.rc
cl /O2 /EHsc /std:c++17 DesktopToggler.cpp DesktopToggler.res /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib advapi32.lib ole32.lib shlwapi.lib
```

## Troubleshooting

- **Icons or taskbar do not react**: open `%LocalAppData%\DesktopToggler\log.txt`. It
  records, for each double-click on the desktop, what was hit, what the hit test
  returned and which toggle method worked. Attach it to a
  [bug report](../../issues/new/choose). The log contains only the app's own events
  (no keystrokes, no file names).
- **Taskbar hides with a delay**: normal, see [Use](#use).
- **Entry missing from the right-click menu on Windows 11**: look under *Show more options*.
- **Antivirus / SmartScreen warning**: see [Install](#install).

## Limitations

- Windows 10 and 11, x64 only (no ARM64 / 32-bit build is provided).
- Windows has no documented API for toggling desktop icons; this app uses the
  command Explorer itself uses (`0x7402`) and verifies the result, with a fallback.
  A future Windows update could change that behaviour.
- The Windows 11 top-level context menu cannot be used by an unpackaged program.
- Only HKCU/LocalAppData are used, so the app is per-user.
- Not tested on every Windows build and no automated tests exist.

## AI-generated code notice

This repository was produced by **Claude** (an AI model made by Anthropic) working
with a human maintainer, who requested the features, ran the program on a real
Windows machine and reported the results. The first versions did not work correctly;
the maintainer's reports (and a rewrite) led to the current design. The installer and
uninstaller logic was additionally exercised under Wine against fake "old versions";
the desktop-icon and taskbar behaviour was verified by the maintainer by hand.

AI-written code can contain mistakes. You are encouraged to read
`src/DesktopToggler.cpp`, build the EXE yourself, and report problems.

## License

[MIT](LICENSE). You may use, modify and redistribute it, including commercially, as
long as the licence text is kept. It is provided **without any warranty**.
