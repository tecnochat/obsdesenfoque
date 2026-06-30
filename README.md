# OBS AI Background Blur Plugin

Plugin nativo para **OBS Studio** que agrega un filtro **AI Background Blur** capaz de difuminar el fondo de cualquier cámara web en tiempo real mediante segmentación por Inteligencia Artificial.

Calidad comparable a Zoom, Microsoft Teams, Google Meet o NVIDIA Broadcast, con bajo consumo de recursos y alta velocidad.

---

## ✨ Características

- ✅ **Difuminado de fondo IA** en tiempo real
- ✅ **3 modelos compatibles**: MediaPipe, MODNet, Robust Video Matting
- ✅ **Blur GPU** mediante shaders (sin carga en CPU)
- ✅ **Múltiples calidad**: Low / Medium / High / Ultra
- ✅ **Aceleración**: DirectML, CUDA, Auto-detect
- ✅ **Bordes refinados** con feathering ajustable
- ✅ **Visualización de máscara** para debug
- ✅ **Totalmente integrado** en OBS como filtro nativo
- ✅ **Baja latencia**: < 10 ms inferencia en 1080p

---

## 📋 Requisitos

| Componente | Versión Mínima |
|---|---|
| **OBS Studio** | ≥ 30.0 |
| **Windows** | 10 u 11 (64-bit) |
| **GPU** | DirectX 11 compatible |
| **ONNX Runtime** | ≥ 1.15 |
| **RAM** | 4 GB (8 GB recomendada) |

---

## 🚀 Instalación

### Descarga pre-compilada

1. Descarga la última versión desde [Releases](https://github.com/your-org/obs-ai-background-blur/releases)
2. Copia `obs-ai-background-blur.dll` en `C:\Program Files\obs-studio\obs-plugins\64bit\`
3. Copia la carpeta `models/` con los modelos ONNX en la misma carpeta
4. Copia la carpeta `shaders/` con los archivos `.effect`
5. Inicia OBS Studio y agrega el filtro "AI Background Blur" a tu cámara

### Compilación desde código

Ver [BUILDING.md](BUILDING.md) para instrucciones detalladas de compilación.

---

## 🎮 Uso

1. Agrega una fuente **Video Capture Device** (tu cámara web)
2. Haz clic derecho → **Filters**
3. Agrega → **AI Background Blur**
4. Ajusta los parámetros a tu gusto

### Controles

| Control | Rango | Descripción |
|---|---|---|
| **Blur Strength** | 0-100 | Intensidad del difuminado |
| **Edge Softness** | 0-100 | Suavizado de bordes persona/fondo |
| **Mask Threshold** | 0-100 | Sensibilidad de la máscara |
| **Blur Radius** | 1-50 | Radio del kernel de blur |
| **Quality** | Low/Med/High/Ultra | Resolución interna del modelo IA |
| **AI Model** | MediaPipe/MODNet/RVM | Modelo de segmentación |
| **Processing Device** | CPU/DirectML/CUDA/Auto | Aceleración |
| **Show Mask** | On/Off | Debug: visualiza la máscara |
| **HQ Edges** | On/Off | Bordes de alta calidad |
| **GPU Blur** | On/Off | Usa shaders GPU para blur |

---

## 📁 Estructura del proyecto

```
obs-ai-background-blur/
├── CMakeLists.txt               # Sistema de compilación
├── plugin-main.cpp/.h           # Punto de entrada OBS
├── background_filter.cpp/.h     # Filtro OBS
├── segmentation_engine.cpp/.h   # Motor de inferencia IA
├── blur_renderer.cpp/.h         # Pipeline de blur GPU
├── gpu_shader.cpp/.h            # Gestión de shaders GPU
├── settings.cpp/.h              # Configuración del filtro
├── utils.cpp/.h                 # Utilidades generales
├── shaders/
│   ├── gaussian.effect          # Blur Gaussiano (H+V)
│   ├── mask.effect              # Refinamiento de máscara
│   └── composite.effect         # Composición final
├── models/                      # Modelos ONNX (descargar)
├── resources/
│   ├── icons/
│   └── locale/
├── BUILDING.md                  # Instrucciones de compilación
├── README.md                    # Este archivo
└── LICENSE                      # Licencia MIT
```

---

## ⚙️ Arquitectura

```
Video Capture Device
       ↓
   Conversión RGB
       ↓
  Inferencia ONNX Runtime (hilo separado)
       ↓
    Máscara Binaria
       ↓
  Refinamiento de Bordes (shader)
       ↓
  Gaussian Blur GPU (2 pases H+V)
       ↓
   Composición Final (shader)
       ↓
     Salida OBS
```

---

## 📊 Rendimiento Esperado

| Resolución | Inferencia | Blur GPU | Total |
|---|---|---|---|
| **720p @ 60 FPS** | < 6 ms | < 2 ms | < 8 ms |
| **1080p @ 60 FPS** | < 10 ms | < 3 ms | < 13 ms |

---

## 🧩 Dependencias

- [OBS Studio](https://obsproject.com/) ≥ 30.0
- [ONNX Runtime](https://github.com/microsoft/onnxruntime) ≥ 1.15
- Windows SDK (DirectX 11)
- CMake ≥ 3.20
- Compilador C++20 (MSVC 2022+)

---

## 📄 Licencia

MIT License — ver [LICENSE](LICENSE).

---

## 🤝 Contribuciones

Las contribuciones son bienvenidas. Por favor abre un issue o pull request en GitHub.

---

## 📣 Créditos

- [MediaPipe](https://github.com/google/mediapipe) — Selfie Segmentation model
- [MODNet](https://github.com/ZHKKKe/MODNet) — Modelo de segmentación
- [Robust Video Matting](https://github.com/PeterL1n/RobustVideoMatting) — RVM model
