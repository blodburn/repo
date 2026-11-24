/*
 * repo_core — C ABI for the repo writing editor core engine.
 *
 * Shared by the macOS AppKit application (swift/) and the Windows Win32
 * application (windows/). The static library is built with
 *
 *     cargo build --release -p repo-core --target <triple>
 *
 * and links as `librepo_core.a` (macOS) / `repo_core.lib` (MSVC) /
 * `librepo_core.a` (MinGW).
 *
 * ## Buffer protocol
 *
 * Functions that return text never allocate memory owned by the caller. Pass a
 * NULL buffer with a zero length to learn the required size, then call again
 * with a buffer of at least that many bytes. The return value of the functions
 * documented as "returns required size" is the full output size in bytes
 * including the terminating NUL; for the simpler copy helpers it is the number
 * of bytes written excluding the NUL.
 *
 * A negative return value is one of the REPO_ERR_* codes below.
 *
 * ## Record formats
 *
 * repo_extract_objects:  name \t normalized_type \t alias_or_empty \n
 * repo_find_mentions:    object_id \t line_index \t utf16_offset \t byte_offset \t matched_name \n
 *
 * All text is UTF-8. `utf16_offset` counts UTF-16 code units from the start of
 * the document, the unit used by NSRange (macOS) and std::wstring (Windows).
 */
#ifndef REPO_CORE_H
#define REPO_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Error codes                                                               */
/* ------------------------------------------------------------------------- */

#define REPO_OK 0
#define REPO_ERR_NULL (-1)
#define REPO_ERR_PANIC (-2)
#define REPO_ERR_UTF8 (-3)

/* ------------------------------------------------------------------------- */
/* Syntactic line classifications                                            */
/* ------------------------------------------------------------------------- */

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

/* ------------------------------------------------------------------------- */
/* Version and ABI                                                           */
/* ------------------------------------------------------------------------- */

/* Semantic version string of the linked core ("0.4.2"). Static storage. */
const char* repo_core_version(void);

/* Integer ABI revision of this header. */
uint32_t repo_core_abi_version(void);

/* ------------------------------------------------------------------------- */
/* Engine lifecycle                                                          */
/* ------------------------------------------------------------------------- */

RepoEngine* repo_engine_new(void);
void repo_engine_free(RepoEngine* engine);

/* ------------------------------------------------------------------------- */
/* Object registration and queries                                           */
/* ------------------------------------------------------------------------- */

/*
 * Registers an object and writes the core-allocated identifier
 * ("CHAR-0001", "LOC-0001", ...) into out_id_buf.
 * Returns REPO_OK on success, or a negative error code.
 */
int32_t repo_engine_register(
    RepoEngine* engine,
    const char* name,
    const char* category,
    char* out_id_buf,
    size_t out_id_len
);

/*
 * Restores an object with an identifier owned by the caller, typically the
 * project database row id. The identifier is opaque to the core and is echoed
 * back in repo_find_mentions records.
 * Returns 1 when added, 0 when the identifier or name already exists,
 * or a negative error code.
 */
int32_t repo_engine_register_with_id(
    RepoEngine* engine,
    const char* object_id,
    const char* name,
    const char* category
);

/* Adds an alias to a registered object. Returns 1 when added, 0 when not. */
int32_t repo_engine_add_alias(
    RepoEngine* engine,
    const char* object_id,
    const char* alias
);

/* Number of registered objects. */
uint32_t repo_engine_count(const RepoEngine* engine);

/* ------------------------------------------------------------------------- */
/* Line parsing                                                              */
/* ------------------------------------------------------------------------- */

/* Classifies one line. Returns a RepoElementKind value, or a negative error. */
int32_t repo_classify_line(const char* line);

/*
 * Classifies one line and writes its cleaned content (wrapper markers removed,
 * [[object]] markup reduced to the display name) into out_content.
 * Returns the RepoElementKind value, or a negative error.
 * out_content may be NULL when only the classification is needed.
 */
int32_t repo_parse_line(
    const char* line,
    char* out_content,
    size_t out_content_len
);

/*
 * Cleans object markup, e.g. [[김철수]] -> 김철수 and [[장소:서울역]] -> 서울역.
 * Returns bytes written excluding the NUL, or a negative error.
 */
int32_t repo_clean_markup(
    const char* input,
    char* out_buf,
    size_t out_len
);

/* ------------------------------------------------------------------------- */
/* Document analysis                                                         */
/* ------------------------------------------------------------------------- */

/*
 * Lists every explicit [[...]] object marker in the manuscript.
 * Record format: name \t normalized_type \t alias_or_empty \n
 * Returns the required buffer size including the NUL, or a negative error.
 */
int32_t repo_extract_objects(
    const char* manuscript,
    char* out_buf,
    size_t out_len
);

/*
 * Finds every mention of a registered object in the manuscript, using Korean
 * particle aware boundaries and longest-match-first resolution.
 * Record format:
 *     object_id \t line_index \t utf16_offset \t byte_offset \t matched_name \n
 * Returns the required buffer size including the NUL, or a negative error.
 */
int32_t repo_find_mentions(
    const RepoEngine* engine,
    const char* manuscript,
    char* out_buf,
    size_t out_len
);

/* ------------------------------------------------------------------------- */
/* Export                                                                    */
/* ------------------------------------------------------------------------- */

/*
 * Exports the manuscript as clean text: syntax removed, dialogue indented,
 * narration marked, notes omitted when omit_notes is non-zero.
 * Returns bytes written excluding the NUL, or a negative error.
 */
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
