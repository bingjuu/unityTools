# UnityTools

UnityTools is an open-source Qt/C++ Unity game translator and runtime game console. It supports Unity Mono and IL2CPP games through separate BepInEx bridges, and can use an OpenAI-compatible model as a tool-calling Unity Agent.

> Fork of [Moil's XUnity-LLMTranslateGUI](https://github.com/sorrowmoil/Moil-s-XUnity-LLMTranslateGUI) (MIT). See [NOTICE.md](XUnity-Moil-LLMTranslateGUI-C++/NOTICE.md).

## What is included

- Qt 6 / C++17 desktop application sources
- Mono BepInEx 5 and IL2CPP BepInEx 6 bridge sources
- Unity runtime object find/inspect/read/write/invoke protocol
- Session-scoped game console window and Unity Agent orchestration
- Translation endpoint sources and Passthrough/next-launch preference handling
- MIT license and third-party source notices

Internal test sources, design notes, game fixtures, generated binaries, private reference assemblies and local build metadata are intentionally not part of this public source snapshot. Windows binaries are distributed separately as GitHub Release assets.

## Runtime boundaries

- The game console only exposes Unity runtime objects and explicitly observed members/methods. It does not implement Cheat Engine, process-memory scanning, addresses, pointer chains, arbitrary C#, shell execution or dynamic assembly loading.
- A write is successful only when an independent readback matches. Unknown, stale, read-only, ambiguous or unsupported targets are reported as not verified; mutations are never replayed automatically.
- There is no universal Unity game-command language. A command adapter is available only when a concrete API is discovered and selected.
- XUAT 5.6.2 translation disablement keeps injection and uses a next-launch Passthrough preference; it does not claim an unsafe live reversible toggle.

## Build prerequisites

- Windows 10/11, Visual Studio C++ toolchain, CMake and Qt 6 Widgets/Network/Core/Concurrent
- .NET 6 SDK for the IL2CPP bridge variant
- A Unity game's `Managed` directory for Mono bridge compilation
- A target game's `BepInEx/core` and `BepInEx/interop` directories for IL2CPP bridge compilation
- Official reference assemblies supplied locally under `XUnity-Moil-LLMTranslateGUI-C++/bridge/refs/` (not distributed here): BepInEx and XUnity.AutoTranslator reference DLLs
- Official deployment packages supplied locally through `UNITYTOOLS_VENDOR_DIR`; the application can also use its pinned official download URLs

Example configure/build:

```powershell
cmake -S XUnity-Moil-LLMTranslateGUI-C++ -B XUnity-Moil-LLMTranslateGUI-C++/build `
  -G "Visual Studio 18 2026" -A x64 `
  -DQt6_DIR="C:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6" `
  -DGAME_MANAGED="C:/Games/MyUnityGame/MyUnityGame_Data/Managed" `
  -DIL2CPP_INTEROP="C:/Games/MyIl2CppGame/BepInEx/interop"
cmake --build XUnity-Moil-LLMTranslateGUI-C++/build --config Release
```

When the reference assemblies are not supplied, the Qt application still builds; the optional bridge targets are skipped with a configure warning.

## Release packages

Use the Windows Release asset for end users. The asset contains the built `unityTools.exe`, Qt runtime files, UnityTools bridge/translation endpoint binaries and the optional bundled font. It does not bundle a game, save data, API keys, BepInEx/XUAT runtime packages or private reference assemblies.

## License

MIT. See [LICENSE](LICENSE) and [NOTICE.md](XUnity-Moil-LLMTranslateGUI-C++/NOTICE.md).
