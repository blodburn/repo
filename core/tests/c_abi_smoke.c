/*
 * C ABI smoke test for core/include/repo_core.h.
 *
 * Compiled and executed by CI on macOS and Windows against the static library,
 * and can be run locally:
 *
 *     cargo build --release -p repo-core
 *     cc core/tests/c_abi_smoke.c -Icore/include -Ltarget/release -lrepo_core -o smoke
 *     ./smoke
 *
 * It proves the header compiles as C, that every symbol the GUIs call exists in
 * the archive, and that the two step buffer protocol behaves as documented.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "repo_core.h"

static int failures = 0;

static void check(int condition, const char* what) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* Reads a tab separated record field. */
static void field(const char* record, int index, char* out, size_t out_len) {
    const char* start = record;
    for (int i = 0; i < index && start; ++i) {
        start = strchr(start, '\t');
        if (start) start++;
    }
    out[0] = '\0';
    if (!start) return;
    size_t n = strcspn(start, "\t\n");
    if (n >= out_len) n = out_len - 1;
    memcpy(out, start, n);
    out[n] = '\0';
}

int main(void) {
    const char* version = repo_core_version();
    check(version != NULL && strcmp(version, "0.4.2") == 0, "repo_core_version()");
    check(repo_core_abi_version() == 1, "repo_core_abi_version()");

    /* Line classification and cleaned content in one call. */
    char content[256];
    check(repo_classify_line("# INT. 병원 복도 - 밤") == REPO_ELEM_SCENE, "classify scene");
    check(repo_parse_line("@@ 안녕하세요 @@", content, sizeof(content)) == REPO_ELEM_DIALOGUE,
          "parse dialogue kind");
    check(strcmp(content, "안녕하세요") == 0, "parse dialogue content");
    check(repo_parse_line("## [[김철수]]가 문을 연다 ##", content, sizeof(content)) ==
              REPO_ELEM_DESCRIPTION,
          "parse description kind");
    check(strcmp(content, "김철수가 문을 연다") == 0, "parse description markup");
    check(repo_classify_line(NULL) == REPO_ERR_NULL, "NULL line is rejected");

    /* Two step buffer protocol for explicit objects. */
    const char* manuscript =
        "# 장면\n[[김철수]]가 들어온다\n[[장소:서울역]]\n[[철수|김철수]]\n"
        "두 번째 줄: 김철수는 웃었다.\n";
    int32_t needed = repo_extract_objects(manuscript, NULL, 0);
    check(needed > 0, "extract_objects probe");
    char* objects = (char*)malloc((size_t)needed);
    check(objects != NULL, "malloc");
    if (!objects) return 1;
    check(repo_extract_objects(manuscript, objects, (size_t)needed) == needed, "extract_objects fill");

    char field_value[128];
    field(objects, 1, field_value, sizeof(field_value));
    check(strcmp(field_value, "character") == 0, "extract_objects type field");
    int lines = 0;
    for (char* p = objects; *p; ++p) if (*p == '\n') lines++;
    check(lines == 3, "extract_objects record count");
    free(objects);

    /* Engine with caller owned identifiers, then mention scanning. */
    RepoEngine* engine = repo_engine_new();
    check(engine != NULL, "repo_engine_new");
    if (!engine) return 1;

    check(repo_engine_register_with_id(engine, "17", "김철수", "character") == 1,
          "register_with_id character");
    check(repo_engine_register_with_id(engine, "42", "서울역", "location") == 1,
          "register_with_id location");
    check(repo_engine_register_with_id(engine, "17", "김철수", "character") == 0,
          "duplicate register_with_id is reported");
    check(repo_engine_count(engine) == 2, "repo_engine_count");
    check(repo_engine_add_alias(engine, "17", "철수") == 1, "repo_engine_add_alias");

    needed = repo_find_mentions(engine, manuscript, NULL, 0);
    check(needed > 0, "find_mentions probe");
    char* mentions = (char*)malloc((size_t)needed);
    check(mentions != NULL, "malloc mentions");
    if (!mentions) return 1;
    check(repo_find_mentions(engine, manuscript, mentions, (size_t)needed) == needed,
          "find_mentions fill");

    /* First record is the [[김철수]] mention on line 1 ("# 장면" is line 0);
       the name starts after "[[" so its UTF-16 offset is 7. */
    field(mentions, 0, field_value, sizeof(field_value));
    check(strcmp(field_value, "17") == 0, "mention object_id is the caller owned id");
    field(mentions, 1, field_value, sizeof(field_value));
    check(strcmp(field_value, "1") == 0, "mention line index");
    field(mentions, 2, field_value, sizeof(field_value));
    check(strcmp(field_value, "7") == 0, "mention utf16 offset");
    field(mentions, 4, field_value, sizeof(field_value));
    check(strcmp(field_value, "김철수") == 0, "mention matched name");
    free(mentions);

    repo_engine_free(engine);

    /* Clean text export: markers are gone and dialogue is indented. */
    char exported[1024];
    const char* export_source = "# 장면\n@@ 안녕하세요 @@\n## [[김철수]]가 문을 연다 ##\n";
    check(repo_export_clean(export_source, 1, exported, sizeof(exported)) > 0, "repo_export_clean");
    check(strstr(exported, "    안녕하세요") != NULL, "export dialogue indentation");
    check(strstr(exported, "김철수가 문을 연다") != NULL, "export cleans object markup");

    if (failures == 0) {
        printf("repo-core C ABI smoke test passed (core %s, abi %u)\n", version,
               repo_core_abi_version());
        return 0;
    }
    fprintf(stderr, "%d C ABI checks failed\n", failures);
    return 1;
}
