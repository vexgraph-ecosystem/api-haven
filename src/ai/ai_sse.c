#include "annotation/intention.h"
#include "annotation/overview.h"
#include "ai/ai_sse.h"

#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: AiSse (ai/ai_sse)
 * LEVEL: L2 — Behavior (pure incremental SSE decoder; caller-fed bytes,
 * R0-budgeted slot binding, zero socket/thread/alloc)
 * ============================================================================
 * Incremental Server-Sent Events decoder for AI token streams. The R0
 * host polls exactly one bound HavenWsFanout slot in 100ms slices
 * (HavenWsFanout_pollStep) and feeds the delivered bytes here with
 * AiSse_feed — this file never touches a socket, spawns a thread, or
 * allocates. Feed parses `event:` / `data:` lines (`:` comments
 * ignored, `\r` stripped), joins multi-line `data:` payloads with
 * `\n`, and reports a `"[DONE]"` payload by latching done. Overlong
 * lines past AI_SSE_LINE_CAP flush mid-line (hostile input never
 * overflows); textOut overflow returns false with the truncation flag
 * set (Rule 35.3, never silent).
 *
 * STRUCT FIELDS (Mirroring ai/ai_sse.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   void *slotHandle;                // borrowed fanout handle (NULL = unbound)
 *   const HavenWsSource *slotSource; // borrowed fn-table (NULL = unbound)
 *   uint64_t timeoutMs;              // per-slice bound (default 100)
 *   bool cancelled;                  // cancel flag: feed degrades to false
 *   bool done;                       // true once a "[DONE]" event dispatches
 *   char lineBuf[AI_SSE_LINE_CAP];   // partial line staging (no alloc)
 *   uint32_t lineLen;                // staged bytes in lineBuf
 *   char eventBuf[AI_SSE_EVENT_CAP]; // current event name ("message" default,
 *                                    // sticky until the next event: line)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors (value struct, no allocation):
 *   - AiSse_0()
 *
 * Core Functions:
 *   - AiSse_feed(self, bytes, byteLen, textOut, textCap, outTruncated)
 *   - AiSse_bind(self, handle, source)
 *   - AiSse_unbind(self)
 *   - AiSse_reset(self)
 *   - AiSse_cancel(self)
 *
 * Setters:
 *   - AiSse_setTimeout(self, timeoutMs)
 *   - AiSse_setCancelled(self, cancelled)
 *   - AiSse_setDone(self, done)
 *
 * Getters:
 *   - AiSse_getSlotHandle(self)
 *   - AiSse_getSlotSource(self)
 *   - AiSse_getTimeout(self)
 *   - AiSse_isCancelled(self)
 *   - AiSse_isDone(self)
 *   - AiSse_getLineLen(self)
 *   - AiSse_getEvent(self, out, outCap)
 * ============================================================================
 */
;;INTENTION("fanout-slot SSE binding: AiSse borrows one HavenWsFanout slot via the existing WsSource table — R0 owns the socket, R1 owns the transport, this class only decodes caller-fed bytes (Rule 33 canonical move, Tier 1 preserved)")

// --- static helpers ---------------------------------------------------------

static void setDefaultEvent(AiSse *self) {
    const char *dflt = "message";
    size_t n = strlen(dflt);
    memcpy((*self).eventBuf, dflt, n + 1);
}

static bool appendText(char *textOut, size_t textCap, size_t *used,
                       const char *s, size_t n, bool *outTruncated) {
    if (*used + n + 1 > textCap) {
        if (outTruncated)
            *outTruncated = true;
        return false;
    }
    memcpy(textOut + *used, s, n);
    *used += n;
    textOut[*used] = '\0';
    return true;
}

// Dispatch one complete line (no trailing newline). Appends data:
// payloads into textOut; latches done on "[DONE]". Returns false only
// on textOut overflow (flag set) — dispatch state still advances.
static bool dispatchLine(AiSse *self, const char *line, size_t lineLen,
                         char *textOut, size_t textCap, size_t *used,
                         bool *hasData, bool *dispatched,
                         bool *outTruncated) {
    if (lineLen == 0) {
        if (*hasData) {
            *dispatched = true;
            *hasData = false;
        }
        return true;
    }
    if ((*line) == ':')
        return true;
    if (lineLen >= 6 && memcmp(line, "event:", 6) == 0) {
        const char *v = line + 6;
        size_t vlen = lineLen - 6;
        if (vlen > 0 && (*v) == ' ') {
            v++;
            vlen--;
        }
        size_t take = vlen;
        if (take >= AI_SSE_EVENT_CAP)
            take = AI_SSE_EVENT_CAP - 1;
        memcpy((*self).eventBuf, v, take);
        (*self).eventBuf[take] = '\0';
        return true;
    }
    if (lineLen >= 5 && memcmp(line, "data:", 5) == 0) {
        const char *v = line + 5;
        size_t vlen = lineLen - 5;
        if (vlen > 0 && (*v) == ' ') {
            v++;
            vlen--;
        }
        if (vlen == 6 && memcmp(v, "[DONE]", 6) == 0) {
            (*self).done = true;
            *hasData = true;
            return true;
        }
        if (!appendText(textOut, textCap, used, v, vlen, outTruncated))
            return false;
        if (!appendText(textOut, textCap, used, "\n", 1, outTruncated))
            return false;
        *hasData = true;
        return true;
    }
    return true;
}

// CONSTRUCTORS

AiSse AiSse_0(void) {
    AiSse sse;
    memset(&sse, 0, sizeof(sse));
    sse.timeoutMs = AI_SSE_POLL_BUDGET_MS;
    setDefaultEvent(&sse);
    return sse;
}

// CORE FUNCTIONS

bool AiSse_feed(AiSse *self, const char *bytes, size_t byteLen,
                char *textOut, size_t textCap, bool *outTruncated) {
    if (self == nullptr)
        return false;
    if (outTruncated)
        *outTruncated = false;
    if ((*self).cancelled)
        return false;
    if ((*self).done)
        return false;
    if ((*self).timeoutMs == 0)
        return false;
    if (textOut == nullptr || textCap == 0)
        return false;
    if (byteLen > 0 && bytes == nullptr)
        return false;
    textOut[0] = '\0';
    size_t used = 0;
    bool hasData = false;
    bool dispatched = false;
    for (size_t i = 0; i < byteLen; i++) {
        char c = bytes[i];
        if (c == '\n') {
            size_t llen = (*self).lineLen;
            if (llen > 0 && (*self).lineBuf[llen - 1] == '\r')
                llen--;
            if (!dispatchLine(self, (*self).lineBuf, llen, textOut, textCap,
                              &used, &hasData, &dispatched, outTruncated))
                return false;
            (*self).lineLen = 0;
            continue;
        }
        if ((*self).lineLen + 1 >= AI_SSE_LINE_CAP) {
            if (!dispatchLine(self, (*self).lineBuf, (*self).lineLen, textOut,
                              textCap, &used, &hasData, &dispatched,
                              outTruncated))
                return false;
            (*self).lineLen = 0;
        }
        (*self).lineBuf[(*self).lineLen] = c;
        (*self).lineLen += 1;
    }
    if (dispatched && used > 0 && textOut[used - 1] == '\n')
        textOut[used - 1] = '\0';
    return dispatched;
}

bool AiSse_bind(AiSse *self, void *handle, const HavenWsSource *source) {
    if (self == nullptr)
        return false;
    if (handle == nullptr || source == nullptr)
        return false;
    if ((*self).slotHandle != nullptr)
        return false;
    (*self).slotHandle = handle;
    (*self).slotSource = source;
    return true;
}

void AiSse_unbind(AiSse *self) {
    if (self == nullptr)
        return;
    (*self).slotHandle = nullptr;
    (*self).slotSource = nullptr;
}

void AiSse_reset(AiSse *self) {
    if (self == nullptr)
        return;
    (*self).cancelled = false;
    (*self).done = false;
    (*self).lineLen = 0;
    setDefaultEvent(self);
}

void AiSse_cancel(AiSse *self) {
    if (self == nullptr)
        return;
    (*self).cancelled = true;
}

// SETTERS

void AiSse_setTimeout(AiSse *self, uint64_t timeoutMs) {
    if (self == nullptr)
        return;
    (*self).timeoutMs = timeoutMs;
}

void AiSse_setCancelled(AiSse *self, bool cancelled) {
    if (self == nullptr)
        return;
    (*self).cancelled = cancelled;
}

void AiSse_setDone(AiSse *self, bool done) {
    if (self == nullptr)
        return;
    (*self).done = done;
}

// GETTERS

void *AiSse_getSlotHandle(const AiSse *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).slotHandle;
}

const HavenWsSource *AiSse_getSlotSource(const AiSse *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).slotSource;
}

uint64_t AiSse_getTimeout(const AiSse *self) {
    if (self == nullptr)
        return 0;
    return (*self).timeoutMs;
}

bool AiSse_isCancelled(const AiSse *self) {
    if (self == nullptr)
        return false;
    return (*self).cancelled;
}

bool AiSse_isDone(const AiSse *self) {
    if (self == nullptr)
        return false;
    return (*self).done;
}

uint32_t AiSse_getLineLen(const AiSse *self) {
    if (self == nullptr)
        return 0;
    return (*self).lineLen;
}

bool AiSse_getEvent(const AiSse *self, char *out, size_t outCap) {
    if (self == nullptr)
        return false;
    if (out == nullptr || outCap == 0)
        return false;
    const char *ev = (*self).eventBuf;
    size_t n = strlen(ev);
    if (n + 1 > outCap)
        return false;
    memcpy(out, ev, n + 1);
    return true;
}
