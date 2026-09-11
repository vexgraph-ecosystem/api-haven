#include "annotation/overview.h"
#include "asset/asset_broker.h"
#include "asset/asset_provider.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: AssetTest (src/asset/tests/asset_test.c)
 * LEVEL: L2 — Behavior verification (headless; descriptors + bounded
 * copies only, no network, no scraping, no exec)
 * ============================================================================
 * Executable proof of the asset/ directory: the AssetProvider table
 * (count, at, get, blessed-API vs catalog-only rows, directory-wide
 * invariants, null-safety) and the AssetBroker chunked-copy contract
 * (cursor copies, per-chunk cap, dest overflow with truncation flag,
 * cancel latch, zero budget, cache-path shaping, setters/getters).
 *
 * Exit code 0 = all checks green; 1 = at least one check failed.
 * ============================================================================
 */

static int sFailures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            sFailures++;                                               \
        }                                                              \
    } while (0)

// [a-z0-9]+(-[a-z0-9]+)* — canonical slug grammar (provider tables).
static int isSlugValid(const char *slug) {
    if (!slug || (*slug) == '\0')
        return 0;
    for (const char *p = slug; *p; p++) {
        char c = *p;
        int isAlnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (c == '-') {
            if (p == slug || *(p + 1) == '\0' || *(p + 1) == '-')
                return 0;
        } else if (!isAlnum) {
            return 0;
        }
    }
    return 1;
}

int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    // --- directory shape ---------------------------------------------------------
    AssetProvider *dir = AssetProvider_shared();
    CHECK(dir != NULL);
    const uint32_t total = AssetProvider_count(dir);
    printf("AssetProvider_count = %u\n", total);
    CHECK(total == 12);
    CHECK(AssetProvider_at(dir, 0) != NULL);
    CHECK(AssetProvider_at(dir, total) == NULL);

    // --- blessed-API row ------------------------------------------------------------
    const AssetProviderSlot *unsplash = AssetProvider_get(dir, "unsplash");
    CHECK(unsplash != NULL);
    CHECK(unsplash && strcmp(AssetProvider_getDisplayName(dir, unsplash), "Unsplash") == 0);
    CHECK(unsplash && AssetProvider_getApiBase(dir, unsplash) != NULL);
    CHECK(unsplash && AssetProvider_getLicenseFamily(dir, unsplash) != NULL);
    CHECK(unsplash && AssetProvider_getSampleTitle(dir, unsplash) != NULL);
    CHECK(unsplash && AssetProvider_getSampleAuthor(dir, unsplash) != NULL);
    CHECK(unsplash && AssetProvider_getSamplePreview(dir, unsplash) != NULL);
    CHECK(unsplash && AssetProvider_getSampleDownload(dir, unsplash) != NULL);

    // --- catalog-only row (no search API, curated manifest) -----------------------------
    const AssetProviderSlot *kenney = AssetProvider_get(dir, "kenney");
    CHECK(kenney && AssetProvider_getApiBase(dir, kenney) == NULL);
    CHECK(kenney && strcmp(AssetProvider_getLicenseFamily(dir, kenney), "CC0") == 0);
    CHECK(kenney && AssetProvider_getSampleDownload(dir, kenney) != NULL);
    CHECK(AssetProvider_get(dir, "quaternius") != NULL);
    CHECK(AssetProvider_get(dir, "definitely-not-a-source") == NULL);

    // --- directory consistency ------------------------------------------------------------
    const char *names[12];
    uint32_t nameCount = 0;
    for (uint32_t i = 0; i < total; i++) {
        const AssetProviderSlot *row = AssetProvider_at(dir, i);
        CHECK(row != NULL);
        CHECK(row && isSlugValid(AssetProvider_getSlug(dir, row)));
        CHECK(row && AssetProvider_get(dir, AssetProvider_getSlug(dir, row)) == row);
        const char *rowName = row ? AssetProvider_getDisplayName(dir, row) : NULL;
        CHECK(rowName && (*rowName) != '\0');
        CHECK(row && AssetProvider_getLicenseFamily(dir, row) != NULL);
        CHECK(row && AssetProvider_getSampleDownload(dir, row) != NULL);
        if (row && rowName) {
            int dup = 0;
            for (uint32_t j = 0; j < nameCount; j++) {
                if (strcmp(names[j], rowName) == 0)
                    dup = 1;
            }
            CHECK(!dup);
            names[nameCount++] = rowName;
        }
    }

    // --- null-safety (Rule 24) -------------------------------------------------------------------
    CHECK(AssetProvider_count(NULL) == 0);
    CHECK(AssetProvider_get(NULL, NULL) == NULL);
    CHECK(AssetProvider_get(dir, NULL) == NULL);
    CHECK(AssetProvider_at(NULL, 0) == NULL);
    CHECK(AssetProvider_getSlug(NULL, NULL) == NULL);
    CHECK(AssetProvider_getDisplayName(NULL, NULL) == NULL);
    CHECK(AssetProvider_getApiBase(NULL, NULL) == NULL);
    CHECK(AssetProvider_getLicenseFamily(NULL, NULL) == NULL);
    CHECK(AssetProvider_getSampleTitle(NULL, NULL) == NULL);
    CHECK(AssetProvider_getSampleAuthor(NULL, NULL) == NULL);
    CHECK(AssetProvider_getSamplePreview(NULL, NULL) == NULL);
    CHECK(AssetProvider_getSampleDownload(NULL, NULL) == NULL);
    CHECK(AssetProvider_getNote(NULL, NULL) == NULL);

    // --- broker: cursor copies --------------------------------------------------------------------------
    AssetBroker broker = AssetBroker_0();
    CHECK(AssetBroker_getChunkBudget(&broker) == ASSET_BROKER_CHUNK_BUDGET_MS);
    CHECK(!AssetBroker_isCancelled(&broker));
    CHECK(AssetBroker_getBytesCopied(&broker) == 0);
    char sub[ASSET_BROKER_CACHE_SUB_CAP];
    CHECK(AssetBroker_getCacheSub(&broker, sub, sizeof(sub)));
    CHECK(strcmp(sub, "assets") == 0);

    uint8_t dest[64];
    uint32_t used = 0;
    bool truncated = false;
    const uint8_t c1[4] = {'a', 'b', 'c', 'd'};
    const uint8_t c2[4] = {'e', 'f', 'g', 'h'};
    CHECK(AssetBroker_copyChunk(&broker, c1, sizeof(c1), dest, sizeof(dest), &used, &truncated));
    CHECK(!truncated && used == 4);
    CHECK(AssetBroker_copyChunk(&broker, c2, sizeof(c2), dest, sizeof(dest), &used, &truncated));
    CHECK(used == 8 && memcmp(dest, "abcdefgh", 8) == 0);
    CHECK(AssetBroker_getBytesCopied(&broker) == 8);

    // --- broker: empty chunk is a no-op success ----------------------------------------------------------------
    CHECK(AssetBroker_copyChunk(&broker, NULL, 0, dest, sizeof(dest), &used, &truncated));
    CHECK(used == 8);

    // --- broker: oversize chunk refused (per-chunk bound, never whole-file) ----------------------------------------
    uint8_t big[ASSET_BROKER_CHUNK_CAP + 1];
    memset(big, 0x5a, sizeof(big));
    CHECK(!AssetBroker_copyChunk(&broker, big, sizeof(big), dest, sizeof(dest), &used, &truncated));

    // --- broker: dest overflow sets the flag (Rule 35.3) --------------------------------------------------------------------
    uint8_t fill[56];
    memset(fill, 0x31, sizeof(fill));
    CHECK(AssetBroker_copyChunk(&broker, fill, sizeof(fill), dest, sizeof(dest), &used, &truncated));
    CHECK(used == 64);
    CHECK(!AssetBroker_copyChunk(&broker, c1, sizeof(c1), dest, sizeof(dest), &used, &truncated));
    CHECK(truncated);
    CHECK(!AssetBroker_copyChunk(&broker, c1, sizeof(c1), dest, sizeof(dest), &used, nullptr));

    // --- broker: cancel latch + zero budget --------------------------------------------------------------------
    AssetBroker canc = AssetBroker_0();
    AssetBroker_cancel(&canc);
    CHECK(AssetBroker_isCancelled(&canc));
    used = 0;
    CHECK(!AssetBroker_copyChunk(&canc, c1, sizeof(c1), dest, sizeof(dest), &used, &truncated));
    AssetBroker_reset(&canc);
    CHECK(!AssetBroker_isCancelled(&canc));
    CHECK(AssetBroker_copyChunk(&canc, c1, sizeof(c1), dest, sizeof(dest), &used, &truncated));

    AssetBroker tmo = AssetBroker_0();
    AssetBroker_setChunkBudget(&tmo, 0);
    CHECK(AssetBroker_getChunkBudget(&tmo) == 0);
    used = 0;
    CHECK(!AssetBroker_copyChunk(&tmo, c1, sizeof(c1), dest, sizeof(dest), &used, &truncated));
    AssetBroker_setChunkBudget(&tmo, ASSET_BROKER_CHUNK_BUDGET_MS);

    // --- broker: null matrix --------------------------------------------------------------------
    CHECK(!AssetBroker_copyChunk(nullptr, c1, sizeof(c1), dest, sizeof(dest), &used, &truncated));
    CHECK(!AssetBroker_copyChunk(&broker, nullptr, 4, dest, sizeof(dest), &used, &truncated));
    CHECK(!AssetBroker_copyChunk(&broker, c1, sizeof(c1), nullptr, sizeof(dest), &used, &truncated));
    CHECK(!AssetBroker_copyChunk(&broker, c1, sizeof(c1), dest, 0, &used, &truncated));
    CHECK(!AssetBroker_copyChunk(&broker, c1, sizeof(c1), dest, sizeof(dest), nullptr, &truncated));
    uint32_t badUsed = 99;
    CHECK(!AssetBroker_copyChunk(&broker, c1, sizeof(c1), dest, sizeof(dest), &badUsed, &truncated));
    AssetBroker_reset(nullptr);
    AssetBroker_cancel(nullptr);
    AssetBroker_setChunkBudget(nullptr, 5);
    AssetBroker_setCancelled(nullptr, true);
    AssetBroker_setBytesCopied(nullptr, 3);
    CHECK(AssetBroker_getChunkBudget(nullptr) == 0);
    CHECK(!AssetBroker_isCancelled(nullptr));
    CHECK(AssetBroker_getBytesCopied(nullptr) == 0);
    CHECK(!AssetBroker_getCacheSub(nullptr, sub, sizeof(sub)));
    CHECK(!AssetBroker_getCacheSub(&broker, nullptr, sizeof(sub)));
    CHECK(!AssetBroker_getCacheSub(&broker, sub, 0));

    // --- broker: setters round-trip --------------------------------------------------------------------
    AssetBroker st = AssetBroker_0();
    bool subTrunc = false;
    CHECK(AssetBroker_setCacheSub(&st, "models", &subTrunc));
    CHECK(!subTrunc);
    CHECK(AssetBroker_getCacheSub(&st, sub, sizeof(sub)));
    CHECK(strcmp(sub, "models") == 0);
    const char *wayTooLong = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdefX";
    CHECK(!AssetBroker_setCacheSub(&st, wayTooLong, &subTrunc));
    CHECK(subTrunc);
    CHECK(!AssetBroker_setCacheSub(&st, NULL, &subTrunc));
    CHECK(!AssetBroker_setCacheSub(nullptr, "x", &subTrunc));
    AssetBroker_setCancelled(&st, true);
    CHECK(AssetBroker_isCancelled(&st));
    AssetBroker_setBytesCopied(&st, 41);
    CHECK(AssetBroker_getBytesCopied(&st) == 41);

    // --- broker: cache path shaping --------------------------------------------------------------------
    char path[128];
    CHECK(AssetBroker_cachePath(&st, "fox.glb", path, sizeof(path)));
    CHECK(strcmp(path, "cache/models/fox.glb") == 0);
    CHECK(!AssetBroker_cachePath(&st, "fox.glb", path, 8));
    CHECK(!AssetBroker_cachePath(nullptr, "fox.glb", path, sizeof(path)));
    CHECK(!AssetBroker_cachePath(&st, nullptr, path, sizeof(path)));
    CHECK(!AssetBroker_cachePath(&st, "", path, sizeof(path)));
    CHECK(!AssetBroker_cachePath(&st, "fox.glb", nullptr, sizeof(path)));
    CHECK(!AssetBroker_cachePath(&st, "fox.glb", path, 0));

    if (sFailures == 0) {
        printf("asset_test: ALL CHECKS PASSED (%u sources)\n", total);
        return 0;
    }
    printf("asset_test: %d FAILURES\n", sFailures);
    return 1;
}
