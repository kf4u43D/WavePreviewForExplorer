# WavePreview for Explorer

Extension native Windows pour préécouter et visualiser les fichiers audio directement dans l’Explorateur Windows.

## Objectif

Le projet vise à fournir :

- un **Preview Handler** pour le volet de prévisualisation de l’Explorateur ;
- un **Thumbnail Provider** pour afficher des miniatures waveform ;
- un **Property Handler** optionnel pour exposer durée, sample rate, bit depth, canaux, codec, BPM, etc. ;
- une application de réglages ;
- un cache local rapide et robuste.

La priorité absolue est la stabilité de l’Explorateur Windows. Les traitements lourds doivent être isolés du composant shell.

## État du workspace

Ce dépôt est un **starter kit de développement**. Il contient :

- documentation produit complète ;
- architecture technique ;
- superprompt pour Codex sous VS Code ;
- squelette C++/CMake ;
- manifest vcpkg ;
- scripts PowerShell d’installation des dépendances ;
- placeholders pour Preview Handler, Thumbnail Provider, Property Handler, cache et moteur audio ;
- base de tests.

Le code fourni est volontairement minimal : il sert de base propre pour lancer le développement, pas de shell extension prête à enregistrer en production.

## Prérequis Windows

- Windows 10 ou Windows 11 64-bit
- Visual Studio 2022 avec workload **Desktop development with C++**
- Git
- CMake récent
- PowerShell 7 recommandé
- vcpkg, installé via `scripts/bootstrap-dev.ps1`
- WiX Toolset, optionnel pour l’installateur

## Démarrage rapide

Depuis PowerShell :

```powershell
cd WavePreviewForExplorer
./scripts/bootstrap-dev.ps1
./scripts/configure.ps1
./scripts/build.ps1
```

Ou manuellement :

```powershell
git clone https://github.com/microsoft/vcpkg C:/dev/vcpkg
C:/dev/vcpkg/bootstrap-vcpkg.bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug
```

## Dépendances prévues

Déclarées dans `vcpkg.json` :

- `sqlite3` : index du cache ;
- `libsndfile` : lecture/analyse WAV, AIFF, FLAC ;
- `miniaudio` : couche audio légère, lecture et décodage possible ;
- `gtest` : tests unitaires.

Media Foundation et Direct2D sont fournis par le SDK Windows.

## Principes de développement

1. Ne jamais bloquer l’Explorateur.
2. Ne jamais analyser récursivement un disque sans action explicite.
3. Préférer les timeouts courts et les résultats approximatifs rapides.
4. Ne pas faire de UI lourde dans le Preview Handler.
5. Prévoir une désinstallation propre dès le début.
6. Tout composant shell doit être conçu comme potentiellement hostile aux fichiers corrompus.

## Documentation

Voir :

- `docs/product-spec.md`
- `docs/architecture.md`
- `docs/shell-integration.md`
- `docs/dependency-setup.md`
- `docs/cache-format.md`
- `docs/ui-guidelines.md`
- `docs/testing-plan.md`
- `docs/SUPERPROMPT_CODEX.md`
