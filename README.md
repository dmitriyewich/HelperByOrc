# HelperByOrc

Native `ASI` plugin for `GTA San Andreas` / `SA:MP`.

The project ports the original HelperByOrc Lua/MoonLoader logic to a native Win32 C++ module. The active repository, solution, project and runtime artifact name is `HelperByOrc`.

## Repository

GitHub: [github.com/dmitriyewich/HelperByOrc](https://github.com/dmitriyewich/HelperByOrc)

The repository intentionally contains:

- project source code;
- build files: `HelperByOrc.slnx`, `HelperByOrc/HelperByOrc.vcxproj`, workflow files;
- vendored dependencies in `HelperByOrc/external`;
- documentation: `README.md`, `README.txt`, `context.md`.

The repository intentionally does not contain local runtime/build artifacts:

- `HelperByOrc/Release`, `HelperByOrc/build`, `HelperByOrc/Debug`;
- `HelperByOrc.asi`, `.pdb`, `.lib` outputs;
- local IDE/service folders like `.cursor`, `.codex` runtime files;
- reference dumps, temporary unpacked DLLs and local archives.

Vendored libraries are stored as normal files, not as nested Git repositories/submodules. Current external tree includes `plugin-sdk`, `imgui`, `MinHook`, `raknet`, `SAMP-API` and `memwrapper`.

## Current State

- Target: `Win32` / `x86`.
- Format: `ASI`.
- Main project: `HelperByOrc/HelperByOrc.vcxproj`.
- Solution: `HelperByOrc.slnx`.
- Output: `HelperByOrc/Release/HelperByOrc.asi`.
- Active hook backend: `MinHook`.
- Release build profile is normal/debuggable: no LTCG, no `/GS-`, no omitted frame pointers, symbols enabled, SDL/GS enabled.
- Arizona-specific `_chat.asi` direct integration is temporarily disabled at compile time through `HelperByOrc/feature_flags.h`; chat input uses the standard SA:MP fallback path.

## Stability Changes

- Heavy initialization and shutdown are moved out of `DllMain` into a bootstrap worker thread.
- SA:MP hooks and RakNet hooks are installed only after SA:MP reaches full-ready state.
- D3D overlay attach is deferred until SA:MP full-ready. This avoids early loading-screen cursor/input conflicts.
- AppCompat diagnostics now log exact current-exe Layer checks, `__COMPAT_LAYER`, known compatibility tags and loaded Windows shim modules such as `apphelp.dll`, `AcLayers.dll` and `AcGenral.dll`.
- D3D9 diagnostics log dummy device creation, vtable targets, target module/RVA for `Reset`, `Present`, `EndScene`, and the final hook policy.
- If `IDirect3DDevice9::Reset` points into `apphelp.dll`, the Reset hook is skipped intentionally. Overlay remains active through `Present` / `EndScene`.
- Runtime logs include SA:MP readiness probes, pointer regions, transfer owner modules for already patched SA:MP functions, and `[probe][stuck]` diagnostics when full-ready is not reached for too long.

## Main Modules

- `mod_app` - lifecycle, top-level UI, SA:MP readiness gate and cursor ownership.
- `imgui_overlay` - D3D9 hooks, ImGui initialization, WndProc routing.
- `samp_api` - safe SA:MP memory access and readiness diagnostics.
- `samp_hooks` - regular SA:MP hooks.
- `samp_rak_hooks` - RakNet hooks and RPC/packet interception.
- `binder_module` - command binder and related UI.
- `tags_module` - variables/tags engine.
- `hotkey_utils` - shared hotkey capture and matching.
- `text_encoding` - UTF-8/game encoding conversion.

## Build

Use MSBuild from Visual Studio.

Local build:

```powershell
msbuild HelperByOrc.slnx /p:Configuration=Release /p:Platform=Win32 /m
```

GitHub Actions build:

- workflow: `.github/workflows/build-release-win32.yml`;
- builds `Release|Win32`;
- verifies vendored dependencies before build;
- uploads `HelperByOrc.asi` and `HelperByOrc.pdb` as workflow/release artifacts.

The project file currently targets local Visual Studio 18 toolset `v145`. The GitHub workflow overrides `PlatformToolset=v143` for hosted Windows runners.

## Runtime Files

- `HelperByOrc.asi` - plugin.
- `HelperByOrc.json` - user settings and bind config.
- `HelperByOrc.log` - diagnostic log.

## Troubleshooting Notes

- `sampInfo=0` with `refGame=1` is an early SA:MP state: SA:MP reached GUI/game initialization, but `CNetGame` is not constructed yet. It is not a standalone error if `sampInfo` later becomes non-null.
- Compatibility mode can route D3D9 `Reset` through `apphelp.dll`. The plugin detects this and skips only the unsafe Reset hook.
- If SA:MP never reaches full-ready, inspect `[samp][diag]`, `[diag][appcompat]`, `[ui][d3d]`, loaded modules and transfer-owner lines in `HelperByOrc.log`.
