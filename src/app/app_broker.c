#include "annotation/intention.h"
#include "annotation/overview.h"
#include "annotation/platform_exclusive.h"
#include "app/app_broker.h"

#include <stddef.h>

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: AppBroker (app/app_broker)
 * LEVEL: L2 — Behavior (Rule 28: slotted action seam over injected drivers)
 * ============================================================================
 * Bounded app/automation action seam: each action owns one slot in a
 * fixed jobs[APP_MAX_JOBS] array (BitPool discipline) — a count,
 * BUSY_FULL when all 16 slots RUNNING, never an unbounded grow.
 * Per-slot timeoutMs bounds the driver call (Rule 27 drop-degrade);
 * per-slot cancel gives teardown its bounded join (Rule 26).
 * Preconditions (target, driver, action) are validated BEFORE a slot is
 * issued, so rejected actions never consume slots. No osascript, no IPC,
 * no sockets, no threads, no allocation — execution runs behind the
 * injected AppDriverTable (vexspoke/R3).
 *
 * STRUCT FIELDS (Mirroring app/app_broker.h — exactly this file's
 * class):
 * ----------------------------------------------------------------------------
 *   AppJob jobs[16];                // fixed slots, BitPool discipline
 *   uint32_t jobCount;              // slots ever issued, capped at 16
 *   uint64_t timeoutMs;             // default bound for new actions (ms)
 *   const AppProviderSlot *target;  // bound target row; nullptr = unbound
 *   void *driver;                   // opaque driver ctx, never touched
 *   AppDriverTable table;           // injected run fn
 *   AppStatus lastStatus;           // verdict of the last action call
 *
 * SLOT RECORD (AppJob — Rule 3 co-location, zero behavior of its own;
 * all behavior hangs off this class):
 * ----------------------------------------------------------------------------
 *   uint32_t jobId;                 // slot index + 1; stable per slot
 *   const AppProviderSlot *target;  // borrowed descriptor row
 *   AppStatus status;               // lifecycle state
 *   uint64_t timeoutMs;             // per-slot bound for the driver
 *   void *driverHandle;             // opaque driver job; nullptr when idle
 *   size_t outLen;                  // bytes written into the caller dest
 *
 * PRIVATE HELPERS (none — slot scan is inline in AppBroker_action):
 * ----------------------------------------------------------------------------
 *   (none)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - AppBroker_0()            : target nullptr, driver nullptr, 100ms default
 *   - AppBroker_1(timeoutMs)   : target nullptr, driver nullptr, given default
 *
 * Core Functions:
 *   - AppBroker_action(self, action, paramsJson, paramsLen, dest, cap,
 *     outJobId) : sync bounded exec into a free/fresh slot (dest-last)
 *   - AppBroker_poll(self, jobId)   : stored slot verdict, never blocks
 *   - AppBroker_cancel(self, jobId) : bounded per-slot teardown to IDLE
 *
 * Setters:
 *   - AppBroker_setTarget(self, target)
 *   - AppBroker_setDriver(self, driver, table)
 *   - AppBroker_setTimeout(self, timeoutMs)
 *
 * Getters (Rule 24; null-safe):
 *   - AppBroker_getTarget / getDriver / getTimeout / isRunning /
 *     getJobCount / getJobAt / getJobStatus / getLastStatus
 * ============================================================================
 */
;;PLATFORM_EXCLUSIVE("macOS")
;;INTENTION("missing targets degrade to UNAVAILABLE via the unbound-driver verdict (IDLE + false) — AppDetect pattern, never a crash when the app or helper is absent")
;;INTENTION("fixed slots + count + BUSY_FULL instead of a growable queue — BitPool discipline; teardown stays bounded per Rule 26/27")
;;INTENTION("no osascript/IPC/sockets/threads/allocation inside api-haven — execution is one injected vexspoke/R3 fn; unbound driver degrades to false — Rule 17 api-haven clause")

#define APP_DEFAULT_TIMEOUT_MS 100

// CONSTRUCTORS

AppBroker AppBroker_0() {
    AppBroker self = { 0 };
    self.timeoutMs = APP_DEFAULT_TIMEOUT_MS;
    self.lastStatus = APP_STATUS_IDLE;
    return self;
}

AppBroker AppBroker_1(uint64_t timeoutMs) {
    AppBroker self = { 0 };
    self.timeoutMs = timeoutMs;
    self.lastStatus = APP_STATUS_IDLE;
    return self;
}

// CORE FUNCTIONS

bool AppBroker_action(AppBroker *self, const char *action,
                      const char *paramsJson, size_t paramsLen,
                      char *outBuf, size_t outCap, uint32_t *outJobId) {
    if (!self)
        return false;
    AppDriverTable table = (*self).table;
    void *driver = (*self).driver;
    const AppProviderSlot *target = (*self).target;
    if (!target || !driver || !table.runActionFn || !action || (*action) == '\0') {
        (*self).lastStatus = APP_STATUS_IDLE;
        return false;
    }
    int32_t freeIndex = -1;
    for (uint32_t i = 0; i < (*self).jobCount; i++) {
        AppJob *job = &(*self).jobs[i];
        if ((*job).status != APP_STATUS_RUNNING) {
            freeIndex = (int32_t) i;
            break;
        }
    }
    if (freeIndex < 0) {
        if ((*self).jobCount >= (uint32_t) APP_MAX_JOBS) {
            (*self).lastStatus = APP_STATUS_BUSY_FULL;
            return false;
        }
        freeIndex = (int32_t) (*self).jobCount;
        (*self).jobCount++;
    }
    uint64_t bound = (*self).timeoutMs;
    AppJob *slot = &(*self).jobs[(uint32_t) freeIndex];
    (*slot).jobId = (uint32_t) freeIndex + 1;
    (*slot).target = target;
    (*slot).status = APP_STATUS_RUNNING;
    (*slot).timeoutMs = bound;
    (*slot).driverHandle = nullptr;
    (*slot).outLen = 0;
    size_t produced = 0;
    bool ok = table.runActionFn(driver, target, action, paramsJson, paramsLen,
                                bound, outBuf, outCap, &produced);
    if (!ok) {
        (*slot).status = APP_STATUS_TIMEOUT;
        (*slot).outLen = 0;
        (*self).lastStatus = APP_STATUS_TIMEOUT;
        if (outJobId)
            *outJobId = (*slot).jobId;
        return false;
    }
    (*slot).status = APP_STATUS_DONE;
    (*slot).outLen = produced;
    (*self).lastStatus = APP_STATUS_DONE;
    if (outJobId)
        (*outJobId) = (*slot).jobId;
    return true;
}

AppStatus AppBroker_poll(AppBroker *self, uint32_t jobId) {
    if (!self || jobId == 0)
        return APP_STATUS_IDLE;
    for (uint32_t i = 0; i < (*self).jobCount; i++) {
        const AppJob *job = &(*self).jobs[i];
        if ((*job).jobId == jobId)
            return (*job).status;
    }
    return APP_STATUS_IDLE;
}

bool AppBroker_cancel(AppBroker *self, uint32_t jobId) {
    if (!self || jobId == 0)
        return false;
    for (uint32_t i = 0; i < (*self).jobCount; i++) {
        AppJob *job = &(*self).jobs[i];
        if ((*job).jobId != jobId)
            continue;
        if ((*job).status != APP_STATUS_RUNNING)
            return false;
        (*job).status = APP_STATUS_IDLE;
        (*job).driverHandle = nullptr;
        return true;
    }
    return false;
}

// SETTERS

void AppBroker_setTarget(AppBroker *self, const AppProviderSlot *target) {
    if (!self)
        return;
    (*self).target = target;
}

void AppBroker_setDriver(AppBroker *self, void *driver, AppDriverTable table) {
    if (!self)
        return;
    (*self).driver = driver;
    (*self).table = table;
}

void AppBroker_setTimeout(AppBroker *self, uint64_t timeoutMs) {
    if (!self)
        return;
    (*self).timeoutMs = timeoutMs;
}

// GETTERS

const AppProviderSlot *AppBroker_getTarget(const AppBroker *self) {
    return self ? (*self).target : nullptr;
}

void *AppBroker_getDriver(const AppBroker *self) {
    return self ? (*self).driver : nullptr;
}

uint64_t AppBroker_getTimeout(const AppBroker *self) {
    if (!self)
        return 0;
    return (*self).timeoutMs;
}

bool AppBroker_isRunning(const AppBroker *self) {
    if (!self)
        return false;
    for (uint32_t i = 0; i < (*self).jobCount; i++) {
        const AppJob *job = &(*self).jobs[i];
        if ((*job).status == APP_STATUS_RUNNING)
            return true;
    }
    return false;
}

uint32_t AppBroker_getJobCount(const AppBroker *self) {
    if (!self)
        return 0;
    return (*self).jobCount;
}

const AppJob *AppBroker_getJobAt(const AppBroker *self, uint32_t i) {
    if (!self)
        return nullptr;
    if (i >= (*self).jobCount)
        return nullptr;
    return &(*self).jobs[i];
}

AppStatus AppBroker_getJobStatus(const AppBroker *self, uint32_t jobId) {
    if (!self || jobId == 0)
        return APP_STATUS_IDLE;
    for (uint32_t i = 0; i < (*self).jobCount; i++) {
        const AppJob *job = &(*self).jobs[i];
        if ((*job).jobId == jobId)
            return (*job).status;
    }
    return APP_STATUS_IDLE;
}

AppStatus AppBroker_getLastStatus(const AppBroker *self) {
    if (!self)
        return APP_STATUS_IDLE;
    return (*self).lastStatus;
}
