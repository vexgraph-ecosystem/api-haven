#include "annotation/overview.h"
#include "database/db_sqlite_file.h"

#include <stdio.h>
#include <string.h>

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: DbSqliteFileTest (src/database/tests/db_sqlite_file_test.c)
 * LEVEL: L2 — Behavior verification (headless; descriptors + opaque
 * delegation only, zero sqlite3.h, no drivers, no filesystem)
 * ============================================================================
 * Executable proof of the DbSqliteFile class: read-only defaults,
 * path/caps/bounds setters, describe rendering, bind/unbind of a fake
 * owner table, delegated exec, unbound drop-degrade, and the full
 * cold-seam matrix (nullptr, empty, zero cap, overflow) per Rule 35.4.
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

static bool fakeExec(void *handle, const char *sql, char *out, size_t outCap) {
    if (!handle || !sql || !out || outCap == 0)
        return false;
    int *calls = (int*) handle;
    (*calls)++;
    const int n = snprintf(out, outCap, "rows for: %s", sql);
    if (n <= 0 || (size_t)n >= outCap)
        return false;
    return true;
}

static const DbSqliteExec sFakeTable = { fakeExec };

int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    // --- read-only defaults ----------------------------------------------------------
    DbSqliteFile bare = DbSqliteFile_0();
    CHECK(DbSqliteFile_isReadOnly(&bare));
    CHECK(DbSqliteFile_getPath(&bare) == NULL);
    CHECK(DbSqliteFile_getCaps(&bare) == 0);
    CHECK(DbSqliteFile_getMaxBytes(&bare) == 0);
    CHECK(DbSqliteFile_getExecHandle(&bare) == NULL);
    CHECK(DbSqliteFile_getExecTable(&bare) == NULL);

    DbSqliteFile file = DbSqliteFile_1("/tmp/catalog.db");
    CHECK(DbSqliteFile_isReadOnly(&file));
    CHECK(strcmp(DbSqliteFile_getPath(&file), "/tmp/catalog.db") == 0);
    CHECK((DbSqliteFile_getCaps(&file) & DB_SQLITE_CAP_READ) != 0);

    // --- describe rendering -----------------------------------------------------------------
    char desc[256];
    DbSqliteFile_setMaxBytes(&file, 1048576);
    CHECK(DbSqliteFile_describe(&file, desc, sizeof(desc)));
    CHECK(strstr(desc, "sqlite file: path=/tmp/catalog.db") != NULL);
    CHECK(strstr(desc, "readOnly=1") != NULL);
    CHECK(strstr(desc, "maxBytes=1048576") != NULL);
    CHECK(!DbSqliteFile_describe(&file, desc, 8)); // overflow
    CHECK(!DbSqliteFile_describe(&bare, desc, sizeof(desc))); // undescribed
    CHECK(!DbSqliteFile_describe(NULL, desc, sizeof(desc)));
    CHECK(!DbSqliteFile_describe(&file, NULL, sizeof(desc)));
    CHECK(!DbSqliteFile_describe(&file, desc, 0));

    // --- unbound exec drop-degrades -----------------------------------------------------------------
    char out[256];
    CHECK(!DbSqliteFile_exec(&file, "SELECT 1", out, sizeof(out)));

    // --- bind + delegated exec ----------------------------------------------------------------------------
    int calls = 0;
    CHECK(DbSqliteFile_getExecHandle(&file) == NULL);
    CHECK(DbSqliteFile_bind(&file, &calls, &sFakeTable));
    CHECK(DbSqliteFile_getExecHandle(&file) == &calls);
    CHECK(DbSqliteFile_getExecTable(&file) == &sFakeTable);
    CHECK(DbSqliteFile_exec(&file, "SELECT 1", out, sizeof(out)));
    CHECK(calls == 1);
    CHECK(strcmp(out, "rows for: SELECT 1") == 0);
    CHECK(!DbSqliteFile_bind(&file, &calls, &sFakeTable)); // rebind refused
    int other = 0;
    CHECK(!DbSqliteFile_bind(&file, &other, &sFakeTable)); // unbind first
    DbSqliteFile_unbind(&file);
    CHECK(DbSqliteFile_getExecHandle(&file) == NULL);
    CHECK(!DbSqliteFile_exec(&file, "SELECT 1", out, sizeof(out)));
    CHECK(!DbSqliteFile_bind(&file, NULL, &sFakeTable));
    CHECK(!DbSqliteFile_bind(&file, &calls, NULL));
    CHECK(!DbSqliteFile_bind(NULL, &calls, &sFakeTable));
    static const DbSqliteExec nullTable = { NULL };
    CHECK(!DbSqliteFile_bind(&file, &calls, &nullTable)); // null fn refused
    DbSqliteFile_unbind(NULL); // null-safe no-op

    // --- exec guard matrix --------------------------------------------------------------------------------------
    CHECK(DbSqliteFile_bind(&file, &calls, &sFakeTable));
    CHECK(!DbSqliteFile_exec(NULL, "SELECT 1", out, sizeof(out)));
    CHECK(!DbSqliteFile_exec(&file, NULL, out, sizeof(out)));
    CHECK(!DbSqliteFile_exec(&file, "", out, sizeof(out)));
    CHECK(!DbSqliteFile_exec(&file, "SELECT 1", NULL, sizeof(out)));
    CHECK(!DbSqliteFile_exec(&file, "SELECT 1", out, 0));

    // --- setters round-trip --------------------------------------------------------------------------------------
    DbSqliteFile st = DbSqliteFile_0();
    DbSqliteFile_setPath(&st, "/data/app.db");
    DbSqliteFile_setCaps(&st, DB_SQLITE_CAP_READ);
    DbSqliteFile_setMaxBytes(&st, 4096);
    DbSqliteFile_setReadOnly(&st, true);
    CHECK(strcmp(DbSqliteFile_getPath(&st), "/data/app.db") == 0);
    CHECK(DbSqliteFile_getCaps(&st) == DB_SQLITE_CAP_READ);
    CHECK(DbSqliteFile_getMaxBytes(&st) == 4096);
    CHECK(DbSqliteFile_isReadOnly(&st));
    DbSqliteFile_setPath(NULL, "x");
    DbSqliteFile_setCaps(NULL, 1);
    DbSqliteFile_setMaxBytes(NULL, 1);
    DbSqliteFile_setReadOnly(NULL, false);
    CHECK(DbSqliteFile_getPath(NULL) == NULL);
    CHECK(DbSqliteFile_getCaps(NULL) == 0);
    CHECK(DbSqliteFile_getMaxBytes(NULL) == 0);
    CHECK(!DbSqliteFile_isReadOnly(NULL));
    CHECK(DbSqliteFile_getExecHandle(NULL) == NULL);
    CHECK(DbSqliteFile_getExecTable(NULL) == NULL);

    if (sFailures == 0) {
        printf("db_sqlite_file_test: ALL CHECKS PASSED\n");
        return 0;
    }
    printf("db_sqlite_file_test: %d FAILURES\n", sFailures);
    return 1;
}
