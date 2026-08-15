#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OmniAdblockEngine OmniAdblockEngine;

typedef struct OmniAdblockNetworkResult {
  int should_block;       /* 1 = block */
  int important;          /* 1 = $important */
  char* redirect;         /* resource name or NULL (caller frees) */
  char* rewritten_url;    /* rewritten request URL or NULL (caller frees) */
} OmniAdblockNetworkResult;

typedef struct OmniAdblockCosmeticResult {
  char* hide_css;         /* CSS text or NULL (caller frees) */
  char* injected_script;  /* scriptlets JS or NULL (caller frees) */
  char* exceptions_json;  /* JSON string array of excepted class/id selectors */
  int generichide;        /* 1 = generichide active */
} OmniAdblockCosmeticResult;

/* Create an empty engine. Returns NULL on failure. */
OmniAdblockEngine* omni_adblock_create(void);

void omni_adblock_destroy(OmniAdblockEngine* engine);

/* Replace engine contents from one or more filter list UTF-8 buffers.
 * lists: array of pointers; lengths: parallel sizes; count: number of lists.
 * Returns 0 on success. */
int omni_adblock_load_lists(OmniAdblockEngine* engine,
                            const char* const* lists,
                            const size_t* lengths,
                            size_t count);

/* Load uBO-style resources JSON (array of {name,aliases,kind,content}).
 * Returns 0 on success. */
int omni_adblock_load_resources_json(OmniAdblockEngine* engine,
                                     const char* json,
                                     size_t length);

/* Network check. request_type examples: document, script, image, xhr, ...
 * method: GET/POST/... (NULL or empty → GET). */
OmniAdblockNetworkResult omni_adblock_check_network(OmniAdblockEngine* engine,
                                                    const char* url,
                                                    const char* source_url,
                                                    const char* request_type,
                                                    const char* method);

/* Cosmetic resources for a document URL.
 * hide_css includes element-hide rules plus :style(...) layout fixes. */
OmniAdblockCosmeticResult omni_adblock_cosmetics(OmniAdblockEngine* engine,
                                                 const char* url);

/* Generic class/id hide selectors (Brave MutationObserver path).
 * classes_json / ids_json / exceptions_json are JSON string arrays.
 * Returns CSS text or NULL (caller frees). */
char* omni_adblock_hidden_class_id_css(OmniAdblockEngine* engine,
                                       const char* classes_json,
                                       const char* ids_json,
                                       const char* exceptions_json);

/* Resolve a redirect resource name to a data: URL (or NULL). Caller frees. */
char* omni_adblock_resource_data_url(OmniAdblockEngine* engine,
                                     const char* name);

void omni_adblock_string_free(char* s);
void omni_adblock_network_result_free(OmniAdblockNetworkResult* r);
void omni_adblock_cosmetic_result_free(OmniAdblockCosmeticResult* r);

#ifdef __cplusplus
}
#endif
