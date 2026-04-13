#include "storage.h"
#include "nrf.h"
#include <RTTStream.h>

extern RTTStream rtt;

// ── Flash layout ───────────────────────────────────────────────────────────
// nRF52832 has 512 KB flash in 4 KB (0x1000) pages. We reserve the last
// page (0x7F000-0x7FFFF) as a dedicated settings page. Linker scripts for
// this project place the application well below this address.
static constexpr uint32_t SETTINGS_PAGE_ADDR = 0x0007F000UL;
static constexpr uint32_t SETTINGS_MAGIC     = 0x414C4733UL;  // "ALG3"
static constexpr uint16_t SETTINGS_VERSION   = 1u;

struct PersistedSettings {
    uint32_t magic;
    uint16_t version;
    uint8_t  therapySubModeIndex;
    uint8_t  reserved;
};

static PersistedSettings g_settings = {
    SETTINGS_MAGIC,
    SETTINGS_VERSION,
    1u,  // default: 10 min
    0u
};

// ── NVMC helpers ───────────────────────────────────────────────────────────
static inline void nvmcWaitReady() {
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) { /* spin */ }
}

static void nvmcErasePage(uint32_t addr) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een;
    nvmcWaitReady();
    NRF_NVMC->ERASEPAGE = addr;
    nvmcWaitReady();
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    nvmcWaitReady();
}

static void nvmcWriteWords(uint32_t addr, const uint32_t* src, uint32_t count) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    nvmcWaitReady();
    volatile uint32_t* dst = reinterpret_cast<volatile uint32_t*>(addr);
    for (uint32_t i = 0; i < count; i++) {
        dst[i] = src[i];
        nvmcWaitReady();
    }
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
    nvmcWaitReady();
}

// ── Persist / load ─────────────────────────────────────────────────────────
static void persist() {
    // Pad struct to whole 32-bit words for NVMC word writes.
    constexpr uint32_t kWordCount =
        (sizeof(PersistedSettings) + 3u) / 4u;
    uint32_t buffer[kWordCount] = {0};
    memcpy(buffer, &g_settings, sizeof(g_settings));

    noInterrupts();
    nvmcErasePage(SETTINGS_PAGE_ADDR);
    nvmcWriteWords(SETTINGS_PAGE_ADDR, buffer, kWordCount);
    interrupts();
}

static bool loadFromFlash() {
    const PersistedSettings* flash =
        reinterpret_cast<const PersistedSettings*>(SETTINGS_PAGE_ADDR);

    if (flash->magic != SETTINGS_MAGIC) return false;
    if (flash->version != SETTINGS_VERSION) return false;
    if (flash->therapySubModeIndex > 2) return false;  // 0/1/2 = 5/10/20 min

    g_settings = *flash;
    return true;
}

// ── Public API ─────────────────────────────────────────────────────────────
void storageSetup() {
    if (loadFromFlash()) {
        rtt.print("Storage: loaded, therapy sub-mode = ");
        rtt.println((int)g_settings.therapySubModeIndex);
    } else {
        rtt.println("Storage: empty, writing defaults");
        persist();
    }
}

uint8_t storageLoadTherapySubMode() {
    return g_settings.therapySubModeIndex;
}

void storageSaveTherapySubMode(uint8_t idx) {
    if (idx > 2) idx = 1;
    if (g_settings.therapySubModeIndex == idx) return;  // avoid flash wear
    g_settings.therapySubModeIndex = idx;
    persist();
    rtt.print("Storage: saved therapy sub-mode = ");
    rtt.println((int)idx);
}
