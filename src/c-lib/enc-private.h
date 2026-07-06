/**
 * GS1 Barcode Syntax Engine
 *
 * @author Copyright (c) 2021-2026 GS1 AISBL.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef ENC_PRIVATE_H
#define ENC_PRIVATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "gs1encoders.h"


// Implementation limits that can be changed
#define MAX_DATA	8191	// Maximum input buffer size


#ifdef _MSC_VER
#include <malloc.h>
#define strtok_r strtok_s
#define ssize_t ptrdiff_t
#define alloca _alloca
#else
#  include <sys/types.h>			// IWYU pragma: export
#  if (defined(__GNUC__) && !defined(alloca) && !defined(__NetBSD__)) || defined(__NuttX__) || defined(_AIX) \
        || (defined(__sun) && defined(__SVR4) /*Solaris*/)
#    include <alloca.h>				// IWYU pragma: export
#  endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#define __ATTR_CONST __attribute__ ((__const__))
#define __ATTR_PURE __attribute__ ((__pure__))
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#elif _MSC_VER
#define __ATTR_CONST __declspec(noalias)
#define __ATTR_PURE
#define likely(x) (x)
#define unlikely(x) (x)
#else
#define __ATTR_CONST
#define __ATTR_PURE
#define likely(x) (x)
#define unlikely(x) (x)
#endif

#if defined(__clang__)
#  define DIAG_PUSH _Pragma("clang diagnostic push")
#  define DIAG_POP _Pragma("clang diagnostic pop")
#  define DIAG_DISABLE_DEPRECATED_DECLARATIONS _Pragma("clang diagnostic ignored \"-Wdeprecated-declarations\"")
#elif defined(__GNUC__)
#  define DIAG_PUSH _Pragma("GCC diagnostic push")
#  define DIAG_POP _Pragma("GCC diagnostic pop")
#  define DIAG_DISABLE_DEPRECATED_DECLARATIONS _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#elif defined(_MSC_VER)
#  define DIAG_PUSH __pragma(warning(push))
#  define DIAG_POP __pragma(warning(pop))
#  define DIAG_DISABLE_DEPRECATED_DECLARATIONS __pragma(warning(disable: 4996))
#  define DIAG_DISABLE_ANALYZER
#endif


#ifdef GS1_ENCODERS_CUSTOM_HEAP_MANAGEMENT_H
#define xstr(s) str(s)
#define str(s) #s
#include xstr(GS1_ENCODERS_CUSTOM_HEAP_MANAGEMENT_H)
#endif

#ifdef GS1_ENCODERS_CUSTOM_HEAP_MANAGEMENT_H
#define GS1_ENCODERS_MALLOC(sz) GS1_ENCODERS_CUSTOM_MALLOC(sz)
#define GS1_ENCODERS_CALLOC(nm, sz) GS1_ENCODERS_CUSTOM_CALLOC(nm, sz)
#define GS1_ENCODERS_REALLOC(p, sz) GS1_ENCODERS_CUSTOM_REALLOC(p, sz)
#define GS1_ENCODERS_FREE(p) GS1_ENCODERS_CUSTOM_FREE(p)
#else
#define GS1_ENCODERS_MALLOC(sz) malloc(sz)
#define GS1_ENCODERS_CALLOC(nm, sz) calloc(nm, sz)
#define GS1_ENCODERS_REALLOC(p, sz) realloc(p, sz)
#define GS1_ENCODERS_FREE(p) free(p)
#endif


/*
 *  The whole gs1_encoder is one allocation, so ASan cannot see an overrun from
 *  one internal buffer into the next field (its redzones sit only between
 *  allocations). Under ASan we insert poisoned guard regions after the large
 *  scratch buffers; an overrun is caught once it reaches the guard (8-byte
 *  shadow granularity). No effect on non-sanitiser builds.
 *
 */
#if defined(__SANITIZE_ADDRESS__)
#  define GS1_ENCODERS_HAVE_ASAN
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define GS1_ENCODERS_HAVE_ASAN
#  endif
#endif

// Structure-agnostic primitives. A struct declares a guard after each protected
// buffer with GS1_ENCODERS_ASAN_GUARD(), and lists them once as an X-macro:
//   #define FOO_GUARDS(GUARD, s)  GUARD(s, buf1) GUARD(s, buf2) ...
// over which GS1_ENCODERS_{POISON,UNPOISON}_GUARDS() expand for a given instance.
#ifdef GS1_ENCODERS_HAVE_ASAN
#include <sanitizer/asan_interface.h>
#define GS1_ENCODERS_ASAN_GUARD(field)			char field##_guard[64];
#define GS1_ENCODERS_ASAN_POISON_GUARD(s,   field)	ASAN_POISON_MEMORY_REGION((s)->field##_guard,   sizeof((s)->field##_guard));
#define GS1_ENCODERS_ASAN_UNPOISON_GUARD(s, field)	ASAN_UNPOISON_MEMORY_REGION((s)->field##_guard, sizeof((s)->field##_guard));
#define GS1_ENCODERS_POISON_GUARDS(guards,   s)		do { guards(GS1_ENCODERS_ASAN_POISON_GUARD,   s) } while (0)
#define GS1_ENCODERS_UNPOISON_GUARDS(guards, s)		do { guards(GS1_ENCODERS_ASAN_UNPOISON_GUARD, s) } while (0)
#else
#define GS1_ENCODERS_ASAN_GUARD(field)
#define GS1_ENCODERS_POISON_GUARDS(guards,   s)		((void)0)
#define GS1_ENCODERS_UNPOISON_GUARDS(guards, s)		((void)0)
#endif


#define GS1_SEARCH_INVALID   (-2)
#define GS1_SEARCH_NOT_FOUND (-1)

#define SIZEOF_ARRAY(x) (sizeof(x) / sizeof(x[0]))
#define SIZEOF_FIELD(t, f) sizeof(((t *)0)->f)

// Portable compile-time assertion
#define GS1_ENCODERS_STATIC_ASSERT_(cond, id)	typedef char gs1_encoders_static_assert_##id[(cond) ? 1 : -1]
#define GS1_ENCODERS_STATIC_ASSERT__(cond, id)	GS1_ENCODERS_STATIC_ASSERT_(cond, id)
#define GS1_ENCODERS_STATIC_ASSERT(cond)	GS1_ENCODERS_STATIC_ASSERT__(cond, __LINE__)


#include "ai.h"


/*
 *  May be stabilised as part of the public API in the future.
 *
 */
typedef enum {
	gs1_encoder_eNO_ERROR = 0,
	gs1_encoder_eAI_TABLE_BROKEN_PREFIXES_DIFFER_IN_LENGTH,
	gs1_encoder_eAI_DATA_IS_EMPTY,
	gs1_encoder_eAI_DATA_HAS_INCORRECT_LENGTH,
	gs1_encoder_eAI_LINTER_ERROR,
	gs1_encoder_eAI_VALUE_IS_TOO_SHORT,
	gs1_encoder_eAI_VALUE_IS_TOO_LONG,
	gs1_encoder_eAI_CONTAINS_ILLEGAL_CARAT_CHARACTER,
	gs1_encoder_eAI_UNRECOGNISED,
	gs1_encoder_eTOO_MANY_AIS,
	gs1_encoder_eAI_PARSE_FAILED,
	gs1_encoder_eMISSING_FNC1_IN_FIRST_POSITION,
	gs1_encoder_eAI_DATA_EMPTY,
	gs1_encoder_eNO_AI_FOR_PREFIX,
	gs1_encoder_eAI_DATA_IS_TOO_LONG,
	gs1_encoder_eINVALID_AI_PAIRS,
	gs1_encoder_eREQUIRED_AIS_NOT_SATISFIED,
	gs1_encoder_eINSTANCES_OF_AI_HAVE_DIFFERENT_VALUES,
	gs1_encoder_eSERIAL_NOT_PRESENT,
	gs1_encoder_eFAILED_TO_REALLOC_FOR_KEY_QUALIFIERS,
	gs1_encoder_eFAILED_TO_MALLOC_FOR_KEY_QUALIFIERS,
	gs1_encoder_eTOO_MANY_DL_KEY_QUALIFIERS,
	gs1_encoder_eURI_CONTAINS_ILLEGAL_CHARACTERS,
	gs1_encoder_eURI_CONTAINS_ILLEGAL_SCHEME,
	gs1_encoder_eURI_MISSING_DOMAIN_AND_PATH_INFO,
	gs1_encoder_eNO_GS1_DL_KEYS_FOUND_IN_PATH_INFO,
	gs1_encoder_eAI_VALUE_PATH_ELEMENT_IS_EMPTY,
	gs1_encoder_eDECODED_AI_FROM_DL_PATH_INFO_CONTAINS_ILLEGAL_NULL,
	gs1_encoder_eUNKNOWN_AI_IN_QUERY_PARAMS,
	gs1_encoder_eAI_VALUE_QUERY_ELEMENT_IN_EMPTY,
	gs1_encoder_eDECODED_AI_VALUE_FROM_QUERY_PARAMS_CONTAINS_ILLEGAL_NULL,
	gs1_encoder_eINVALID_KEY_QUALIFIER_SEQUENCE,
	gs1_encoder_eDUPLICATE_AI,
	gs1_encoder_eAI_IS_NOT_VALID_DATA_ATTRIBUTE,
	gs1_encoder_eAI_SHOULD_BE_IN_PATH_INFO,
	gs1_encoder_eDL_URI_PARSE_FAILED,
	gs1_encoder_eCANNOT_CREATE_DL_URI_WITHOUT_PRIMARY_KEY_AI,
	gs1_encoder_eDL_URI_TOO_LONG,
	gs1_encoder_eUNKNOWN_SYMBOLOGY,
	gs1_encoder_eUNKNOWN_VALIDATION,
	gs1_encoder_eVALIDATION_CANNOT_BE_AMENDED,
	gs1_encoder_eDATA_TOO_LONG,
	gs1_encoder_ePRIMARY_DATA_MUST_BE_N_DIGITS_WITHOUT_CHECK_DIGIT,
	gs1_encoder_ePRIMARY_DATA_MUST_BE_N_DIGITS,
	gs1_encoder_ePRIMARY_DATA_MUST_BE_ALL_DIGITS,
	gs1_encoder_ePRIMARY_DATA_CHECK_DIGIT_IS_INCORRECT,
	gs1_encoder_ePRIMARY_DATA_IS_TOO_LARGE,
	gs1_encoder_eMISSING_SYMBOLOGY_IDENTIFIER,
	gs1_encoder_eUNSUPPORTED_SYMBOLOGY_IDENTIFIER,
	gs1_encoder_ePRIMARY_SCAN_DATA_IS_TOO_SHORT,
	gs1_encoder_ePRIMARY_MESSAGE_IS_TOO_LONG,
	gs1_encoder_ePRIMARY_MESSAGE_MAY_ONLY_CONTAIN_DIGITS,
	gs1_encoder_ePRIMARY_MESSAGE_CHECK_DIGIT_IS_INCORRECT,
	gs1_encoder_eSCAN_DATA_CONTAINS_ILLEGAL_CARAT,
	gs1_encoder_eFAILED_TO_PROCESS_SCAN_DATA,
	gs1_encoder_eFORMAT_SPEC_FOR_OPT_COMPONENT_MISSING_RT_SQ_BRACKET,
	gs1_encoder_eUNKNOWN_CHARACTER_SET,
	gs1_encoder_eFORMAT_SPEC_TOO_SHORT,
	gs1_encoder_eAI_LENGTH_TOO_LONG,
	gs1_encoder_eAI_LENGTH_IS_NOT_A_NUMBER,
	gs1_encoder_eUNRECOGNISED_FORMAT_SPECIFICATION,
	gs1_encoder_eNUMBER_OF_LINTERS_EXCEEDS_IMPL_LIMIT,
	gs1_encoder_eUNKNOWN_LINTER,
	gs1_encoder_eENTRY_TOO_LONG,
	gs1_encoder_eSYNTAX_DICTIONARY_CAPACITY_TOO_SMALL,
	gs1_encoder_eAI_RANGE_HAS_WRONG_WIDTH,
	gs1_encoder_eAIS_IN_RANGE_MUST_HAVE_EQUAL_WIDTH,
	gs1_encoder_eAIS_MUST_BE_NUMERIC,
	gs1_encoder_eAI_RANGE_PARTS_MAY_ONLY_DIFFER_IN_LAST_DIGIT,
	gs1_encoder_eAI_RANGE_END_MUST_EXCEED_RANGE_START,
	gs1_encoder_eAI_HAS_WRONG_WIDTH,
	gs1_encoder_eAI_MUST_BE_NUMERIC,
	gs1_encoder_eAIS_MUST_BE_IN_ASCENDING_ORDER,
	gs1_encoder_eTRUNCATED_AFTER_AI,
	gs1_encoder_eTRUNCATED_AFTER_FLAGS,
	gs1_encoder_eNUMBER_OF_AI_COMPONENTS_EXCEEDS_IMPL,
	gs1_encoder_eAI_IS_MISSING_COMPONENTS,
	gs1_encoder_eONLY_FINAL_COMPONENT_MAY_HAVE_VARIABLE_LENGTH,
	gs1_encoder_eMANDATORY_COMPONENT_CANNOT_FOLLOW_OPTIONAL_COMPONENTS,
	gs1_encoder_eNO_FNC1_AI_MUST_BE_PREDEFINED_LENGTH,
	gs1_encoder_eATTRIBUTE_NAME_REQUIRED_ON_LHS_OF_ASSIGNMENT,
	gs1_encoder_eATTRIBUTE_NAME_CONTAINS_ILLEGAL_CHARACTERS,
	gs1_encoder_eATTRIBUTE_VALUE_CONTAINS_ILLEGAL_CHARACTERS,
	gs1_encoder_eATTRIBUTE_VALUE_REQUIRED_ON_RHS_OF_ASSIGNMENT,
	gs1_encoder_eATTRIBUTE_AI_IS_NOT_WELL_FORMED,
	gs1_encoder_eSINGLETON_ATTRIBUTE_NAME_CONTAINS_ILLEGAL_CHARACTERS,
	gs1_encoder_eATTRIBUTES_TOO_LONG,
	gs1_encoder_eFAILED_TO_ALLOCATE_MEMORY_FOR_ATTRS,
	gs1_encoder_eTITLE_CONTAINS_ILLEGAL_CHARACTERS,
	gs1_encoder_eFAILED_TO_ALLOCATE_MEMORY_FOR_TITLE,
	gs1_encoder_eFAILED_TO_ALLOCATE_AI_TABLE,
	gs1_encoder_eCANNOT_READ_FILE,
	gs1_encoder_eSYNTAX_DICTIONARY_LINE_EXCEEDS_IMPL,
	gs1_encoder_eSYNTAX_DICTIONARY_LINE_ERROR,
	gs1_encoder_eDOMAIN_CONTAINS_ILLEGAL_CHARACTERS,
	gs1_encoder_eAI_VALUE_LENGTH_EXCEEDS_IMPL,
	gs1_encoder_eAI_TITLE_TOO_LONG,
	gs1_encoder_eNO_SYMBOLOGY_SELECTED,
	__GS1_ENCODERS_NUM_ERRS
} gs1_encoder_err_t;


struct gs1_encoder {

	gs1_encoder_symbologies_t sym;		// Symbology type

	bool addCheckDigit;			// For EAN/UPC and RSS-14/Lim, calculated if true, otherwise validated
	bool permitUnknownAIs;			// Extract AIs that are not in our AI table during AI element string and DL URI parsing
	bool permitZeroSuppressedGTINinDLuris;	// Whether to permit a path component GTIN value to be in GTIN-{8,12,13} format
	bool permitConvenienceAlphas;		// Whether to permit convenience alphas (deprecated, so no API)
	bool includeDataTitlesInHRI;		// Whether to include the Data Titles in HRI string output

	char errMsg[512];			// The translated error message
	GS1_ENCODERS_ASAN_GUARD(errMsg)

	gs1_encoder_err_t err;			// The error
	gs1_lint_err_t linterErr;		// Error returned by a linter

	char linterErrMarkup[512];
	GS1_ENCODERS_ASAN_GUARD(linterErrMarkup)

	char dataStr[MAX_DATA+1];		// Input data buffer passed to the encoders
	GS1_ENCODERS_ASAN_GUARD(dataStr)

	char dlAIbuffer[MAX_DATA+1];		// Populated with unbracketed AI string extracted from DL input
	GS1_ENCODERS_ASAN_GUARD(dlAIbuffer)

	char outStr[2*MAX_DATA+1];		// Buffer to return formatted data
	GS1_ENCODERS_ASAN_GUARD(outStr)

	char *outHRI[MAX_AIS];			// Array of AI element string for HRI printing
	GS1_ENCODERS_ASAN_GUARD(outHRI)

	bool localAlloc;			// True if we malloc()ed this struct
	FILE *outfp;

	struct aiEntry *aiTable;		// Pointer to the AI table
	size_t aiTableEntries;			// Number of entries in the AI table
	bool aiTableIsDynamic;			// True if the AI table is loaded from the Syntax Dictionary

	struct aiValue aiData[MAX_AIS];		// List of AI components
	GS1_ENCODERS_ASAN_GUARD(aiData)
	int numAIs;

	struct aiValue *sortedAIs[MAX_AIS];	// Sorted pointers to aiData entries
	GS1_ENCODERS_ASAN_GUARD(sortedAIs)
	int numSortedAIs;			// Number of entries in sortedAIs

	struct validationEntry validationTable[gs1_encoder_vNUMVALIDATIONS];
						// Table of all global validation functions

	uint8_t aiLengthByPrefix[100];		// AI length by two-digit prefix

	char** dlKeyQualifiers;			// List of valid DL key qualifier association strings
	int numDLkeyQualifiers;			// Number of dlKeyQualifiers strings

};


/*
 *  Compile-time guarantee that every per-AI output line fits within its share
 *  of outStr (sizeof(outStr) / MAX_AIS), so that MAX_AIS lines never overrun it.
 *  The loader caps AI value and data title lengths so these bounds hold:
 *
 *    getHRI:       "data_title (AI) value\0"
 *    getAIdataStr: "(AI)" then the value with each byte escaped ("(" -> "\(")
 *
 */
GS1_ENCODERS_STATIC_ASSERT(SIZEOF_FIELD(struct gs1_encoder, outStr) / MAX_AIS >= MAX_AI_TITLE_LEN + MAX_AI_LEN + MAX_AI_VALUE_LEN + 5);
GS1_ENCODERS_STATIC_ASSERT(SIZEOF_FIELD(struct gs1_encoder, outStr) / MAX_AIS >= MAX_AI_LEN + 2*MAX_AI_VALUE_LEN + 2);

// Linter failure markup is "(AI)before|error|after" over a single AI value,
// whose length the loader caps at MAX_AI_VALUE_LEN
GS1_ENCODERS_STATIC_ASSERT(SIZEOF_FIELD(struct gs1_encoder, linterErrMarkup) >= MAX_AI_LEN + MAX_AI_VALUE_LEN + 5);

// getAIdataStr rendering of dataStr may escape ("(" -> "\(") every byte
GS1_ENCODERS_STATIC_ASSERT(SIZEOF_FIELD(struct gs1_encoder, outStr) >= 2 * MAX_DATA + 1);

// AI lengths, including the length-by-prefix table values, are held in uint8_t
GS1_ENCODERS_STATIC_ASSERT(MAX_AI_LEN <= UINT8_MAX);

// A single aiComponent may span a whole AI value (e.g. the unknown-AI
// entries); aiComponent.max is uint8_t
GS1_ENCODERS_STATIC_ASSERT(MAX_AI_VALUE_LEN <= UINT8_MAX);

// aiValue.vallen is uint16_t and DL ignored query params are bounded only by
// the input size
GS1_ENCODERS_STATIC_ASSERT(MAX_DATA <= UINT16_MAX);

// AI lookup and validation machinery indexes by the first two digits of an AI
GS1_ENCODERS_STATIC_ASSERT(MIN_AI_LEN >= 2);


// The guarded buffers of a gs1_encoder, declared once; passed to
// GS1_ENCODERS_{POISON,UNPOISON}_GUARDS() to act on every guard of an instance
#define GS1_ENCODER_GUARDS(GUARD, s)	\
	GUARD(s, errMsg)		\
	GUARD(s, linterErrMarkup)	\
	GUARD(s, dataStr)		\
	GUARD(s, dlAIbuffer)		\
	GUARD(s, outStr)		\
	GUARD(s, outHRI)		\
	GUARD(s, aiData)		\
	GUARD(s, sortedAIs)


/*
 *  Inline helpers
 *
 */
// Append up to the remaining length of the destination
static inline char* gs1_buf_append(char *dst, size_t *rem, const void *src, size_t len) {
	if (len == 0 || len > SIZE_MAX / 2)	// Prevent underflow of src
		return dst;
	if (*rem > 1) {
		if (len > *rem - 1)
			len = *rem - 1;  // LCOV_EXCL_LINE: defensive truncation; current callers size the destination beyond any reachable input length
		memcpy(dst, src, len);
		dst += len;
		*rem -= len;
	}
	return dst;
}

// Buffer is all digits
static inline __ATTR_PURE bool gs1_allDigits(const uint8_t* const str, size_t len) {
	size_t i;
	for (i = 0; i < len; i++)
		if (unlikely(str[i] < '0' || str[i] > '9'))
			return false;
	return true;
}


/*
 *  Utility functions
 *
 */
typedef struct {
	const char *ptr;
	size_t len;
	const char *next;	// Resume position for next call, or NULL if done
	const char *end;	// End boundary (NULL for null-terminated strings)
} gs1_tok_t;

bool gs1_tokenise(const char *data, char delim, gs1_tok_t *tok);

char* gs1_strdup_alloc(const char *s);

ssize_t gs1_binarySearch(const void* needle, const void* haystack, const size_t haystack_size,
			 int (*compare)(const void* key, const void* element, const size_t index),
			 bool (*validate)(const void* key, const void* element, const size_t index));


#ifdef UNIT_TESTS

void test_api_getVersion(void);
void test_api_instanceSize(void);
void test_api_init(void);
void test_api_init_deprecatedFlags(void);
void test_api_init_opts_layout(void);
void test_api_init_enum_values(void);
void test_api_defaults(void);
void test_api_sym(void);
void test_api_addCheckDigit(void);
void test_api_permitUnknownAIs(void);
void test_api_permitZeroSuppressedGTINinDLuris(void);
void test_api_validateAIassociations(void);
void test_api_validations(void);
void test_api_getters(void);
void test_api_dataStr(void);
void test_api_getAIdataStr(void);
void test_api_getScanData(void);
void test_api_setScanData(void);
void test_api_getHRI(void);
void test_api_copyHRI(void);
void test_api_getDLignoredQueryParams(void);
void test_api_copyDLignoredQueryParams(void);
void test_api_allocFailures(void);
#ifndef EXCLUDE_SYNTAX_DICTIONARY_LOADER
void test_api_brokenPrefixSyndict(void);
void test_api_tooManyDLkeyQualifiersSyndict(void);
#endif

#endif


#endif /* ENC_PRIVATE_H */
