#pragma once

// Secteur 7, 128 Ko, base 0x08060000 : copie A du blob de donnees
#define STORAGE_ADDR   0x08060000UL
// Secteur 5, 128 Ko, base 0x08020000 : copie B (redondance A/B).
// Les deux copies sont dans des secteurs d'effacement distincts : une
// coupure pendant une sauvegarde laisse toujours l'autre copie lisible.
#define STORAGE_B_ADDR 0x08020000UL

// Secteur 6 (128 Ko a 0x08040000) : sector dedie au log, JAMAIS touche
// par save()/erase() du secteur 7 ni par le firmware (~71 Ko) -> le
// log survit aux sauvegardes. Effacement uniquement via logClear()
// (commande 'logclear'), a reserver une fois un probleme compris.
// Enregistrement de 16 o : magic 'BTL1', seq, RCC->CSR, reserve.
#define BOOTLOG_BASE  0x08040000UL                       // secteur 6
#define BOOTLOG_END   0x08060000UL                       // fin secteur 6
#define BOOTLOG_HDR   0x424C4831UL                       // 'BLH1' en-tete
#define BOOTREC_MAGIC 0x42544C31UL                       // 'BTL1'
#define BOOTREC_SIZE  16UL
#define BOOTLOG_ADDR  (BOOTLOG_BASE + 16UL)              // recs apres l'en-tete

// Gardes de coherence de la disposition (secteurs F411 : 6 puis 7).
static_assert(STORAGE_ADDR == BOOTLOG_END, "secteur 7 doit suivre le journal");
static_assert(BOOTLOG_END - BOOTLOG_BASE == 0x20000UL, "secteur 6 = 128 Ko");
// Limite imposee au firmware : ne jamais depasser 0x08020000 (128 Ko,
// secteurs 0-4), alignee sur board_upload.maximum_size = 131072.
#define FIRMWARE_LIMIT_ADDR STORAGE_B_ADDR
