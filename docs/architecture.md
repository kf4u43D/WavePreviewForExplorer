# Architecture technique

## 1. Vue générale

WavePreview for Explorer est organisé en composants découplés :

```text
Explorer / Preview Host
        |
        v
ShellExtension.dll
        |
        +--> PreviewHandler
        +--> ThumbnailProvider
        +--> PropertyHandler optionnel
        |
        v
AudioEngine
        |
        +--> Decoders
        +--> Metadata
        +--> Waveform
        +--> Playback
        |
        v
Cache SQLite + WaveformStore
```

## 2. ShellExtension

Composant COM natif C++.

Responsabilités :

- implémenter les interfaces COM ;
- recevoir un fichier ou stream depuis Windows ;
- créer une fenêtre enfant pour le Preview Handler ;
- demander les métadonnées au moteur audio ;
- demander un rendu waveform ;
- ne jamais effectuer de calcul lourd en synchrone dans l’UI Explorer.

## 3. AudioEngine

Bibliothèque C++ interne.

Responsabilités :

- lire les métadonnées ;
- décoder un échantillonnage simplifié ;
- générer des points min/max pour la waveform ;
- fournir une lecture audio simple ;
- gérer les erreurs.

Le moteur doit être testable sans Explorer avec `tools/waveform-test-cli`.

## 4. Cache

Deux niveaux :

- SQLite pour l’index ;
- fichiers binaires pour les données waveform.

Clé de cache recommandée :

```text
canonical_path + file_size + last_write_time + partial_hash
```

Le cache ne doit pas empêcher le fonctionnement. En cas d’erreur, on recalcule ou on affiche une preview minimale.

## 5. Rendu UI

- Direct2D pour la waveform ;
- Win32 pour la fenêtre du Preview Handler ;
- WinUI 3 ou WPF plus tard pour Settings App ;
- pas de framework lourd dans la DLL shell.

## 6. Threading

Règles :

- UI thread uniquement pour affichage et interactions ;
- worker thread pour lecture, analyse, cache ;
- annulation immédiate lors du changement de fichier ;
- timeouts courts ;
- jamais de join bloquant depuis un callback Explorer.

## 7. Robustesse

Tous les fichiers doivent être considérés comme potentiellement invalides :

- fichiers tronqués ;
- headers incohérents ;
- chemins réseau lents ;
- disques débranchés ;
- fichiers verrouillés ;
- fichiers énormes.

Chaque API publique interne doit retourner un `Result<T>` ou un statut explicite, pas lever des exceptions non maîtrisées au-dessus de la frontière shell.
