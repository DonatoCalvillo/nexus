# Nexus

Nexus is a lightweight Windows application launcher inspired by macOS Spotlight. Press **Ctrl+Space** to open a frosted overlay, type to filter installed apps, and launch with Enter.

It stays resident in the background (no tray required): the overlay hides when you dismiss it, and the hotkey keeps working until you quit the process.

## Features

- Global hotkey: `Ctrl+Space`
- Runs in the background after dismiss / on login (`--autostart`)
- Optional install to Windows startup (current user, no admin)
- Indexes Windows Apps folder plus Start Menu shortcuts
- Substring search with up to 5 results
- Simple arithmetic (`10*1`, `100 / 2 * .5 + 19`): result appears as a row; `Enter` copies it (max 64 characters, 15 digits per number)
- Direct web search shortcuts: `g <query>` (Google), `yt <video>` (YouTube), `gh <repo>` (GitHub), `so <error>` (Stack Overflow) — `Enter` opens the default browser
- Direct URLs: queries starting with `http://` / `https://`, or hosts like `example.com` / `localhost:3000`, show **Abrir URL** as the first result
- Port process lookup: `-p <port>` lists the process listening on that TCP/UDP port; `Enter` asks for confirmation before terminating it
- Native system commands: `lock`, `sleep`, `restart`, `shutdown`, `empty trash`, `kill <process>`, `mute`, `vol <0-100>`
- Keyboard navigation: `↑` / `↓`, `Enter` to launch, copy, open URL, or run a command, `Esc` to dismiss
- Windows 11 DWM acrylic backdrop and rounded corners

## Download / install (end users)

1. Open the [GitHub Releases](../../releases) page of this repo.
2. Download **NexusSetup-x.y.z.exe** (recommended) or the portable `.zip`.
3. Run the installer — leave **Start Nexus when I sign in** checked.
4. After login, Nexus starts hidden. Press `Ctrl+Space` anytime.

Without an installer, from a Release build:

```powershell
# Copy Nexus.exe somewhere permanent, then:
.\Nexus.exe --install-startup
.\Nexus.exe --autostart
```

Disable login startup:

```powershell
.\Nexus.exe --uninstall-startup
```

## Usage

| Action | Shortcut / flag |
| --- | --- |
| Show / hide overlay | `Ctrl+Space` |
| Move selection | `↑` / `↓` |
| Launch selected app / copy calculation / open URL | `Enter` |
| Google search | `g <query>` |
| YouTube search | `yt <video>` |
| GitHub search | `gh <repo>` |
| Stack Overflow search | `so <error>` |
| Open URL in browser | `https://…`, `example.com`, `localhost:3000` |
| Find process by port | `-p <port>` (e.g. `-p 3000`) |
| Kill process on port | Select result → `Enter` → confirm in dialog |
| Lock session | `lock` |
| Suspend computer | `sleep` |
| Restart / shut down | `restart` / `shutdown` |
| Empty Recycle Bin | `empty trash` |
| Kill process by name | `kill <name>` (e.g. `kill chrome`) → `Enter` → confirm |
| Mute / set volume | `mute` / `vol <0-100>` |
| Close overlay | `Esc` or click away |
| Start hidden (background) | `Nexus.exe --autostart` |
| Register login startup | `Nexus.exe --install-startup` |
| Remove login startup | `Nexus.exe --uninstall-startup` |

## Requirements (developers)

- Windows 10 / 11
- Visual Studio 2022+ with **Desktop development with C++**
- MSVC toolset (`v145`) and Windows 10 SDK
- Optional: [Inno Setup 6](https://jrsoftware.org/isinfo.php) to build the installer

## Build

```powershell
msbuild Nexus.slnx /p:Configuration=Release /p:Platform=x64
```

Output: `x64\Release\Nexus.exe`

### Run unit tests

```powershell
msbuild tests\Nexus.Tests.vcxproj /p:Configuration=Release /p:Platform=x64
.\x64\Release\Nexus.Tests.exe
```

### Build the installer

1. Install [Inno Setup](https://jrsoftware.org/isinfo.php).
2. Build **Release | x64** as above.
3. Open `installer\Nexus.iss` in Inno Setup Compiler (or run `ISCC.exe installer\Nexus.iss`).
4. Find `dist\NexusSetup-1.0.0.exe` — that is what you attach to a GitHub Release.

The installer copies Nexus to `%LOCALAPPDATA%\Nexus`, registers HKCU startup with `--autostart`, and does not need administrator rights.

## Publish as open source (GitHub)

1. Create a new empty repository on GitHub.
2. Push this project:

```powershell
git init
git add .
git commit -m "Initial open-source release of Nexus"
git branch -M main
git remote add origin https://github.com/YOUR_USER/Nexus.git
git push -u origin main
```

3. Cut a release with a version tag — **GitHub Actions builds and uploads the artifacts**:

```powershell
git tag v1.0.0
git push origin v1.0.0
```

That runs `.github/workflows/release.yml` and publishes:

- `NexusSetup-<version>.exe` — installer (startup registration)
- `Nexus-<version>-windows-x64.zip` — portable
- `Nexus.exe` — raw binary

You can also run the **Release** workflow manually from the Actions tab (`workflow_dispatch`).

CI (`.github/workflows/ci.yml`) builds Nexus and runs the calculator test suite on every push/PR to `main` so regressions show up before tagging.

4. This repo already includes an **MIT** `LICENSE`.

## Project structure

```
Nexus/
├── include/           # Public headers
│   ├── Calculator.h
│   ├── WebSearch.h
│   ├── PortLookup.h
│   ├── SystemCommands.h
│   ├── Constants.h
│   ├── Types.h
│   ├── StringUtils.h
│   ├── AppIndexer.h
│   ├── IconManager.h
│   ├── Overlay.h
│   └── Startup.h      # HKCU Run registration
├── src/
│   ├── main.cpp
│   ├── StringUtils.cpp
│   ├── AppIndexer.cpp
│   ├── Calculator.cpp
│   ├── WebSearch.cpp
│   ├── PortLookup.cpp
│   ├── SystemCommands.cpp
│   ├── IconManager.cpp
│   ├── Overlay.cpp
│   └── Startup.cpp
├── tests/
│   ├── CalculatorTests.cpp
│   ├── WebSearchTests.cpp
│   ├── PortLookupTests.cpp
│   ├── SystemCommandsTests.cpp
│   └── Nexus.Tests.vcxproj
├── installer/
│   └── Nexus.iss      # Inno Setup script
├── LICENSE
└── README.md
```

## Architecture notes

| Module | Responsibility |
| --- | --- |
| `AppIndexer` | Enumerate apps, apply search filter |
| `Calculator` | Detect and evaluate simple arithmetic expressions |
| `WebSearch` | Prefix searches and direct URL / host detection |
| `PortLookup` | Find / kill processes by local TCP/UDP port |
| `SystemCommands` | Native lock / sleep / power / trash / kill / volume |
| `IconManager` | Resolve and cache visible icons |
| `Overlay` | Window UI, hotkey, painting |
| `Startup` | Enable/disable launch on Windows sign-in |
| `main` | CLI flags, single-instance mutex, message loop |

## Contributing

1. Keep functions small and single-purpose.
2. Prefer descriptive English names.
3. Do not change launcher behavior unless intentional and documented.
4. Match the existing MSVC / C++20 Win32 style.

## License

MIT — see [LICENSE](LICENSE).
