#!/bin/bash

set -e

echo "[1/5] Eliminando repositorio Git actual..."
rm -rf .git

echo "[2/5] Inicializando nuevo repositorio..."
git init

echo "[3/5] Configurando autocrlf..."
git config core.autocrlf true
git config core.safecrlf false

echo "[4/5] Agregando remoto..."
git remote add origin https://github.com/AnarchDevelopment/aegledll.git

echo "[5/5] Creando commit inicial..."
git add .
git commit -m "Upload project"

echo ""
echo "Repositorio reinicializado."
echo ""
echo "Ahora ejecuta:"
echo "  git fetch origin"
echo "  git checkout -b main"
echo "  git pull origin main --allow-unrelated-histories"
echo "  git push origin main"