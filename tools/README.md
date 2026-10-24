# Outils du projet

Tous les scripts se lancent depuis la racine du projet. Dépendances Python :
`pyserial` pour le série, `pyusb` + `libusb_package` pour le DFU.

## Sans matériel

- `verify.ps1` — tests JavaScript puis compilation ARM (`-SkipBuild` pour tests seuls).
- `snapshot.py` — archive de retour arrière avec manifeste SHA-256 :
  `python tools/snapshot.py .` puis contrôle via `--verify <archive.zip>`.
- `indexing_audit.py` — volumes par dossier, config C++ générée,
  couverture de `compile_commands.json`, exclusions du workspace.

## Avec le STM32 (COM10, 115200)

- `serial_monitor.py` — affiche les lignes horodatées ; `--cmd bootlog`,
  `--minutes N`, ou `--cmdfile <fichier>` pour envoyer des commandes
  depuis un autre terminal.
- `fan_watch.py` — surveillance prolongée : journal horodaté dans `logs/`,
  réouverture automatique du port, requêtes `bootlog`/`status` périodiques
  (aucune commande RF).
- `rf_learn_dump.py` — `learnN` puis `dumpN` et sauvegarde le CSV :
  `python tools/rf_learn_dump.py --slot 14 --out dump14.txt`.
- `dfu_read.py` — lecture de la flash en mode DFU :
  `python tools/dfu_read.py flash.bin 0x08000000 0x80000`.
