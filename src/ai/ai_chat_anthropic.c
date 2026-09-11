#include "annotation/intention.h"
#include "annotation/overview.h"
#include "ai/ai_chat_anthropic.h"
#include "api/rest.h"
#include "net/json.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: AiChatAnthropic (ai/ai_chat_anthropic)
 * LEVEL: L2 — Behavior (class API surface: constructors, core, setters,
 * getters; consumes the L1 AiProvider directory + api/rest core)
 * ============================================================================
 * Anthropic Messages chat client — Shape-A sister of AiChat. Resolves
 * the selected provider row to a native endpoint (Anthropic family
 * default https://api.anthropic.com/v1 when the row carries no verified
 * base), renders the Messages envelope (model, max_tokens, messages[])
 * into caller-owned scratch, and sends it via Rest_postJson. Auth rides
 * the x-api-key header. Zero allocation on every path. The AiMessage
 * turn record is borrowed from ai/ai_chat.h (slot-record reuse — no
 * second turn struct, Rule 3 stays green).
 *
 * STRUCT FIELDS (Mirroring ai/ai_chat_anthropic.h — exactly this
 * file's class):
 * ----------------------------------------------------------------------------
 *   const AiProvider *provider;  // directory handle; NULL = shared default
 *   const AiProviderSlot *peer;  // selected provider row; NULL = gateway
 *   const char *model;           // e.g. "claude-sonnet-4-5"; must be set
 *   const char *apiKey;          // x-api-key credential; NULL = no auth
 *   const char *baseUrlOverride; // wins over peer base / family default
 *   uint32_t maxTokens;          // token ceiling (default 1024)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors (value structs — ApiAuth precedent):
 *   - AiChatAnthropic_0()
 *   - AiChatAnthropic_1(model)
 *   - AiChatAnthropic_2(model, apiKey)
 *
 * Core Functions:
 *   - AiChatAnthropic_buildRequest(self, msgs, msgCount, bodyBuf, bodyCap,
 *                                  urlBuf, urlCap, authOut) : render-only
 *   - AiChatAnthropic_complete(self, msgs, msgCount, bodyBuf, bodyCap, resp)
 *                                                    : render + send
 *
 * Setters:
 *   - AiChatAnthropic_setProvider / setPeer / setModel / setApiKey /
 *     setBaseUrl / setMaxTokens
 *
 * Getters:
 *   - AiChatAnthropic_getProvider / getPeer / getModel / getApiKey /
 *     getBaseUrl / getMaxTokens
 * ============================================================================
 */
;;INTENTION("Anthropic Messages gets its own class pair — promised by the ;;INTENTION in ai_chat.c; a mode flag on AiChat would fake the wire contract and the two-layer cap")

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

AiChatAnthropic AiChatAnthropic_0(void) {
    AiChatAnthropic chat;
    memset(&chat, 0, sizeof(chat));
    chat.maxTokens = AI_CHAT_ANTHROPIC_DEFAULT_MAX_TOKENS;
    return chat;
}

AiChatAnthropic AiChatAnthropic_1(const char *model) {
    AiChatAnthropic chat = AiChatAnthropic_0();
    // provider borrows the shared singleton so directory calls are legal.
    chat.provider = AiProvider_shared();
    chat.model = model;
    return chat;
}

AiChatAnthropic AiChatAnthropic_2(const char *model, const char *apiKey) {
    AiChatAnthropic chat = AiChatAnthropic_1(model);
    chat.apiKey = apiKey;
    return chat;
}

// CORE FUNCTIONS

bool AiChatAnthropic_buildRequest(const AiChatAnthropic *self, const AiMessage *msgs,
                                  uint32_t msgCount, char *bodyBuf, size_t bodyCap,
                                  char *urlBuf, size_t urlCap, ApiAuth *authOut) {
    if (!self || !bodyBuf || !urlBuf || !authOut || !(*self).model)
        return false;
    if (!msgs && msgCount > 0)
        return false;
    if (urlCap == 0 || bodyCap == 0)
        return false;
    if ((*self).maxTokens == 0)
        return false;

    // Resolve endpoint: override > peer native base > Anthropic default.
    const char *base = "https://api.anthropic.com/v1";
    if ((*self).baseUrlOverride)
        base = (*self).baseUrlOverride;
    else if ((*self).peer) {
        const AiProvider *table = (*self).provider ? (*self).provider : AiProvider_shared();
        const char *resolved = AiProvider_resolveBaseUrl(table, (*self).peer);
        if (resolved)
            base = resolved;
    }

    const int printed = snprintf(urlBuf, urlCap, "%s/v1/messages", base);
    if (printed < 0 || (size_t)printed >= urlCap)
        return false;

    // Credential: Anthropic wire takes x-api-key, never Bearer.
    if ((*self).apiKey)
        (*authOut) = ApiAuth_apiKey("x-api-key", (*self).apiKey);
    else
        (*authOut) = ApiAuth_none();

    // Render {"model":"...","max_tokens":N,"messages":[{...},...]}
    size_t used = 0;
    if (!appendLiteral(bodyBuf, bodyCap, &used, "{\"model\":"))
        return false;
    if (!appendJsonString(bodyBuf, bodyCap, &used, (*self).model))
        return false;
    char tokenBuf[32];
    const int tw = snprintf(tokenBuf, sizeof(tokenBuf), ",\"max_tokens\":%u,\"messages\":[",
                            (*self).maxTokens);
    if (tw < 0 || (size_t)tw >= sizeof(tokenBuf))
        return false;
    if (!appendLiteral(bodyBuf, bodyCap, &used, tokenBuf))
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

bool AiChatAnthropic_complete(const AiChatAnthropic *self, const AiMessage *msgs,
                               uint32_t msgCount, char *bodyBuf, size_t bodyCap,
                               HttpResponse *resp) {
    if (!resp)
        return false;
    char urlBuf[512];
    ApiAuth auth = ApiAuth_none();
    if (!AiChatAnthropic_buildRequest(self, msgs, msgCount, bodyBuf, bodyCap,
                                      urlBuf, sizeof(urlBuf), &auth))
        return false;
    return Rest_postJson(urlBuf, &auth, bodyBuf, strlen(bodyBuf), resp);
}

// SETTERS

void AiChatAnthropic_setProvider(AiChatAnthropic *self, const AiProvider *provider) {
    if (self)
        (*self).provider = provider;
}

void AiChatAnthropic_setPeer(AiChatAnthropic *self, const AiProviderSlot *peer) {
    if (self)
        (*self).peer = peer;
}

void AiChatAnthropic_setModel(AiChatAnthropic *self, const char *model) {
    if (self)
        (*self).model = model;
}

void AiChatAnthropic_setApiKey(AiChatAnthropic *self, const char *apiKey) {
    if (self)
        (*self).apiKey = apiKey;
}

void AiChatAnthropic_setBaseUrl(AiChatAnthropic *self, const char *baseUrlOverride) {
    if (self)
        (*self).baseUrlOverride = baseUrlOverride;
}

void AiChatAnthropic_setMaxTokens(AiChatAnthropic *self, uint32_t maxTokens) {
    if (self)
        (*self).maxTokens = maxTokens;
}

// GETTERS

const AiProvider *AiChatAnthropic_getProvider(const AiChatAnthropic *self) {
    return self ? (*self).provider : NULL;
}

const AiProviderSlot *AiChatAnthropic_getPeer(const AiChatAnthropic *self) {
    return self ? (*self).peer : NULL;
}

const char *AiChatAnthropic_getModel(const AiChatAnthropic *self) {
    return self ? (*self).model : NULL;
}

const char *AiChatAnthropic_getApiKey(const AiChatAnthropic *self) {
    return self ? (*self).apiKey : NULL;
}

const char *AiChatAnthropic_getBaseUrl(const AiChatAnthropic *self) {
    return self ? (*self).baseUrlOverride : NULL;
}

uint32_t AiChatAnthropic_getMaxTokens(const AiChatAnthropic *self) {
    if (!self)
        return 0;
    return (*self).maxTokens;
}
