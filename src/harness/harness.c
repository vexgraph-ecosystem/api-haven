#include "annotation/intention.h"
#include "annotation/overview.h"
#include "harness/harness.h"

#include <stddef.h>

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Harness (harness/harness)
 * ============================================================================
 * Bounded CLI-engine job seam: each job owns one slot in a fixed
 * jobs[HARNESS_MAX_JOBS] array (BitPool discipline) — a count, BUSY_FULL
 * when all 16 slots RUNNING, never an unbounded grow. Per-slot timeoutMs
 * bounds every driver wait (Rule 27 drop-degrade); per-slot cancel gives
 * teardown its bounded join (Rule 26). Preconditions (engine, driver,
 * prompt) are validated BEFORE a slot is issued, so rejected runs never
 * consume slots. No exec, no threads, no allocation — spawn/poll/cancel
 * run behind the injected HarnessDriverTable (vexspoke/R3).
 *
 * STRUCT FIELDS (Mirroring harness/harness.h — exactly this file's
 * class):
 * ----------------------------------------------------------------------------
 *   HarnessJob jobs[16];               // fixed slots, BitPool discipline
 *   uint32_t jobCount;                 // slots ever issued, capped at 16
 *   uint64_t timeoutMs;                // default bound for new jobs (ms)
 *   const EngineProviderSlot *engine;  // bound engine row; NULL = unbound
 *   void *driver;                      // opaque driver ctx, never touched
 *   HarnessDriverTable table;          // injected spawn/poll/cancel fns
 *   HarnessStatus lastStatus;          // verdict of the last run call
 *
 * SLOT RECORD (HarnessJob — Rule 3 co-location, zero behavior of its
 * own; all behavior hangs off this class):
 * ----------------------------------------------------------------------------
 *   uint32_t jobId;                    // slot index + 1; stable per slot
 *   const EngineProviderSlot *engine;  // borrowed descriptor row
 *   HarnessStatus status;              // lifecycle state
 *   uint64_t timeoutMs;                // per-slot bound for the driver
 *   void *driverHandle;                // opaque driver job; NULL when idle
 *   size_t outLen;                     // driver-reported output length
 *
 * PRIVATE HELPERS (none — slot scan is inline in Harness_run):
 * ----------------------------------------------------------------------------
 *   (none)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Harness_0()            : engine NULL, driver NULL, 100ms default
 *   - Harness_1(timeoutMs)   : engine NULL, driver NULL, given default
 *
 * Core Functions:
 *   - Harness_run(self, prompt, promptLen, timeoutMs, outJobId) : issue
 *     a job into a free/fresh slot; BUSY_FULL + false when full (dest-last)
 *   - Harness_poll(self, jobId)   : bounded status via pollFn (or stored)
 *   - Harness_cancel(self, jobId) : bounded per-slot teardown to IDLE
 *
 * Setters:
 *   - Harness_setEngine(self, engine)
 *   - Harness_setDriver(self, driver, table)
 *   - Harness_setTimeout(self, timeoutMs)
 *
 * Getters (Rule 24; null-safe):
 *   - Harness_getEngine / getDriver / getTimeout / isRunning /
 *     getJobCount / getJobAt / getJobStatus / getLastStatus
 * ============================================================================
 */
;;INTENTION("fixed slots + count + BUSY_FULL instead of a growable queue — BitPool discipline; teardown stays bounded per Rule 26/27")
;;INTENTION("no exec/threads/allocation inside api-haven — spawn/poll/cancel are injected vexspoke/R3 fns; unbound driver degrades to false, never crashes — Rule 17 api-haven clause")

#define HARNESS_DEFAULT_TIMEOUT_MS 100

// CONSTRUCTORS

Harness Harness_0(void) {
    Harness self = { 0 };
    self.timeoutMs = HARNESS_DEFAULT_TIMEOUT_MS;
    self.lastStatus = HARNESS_STATUS_IDLE;
    return self;
}

Harness Harness_1(uint64_t timeoutMs) {
    Harness self = { 0 };
    self.timeoutMs = timeoutMs;
    self.lastStatus = HARNESS_STATUS_IDLE;
    return self;
}

// CORE FUNCTIONS

bool Harness_run(Harness *self, const char *prompt, size_t promptLen,
                 uint64_t timeoutMs, uint32_t *outJobId) {
    if (!self)
        return false;
    HarnessDriverTable table = (*self).table;
    void *driver = (*self).driver;
    const EngineProviderSlot *engine = (*self).engine;
    if (!engine || !driver || !table.spawnFn || !prompt || promptLen == 0) {
        (*self).lastStatus = HARNESS_STATUS_IDLE;
        return false;
    }
    int32_t freeIndex = -1;
    for (uint32_t i = 0; i < (*self).jobCount; i++) {
        HarnessJob *job = &(*self).jobs[i];
        if ((*job).status != HARNESS_STATUS_RUNNING) {
            freeIndex = (int32_t) i;
            break;
        }
    }
    if (freeIndex < 0) {
        if ((*self).jobCount >= (uint32_t) HARNESS_MAX_JOBS) {
            (*self).lastStatus = HARNESS_STATUS_BUSY_FULL;
            return false;
        }
        freeIndex = (int32_t) (*self).jobCount;
        (*self).jobCount++;
    }
    uint64_t bound = timeoutMs != 0 ? timeoutMs : (*self).timeoutMs;
    void *handle = nullptr;
    bool ok = table.spawnFn(driver, engine, prompt, promptLen, bound, &handle);
    HarnessJob *slot = &(*self).jobs[(uint32_t) freeIndex];
    if (!ok) {
        (*slot).status = HARNESS_STATUS_IDLE;
        (*slot).driverHandle = nullptr;
        (*self).lastStatus = HARNESS_STATUS_IDLE;
        return false;
    }
    (*slot).jobId = (uint32_t) freeIndex + 1;
    (*slot).engine = engine;
    (*slot).status = HARNESS_STATUS_RUNNING;
    (*slot).timeoutMs = bound;
    (*slot).driverHandle = handle;
    (*slot).outLen = 0;
    (*self).lastStatus = HARNESS_STATUS_RUNNING;
    if (outJobId)
        (*outJobId) = (*slot).jobId;
    return true;
}

HarnessStatus Harness_poll(Harness *self, uint32_t jobId) {
    if (!self || jobId == 0)
        return HARNESS_STATUS_IDLE;
    for (uint32_t i = 0; i < (*self).jobCount; i++) {
        HarnessJob *job = &(*self).jobs[i];
        if ((*job).jobId != jobId)
            continue;
        HarnessDriverTable table = (*self).table;
        if (!table.pollFn)
            return (*job).status;
        HarnessStatus now = table.pollFn((*self).driver, (*job).driverHandle,
                                        (*job).timeoutMs);
        if (now != HARNESS_STATUS_RUNNING && now != HARNESS_STATUS_BUSY_FULL) {
            (*job).status = now;
            if (now == HARNESS_STATUS_IDLE)
                (*job).driverHandle = nullptr;
        }
        return (*job).status;
    }
    return HARNESS_STATUS_IDLE;
}

bool Harness_cancel(Harness *self, uint32_t jobId) {
    if (!self || jobId == 0)
        return false;
    for (uint32_t i = 0; i < (*self).jobCount; i++) {
        HarnessJob *job = &(*self).jobs[i];
        if ((*job).jobId != jobId)
            continue;
        if ((*job).status != HARNESS_STATUS_RUNNING)
            return false;
        HarnessDriverTable table = (*self).table;
        if (table.cancelFn)
            table.cancelFn((*self).driver, (*job).driverHandle);
        (*job).status = HARNESS_STATUS_IDLE;
        (*job).driverHandle = nullptr;
        return true;
    }
    return false;
}

// SETTERS

void Harness_setEngine(Harness *self, const EngineProviderSlot *engine) {
    if (!self)
        return;
    (*self).engine = engine;
}

void Harness_setDriver(Harness *self, void *driver, HarnessDriverTable table) {
    if (!self)
        return;
    (*self).driver = driver;
    (*self).table = table;
}

void Harness_setTimeout(Harness *self, uint64_t timeoutMs) {
    if (!self)
        return;
    (*self).timeoutMs = timeoutMs;
}

// GETTERS

const EngineProviderSlot *Harness_getEngine(const Harness *self) {
    return self ? (*self).engine : nullptr;
}

void *Harness_getDriver(const Harness *self) {
    return self ? (*self).driver : nullptr;
}

uint64_t Harness_getTimeout(const Harness *self) {
    if (!self)
        return 0;
    return (*self).timeoutMs;
}

bool Harness_isRunning(const Harness *self) {
    if (!self)
        return false;
    for (uint32_t i = 0; i < (*self).jobCount; i++) {
        const HarnessJob *job = &(*self).jobs[i];
        if ((*job).status == HARNESS_STATUS_RUNNING)
            return true;
    }
    return false;
}

uint32_t Harness_getJobCount(const Harness *self) {
    if (!self)
        return 0;
    return (*self).jobCount;
}

const HarnessJob *Harness_getJobAt(const Harness *self, uint32_t i) {
    if (!self)
        return nullptr;
    if (i >= (*self).jobCount)
        return nullptr;
    return &(*self).jobs[i];
}

HarnessStatus Harness_getJobStatus(const Harness *self, uint32_t jobId) {
    if (!self || jobId == 0)
        return HARNESS_STATUS_IDLE;
    for (uint32_t i = 0; i < (*self).jobCount; i++) {
        const HarnessJob *job = &(*self).jobs[i];
        if ((*job).jobId == jobId)
            return (*job).status;
    }
    return HARNESS_STATUS_IDLE;
}

HarnessStatus Harness_getLastStatus(const Harness *self) {
    if (!self)
        return HARNESS_STATUS_IDLE;
    return (*self).lastStatus;
}
