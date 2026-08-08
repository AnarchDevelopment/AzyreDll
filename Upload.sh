#!/usr/bin/env bash

set -e

REMOTE="https://github.com/AnarchDevelopment/aegledll.git"
BRANCH="main"

echo
echo "=============================================="
echo "   REEMPLAZO COMPLETO DE origin/main"
echo "=============================================="
echo
echo "Repositorio: $REMOTE"
echo "Rama:        $BRANCH"
echo

# --------------------------------------------------
# Verificar que estamos en la carpeta correcta
# --------------------------------------------------

if [ ! -d ".git" ]; then
    echo "[INFO] No existe .git/. Se inicializará Git."
else
    echo "[INFO] Eliminando .git/..."
    rm -rf .git
fi

# --------------------------------------------------
# Inicializar repositorio
# --------------------------------------------------

echo "[INFO] Inicializando Git..."
git init

# Evitar conversiones LF/CRLF
git config core.autocrlf false
git config core.safecrlf false

# --------------------------------------------------
# Configurar remote
# --------------------------------------------------

echo "[INFO] Añadiendo origin..."
git remote add origin "$REMOTE"

# --------------------------------------------------
# Obtener main del remoto
# --------------------------------------------------

echo "[INFO] Descargando origin/main..."
git fetch origin main

# --------------------------------------------------
# Crear directorio temporal compatible con Git Bash
# --------------------------------------------------

TEMP_DIR="$(mktemp -d)"

cleanup() {
    rm -rf "$TEMP_DIR"
}

trap cleanup EXIT

# --------------------------------------------------
# Guardar .github/ del remoto
# --------------------------------------------------

echo "[INFO] Conservando .github/ de origin/main..."

if git cat-file -e "origin/main:.github" 2>/dev/null; then

    mkdir -p "$TEMP_DIR"

    git archive "origin/main" ".github" | tar -x -C "$TEMP_DIR"

    echo "[OK] .github/ guardado."

else
    echo "[WARN] origin/main no contiene .github/"
fi

# --------------------------------------------------
# Crear nueva rama main sin historial
# --------------------------------------------------

echo "[INFO] Creando una nueva main sin historial..."

git checkout --orphan "$BRANCH"

# --------------------------------------------------
# Eliminar archivos rastreados del índice
# --------------------------------------------------

echo "[INFO] Limpiando índice de Git..."

git rm -r --cached . 2>/dev/null || true

# --------------------------------------------------
# Eliminar .github local
# para reemplazarlo por el remoto
# --------------------------------------------------

if [ -d ".github" ]; then
    echo "[INFO] Eliminando .github/ local..."
    rm -rf ".github"
fi

# --------------------------------------------------
# Restaurar .github/ del remoto
# --------------------------------------------------

if [ -d "$TEMP_DIR/.github" ]; then
    echo "[INFO] Restaurando .github/ del remoto..."
    cp -a "$TEMP_DIR/.github" "./.github"
fi

# --------------------------------------------------
# Función para commit por componente
# --------------------------------------------------

commit_component() {
    local paths="$1"
    local message="$2"

    local has_files=false
    for p in $paths; do
        if [ -e "$p" ]; then
            has_files=true
            break
        fi
    done

    if [ "$has_files" = false ]; then
        echo "[SKIP] $message (no existe)"
        return
    fi

    git add $paths 2>/dev/null || true

    # Solo hacer commit si hay cambios staged
    if ! git diff --cached --quiet 2>/dev/null; then
        git commit -m "$message"
        echo "[OK] $message"
    else
        echo "[SKIP] $message (sin cambios)"
    fi
}

# --------------------------------------------------
# Commits por componente
# --------------------------------------------------

echo
echo "=============================================="
echo " Creando commits por componente"
echo "=============================================="
echo

# --- Archivos raíz del proyecto ---
commit_component \
    ".gitignore .gitattributes LICENSE README.md CONTRIBUTING.md SECURITY.md Upload.sh" \
    "chore: add project root files (license, readme, gitignore)"

# --- Archivos de solución y proyecto MSVC ---
commit_component \
    "AegleDllMSVC.slnx AegleDllMSVC.vcxproj" \
    "build: add Visual Studio solution and project files"

# --- Archivos de proyecto internos (filters, user) ---
commit_component \
    "AegleDllMSVC/AegleDllMSVC.vcxproj AegleDllMSVC/AegleDllMSVC.filters AegleDllMSVC/AegleDllMSVC.user AegleDllMSVC/AegleDllMSVC.vcxproj.user" \
    "build: add inner MSVC project and filter files"

# --- .github (conservado del remoto) ---
commit_component \
    ".github" \
    "ci: preserve .github workflows from remote"

# --- DLL entry point ---
commit_component \
    "AegleDllMSVC/dllmain.cpp" \
    "feat: add DLL entry point (dllmain)"

# --- Third-party: ImGui ---
commit_component \
    "AegleDllMSVC/ImGui" \
    "vendor: add ImGui library and backends (DX11, Win32)"

# --- Third-party: MinHook ---
commit_component \
    "AegleDllMSVC/MinHook" \
    "vendor: add MinHook hooking library"

# --- Third-party: nlohmann/json ---
commit_component \
    "AegleDllMSVC/nlohmann" \
    "vendor: add nlohmann/json header-only library"

# --- Third-party: miniaudio ---
commit_component \
    "AegleDllMSVC/miniaudio" \
    "vendor: add miniaudio audio library"

# --- Assets (imágenes, sonidos, recursos) ---
commit_component \
    "AegleDllMSVC/Assets" \
    "assets: add images, sounds, resources, and stb headers"

# --- ArrayList ---
commit_component \
    "AegleDllMSVC/ArrayList" \
    "feat: add ArrayList component"

# --- Animations ---
commit_component \
    "AegleDllMSVC/Animations" \
    "feat: add animation system"

# --- Core (Present hook) ---
commit_component \
    "AegleDllMSVC/Core" \
    "feat: add core Present hook system"

# --- Hook ---
commit_component \
    "AegleDllMSVC/Hook" \
    "feat: add hooking module"

# --- Input ---
commit_component \
    "AegleDllMSVC/Input" \
    "feat: add input handling system"

# --- Config ---
commit_component \
    "AegleDllMSVC/Config" \
    "feat: add configuration manager"

# --- Utils ---
commit_component \
    "AegleDllMSVC/Utils" \
    "feat: add utility helpers (HudElement)"

# --- Module system (globals, manager, header) ---
commit_component \
    "AegleDllMSVC/Modules/Globals.cpp AegleDllMSVC/Modules/Globals.hpp AegleDllMSVC/Modules/ModuleManager.cpp AegleDllMSVC/Modules/ModuleManager.hpp AegleDllMSVC/Modules/ModuleHeader.hpp" \
    "feat: add module system (globals, manager, header)"

# --- PatternScan ---
commit_component \
    "AegleDllMSVC/Modules/PatternScan" \
    "feat: add pattern scanning module"

# --- Alloc ---
commit_component \
    "AegleDllMSVC/Modules/Alloc" \
    "feat: add memory allocation module (AllocateNear)"

# --- Info ---
commit_component \
    "AegleDllMSVC/Modules/Info" \
    "feat: add info module"

# --- Terminal ---
commit_component \
    "AegleDllMSVC/Modules/Terminal" \
    "feat: add in-game terminal module"

# --- Combat modules ---
commit_component \
    "AegleDllMSVC/Modules/Combat" \
    "feat: add combat modules (Hitbox, Reach)"

# --- Movement modules ---
commit_component \
    "AegleDllMSVC/Modules/Movement" \
    "feat: add movement modules (AutoSprint, Timer)"

# --- Misc modules ---
commit_component \
    "AegleDllMSVC/Modules/Misc" \
    "feat: add misc modules (AntiAFK, AutoClicker, Screenshot, UnlockFPS)"

# --- Visual modules ---
commit_component \
    "AegleDllMSVC/Modules/Visuals" \
    "feat: add visual modules (ClickGUI, Keystrokes, FPS, CPS, Watermark, FullBright, MotionBlur, etc.)"

# --- GUI / DX11 Renderer ---
commit_component \
    "AegleDllMSVC/GUI" \
    "feat: add GUI system and DX11 ImGui renderer"

# --- Networking ---
commit_component \
    "AegleDllMSVC/Networking" \
    "feat: add networking system (IRC client and chat)"

# --------------------------------------------------
# Capturar cualquier archivo restante
# --------------------------------------------------

git add --all 2>/dev/null || true
if ! git diff --cached --quiet 2>/dev/null; then
    git commit -m "chore: add remaining project files"
    echo "[OK] Remaining files committed"
fi


# Asegurar nombre main
git branch -M main

# --------------------------------------------------
# Confirmación antes del force push
# --------------------------------------------------

echo
echo "=============================================="
echo "           ¡ATENCIÓN!"
echo "=============================================="
echo
echo "Se reemplazará COMPLETAMENTE:"
echo
echo "    origin/main"
echo
echo "El historial anterior de main será reemplazado."
echo
echo "gh-pages NO será modificado."
echo ".github/ será conservado desde origin/main."
echo

read -rp "¿Continuar? [y/N]: " CONFIRM

if [[ "$CONFIRM" != "y" && "$CONFIRM" != "Y" ]]; then
    echo
    echo "[CANCELADO]"
    exit 0
fi

# --------------------------------------------------
# Force push
# --------------------------------------------------

echo
echo "[INFO] Ejecutando force push..."
echo

git push --force -u origin main

echo
echo "=============================================="
echo "              COMPLETADO"
echo "=============================================="
echo
echo "origin/main ha sido reemplazada."
echo
echo "La rama gh-pages no fue modificada."
echo ".github/ fue conservado."
echo "El contenido restante proviene del proyecto local."
echo