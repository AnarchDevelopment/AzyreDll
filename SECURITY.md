# Security Policy

[![Security](https://img.shields.io/badge/Security-Responsible%20Disclosure-critical?style=flat-square&logo=shield&logoColor=white)](https://github.com/AnarchDevelopment/aegledll/security)
[![Windows](https://img.shields.io/badge/Platform-Windows%2010%2F11-0078D6?style=flat-square&logo=windows)](https://microsoft.com/windows)

This document describes the security policy for **Azyre Client** and how to responsibly report vulnerabilities.

---

## 📌 Supported Versions

Security patches are only provided for the **latest** version of Azyre.

| Version | Status |
|---|---|
| `latest` (main branch) | ✅ Supported |
| Older releases | ❌ Not supported |

> Always use the most recent commit from `main` to ensure you have the latest fixes.

---

## 🔐 Reporting a Vulnerability

> [!CAUTION]
> **Do NOT report security vulnerabilities through public GitHub Issues.**  
> Public disclosure before a fix is available may put other users at risk.

### How to Report

Please report security issues **privately** through one of the following channels:

| Channel | Contact |
|---|---|
| GitHub | [@iVyz3r](https://github.com/iVyz3r) — send a private message or use [GitHub Security Advisories](../../security/advisories/new) |
| Discord | `nqtvyzer` |

### What to Include

A good security report contains:

- **Description** — What is the vulnerability?
- **Affected component** — Which module, system, or file is affected?
- **Steps to reproduce** — Minimal steps to trigger the issue
- **Impact** — What could an attacker do with this?
- **Suggested fix** *(optional)* — Any ideas on how to resolve it?
- **Logs / screenshots** *(if applicable)*

---

## ⏱️ Response Timeline

| Stage | Target |
|---|---|
| Acknowledgement | Within 72 hours |
| Initial assessment | Within 1 week |
| Fix / patch | Depends on severity |
| Public disclosure | After fix is released |

We follow a **responsible disclosure** model. Credit will be given to reporters upon request.

---

## 🎯 Scope

Security reports are relevant for issues in:

| Category | Examples |
|---|---|
| Memory safety | Buffer overflows, use-after-free, null dereferences |
| Hooking integrity | Incorrect hook installation / teardown |
| Network security | IRC client injection, unvalidated input |
| Config handling | JSON parsing crashes, path traversal |
| Resource loading | Malformed image/audio file crashes |
| Build system | Malicious CMakeLists / dependency confusion |

### Out of Scope

- Issues in **third-party vendored libraries** (Dear ImGui, MinHook, nlohmann/json, etc.) — please report those upstream
- Game-side anti-cheat detection (by design)
- Feature requests disguised as security reports

---

## 🙏 Recognition

Responsible reporters who help improve Azyre's security will be acknowledged in release notes (with their permission).

---

## 📞 Contact

| Platform | Handle |
|---|---|
| GitHub | [@iVyz3r](https://github.com/iVyz3r) |
| Discord | `nqtvyzer` |
| Organization | [an4rch Development](https://anarchdevelopment.github.io/) |
