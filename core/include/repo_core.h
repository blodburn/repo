#ifndef REPO_CORE_H
#define REPO_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Syntactic line classifications */
typedef enum RepoElementKind {
    REPO_ELEM_EMPTY = 0,
    REPO_ELEM_SCENE = 1,
    REPO_ELEM_DIALOGUE = 2,
    REPO_ELEM_DESCRIPTION = 3,
    REPO_ELEM_NARRATION = 4,
    REPO_ELEM_NOTE = 5,
    REPO_ELEM_PLAIN_TEXT = 6,
    REPO_ELEM_ERROR = -1
} RepoElementKind;

/* Opaque pointer to the object engine */
typedef struct RepoEngineOpaque RepoEngine;

/* Query semantic version string of the core */
const char* repo_core_version(void);

/* Engine allocation and lifecycle */
RepoEngine* repo_engine_new(void);
void repo_engine_free(RepoEngine* engine);

/* Object registration and queries */
int32_t repo_engine_register(
    RepoEngine* engine,
    const char* name,
    const char* category,
    char* out_id_buf,
    size_t out_id_len
);
uint32_t repo_engine_count(const RepoEngine* engine);

/* Line classification */
int32_t repo_classify_line(const char* line);

/* Clean object markup e.g. [[김철수]] -> 김철수 */
int32_t repo_clean_markup(
    const char* input,
    char* out_buf,
    size_t out_len
);

/* Clean text export without markup symbols */
int32_t repo_export_clean(
    const char* manuscript,
    int32_t omit_notes,
    char* out_buf,
    size_t out_len
);

#ifdef __cplusplus
}
#endif

#endif /* REPO_CORE_H */
