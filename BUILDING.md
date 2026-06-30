# Guía de Compilación — OBS AI Background Blur Plugin

> ⚠️ **Importante**: No necesitas Visual Studio 2022 completo. Hay 3 formas de obtener el plugin compilado, **elige la que mejor se adapte a tu espacio en disco:**

---

## 🥇 Opción 1: GitHub Actions (0 GB en tu PC) — RECOMENDADA

**No necesitas instalar nada.** La compilación ocurre en la nube de GitHub.

### Pasos:

1. **Sube el código a GitHub:**
   ```powershell
   # Crea un repositorio en github.com y sube el código
   git init
   git add .
   git commit -m "Initial commit"
   git remote add origin https://github.com/tu-usuario/obs-ai-background-blur.git
   git push -u origin main
   ```

2. **Ve a la pestaña Actions** de tu repositorio en GitHub
   - Verás el workflow "Build" ejecutándose automáticamente
   - Al terminar, verás un artifact llamado `obs-ai-background-blur-windows.zip`

3. **Descarga el artifact** — contiene:
   - `obs-ai-background-blur.dll` — El plugin compilado
   - `shaders/` — Los shaders GPU
   - `models/` — El modelo ONNX (MediaPipe)
   - `resources/` — Traducciones

4. **Instala en OBS:**
   ```powershell
   # Copia todo a la carpeta de plugins de OBS
   copy obs-ai-background-blur.dll "C:\Program Files\obs-studio\obs-plugins\64bit\"
   xcopy /E /I shaders "C:\Program Files\obs-studio\obs-plugins\64bit\shaders\"
   xcopy /E /I models "C:\Program Files\obs-studio\obs-plugins\64bit\models\"
   ```

> Puedes también crear un **Release** en GitHub etiquetando con `v1.0.0` y el workflow generará un Release automático con el .dll adjunto.

---

## 🥈 Opción 2: MSVC Build Tools (~1.5 GB en disco)

Si prefieres compilar localmente pero **sin instalar Visual Studio completo**, usa solo las herramientas de compilación:

```powershell
# 1. Instalar MSVC Build Tools (solo el compilador, ~1.5 GB)
winget install Microsoft.VisualStudio.2022.BuildTools

# 2. Instalar CMake (~80 MB)
winget install Kitware.CMake

# 3. Descargar ONNX Runtime (~150 MB)
# https://github.com/microsoft/onnxruntime/releases/tag/v1.15.1
# Extraer a C:\dev\onnxruntime

# 4. Compilar (desde "Developer Command Prompt for VS 2022")
cd D:\DesenfoqueOBS\obs-ai-background-blur
cmake -B build -G "Visual Studio 17 2022" -A x64 `
    -DONNX_RUNTIME_DIR="C:\dev\onnxruntime\onnxruntime-win-x64-1.15.1"
cmake --build build --config RelWithDebInfo --parallel
```

> 📦 **Total aproximado:** 1.7 GB — significativamente menos que los 10+ GB de Visual Studio completo.

---

## 🥉 Opción 3: LLVM/clang (~300 MB)

Si prefieres algo todavía más ligero, puedes usar el compilador LLVM:

```powershell
# 1. Instalar LLVM
winget install LLVM.LLVM

# 2. Instalar CMake
winget install Kitware.CMake

# 3. Compilar con clang
cd D:\DesenfoqueOBS\obs-ai-background-blur
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

---

## 📦 Instalación en OBS Studio

Independientemente de cómo hayas compilado:

```powershell
# Copiar todo a la carpeta de plugins
copy build\RelWithDebInfo\obs-ai-background-blur.dll `
    "C:\Program Files\obs-studio\obs-plugins\64bit\"
xcopy /E /I shaders "C:\Program Files\obs-studio\obs-plugins\64bit\shaders\"
xcopy /E /I models "C:\Program Files\obs-studio\obs-plugins\64bit\models\"
xcopy /E /I resources "C:\Program Files\obs-studio\obs-plugins\64bit\resources\"
```

---

## 🧪 Verificar instalación

1. Abre **OBS Studio**
2. Agrega una fuente **Video Capture Device** (tu cámara)
3. Botón derecho sobre la fuente → **Filters**
4. Clica **+** → busca **AI Background Blur**
5. Si aparece en la lista → ¡el plugin funciona!

---

## ⚠️ Solución de problemas

| Problema | Solución |
|---|---|
| `onnxruntime.h` no encontrado | Verificar `-DONNX_RUNTIME_DIR` apunta al directorio correcto |
| `d3d11.h` no encontrado | Instalar **Windows SDK** (solo SDK, no todo VS) |
| Error de enlace con OBS | OBS SDK se instala con OBS Studio — reinstalar OBS |
| El filtro no aparece en OBS | Verificar que el .dll NO sea de 32 bits si tienes OBS 64 bits |
| Error "Modelo no encontrado" | Copiar modelos ONNX a `obs-plugins\64bit\models\` |
| Bajo rendimiento | Usar calidad "Low" o "Medium", activar DirectML/CUDA |
