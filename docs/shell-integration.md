# Intégration Shell Windows

## 1. Preview Handler

Le Preview Handler est le cœur du projet.

Interfaces à implémenter :

- `IPreviewHandler`
- `IInitializeWithStream` de préférence
- `IObjectWithSite`
- `IOleWindow`

Rôle : afficher une preview légère dans le volet de prévisualisation. L’UI doit rester minimale.

## 2. Thumbnail Provider

Interfaces :

- `IThumbnailProvider`
- `IInitializeWithStream`

Rôle : générer une image HBITMAP représentant la waveform.

Contraintes :

- rapide ;
- cache obligatoire ;
- rendu utile à petite taille ;
- pas d’analyse complète de fichiers très longs.

## 3. Property Handler

À reporter après le MVP.

Règles :

- ne jamais lancer une analyse longue ;
- ne retourner que des métadonnées déjà disponibles ;
- éviter les dépendances lourdes ;
- prévoir timeouts et erreurs silencieuses.

## 4. Enregistrement COM

L’enregistrement final devra :

- déclarer les CLSID ;
- associer les extensions `.wav`, `.aiff`, `.flac`, `.mp3`, etc. ;
- enregistrer la DLL ;
- fournir un unregister propre.

Les scripts fournis sont des placeholders prudents. Ne pas enregistrer une DLL non finalisée sur une machine de production.

## 5. Sécurité développement

Développer dans une VM Windows de test est fortement recommandé.

Cycle conseillé :

1. compiler ;
2. enregistrer en mode développeur ;
3. tester dans Explorer ;
4. fermer Explorer ou redémarrer le processus ;
5. désenregistrer ;
6. corriger.

Ne jamais tester en premier sur une machine de travail critique.
