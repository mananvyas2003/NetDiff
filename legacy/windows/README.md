# Legacy Windows-only artifacts (not part of libnetdiff / core build)

Moved out of `engine/` in T0.2 so the core path has no `<windows.h>`,
`ShellExecute`, Winsock, or Visual Studio project files.

| File | Why it was removed from core |
|------|------------------------------|
| `web_dashboard.cpp` / `.h` | Winsock HTTP server + `ShellExecuteA` |
| `dashboard.cpp` / `.h` | Console/stats helper only used by the web dashboard |
| `parsernewb.sln` / `.vcxproj` | MSVC-only project; replaced by CMake (T0.1) |

Do not link these into `libnetdiff` or the core CLI.
