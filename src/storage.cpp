#include "storage.h"
#include "nrf.h"
#include <RTTStream.h>
#include <string.h>
#include <math.h>

extern RTTStream rtt;

// ── Flash layout ───────────────────────────────────────────────────────────
// nRF52832 has 512 KB flash in 4 KB (0x1000) pages. We reserve the last
// page (0x7F000-0x7FFFF) as a dedicated settings page. Linker scripts for
// this project place the application well below this address.
static constexpr uint32_t SETTINGS_PAGE_ADDR = 0x0007F000UL;
static constexpr uint32_t SETTINGS_MAGIC     = 0x414C4733UL;  // "ALG3"
static constexpr uint16_t SETTINGS_VERSION   = 2u;

struct PersistedSettingsV1 {
    uint32_t magic;
    uint16_t version;
    uint8_t  therapySubModeIndex;
    uint8_t  reserved;
};

struct PersistedSettings {
    uint32_t magic;
    uint16_t version;
    uint8_t  therapySubModeIndex;
    uint8_t  reserved;
    float     calY;
    float     calZ;
};

static PersistedSettings g_settings = {
    SETTINGS_MAGIC,
    SETTINGS_VERSION,
    0u,
    0u,
    6.75f,
    6.75f
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
    const uint8_t* base = reinterpret_cast<const uint8_t*>(SETTINGS_PAGE_ADDR);
    const uint32_t* magic = reinterpret_cast<const uint32_t*>(base);
    if (*magic != SETTINGS_MAGIC) return false;

    const uint16_t* ver = reinterpret_cast<const uint16_t*>(base + 4);
    if (*ver == 1u) {
        const PersistedSettingsV1* v1 =
            reinterpret_cast<const PersistedSettingsV1*>(SETTINGS_PAGE_ADDR);
        if (v1->therapySubModeIndex > 2) return false;
        g_settings.magic                = SETTINGS_MAGIC;
        g_settings.version              = SETTINGS_VERSION;
        g_settings.therapySubModeIndex  = v1->therapySubModeIndex;
        g_settings.reserved             = 0;
        g_settings.calY                 = 6.75f;
        g_settings.calZ                 = 6.75f;
        persist();  // migrate flash layout v1 -> v2
        return true;
    }
    if (*ver != SETTINGS_VERSION) return false;

    const PersistedSettings* flash =
        reinterpret_cast<const PersistedSettings*>(SETTINGS_PAGE_ADDR);
    if (flash->therapySubModeIndex > 2) return false;

    g_settings = *flash;
    if (fabsf(g_settings.calY) < 0.1f && fabsf(g_settings.calZ) < 0.1f) {
        g_settings.calY = 6.75f;
        g_settings.calZ = 6.75f;
    }
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
    if (idx > 2) idx = 0;
    if (g_settings.therapySubModeIndex == idx) return;  // avoid flash wear
    g_settings.therapySubModeIndex = idx;
    persist();
    rtt.print("Storage: saved therapy sub-mode = ");
    rtt.println((int)idx);
}

void storageLoadCalibration(float* y, float* z) {
    if (y) *y = g_settings.calY;
    if (z) *z = g_settings.calZ;
}

void storageSaveCalibration(float y, float z) {
    if (fabsf(y) < 0.1f && fabsf(z) < 0.1f) {
        y = 6.75f;
        z = 6.75f;
    }
    if (g_settings.calY == y && g_settings.calZ == z) return;
    g_settings.calY = y;
    g_settings.calZ = z;
    persist();
    rtt.println("Storage: saved posture calibration");
}
