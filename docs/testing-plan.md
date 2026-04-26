# Plan de test

## 1. Tests unitaires

- parsing métadonnées WAV ;
- génération waveform min/max ;
- invalidation cache ;
- chemins Unicode ;
- fichiers vides ;
- fichiers tronqués.

## 2. Tests intégration

- chargement Preview Handler ;
- changement rapide de fichier ;
- fermeture Explorer pendant analyse ;
- fichier réseau lent ;
- gros fichier WAV ;
- dossier 5000 fichiers.

## 3. Tests stabilité

- navigation rapide au clavier pendant 10 minutes ;
- fichiers corrompus en série ;
- suppression d’un fichier pendant preview ;
- disque externe retiré ;
- cache verrouillé ou inaccessible.

## 4. Tests performance

Objectifs initiaux :

- métadonnées WAV court < 50 ms ;
- waveform depuis cache < 100 ms ;
- thumbnail depuis cache < 50 ms ;
- aucun freeze Explorer perceptible.
