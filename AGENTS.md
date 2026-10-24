# Vérification

- Node n'est pas dans le PATH ; un exécutable utilisable pour les tests JavaScript est `C:\Program Files\Adobe\Adobe Creative Cloud Experience\libs\node.exe`.

- Compiler avec `%APPDATA%\Python\Python312\Scripts\platformio.exe run -e blackpill_f411ce` depuis ce dossier (PlatformIO n'est pas dans le PATH).
- Téléverser uniquement après confirmation DFU : même commande avec `-t upload`.
- Le port série observé est COM10, 115200 bauds. Fermer la page Web Serial avant de l'ouvrir depuis Python.
- `bootlog` et `status` permettent un diagnostic sans commande RF. Ne pas envoyer `logclear` sans accord explicite.
- Le secteur 6 (0x08040000) contient le journal ; le secteur 7 (0x08060000) contient les captures RF et l'apprentissage. Le firmware doit rester sous 0x08040000.
- La taille du buffer de sérialisation doit correspondre exactement à `FanLearner::serializedSize()` ; le format des seuils comprend deux saisons de trois floats.

## Référence avant réorganisation — étape 1

- Aucun dépôt Git n'était présent lors de l'établissement de la référence ; aucun dépôt ni commit n'a été créé.
- Archive de retour arrière dans le dossier parent : `Projet_ventillateur_reference_20261022_154924.zip`.
- SHA-256 de l'archive : `e0136b1847e65f9dc8eeb4de333c863be853a061c95c1859ba87daf76128e99f`.
- Elle contient les 21 fichiers d'origine hors `.pio/`, les trois artefacts `firmware.bin`, `firmware.elf`, `firmware.map` disponibles à cet instant et `_reference_manifest.json` avec leurs empreintes SHA-256. Les dépendances et objets intermédiaires ne sont pas archivés.
- Ce point de retour couvre les fichiers du PC, pas les données actuellement présentes dans la flash du STM32. Toute restauration devra d'abord être extraite dans un dossier séparé et comparée, sans écraser les travaux courants.

## Tests PC sans STM32

- Depuis la racine : `node --test tests/web/state_consistency.test.js tests/web/protocol.test.js tests/web/firmware_contract.test.js` (Node 18+ ; exécuté avec Node 22.17.1 sur ce PC).
- Sous PowerShell si Node reste absent du PATH : préfixer les arguments par `& "C:\Program Files\Adobe\Adobe Creative Cloud Experience\libs\node.exe"` à la place de `node`.
- Les tests n'utilisent aucune dépendance npm et n'ouvrent aucun port série.
- Le harnais charge les fonctions réelles du script de `web/index.html` dans un DOM minimal simulé et contrôle la syntaxe de tout le script. Il faudra adapter son chargement lors de l'extraction des fichiers JavaScript, sans remplacer les comportements attendus par une copie du parser.
- Les fixtures série sont synthétiques. Les contrôles du contrat firmware vérifient les commandes, capacités, identifiants et déclarations persistantes ; ce ne sont pas des tests C++ d'exécution, de timing RF, de navigateur réel ou de récupération après coupure.
- `tests/fixtures/firmware_contract.json` conserve la référence des contrats à préserver. Une évolution volontaire doit faire l'objet d'une revue, pas d'une mise à jour automatique des valeurs attendues.
- Risque déjà identifié et non corrigé à l'étape 1 : `board_upload.maximum_size = 393216` dépasse le début du journal à 256 Kio. La garantie par configuration/linker reste réservée à l'étape dédiée à la carte mémoire.

## Indexation — étape 2

- Les exclusions locales sont dans le fichier existant `Projet ventillateur.code-workspace` ; ouvrir cet espace de travail pour qu'elles s'appliquent. Elles n'ajoutent aucun réglage global.
- La recherche exclut `.pio/`, `compile_commands.json` et les configurations C++/debug générées. La surveillance de fichiers exclut les sorties `.pio/build/` et les caches C++ de l'éditeur, pas les bibliothèques `.pio/libdeps/`.
- Ces réglages ne retirent pas les en-têtes nécessaires au compilateur ou au moteur C++. Aucun moteur d'analyse C++ n'a été désactivé ni remplacé.
- Régénérer les métadonnées via `platformio project init -e blackpill_f411ce --ide vscode --no-install-dependencies`, depuis la racine. Ne pas éditer manuellement `.vscode/c_cpp_properties.json` ou `.vscode/launch.json`.
- Régénérer la base après ajout, déplacement ou suppression d'une unité C++ : `platformio run -e blackpill_f411ce -t compiledb`. La base reste à la racine, sans automatisation supplémentaire dans `platformio.ini`.
- Audit après régénération : 260 entrées, les 5 unités C++ locales couvertes, aucun fichier d'entrée ni répertoire de travail manquant.
- Deux chemins CMSIS-DSP historiques restent émis par le générateur dans la configuration C++ (`framework-cmsis/CMSIS/DSP/Include` et `PrivateInclude`). Ils n'existent pas avec les paquets installés ; la compilation réussit. Ne pas créer de faux dossiers ni modifier le framework pour les masquer.
- Aucun gain de temps d'indexation n'a été mesuré. Les exclusions de recherche ne constituent pas une exclusion universelle des outils de l'agent ; aucun réglage global Devin n'a été modifié.

## Définitions communes — étape 3

- Point de retour avant extraction dans le dossier parent : `Projet_ventillateur_reference_20261022_172206.zip`, SHA-256 `496825ac1bc579641d951583336ea59e61dc59f294bc101c9022102e51471a04`.
- Types communs : `src/domain/fan_types.h` et `learning_types.h`. Limites communes : `src/domain/limits.h`. Correspondance RF : `src/domain/remote_buttons.h`.
- `CC1101::MAX_*` et `FanLearner::CAPACITY/KNOW_CAP` restent disponibles comme alias des limites communes.
- Formats et identifiants persistants : `src/persistence/storage_format.h`. Adresses et géométrie du journal : `src/persistence/flash_layout.h`. Ces en-têtes n'importent pas les classes applicatives ni Arduino.
- Le format reste v5 ; le bloc apprentissage fait 2352 octets, le préfixe compatible fait 2152 octets et le bloc RF/état place le learner à l'offset 16436. Les tailles et offsets sont vérifiés par `static_assert` lors de la compilation pour le F411.
- Les tests conservent les fixtures de référence ; seules les localisations des déclarations ont changé. La comparaison des structures reconnaît les alias `RadioLimits`/`LearningLimits`, dont les valeurs et les alias publics sont contrôlés séparément.
- Validation de cette étape : 43 tests PC réussis et compilation ARM réussie. Les empreintes des trois artefacts firmware ont été comparées au point de retour de cette étape et étaient identiques après extraction.
- La limite de taille du firmware et les règles de décision n'ont pas été modifiées. Aucun flash ni accès série n'est nécessaire pour cette étape.

## Extraction de `main.cpp` — étape 4

- `main.cpp` (~395 lignes) ne garde que les includes, `retainControl()`, `setup()` et `loop()` : orchestration, écoute RF, lecture série et boucle de décision.
- `src/controller.cpp` (~525 lignes) contient l'état partagé (capteur, radio, learner, fan, mode, horloge/météo, dirty flags), les commandes série (`handleCommand`), la télémétrie (`printState`/`printStatus`), `applyFan`, `saveFlash`, `enterManual` et les helpers de temps. `src/controller.h` expose les `extern` et constantes (`MANUAL_TIMEOUT_MS`, `STATE_SAVE_PERIOD_MS`).
- `src/ledfx.cpp/.h` : moteur d'effets LED (`ledFxInit/Tick/RfPulse/ThinkPulse`), signatures temporelles inchangées.
- `src/resume.cpp/.h` : registres de sauvegarde `RTC->BKP*` (magie, checkpoint, `Snapshot`, `retain`/`snapshot`). Le format 32 bits est figé ; `main.cpp` n'accède plus aux registres directement.
- `src/envctx.cpp/.h` : contexte environnemental (saison EMA ~6 h, pente ~10 min, événements cuisson/aération remontés à l'appelant). Seuils identiques.
- Les noms de commandes série sont vérifiés dans `controller.cpp` par le test de contrat, valeurs de référence inchangées.
- Validation de cette étape : 43 tests PC réussis et compilation ARM réussie après chaque extraction. Taille flash ~80,8 Ko (vs 80 676 o avant extraction : +~170 o d'appels inter-modules), RAM 33 332 o inchangée. Contrairement à l'étape 3, le binaire n'est PAS identique : comportement censé être équivalent, à valider sur matériel.
- Base de compilation régénérée : elle couvre les 8 unités C++ locales.

## Découpage du learner et du stockage — étape 5

- `src/learning.cpp` (589 lignes) garde le calcul : seuils adaptatifs, observations, consolidation, préférences, exploration, décision.
- `src/learning_store.cpp` : `serializedSize`/`serialize`/`deserialize` + helpers `wr*`/`rd*`/`learnCrc`. Format v5 inchangé.
- `src/learning_report.cpp` : `printState`/`printObservations`/`printKnowledge` (télémétrie série), lignes `[LEARN]`/`[OBS]`/`[PREF]`/`[KNOW]` inchangées.
- `src/bootlog.cpp` : journal de boot persistant sorti de `storage.cpp` (`logBoot`/`dumpBootLog`/`logClear`/`bootLogCount`/`resetCause` + helpers privés). `storage.cpp` ne garde que le blob RF/état/apprentissage du secteur 7.
- Le test de contrat vérifie `LEARN_PREFERENCE_MAGIC` et `CRC32_POLYNOMIAL` dans `learning_store.cpp`.
- Validation : 43 tests PC réussis, compilation ARM réussie. Flash 80 888 o, RAM 33 348 o. Base de compilation régénérée : 12 unités C++ locales couvertes.

## Découpage de la page web — étape 6

- `web/index.html` (210 lignes) garde le balisage et charge, dans l'ordre : `web/style.css`, `web/js/core.js`, `web/js/protocol.js`, `web/js/views.js`, `web/js/actions.js`. Scripts classiques (non-module) : les globales sont partagées entre fichiers.
- `core.js` : état RF (SLOT_NAMES, learned, saveData), onglets, roue, `$`, journaux, connexion/lecture série, météo.
- `protocol.js` : `handleLine` (parser des lignes série) + `flashTab`.
- `views.js` : cartes État et Apprentissage (`updateState`, `renderLearningState`, `updateLearn`, `renderKnow`, REASON_FR).
- `actions.js` : boutons télécommande, modes, sauvegarde/chargement, `send`, micro et vérification par bip.
- Le harnais exige les quatre fichiers dans cet ordre et concatène leur source pour les tests ; les limites `sourceBetween` restent valables.
- Validation : 43 tests PC réussis (syntaxe de chaque fichier vérifiée, DOM ids, parser réel). Aucune recompilation firmware nécessaire.

## Outils consolidés — étape 7

- `tools/` regroupe les scripts sortis du dossier temporaire : `verify.ps1` (tests + compilation), `snapshot.py` (archive SHA-256), `indexing_audit.py`, `serial_monitor.py`, `fan_watch.py` (journaux dans `logs/`), `rf_learn_dump.py`, `dfu_read.py`. Voir `tools/README.md`.
- Vérification reproductible : `powershell -ExecutionPolicy Bypass -File tools\verify.ps1` (option `-SkipBuild`).
- `logs/` créé pour les journaux de surveillance ; `logs/*.log` ignoré par `.gitignore` et exclu des archives `snapshot.py`.
- Validation : 43 tests PC réussis via `verify.ps1 -SkipBuild`, les six scripts Python compilent sans erreur.

## Réservation flash garantie — étape 8

- `board_upload.maximum_size` ramené de 393216 à **262144** dans `platformio.ini` : le firmware ne peut plus déborder à la compilation sur le secteur 6 (journal, `0x08040000`) ni le secteur 7 (données, `0x08060000`).
- `flash_layout.h` : `static_assert` de cohérence des secteurs et `FIRMWARE_LIMIT_ADDR = BOOTLOG_BASE` documentant la limite.
- Contrat de test mis à jour : `currentUploadMaximum = 262144`, assertion `<= requiredFirmwareMaximum`.
- Vérification : compilation OK (flash 80 888/262 144 o = 30,9 %, RAM 33 348 o), adresse max `0x0800fee8` ≪ `0x08040000`, 43/43 tests réussis.

## Validation finale PC — étape 9

- Compilation depuis un build nettoyé (`-t clean`) : flash 80 888 o (30,9 %), RAM 33 348 o (25,4 %).
- 43/43 tests réussis via `tools/verify.ps1` ; audit : 12/12 unités C++ locales dans `compile_commands.json`, 0 input manquant.
- Diff par rapport à l'archive initiale `..._154924.zip` : 18 fichiers modifiés (extraction en modules, web découpé, limite flash, outils/docs) — tous attendus.
- Nouveau point de retour : `Projet_ventillateur_reference_20261023_212433.zip` (66 fichiers, vérifié SHA-256). Prochaine étape : validation sur matériel (flash DFU + essais).

## Flash et property-based testing

- Firmware 80 888 o flashé en DFU et démarrage vérifié sur COM10 : état restauré (AUTO, obs=3/64), capteur OK, `decision=`/`evaluating=` présents.
- `tests/web/helpers/pbt.js` : moteur PBT sans dépendance — PRNG mulberry32 seedable, réduction du contre-exemple par suppression, `check({gen, prop, runs, seed})`.
- `tests/web/pbt_state.test.js` : 400 séquences aléatoires + 300 centrées transitions MANUEL/AUTO + **3 360 cas exhaustifs** (état × décision × évaluation) ; oracle répliquant `updateState`/`updateLearn`. Seeds fixes (`0xC0FFEE`, `0xBADC0DE`) pour reproductibilité.
- Harnais enrichi de stubs (`querySelector`, `logEv`, `SLOT_NAMES`, `learned`, `markLearned`, `finishLearn`) pour élargir la surface fuzzable (`[HEARD]`, dumps).
- `verify.ps1` exécute désormais tous les `tests/web/*.test.js` : **46/46 tests réussis**.

## Redondance A/B du stockage flash

- Copie A : secteur 7 (`0x08060000`) ; copie B : secteur 5 (`0x08020000`). Chaque copie = en-tête `{COPY_MAGIC 'CPY1', seq}` + blob inchangé (format v1 conservé).
- `save()` écrit dans l'autre secteur que la meilleure copie (seq+1) : une coupure pendant effacement/programmation laisse toujours une copie cohérente. Séquencement signé 32 bits (wraparound géré).
- `load()` prend la copie valide la plus récente ; le format historique (blob seul en A) est relu comme seq=0 → migration automatique, aucune donnée perdue.
- `erase()` (`flasherase`) efface les deux secteurs.
- `board_upload.maximum_size = 131072` : firmware plafonné aux secteurs 0-4 (usage actuel 81 156 o, 61,9 %).
- Validation PC : compilation OK (RAM 33 348 o), 46/46 tests.
- Validation matériel : flashé en DFU ; migration automatique du blob historique (relu comme seq=0, 3 obs + 14 boutons restaurés) ; premier `save` écrit en copie B (secteur 5, seq=1) vérifié, `[FLASH] sauvegarde OK`, slots marqués `(flash)`.
