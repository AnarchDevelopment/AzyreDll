# Contributing to Azyre Client

Thank you for your interest in contributing to Azyre Client! This guide covers everything you need to know before opening a pull request.

---

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Environment](#development-environment)
- [Project Architecture](#project-architecture)
- [Code Style](#code-style)
- [Creating a Module](#creating-a-module)
- [Creating a Command](#creating-a-command)
- [Scripting API Changes](#scripting-api-changes)
- [Portability Checklist](#portability-checklist)
- [Testing & CI](#testing--ci)
- [Commit Messages](#commit-messages)
- [Pull Request Process](#pull-request-process)
- [License](#license)

---

## Code of Conduct

Be respectful and constructive. Harassment, discrimination, and spam will not be tolerated. Use issues and pull request discussions for technical feedback only.

---

## Getting Started

1. **Fork** the repository on GitHub.
2. **Clone** your fork:
   ```bash
   git clone https://github.com/<your-username>/AzyreDll.git
   cd AzyreDll
   ```
3. **Create a branch** from `main`:
   ```bash
   git checkout -b feat/my-feature
   ```
4. **Build** the project (see [Development Environment](#development-environment)).
5. **Commit** your changes following the [Commit Messages](#commit-messages) convention.
6. **Push** and open a pull request against `main`.

---

## Development Environment

### Prerequisites

| Requirement | Minimum version |
|-------------|-----------------|
| **OS** | Windows 10 / 11 (x64) |
| **CMake** | 3.25+ |
| **Windows SDK** | With C++/WinRT headers |
| **C++ Toolchain** | MSVC 2022/2026, Clang-CL, or MinGW-w64 (GCC) |

### Building

All dependencies are fetched automatically through CMake `FetchContent`.

```bash
# MSVC (recommended for development)
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --parallel

# Clang-CL
cmake -B build -G "Visual Studio 18 2026" -A x64 -T ClangCL
cmake --build build --config Release --parallel

# MinGW GCC (Ninja, single-config)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build --parallel
```

> **Tip:** Use `Release` for testing. `Debug` builds are substantially larger and slower to inject.

---

## Project Architecture

```
src/
├── Commands/        Dot-commands (.bind, .config, .friend, …)
├── Config/          JSON configuration, presets, global settings
├── Features/        ECS, friends, bot filter, smoothing, raycasting, world chunks
├── Framework/       Hooking, memory, logging, math, sound, compat layer
├── GUI/             Menu, widgets, theme
├── Input/           Input system, wheel/mouse hooks, keybinds
├── Modules/         Feature modules (Combat / Movement / Visuals / Misc / Interface)
├── Networking/      IRC bridge, Spotify service
├── Render/          DX11 Present hook, BlurHelper, EffectSprites, ResourceSprites, D2D
├── SDK/             Game SDK: offsets, vtables, classes, code patches, effects hook
└── Scripting/       Luau runtime and native bindings
```

### Key design rules

| Rule | Explanation |
|------|-------------|
| **Modules never touch raw offsets** | Go through the ECS (`Features/ECS/`) or the `SDK/Game` facade. |
| **Lifecycle hooks are guarded** | `ModuleManager` wraps every dispatch in SEH + `try/catch`. A faulty module is disabled, never crashes the process. |
| **Thread safety** | Commands are queued from the network thread and drained in `onTick` (Present thread). Never call `ModuleManager::add/remove` from a non-Present thread. |
| **Compiler portability** | MSVC-specific SEH is remapped via `Framework/Compat.hpp`. All three toolchains must compile cleanly. |

---

## Code Style

The project uses **clang-format** (`.clang-format`) and **clang-tidy** (`.clang-tidy`). Run them before committing:

```bash
# Format
clang-format -i src/**/*.cpp src/**/*.hpp

# Lint
clang-tidy src/YourFile.cpp -- -std=c++20 -Isrc -Iresources
```

### Quick reference

| Setting | Value |
|---------|-------|
| **Base style** | LLVM |
| **Standard** | C++20 |
| **Indent** | 4 spaces (no tabs) |
| **Column limit** | 100 |
| **Braces** | Attach (same-line opening brace) |
| **Pointer alignment** | Right (`Type* ptr`) |
| **Include order** | Case-sensitive, preserve blocks |
| **Trailing comments** | 2 spaces before `//` |

### Naming conventions

| Element | Style | Example |
|---------|-------|---------|
| Classes / structs | `PascalCase` | `BlockESP`, `LocalPlayer` |
| Functions / methods | `camelCase` | `onTick()`, `drawSettings()` |
| Member variables | `camelCase_` (trailing underscore) | `range_`, `enabled_` |
| Constants | `kPascalCase` | `kRegKey` |
| Namespaces | `lowercase` | `mc`, `mc::dx11`, `mc::scripts` |
| Macros | `UPPER_SNAKE_CASE` | `MC_REGISTER_MODULE`, `MC_LOG` |

---

## Creating a Module

Every module lives in its own folder under `src/Modules/impl/<Category>/`. Here's how to add one:

### 1. Create the folder and files

```
src/Modules/impl/Movement/MyModule/
├── MyModule.hpp
└── MyModule.cpp
```

### 2. Declare the class (`.hpp`)

```cpp
#pragma once

#include "Modules/Module.hpp"

namespace mc {

class MyModule : public Module
{
public:
    MyModule();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    float speed_ = 1.0f;
};

}
```

### 3. Implement and register (`.cpp`)

```cpp
#include "MyModule.hpp"

#include "SDK/Game.hpp"

#include <imgui.h>

namespace mc {

MyModule::MyModule()
    : Module("MyModule", "Short description of what it does", Category::Movement, 0)
{
    markHasSettings();  // Call this if you have drawSettings()
}

void MyModule::onEnable() {
    // Called when the module is toggled on.
}

void MyModule::onDisable() {
    // Called when the module is toggled off. Clean up patches here.
}

void MyModule::onTick() {
    // Called every game tick while the module is enabled.
    // Access game state through the ECS or SDK facade.
}

void MyModule::drawSettings() {
    // ImGui widgets for the module's settings panel.
    ImGui::SliderFloat("Speed", &speed_, 0.1f, 10.0f);
}

nlohmann::json MyModule::saveSettings() const {
    return nlohmann::json{{"speed", speed_}};
}

void MyModule::loadSettings(const nlohmann::json& j) {
    if (j.contains("speed")) speed_ = j["speed"].get<float>();
}

// This macro registers the module at static-init time.
MC_REGISTER_MODULE(MyModule);

}
```

### 4. Add to CMakeLists.txt

Add your `.cpp` and `.hpp` to the appropriate source list in `CMakeLists.txt`.

### Available lifecycle hooks

| Hook | When it's called |
|------|------------------|
| `onEnable()` | Module toggled on |
| `onDisable()` | Module toggled off |
| `onTick()` | Every game tick (while enabled) |
| `onFrame(float dt)` | Every render frame with delta time (while enabled) |
| `onRender()` | ImGui render pass (while enabled) |
| `drawSettings()` | When the settings panel is drawn in the menu |
| `onShutdown()` | DLL unloading — free all resources |
| `onHurt(...)` | When the local player is hit |
| `onGetFriction(float&)` | When ground friction is queried |
| `onMotionPacket(...)` | When a motion packet is about to be sent |
| `onCommand(const std::string&)` | When any dot-command is executed (for cross-module reactivity) |
| `saveSettings() / loadSettings()` | JSON serialization for config persistence |

> **Important:** Always implement `onShutdown()` if your module allocates code caves (`CodePatch`), hooks, or COM resources. The graveyard/sweep pattern keeps the pointer alive until the current tick ends.

---

## Creating a Command

Commands live under `src/Commands/impl/` as header-only classes.

### Template

```cpp
#pragma once

#include "Commands/Command.hpp"

#include "Framework/Log.hpp"
#include "SDK/ChatHook.hpp"

namespace mc {

class MyCommand : public Command
{
public:
    MyCommand()
        : Command("mycommand", ".mycommand <arg>", "Does something useful")
    {
        addAlias("mc");  // Optional short alias
    }

    void execute(const std::vector<std::string>& args) override
    {
        if (args.empty())
        {
            chathook::display("§cUsage: " + usage());
            return;
        }
        // Your logic here.
        chathook::display("§aDone!");
    }
};

}
```

Then register it in `CommandManager.cpp` alongside the existing commands.

---

## Scripting API Changes

If you add or modify functions exposed to Luau scripts (`src/Scripting/Scripting.cpp`):

1. **Keep the sandbox tight.** Scripts must not be able to crash the process or access arbitrary memory.
2. **Document the function** with a comment above the C binding (`l_game_xxx`, `l_draw_xxx`, etc.).
3. **Register it** in `installBindings()` under the appropriate namespace (`game`, `draw`, `ui`, `input`, `friends`, `chat`, `modules`).
4. **Update the README** — the Scripting section lists exposed namespaces.

---

## Portability Checklist

Every pull request must compile cleanly on all three toolchains. Review these before submitting:

- [ ] **No MSVC-only extensions** outside `#ifdef _MSC_VER` guards.
- [ ] **No `__try`/`__except`** in the same function as `try`/`catch`. Use the SEH-in-separate-function pattern (see `ModuleManager.cpp`).
- [ ] **C++/WinRT usage** is wrapped in `#ifdef _MSC_VER`.
- [ ] **No `/MP` flag assumptions** — Clang-CL rejects `/MP`.
- [ ] **Static linking** — GCC builds use `-static -static-libgcc`. Don't introduce dynamic `libstdc++` dependencies.
- [ ] **`Compat.hpp`** is force-included project-wide; do not manually include it.

---

## Testing & CI

### Local testing

1. Build with `Release` configuration.
2. Inject the DLL into Minecraft Bedrock (Windows 10/11).
3. Press **INSERT** to open the menu.
4. Verify your module/feature works and that existing modules are not broken.

### Continuous integration

CI runs on every push and pull request via [`.github/workflows/cmake-on-windows.yml`](.github/workflows/cmake-on-windows.yml). It builds all three toolchains (MSVC, Clang-CL, MinGW GCC) on `windows-latest`.

**Your PR will not be merged if CI fails.** Fix any compilation errors or warnings across all toolchains before requesting review.

---

## Commit Messages

We follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <short summary>
```

### Types

| Type | When to use |
|------|-------------|
| `feat` | New feature or module |
| `fix` | Bug fix |
| `refactor` | Code change that neither fixes a bug nor adds a feature |
| `perf` | Performance improvement |
| `docs` | Documentation only |
| `style` | Formatting, whitespace (no logic change) |
| `chore` | Build, CI, tooling, deps |
| `test` | Adding or updating tests |

### Examples

```
feat(modules): add Scaffold module with Stairs/Full/Down modes
fix(ecs): prevent crash when player list is empty
refactor(render): extract blur pass into BlurHelper
docs(readme): update module count and config path
chore(ci): add MinGW-w64 to the build matrix
```

---

## Pull Request Process

1. **One feature per PR.** Keep changes focused and reviewable.
2. **Describe what and why** in the PR description. Link related issues with `Closes #123`.
3. **Ensure CI passes** on all three toolchains.
4. **Run clang-format** on all changed files.
5. **Update documentation** — if you add a module, command, or scripting API function, update `README.md` accordingly.
6. **Request review** from at least one maintainer.
7. **Squash or rebase** your commits before merge if requested.

### PR checklist

```markdown
- [ ] Code compiles on MSVC, Clang-CL, and MinGW GCC
- [ ] clang-format applied to changed files
- [ ] New module registered with `MC_REGISTER_MODULE`
- [ ] `saveSettings()` / `loadSettings()` implemented (if module has settings)
- [ ] `onShutdown()` frees all resources (patches, hooks, COM)
- [ ] README.md updated (if user-visible change)
- [ ] No new compiler warnings introduced
```

---

## License

By contributing, you agree that your contributions will be licensed under the same **ADPSL 1.1** license that covers the project. See the [LICENSE](LICENSE) file for full terms.

---

<div align="center">

Thank you for helping make Azyre better! 🚀

</div>
