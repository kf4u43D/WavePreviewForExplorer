# Installation des dépendances

## 1. Outils nécessaires

Installer :

- Visual Studio 2022 Community ou supérieur ;
- workload `Desktop development with C++` ;
- Windows 10/11 SDK ;
- Git ;
- CMake ;
- PowerShell 7 conseillé.

## 2. vcpkg

Méthode recommandée :

```powershell
git clone https://github.com/microsoft/vcpkg C:/dev/vcpkg
C:/dev/vcpkg/bootstrap-vcpkg.bat
```

Le projet utilise le mode manifest via `vcpkg.json`.

Configuration :

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake
```

Build :

```powershell
cmake --build build --config Debug
```

## 3. Dépendances C++

- `sqlite3` : cache ;
- `libsndfile` : analyse WAV/AIFF/FLAC ;
- `miniaudio` : audio léger ;
- `gtest` : tests.

## 4. WiX Toolset

Optionnel pour la première phase. Utile pour générer un MSI propre.

Installation possible :

```powershell
dotnet tool install --global wix
```

Puis :

```powershell
wix --version
```

## 5. Direct2D et Media Foundation

Ces API viennent du Windows SDK. Aucun téléchargement séparé n’est nécessaire si Visual Studio et le SDK Windows sont correctement installés.
