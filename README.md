# UnityTools

English | [简体中文](README.zh-CN.md)

An MTool-style Unity game translator and runtime game console. Supports Unity Mono / IL2CPP, with a built-in tool-calling Unity Agent that reads and writes game values from natural language.

> Fork of [Moil's XUnity-LLMTranslateGUI](https://github.com/sorrowmoil/Moil-s-XUnity-LLMTranslateGUI) (MIT). Differences from upstream are described below; see [NOTICE.md](NOTICE.md) for licensing and third-party notices.

## Features

### Translation

- **One-click injection**: drop a game exe → engine and architecture detected from the PE header (Mono/IL2CPP, x86/x64) → deploy BepInEx + XUAT + UniversalUnityDemosaics (demosaic) + RuntimeUnityEditor as selected → launch
- **Two-phase IL2CPP startup**: first launch auto-generates interop assemblies and restarts automatically; the game only starts once injection is confirmed, with explicit failure reporting
- **Truly concurrent translation proxy**: a built-in `127.0.0.1:6800` proxy forwards all in-game XUAT requests to any OpenAI-compatible model you configure (local Sakura / DeepSeek / GPT, ...). A custom endpoint plugin replaces the serial built-in HttpEndpoint of XUAT 5.6.2 (whose `MaxConcurrentTranslations` key is a dead setting in that version) and batches requests for real concurrency
- **Disable translation without losing injection**: a preference writes the `Passthrough` endpoint for the next launch while XUAT/Bridge stay deployed; the shared proxy is managed per-session owner so exiting one game never breaks another
- **Official components fetched for you**: BepInEx / XUAT / UUD / RUE are read from a local vendor directory first, otherwise downloaded automatically from pinned official URLs (with mirror acceleration)
- **ASCII launch path**: non-ASCII game directories are resolved to pure-ASCII launch paths (NTFS 8.3 short paths / junctions); the original directory is untouched
- **Save locator**: scans the game directory and Unity's persistentDataPath for save locations

### Game console

- **Standalone non-modal window**: opens automatically after the injection handshake (sessionId / PID / runtime / token verified); closing it never kills the game; game exit or switch cleans up the old session and rejects stale responses
- **Unity runtime object access**: find scene GameObjects / Components / loaded game static types; inspect fields, properties, collection entries (List / Dictionary / array) and method descriptions — inspection never triggers property getters
- **Verified writes**: read → write with snapshot validation → independent readback. Exact Int64 / UInt64 / decimal text (no JSON double rounding), null vs empty string, enum / bool; structs are written back along the parent path instead of mutating a copy; collections are validated against key/index snapshots, and writes are refused when the parent was replaced or the collection changed
- **Explicit data entries**: data providers (`GetCurrentPlayData`) and in-game command entries are listed by the capability report and selected by you; multiple candidates pause for your choice — never "first match wins"
- **One operation set for Mono and IL2CPP**: reflection vs Il2CppInterop generated projections underneath; every Unity access runs on the game's main thread, large discoveries yield per frame

### Unity Agent

- **Natural-language value editing**: type "set money to 9999" in the console; the Agent speaks the OpenAI-compatible `tools/tool_calls` protocol and may only call tools declared by UnityTools
- **Fixed safe pipeline**: find → inspect → read original value → write → independent readback; `set` and `add` are strictly distinguished; success requires a matching readback
- **No fake success**: tasks without verified evidence are reported as failed; on unknown results (timeout/disconnect) it queries status and re-reads the actual value, never replaying a mutation (an `add` would double-apply)
- **Multi-candidate pause**: ambiguous targets list candidates and wait for your selection; memory/address/shell/arbitrary-code tools are prohibited
- Translation and Agent APIs are configured independently; keys never leave your machine

## Differences from upstream (the fork parent)

Upstream (baseline ae62fd9) is a translation-configuration tool: a C++ translator GUI on top of XUAT's built-in endpoints, with no launcher, no in-game access and no Agent. This fork completes the pipeline from "launch and deploy" to "in-game operations" to "natural-language Agent", and substantially simplifies the interface.

### Added (not present upstream)

| Feature | Upstream | This project |
|---|---|---|
| One-click injection | No launcher; injection components must be configured manually | Drop a game exe to auto-detect engine and architecture, deploy BepInEx / XUAT / UUD / RUE as selected; two-phase IL2CPP with automatic restart |
| Game console | No in-game capability at all | Mono / IL2CPP dual bridges, object-level Unity runtime read/write, confirmed by independent readback |
| Unity Agent | None | Natural-language value editing over the OpenAI tool-calling protocol, with anti-fake-success semantics |
| Concurrent translation endpoint | Uses XUAT's built-in endpoints (serial) | A custom endpoint plugin batches requests for real concurrency |
| Translation toggle | None | Disabling keeps injection via a next-launch preference; multiple games share the proxy without interfering |
| Launch helpers | None | ASCII launch paths, save locator, bundled CJK font deployment (with system-font fallback) |

### Removed / simplified (present upstream, dropped here)

| Upstream original | Handled in this project |
|---|---|
| Modern themes and glass-morphism looks (ModernWindow / ModernUI) | Removed; unified into a standard two-tab (Launch / Settings) layout |
| Environment scan window (EnvScanWindow, incl. the Modern variant) | Removed |
| Advanced settings dialog (AdvancedSettings) | Removed; common options merged into the standard settings page |
| GoogleTranslate endpoint option | Removed; translation goes through the local self-hosted proxy only |
| Legacy Python variant | Removed; C++ app only |

## Tech stack

- **Desktop app**: Qt 6 (Widgets / Network / Concurrent), C++17, MSVC x64, CMake; QtTest for tests
- **Translation proxy**: [cpp-httplib](https://github.com/yhirose/cpp-httplib) (MIT) thread pool with request-scoped network object lifetimes
- **JSON**: [nlohmann/json](https://github.com/nlohmann/json) (MIT, C++ side); [SimpleJSON](https://github.com/MParticle-Solutions/SimpleJSON) (MIT, C# side, source bundled)
- **Mono bridge**: C# / BepInEx 5 plugin, System.Reflection object access, single-file csc build
- **IL2CPP bridge**: C# / BepInEx 6 + [Il2CppInterop](https://github.com/BepInEx/Il2CppInterop) generated projections, .NET 6, dotnet build
- **Unity Agent**: native OpenAI Chat Completions `tools/tool_calls`; assistant messages preserved verbatim (including DeepSeek `reasoning_content`), tool results posted back with the matching `tool_call_id`
- **Windows integration**: PE header parsing for engine/arch detection, NTFS 8.3 short paths and junctions for non-ASCII launch paths, System32 bsdtar for extracting official packages, machine-wide font registration (with a system-font fallback chain)

## Runtime boundaries

- Only Unity runtime objects and explicitly observed members/methods are exposed. No Cheat Engine, process-memory scanning, addresses, pointer chains, arbitrary C#, shell execution or dynamic assembly loading
- A write is successful only when an independent readback matches. Unknown, stale, read-only, ambiguous or unsupported targets are reported as such; mutations are never replayed automatically
- There is no universal game-command language. A command adapter exists only when a concrete API is discovered and selected; submission does not verify a business outcome
- XUAT 5.6.2 translation disablement keeps injection and uses a next-launch Passthrough preference; no unsafe live reversible toggle is claimed

## Build prerequisites

- Windows 10/11, Visual Studio C++ toolchain, CMake, Qt 6 (Widgets/Network/Core/Concurrent)
- .NET 6 SDK for the IL2CPP bridge variant
- A Unity game's `Managed` directory for Mono bridge compilation; a target game's `BepInEx/core` and `BepInEx/interop` directories for IL2CPP bridge compilation
- Official reference assemblies supplied locally under `bridge/refs/` (not distributed here): BepInEx and XUnity.AutoTranslator reference DLLs
- Official deployment packages supplied locally through `UNITYTOOLS_VENDOR_DIR`; the application can also use its pinned official download URLs

Example configure/build:

```powershell
cmake -S . -B build `
  -G "Visual Studio 18 2026" -A x64 `
  -DQt6_DIR="C:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6" `
  -DGAME_MANAGED="C:/Games/MyUnityGame/MyUnityGame_Data/Managed" `
  -DIL2CPP_INTEROP="C:/Games/MyIl2CppGame/BepInEx/interop"
cmake --build build --config Release
```

When the reference assemblies are not supplied, the Qt application still builds; the optional bridge targets are skipped with a configure warning.

## Release packages

End users should use the [Windows Release asset](https://github.com/bingjuu/unityTools/releases). It contains the built `unityTools.exe`, Qt and MSVC runtimes, bridge/translation endpoint binaries and the optional bundled font. It does not bundle a game, save data, API keys, BepInEx/XUAT runtime packages or private reference assemblies.

## License

MIT. See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
