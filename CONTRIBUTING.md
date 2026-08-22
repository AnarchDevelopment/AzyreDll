# Contributing to Azyre Client

[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen?style=flat-square)](https://github.com/AnarchDevelopment/AzyreDll/pulls)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/20)
[![Discord](https://img.shields.io/badge/Discord-nqtvyzer-5865F2?style=flat-square&logo=discord&logoColor=white)](https://discord.com/)

Thank you for your interest in contributing to **Azyre Client**!  
This document outlines the guidelines for contributing code, reporting bugs, and requesting features.

---

## 📋 Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Reporting Issues](#reporting-issues)
- [Feature Requests](#feature-requests)
- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Code Guidelines](#code-guidelines)
- [Pull Request Process](#pull-request-process)
- [Contact](#contact)

---

## 🤝 Code of Conduct

Be respectful and constructive. This project follows a simple rule: treat others how you want to be treated.

---

## 🐛 Reporting Issues

Before opening a new issue:

1. Search [existing issues](../../issues) to avoid duplicates.
2. Collect relevant information:
   - OS version (Windows 10 / 11)
   - Visual Studio / CMake version
   - Build configuration (`Debug`/`Release`, `x64`)
   - Steps to reproduce
   - Crash logs or screenshots

Then open a [new issue](../../issues/new) with a clear, descriptive title.

---

## 💡 Feature Requests

Feature requests are welcome! Please:

1. Check [existing issues](../../issues) and [PRs](../../pulls) first.
2. Open an issue labeled `enhancement`.
3. Describe the feature, its motivation, and how it fits Azyre's module architecture.

---

## 🚀 Getting Started

### 1. Fork & Clone

```bash
git clone https://github.com/<your-username>/AzyreDll.git
cd AzyreDll
```

### 2. Set Up the Remote

```bash
git remote add upstream https://github.com/AnarchDevelopment/AzyreDll.git
```

### 3. Build the Project

**With CMake:**
```bash
cmake -B build -A x64
cmake --build build --config Release
```

**With Visual Studio:**
- Open `build/Azyre.slnx`
- Select `Release | x64`
- Build → Build Solution

---

## 🔄 Development Workflow

### Branch Naming

Use descriptive branch names:

```
<issue-number>-<short-description>

# Examples:
42-add-esp-module
17-fix-motion-blur-crash
```

### Create Your Branch

```bash
git checkout -b 42-add-esp-module
```

### Keep Up to Date

```bash
git fetch upstream
git rebase upstream/main
```

---

## 📐 Code Guidelines

### Architecture

- Each module lives in its own directory under `Modules/<Category>/<ModuleName>/`
- A module consists of a `.hpp` header and a `.cpp` implementation
- Register the module in `Modules/ModuleManager.cpp`

```
Modules/
└── Visuals/
    └── MyNewModule/
        ├── MyNewModule.hpp
        └── MyNewModule.cpp
```

### Style

| Rule | Detail |
|---|---|
| Standard | C++20 |
| Naming | `PascalCase` for classes, `g_camelCase` for globals |
| Includes | Relative paths preferred |
| Comments | English only |
| Line endings | LF (Unix) or CRLF (Windows) — consistent per file |

### Do's and Don'ts

✅ **Do:**
- Keep modules self-contained
- Use `ImGui` drawing calls only inside render callbacks
- Guard platform-specific code with `#ifdef _WIN32`
- Remove unused variables and includes

❌ **Don't:**
- Break existing module interfaces without discussion
- Add external dependencies without prior approval
- Commit generated files (`build/`, `.obj`, `.pdb`, etc.)
- Include secrets, tokens, or credentials

---

## 📬 Pull Request Process

1. Ensure your branch is rebased on `upstream/main`.
2. Verify the project builds without errors:
   ```bash
   cmake --build build --config Release
   ```
3. Open a Pull Request with:
   - **Clear title**: `[Module] Short description`
   - **Description**: What the PR does and why
   - **Related issue**: `Closes #<issue-number>` if applicable
4. Respond to reviewer feedback promptly.

PRs will be reviewed by maintainers. We may request changes before merging.

---

## 📞 Contact

| Platform | Handle |
|---|---|
| GitHub | [@iVyz3r](https://github.com/iVyz3r) |
| Discord | `nqtvyzer` |
| Organization | [an4rch Development](https://anarchdevelopment.github.io/) |
