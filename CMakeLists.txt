cmake_minimum_required(VERSION 3.28)
project(api_haven C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Release: dead-code stripping so unused symbols cost zero bytes in artifacts.
# (Duplicated per repo for standalone builds; harmless when built in-tree.)
if(APPLE)
    add_compile_options($<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    add_link_options($<$<CONFIG:Release>:-Wl,-dead_strip>)
elseif(UNIX)
    add_compile_options($<$<CONFIG:Release>:-ffunction-sections> $<$<CONFIG:Release>:-fdata-sections>)
    add_link_options($<$<CONFIG:Release>:-Wl,--gc-sections>)
endif()

# C-transition seam for the Java API surface at the repo root
# (APIClient.java, com/discord/DiscordWebhook.java).
#
# The Java originals speak the old engine stack (net/HTTPClient, net/JSON,
# primitive/string, nio/ForeignMemory, telemetry/CrashDumper). Their C ports
# land in src/api/ and speak vexspoke instead: src/net (http, json,
# netfacade, url, tls), nio/mem, primitive/string.
#
# Header-only until the first port lands; switch to STATIC with sources
# as files arrive.
if(NOT TARGET api_haven)
add_library(api_haven STATIC
    src/api/client.c
    src/api/auth.c
    src/api/rest.c
    src/api/haven_ws_fanout.c
    src/com/discord/discord.c
    src/com/slack/slack.c
    src/ai/ai_provider.c
    src/ai/ai_chat.c
    src/ai/ai_chat_anthropic.c
    src/ai/ai_chat_gemini.c
    src/ai/ai_sse.c
    src/asset/asset_provider.c
    src/asset/asset_broker.c
    src/database/db_provider.c
    src/database/db_sqlite_file.c
    src/harness/engine_provider.c
    src/harness/harness.c
    src/app/app_provider.c
    src/app/app_broker.c
    src/search/search_provider.c
    src/mcp/mcp_server.c
)
target_include_directories(api_haven PUBLIC src)
target_compile_options(api_haven PRIVATE -Wall -Wextra -Werror)

if(TARGET vexspoke)
    target_link_libraries(api_haven PUBLIC vexspoke)
else()
    option(API_HAVEN_USE_VEXSPOKE "Depend on vexspoke core via FetchContent" OFF)
    if(API_HAVEN_USE_VEXSPOKE)
        include(FetchContent)
        FetchContent_Declare(
            vexspoke
            GIT_REPOSITORY https://github.com/vexgraph-dev/vexspoke.git
            GIT_TAG main
        )
        FetchContent_MakeAvailable(vexspoke)
        target_include_directories(api_haven PUBLIC ${vexspoke_SOURCE_DIR}/src)
        target_link_libraries(api_haven PUBLIC vexspoke)
    endif()
endif()
endif() # NOT TARGET api_haven

# Port queue (Java => C, in dependency order):
# 1. src/api/client.c — APIClient.sendTelemetry (needs net/http + net/json)
# 2. src/api/discord.c — DiscordWebhook sendText/sendEmbed/sendRichEmbed/
#    sendEmbedWithFile/sendTextFile/sendFile + transmit() (needs net/http
#    request with headers + bodyLen, io/file for attachments)

# Shape-A/B loopback proof: headless, no accounts (server on 127.0.0.1).
if(NOT TARGET webhook_loopback_test)
    add_executable(webhook_loopback_test
        src/tests/webhook_loopback_test.c
    )
    target_link_libraries(webhook_loopback_test PRIVATE api_haven)
endif()

# src/ai directory proof: provider table + OpenAI-compatible envelope.
# Headless: no network; buildRequest only (see ai_provider_test.c overview).
if(NOT TARGET ai_provider_test)
    add_executable(ai_provider_test
        src/ai/tests/ai_provider_test.c
    )
    target_link_libraries(ai_provider_test PRIVATE api_haven)
endif()

# src/ai Shape-A sisters proof: Anthropic Messages + Gemini generateContent
# envelopes, headless buildRequest only (see ai_chat_sisters_test.c overview).
if(NOT TARGET ai_chat_sisters_test)
    add_executable(ai_chat_sisters_test
        src/ai/tests/ai_chat_sisters_test.c
    )
    target_link_libraries(ai_chat_sisters_test PRIVATE api_haven)
endif()

# src/ai SSE proof: incremental data:/event: decoder, caller-fed only.
# Headless: no sockets, no threads (see ai_sse_test.c overview).
if(NOT TARGET ai_sse_test)
    add_executable(ai_sse_test
        src/ai/tests/ai_sse_test.c
    )
    target_link_libraries(ai_sse_test PRIVATE api_haven)
endif()

# src/asset directory proof: blessed source descriptors + bounded
# chunked-copy broker. Headless: no network, no scraping, no exec
# (see asset_test.c overview).
if(NOT TARGET asset_test)
    add_executable(asset_test
        src/asset/tests/asset_test.c
    )
    target_link_libraries(asset_test PRIVATE api_haven)
endif()

# src/database directory proof: data-source descriptors, no drivers.
if(NOT TARGET db_provider_test)
    add_executable(db_provider_test
        src/database/tests/db_provider_test.c
    )
    target_link_libraries(db_provider_test PRIVATE api_haven)
endif()

# src/database SQLite file proof: catalog-only read-only descriptor +
# opaque delegation, zero sqlite3.h (see db_sqlite_file_test.c overview).
if(NOT TARGET db_sqlite_file_test)
    add_executable(db_sqlite_file_test
        src/database/tests/db_sqlite_file_test.c
    )
    target_link_libraries(db_sqlite_file_test PRIVATE api_haven)
endif()

# Seam proof: vexspoke R1 probes (AppDetect, CaptureTool, ProcessProbe)
# are consumed here for CLI/MCP work — never re-implemented (Rule 17).
if(NOT TARGET probe_consumer_test)
    add_executable(probe_consumer_test
        src/tests/probe_consumer_test.c
    )
    target_link_libraries(probe_consumer_test PRIVATE api_haven)
endif()

# Model Context Protocol: stdio JSON-RPC server executable + headless
# protocol conformance test (mcp/mcp_server.c engine, mcp_main.c runner).
if(NOT TARGET mcp_server)
    add_executable(mcp_server
        src/main/mcp_main.c
    )
    target_link_libraries(mcp_server PRIVATE api_haven)
endif()

if(NOT TARGET mcp_server_test)
    add_executable(mcp_server_test
        src/mcp/tests/mcp_server_test.c
    )
    target_link_libraries(mcp_server_test PRIVATE api_haven)
endif()
