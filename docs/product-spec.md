# Spécification produit - AudioPreview for Explorer

## 1. Promesse

**Voir, écouter et identifier ses fichiers audio directement dans l’Explorateur Windows, sans ouvrir de logiciel externe.**

AudioPreview for Explorer s’adresse aux utilisateurs qui manipulent des banques de sons, samples, field recordings, stems, exports audio, stimuli de test ou fichiers techniques. Le produit doit donner à l’Explorateur Windows une capacité de préécoute audio professionnelle, lisible et rapide.

## 2. Produit

Le produit comprend trois intégrations Windows :

1. **Preview Handler** : panneau de préécoute dans le volet de prévisualisation.
2. **Thumbnail Provider** : miniatures waveform dans les vues de dossiers.
3. **Property Handler** : métadonnées audio exposées à Windows, optionnel et prudent.

Une application de configuration complète l’ensemble.

## 3. MVP

Le MVP doit uniquement prouver que le Preview Handler est fiable.

Fonctions MVP :

- ouverture d’un fichier WAV PCM ;
- extraction durée, sample rate, bit depth, canaux ;
- affichage d’une waveform simplifiée ;
- bouton lecture/pause ;
- barre de progression ;
- arrêt immédiat lors du changement de fichier ;
- gestion des fichiers invalides ;
- rendu Direct2D simple ;
- aucun autoplay par défaut.

## 4. Version 1.0

- support WAV, AIFF, FLAC, MP3, OGG ;
- cache SQLite ;
- miniatures waveform ;
- réglages de base ;
- installateur propre ;
- désinstallation propre ;
- logs de diagnostic ;
- tests sur dossiers lourds.

## 5. Version avancée

- BPM estimé ;
- tonalité estimée ;
- RMS, peak, LUFS approximatif ;
- détection one-shot / loop ;
- tags utilisateur ;
- menu contextuel ;
- export des infos audio.

## 6. Non-objectifs

Le produit ne doit pas devenir :

- un DAW ;
- un éditeur audio ;
- un gestionnaire complet de sample library ;
- un outil cloud ;
- un indexeur agressif ;
- un lecteur audio généraliste.

## 7. Critères de réussite

- Explorer reste stable.
- La prévisualisation est plus rapide que l’ouverture d’un lecteur externe.
- La waveform est lisible.
- Les miniatures ont une vraie utilité.
- L’utilisateur peut parcourir un dossier de samples au clavier.
- L’installation et la désinstallation ne laissent pas de pollution système.
