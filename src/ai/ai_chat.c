#include "annotation/intention.h"
#include "annotation/overview.h"
#include "ai/ai_chat.h"
#include "api/rest.h"
#include "net/json.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: AiChat (ai/ai_chat)
 * getters; consumes the L1 AiProvider directory + api/rest core)
 * ============================================================================
 * OpenAI-compatible chat completion client. Resolves the selected provider
 * row to a native endpoint (or falls back to the family default), renders
 * the chat/completions envelope JSON into caller-owned scratch, and sends
 * it via Rest_postJson. Zero allocation on every path.
 *
 * STRUCT FIELDS (Mirroring ai/ai_chat.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   const AiProvider *provider;  // directory handle; NULL = shared default
 *   const AiProviderSlot *peer;  // selected provider row; NULL = gateway
 *   const char *model;           // e.g. "grok-2-latest"; must be set
 *   const char *apiKey;          // Bearer credential; NULL = no auth
 *   const char *baseUrlOverride; // wins over peer base / family default
 *
 * SLOT RECORD (AiMessage — Rule 3 co-location, zero behavior of its own):
 * ----------------------------------------------------------------------------
 *   const char *role;    // "system" | "user" | "assistant"
 *   const char *content; // message body
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors (value structs — ApiAuth precedent):
 *   - AiChat_0()
 *   - AiChat_1(model)
 *   - AiChat_2(model, apiKey)
 *
 * Core Functions:
 *   - AiChat_buildRequest(self, msgs, msgCount, bodyBuf, bodyCap,
 *                         urlBuf, urlCap, authOut)  : render-only, headless
 *   - AiChat_complete(self, msgs, msgCount, bodyBuf, bodyCap, resp)
 *                                                    : render + send
 *
 * Setters:
 *   - AiChat_setProvider / setPeer / setModel / setApiKey / setBaseUrl
 *
 * Getters:
 *   - AiChat_getProvider / getPeer / getModel / getApiKey / getBaseUrl
 * ============================================================================
 */
;;INTENTION("OpenAI-compatible envelope only — Anthropic Messages gets its own class pair (ai_chat_anthropic) in a later cycle; a mode flag would fake the two-layer cap")

// --- static helpers ---------------------------------------------------------

static bool appendLiteral(char *out, size_t cap, size_t *used, const char *s) {
    const size_t n = strlen(s);
    if (!out || !used || *used + n + 1 > cap)
        return false;
    memcpy(out + *used, s, n);
    *used += n;
    out[*used] = '\0';
    return true;
}

static bool appendJsonString(char *out, size_t cap, size_t *used, const char *s) {
    const int64_t w = Json_writeString(out + *used, cap - *used, s);
    if (w < 0)
        return false;
    *used += (size_t)w;
    return true;
}

// CONSTRUCTORS

AiChat AiChat_0(void) {
    AiChat chat;
    memset(&chat, 0, sizeof(chat));
    return chat;
}

AiChat AiChat_1(const char *model) {
    AiChat chat = AiChat_0();
    // provider borrows the shared singleton so directory calls are legal.
    chat.provider = AiProvider_shared();
    chat.model = model;
    return chat;
}

AiChat AiChat_2(const char *model, const char *apiKey) {
    AiChat chat = AiChat_1(model);
    chat.apiKey = apiKey;
    return chat;
}

// CORE FUNCTIONS

bool AiChat_buildRequest(const AiChat *self, const AiMessage *msgs, uint32_t msgCount,
                         char *bodyBuf, size_t bodyCap,
                         char *urlBuf, size_t urlCap,
                         ApiAuth *authOut) {
    if (!self || !bodyBuf || !urlBuf || !authOut || !(*self).model)
        return false;
    if (!msgs && msgCount > 0)
        return false;
    if (urlCap == 0 || bodyCap == 0)
        return false;

    // Resolve endpoint: override > peer native base > family default > gateway.
    const char *base = "https://openrouter.ai/api/v1";
    if ((*self).baseUrlOverride)
        base = (*self).baseUrlOverride;
    else if ((*self).peer) {
        const AiProvider *table = (*self).provider ? (*self).provider : AiProvider_shared();
        const char *resolved = AiProvider_resolveBaseUrl(table, (*self).peer);
        if (resolved)
            base = resolved;
    }

    const int printed = snprintf(urlBuf, urlCap, "%s/chat/completions", base);
    if (printed < 0 || (size_t)printed >= urlCap)
        return false;

    // Credential: peer row may dictate scheme; only Bearer is modeled today.
    if ((*self).apiKey)
        (*authOut) = ApiAuth_bearer((*self).apiKey);
    else
        (*authOut) = ApiAuth_none();

    // Render {"model":"...","messages":[{"role":"...","content":"..."},...]}
    size_t used = 0;
    if (!appendLiteral(bodyBuf, bodyCap, &used, "{\"model\":"))
        return false;
    if (!appendJsonString(bodyBuf, bodyCap, &used, (*self).model))
        return false;
    if (!appendLiteral(bodyBuf, bodyCap, &used, ",\"messages\":["))
        return false;
    for (uint32_t i = 0; i < msgCount; i++) {
        const AiMessage *m = &msgs[i];
        if (i > 0 && !appendLiteral(bodyBuf, bodyCap, &used, ","))
            return false;
        if (!appendLiteral(bodyBuf, bodyCap, &used, "{\"role\":"))
            return false;
        if (!appendJsonString(bodyBuf, bodyCap, &used, (*m).role))
            return false;
        if (!appendLiteral(bodyBuf, bodyCap, &used, ",\"content\":"))
            return false;
        if (!appendJsonString(bodyBuf, bodyCap, &used, (*m).content))
            return false;
        if (!appendLiteral(bodyBuf, bodyCap, &used, "}"))
            return false;
    }
    if (!appendLiteral(bodyBuf, bodyCap, &used, "]}"))
        return false;
    return true;
}

bool AiChat_complete(const AiChat *self, const AiMessage *msgs, uint32_t msgCount,
                     char *bodyBuf, size_t bodyCap,
                     HttpResponse *resp) {
    if (!resp)
        return false;
    char urlBuf[512];
    ApiAuth auth = ApiAuth_none();
    if (!AiChat_buildRequest(self, msgs, msgCount, bodyBuf, bodyCap,
                             urlBuf, sizeof(urlBuf), &auth))
        return false;
    return Rest_postJson(urlBuf, &auth, bodyBuf, strlen(bodyBuf), resp);
}

// SETTERS

void AiChat_setProvider(AiChat *self, const AiProvider *provider) {
    if (self)
        (*self).provider = provider;
}

void AiChat_setPeer(AiChat *self, const AiProviderSlot *peer) {
    if (self)
        (*self).peer = peer;
}

void AiChat_setModel(AiChat *self, const char *model) {
    if (self)
        (*self).model = model;
}

void AiChat_setApiKey(AiChat *self, const char *apiKey) {
    if (self)
        (*self).apiKey = apiKey;
}

void AiChat_setBaseUrl(AiChat *self, const char *baseUrlOverride) {
    if (self)
        (*self).baseUrlOverride = baseUrlOverride;
}

// GETTERS

const AiProvider *AiChat_getProvider(const AiChat *self) {
    return self ? (*self).provider : nullptr;
}

const AiProviderSlot *AiChat_getPeer(const AiChat *self) {
    return self ? (*self).peer : nullptr;
}

const char *AiChat_getModel(const AiChat *self) {
    return self ? (*self).model : nullptr;
}

const char *AiChat_getApiKey(const AiChat *self) {
    return self ? (*self).apiKey : nullptr;
}

const char *AiChat_getBaseUrl(const AiChat *self) {
    return self ? (*self).baseUrlOverride : nullptr;
}
