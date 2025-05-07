/**
 * GS1 Barcode Syntax Engine
 *
 * @author Copyright (c) 2021-2025 GS1 AISBL.
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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "syntax/gs1syntaxdictionary.h"
#include "gs1encoders.h"
#include "enc-private.h"
#include "debug.h"
#include "ai.h"
#include "dl.h"
#include "tr.h"


/*
 * An embedded AI table that can be loaded when the Syntax Dictionary is not
 * available.
 *
 */
#ifndef EXCLUDE_EMBEDDED_AI_TABLE
#include "aitable.inc"
#endif  /* EXCLUDE_EMBEDDED_AI_TABLE */


/*
 *  This library stores a compact representation of AI data (FNC1 in first) in
 *  unbracketed format where "^" represents FNC1, i.e. "^..."
 *
 *  Ingested AI element strings and GS1 Digital Link URI data is parsed then
 *  processed (validated) into the aforementioned form. Either during parsing
 *  or processing a table of extracted AIs is populated consisting of a pointer
 *  to an AI table entry, as well as pointers to the start of the AI and its
 *  value, as well as their respective lengths:
 *
 *    kind        :  the kind of entry
 *    aiEntry     -> aiTable entry
 *    ai          -> Start of AI in the AI data string
 *    ailen       :  Length of AI
 *    value       -> Start of value in the AI data string
 *    vallen      :  Length of value
 *    dlPathOrder :  Denotes the position in a DL URI path component
 *
 *  This ensures that we only store a single instance of the input that has
 *  been provided by the provided by the user, whether they have provided
 *  bracketed AI data or scan data.
 *
 *  GS1 Digital Link inputs are an exception since they must be stored as
 *  given, ready to be encoded directly into a barcode symbol. Unlike
 *  conversion between bracketed/unbracketed AI data and scan data, by
 *  specification the conversion between GS1 Digital Link URIs and AI syntax is
 *  not bijective: the URI stem is lost, element order may not be preserved and
 *  AI values may be normalised into canonical form.
 *
 *  The extracted AI element string is stored in dlAIbuffer which is used as
 *  the storage for HRI text and construction bracketed AI strings.
 *
 */


/*
 *  Create a mapping of two-digit AI prefixes to AI length. All AIs that start
 *  with the same two digits shall have the same AI length.
 *
 */
static bool populateAIlengthByPrefix(gs1_encoder* const ctx) {
	const struct aiEntry *e;

	memset(ctx->aiLengthByPrefix, 0, sizeof(ctx->aiLengthByPrefix));

	for (e = ctx->aiTable; *e->ai; e++) {
		uint8_t prefix = (uint8_t)((e->ai[0] - '0') * 10 + (e->ai[1] - '0'));
		uint8_t length = (uint8_t)strlen(e->ai);
		if (ctx->aiLengthByPrefix[prefix] != 0 && ctx->aiLengthByPrefix[prefix] != length) {
			SET_ERR_V(AI_TABLE_BROKEN_PREFIXES_DIFFER_IN_LENGTH, e->ai[0], e->ai[1]);
			return false;
		}
		ctx->aiLengthByPrefix[prefix] = length;
	}

	return true;
}

static inline __ATTR_PURE uint8_t aiLengthByPrefix(const gs1_encoder* const ctx, const char *ai) {
	assert(ai[0] >= '0' && ai[0] <= '9' && ai[1] >= '0' && ai[1] <= '9');
	return ctx->aiLengthByPrefix[(ai[0] - '0') * 10 + (ai[1] - '0')];
}


void gs1_setAItable(gs1_encoder* const ctx, struct aiEntry *aiTable) {

	struct aiEntry *e;

#ifndef EXCLUDE_EMBEDDED_AI_TABLE
redo:
#endif

	/*
	 *  Clear the current AI table
	 *
	 */
	if (ctx->aiTable && ctx->aiTableIsDynamic)
		GS1_ENCODERS_FREE(ctx->aiTable);

	/*
	 *  Set the given AI table and populate the various additional
	 *  structures with information extracted from the AI table.
	 *
	 */
	ctx->aiTableIsDynamic = true;
	if (!aiTable) {
#ifndef EXCLUDE_EMBEDDED_AI_TABLE
		aiTable = embedded_ai_table;
		ctx->aiTableIsDynamic = false;
#else
		printf("*** Embedded AI table is not available.\n");
		printf("***  Unable to continue. STOPPING.\n");
		abort();
#endif
	}

	ctx->aiTable = aiTable;

	ctx->aiTableEntries = 0;
	for (e = ctx->aiTable; *e->ai; e++)
		ctx->aiTableEntries++;

	if (!populateAIlengthByPrefix(ctx))
		goto fail;

	if (!gs1_populateDLkeyQualifiers(ctx))
		goto fail;

	return;

fail:

	printf("*** Failed to process the AI table.\n");
	printf("*** %s\n", ctx->errMsg);

#ifndef EXCLUDE_EMBEDDED_AI_TABLE
	if (aiTable != embedded_ai_table) {
		printf("*** Loading embedded AI table as a fallback!\n");
		aiTable = embedded_ai_table;
		goto redo;
	}
#endif

	printf("*** Unable to continue. STOPPING.\n");
	abort();

}


/*
 *  AI prefixes that are pre-defined as fixed-length and do not require
 *  termination by an FNC1 character. Normally the AI table entry determines
 *  whether an FNC1 is required, however this list is consulted when vivifying
 *  an unknown AI since not all prefixes are currently in use.
 *
 */
#define VL 0		// Variable-length

static const uint8_t fixedAIprefixLengths[100] = {
	18, 14, 14, 14, 16,				/* (00) - (04) */
	VL, VL, VL, VL, VL, VL,
	 6,  6,  6,  6,  6,  6,  6,  6,  6,  2,		/* (11) - (20) */
	VL, VL,
	VL,						/* (23) no longer defined as fixed length, now (235) allocated as TPX */
	VL, VL, VL, VL, VL, VL, VL,
	 6,  6,  6,  6,  6,  6,				/* (31) - (36) */
	VL, VL, VL, VL,
	13,						/* (41)        */
	VL, VL, VL, VL, VL, VL, VL, VL,
	VL, VL, VL, VL, VL, VL, VL, VL, VL, VL,
	VL, VL, VL, VL, VL, VL, VL, VL, VL, VL,
	VL, VL, VL, VL, VL, VL, VL, VL, VL, VL,
	VL, VL, VL, VL, VL, VL, VL, VL, VL, VL,
	VL, VL, VL, VL, VL, VL, VL, VL, VL, VL,
};

static inline __ATTR_PURE uint8_t valLengthByPrefix(const char* const ai) {
	assert(ai[0] >= '0' && ai[0] <= '9' && ai[1] >= '0' && ai[1] <= '9');
	return fixedAIprefixLengths[(ai[0] - '0') * 10 + (ai[1] - '0')];
}


/*
 * Pseudo AI table entries allowing AIs that are not present in the above table
 * to be "vivified" if PermitUnknownAIs is enabled
 *
 */
static const struct aiEntry unknownAI =
	AI_ENTRY( ""    , DO_FNC1, XX_DATA_ATTR, X,1,90,MAN,_,_,_,  __, __, __, __, "", "UNKNOWN" );
static const struct aiEntry unknownAI2 =
	AI_ENTRY( "XX"  , DO_FNC1, XX_DATA_ATTR, X,1,90,MAN,_,_,_,  __, __, __, __, "", "UNKNOWN" );
static const struct aiEntry unknownAI3 =
	AI_ENTRY( "XXX" , DO_FNC1, XX_DATA_ATTR, X,1,90,MAN,_,_,_,  __, __, __, __, "", "UNKNOWN" );
static const struct aiEntry unknownAI4 =
	AI_ENTRY( "XXXX", DO_FNC1, XX_DATA_ATTR, X,1,90,MAN,_,_,_,  __, __, __, __, "", "UNKNOWN" );
static const struct aiEntry unknownAI2fixed2 =
	AI_ENTRY( "XX"  , NO_FNC1, XX_DATA_ATTR, X,2,2,MAN,_,_,_,   __, __, __, __, "", "UNKNOWN" );
static const struct aiEntry unknownAI2fixed14 =
	AI_ENTRY( "XX"  , NO_FNC1, XX_DATA_ATTR, X,14,14,MAN,_,_,_, __, __, __, __, "", "UNKNOWN" );
static const struct aiEntry unknownAI2fixed16 =
	AI_ENTRY( "XX"  , NO_FNC1, XX_DATA_ATTR, X,16,16,MAN,_,_,_, __, __, __, __, "", "UNKNOWN" );
static const struct aiEntry unknownAI2fixed18 =
	AI_ENTRY( "XX"  , NO_FNC1, XX_DATA_ATTR, X,18,18,MAN,_,_,_, __, __, __, __, "", "UNKNOWN" );
static const struct aiEntry unknownAI3fixed13 =
	AI_ENTRY( "XXX" , NO_FNC1, XX_DATA_ATTR, X,13,13,MAN,_,_,_, __, __, __, __, "", "UNKNOWN" );
static const struct aiEntry unknownAI4fixed6 =
	AI_ENTRY( "XXXX", NO_FNC1, XX_DATA_ATTR, X,6,6,MAN,_,_,_,   __, __, __, __, "", "UNKNOWN" );


/*
 * Lookup an AI table entry matching a given AI or matching prefix of given
 * data
 *
 * For an exact AI lookup its length is given. Otherwise 0 length will look for
 * an AI in the table that matches a prefix of the given data.
 *
 */
const struct aiEntry* gs1_lookupAIentry(const gs1_encoder* const ctx, const char *ai, size_t ailen) {

	size_t aiLenByPrefix;
	size_t s = 0, e = ctx->aiTableEntries;

	assert(ailen == 0 || ailen <= strlen(ai));

	if (ailen != 0 && (ailen < MIN_AI_LEN || ailen > MAX_AI_LEN))	// Even for unknown AIs
		return NULL;

	// Don't attempt to find a non-digit AI
	if (!gs1_allDigits((uint8_t *)ai, ailen != 0 ? ailen : MIN_AI_LEN))
		return NULL;

	/*
	 * Binary search through the AI table to find an entry that matches a
	 * prefix, optionally ensuring that the AI also has a specified length
	 *
	 */
	while (s < e) {
		const size_t m = s + (e - s) / 2;
		const struct aiEntry* const entry = &ctx->aiTable[m];
		const size_t entrylen = strlen(entry->ai);
		const int cmp = strncmp(entry->ai, ai, entrylen);
		if (cmp == 0) {
			if (ailen != 0 && entrylen != ailen)
				return NULL;	// Prefix match, but incorrect length
			return entry;		// Found
		}
		if (ailen != 0 && strncmp(ai, entry->ai, ailen) == 0)
			return NULL;	// Don't vivify an AI that is a prefix of a known AI
		if (cmp < 0)
			s = m + 1;
		else
			e = m;
	}

	if (!ctx->permitUnknownAIs)
		return NULL;

	/*
	 * If permitUnknownAIs is enabled then we vivify the AI by returning a
	 * pseudo "unknownAI" entry, but only if the length matches that
	 * indicated by the prefix where such a length is defined.
	 *
	 * Otherwise we return NULL ("not found") to indicate an error.
	 *
	 */
	aiLenByPrefix = aiLengthByPrefix(ctx, ai);
	if (ailen != 0 && aiLenByPrefix != 0 && aiLenByPrefix != ailen)
		return NULL;

	// Don't vivify a non-digit AI
	if (aiLenByPrefix != 0 && !gs1_allDigits((uint8_t *)ai, aiLenByPrefix))
		return NULL;

	// Return unknownAI indicator for corresponding AI length
	if (aiLenByPrefix == 2) {
		uint8_t valLenByPrefix = valLengthByPrefix(ai);
		if (valLenByPrefix == VL) return &unknownAI2;
		if (valLenByPrefix ==  2) return &unknownAI2fixed2;
		if (valLenByPrefix == 14) return &unknownAI2fixed14;
		if (valLenByPrefix == 16) return &unknownAI2fixed16;
		if (valLenByPrefix == 18) return &unknownAI2fixed18;
	} else if (aiLenByPrefix == 3) {
		uint8_t valLenByPrefix = valLengthByPrefix(ai);
		if (valLenByPrefix == VL) return &unknownAI3;
		if (valLenByPrefix == 13) return &unknownAI3fixed13;
	} else if (aiLenByPrefix == 4) {
		uint8_t valLenByPrefix = valLengthByPrefix(ai);
		if (valLenByPrefix == VL) return &unknownAI4;
		if (valLenByPrefix ==  6) return &unknownAI4fixed6;
	}

	return &unknownAI;	// Unknown AI length

}


/*
 *  Validate string between start and end pointers according to rules for an AI
 *
 */
static size_t validate_ai_val(gs1_encoder* const ctx, const char* const ai, const struct aiEntry* const entry, const char* const start, const char* const end) {

	const struct aiComponent *part;
	const char *p = start, *r = end;

	assert(ctx);
	assert(entry);
	assert(start);
	assert(end);
	assert(end >= start);

	DEBUG_PRINT("  Considering AI (%.*s): %.*s\n", (int)strlen(entry->ai), ai, (int)(r-p), start);

	if (p == r) {
		SET_ERR_V(AI_DATA_IS_EMPTY, (int)strlen(entry->ai), ai);
		return 0;
	}

	for (part = entry->parts; part->cset; part++) {

		char compval[MAX_AI_VALUE_LEN+1];
		gs1_linter_t cset_linter;
		const gs1_linter_t *l;

		size_t complen = (size_t)(r-p);	// Until given FNC1 or end...
		if (part->max < r-p)
			complen = part->max;	// ... reduced to max length of component
		strncpy(compval, p, complen);
		compval[complen] = '\0';

		DEBUG_PRINT("    Validating component: %s\n", compval);

		if (part->opt == OPT && complen == 0)	// Nothing to be done for an empty optional component
			continue;

		if (complen < part->min) {
			SET_ERR_V(AI_DATA_HAS_INCORRECT_LENGTH, (int)strlen(entry->ai), ai);
			return 0;
		}

		/*
		 *  Run the cset linter followed by each additional linter for
		 *  the component
		 *
		 */
		switch (part->cset) {
			case cset_N: cset_linter = gs1_lint_csetnumeric; break;
			case cset_X: cset_linter = gs1_lint_cset82; break;
			case cset_Y: cset_linter = gs1_lint_cset39; break;
			case cset_Z: cset_linter = gs1_lint_cset64; break;
			default: cset_linter = NULL; break;
		}
		assert(cset_linter);
		l = &cset_linter;
		do {

			gs1_lint_err_t err;
			size_t errpos, errlen;

			err = (*l)(compval, &errpos, &errlen);
			if (err) {
				SET_ERR_V(AI_LINTER_ERROR, (int)strlen(entry->ai), ai, gs1_lint_err_str[err]);
				ctx->linterErr = err;
				errpos += (size_t)(p-start);
				snprintf(ctx->linterErrMarkup, sizeof(ctx->linterErrMarkup), "(%.*s)%.*s|%.*s|%.*s",
					(int)strlen(entry->ai), ai,
					(int)errpos, start,
					(int)errlen, start + errpos,
					(int)(strlen(compval) - errpos - errlen), start + errpos + errlen);
				return 0;
			}
			l = (l == &cset_linter) ? &(part->linters[0]) : l+1;

		} while (*l);

		p += complen;

	}

	return (size_t)(p-start);	// Amount of data that validation consumed

}


/*
 * Return the overall minimum and maximum lengths for an AI, by summing the components.
 *
 */
static inline __ATTR_PURE size_t aiEntryMinLength(const struct aiEntry* const entry) {
	const struct aiComponent *part;
	size_t l;
	for (part = entry->parts, l = 0; part->cset; l+= (part->opt == MAN ? part->min : 0), part++);
	return l;
}
static inline __ATTR_PURE size_t aiEntryMaxLength(const struct aiEntry* const entry) {
	const struct aiComponent *part;
	size_t l;
	for (part = entry->parts, l = 0; part->cset; l+= part->max, part++);
	return l;
}


/*
 * AI length and content check (no "^") used by parsers prior to performing
 * component-based validation since reporting issues such as checksum failure
 * isn't helpful when the AI is too long
 *
 */
bool gs1_aiValLengthContentCheck(gs1_encoder* const ctx, const char* const ai, const struct aiEntry* const entry, const char* const aiVal, const size_t vallen) {

	assert(ctx);
	assert(entry);
	assert(aiVal);

	if (vallen < aiEntryMinLength(entry)) {
		SET_ERR_V(AI_VALUE_IS_TOO_SHORT, (int)strlen(entry->ai), ai);
		return false;
	}

	if (vallen > aiEntryMaxLength(entry)) {
		SET_ERR_V(AI_VALUE_IS_TOO_LONG, (int)strlen(entry->ai), ai);
		return false;
	}

	// Also forbid data "^" characters at this stage so we don't conflate with FNC1
	if (memchr(aiVal, '^', vallen) != NULL) {
		SET_ERR_V(AI_CONTAINS_ILLEGAL_CARAT_CHARACTER, (int)strlen(entry->ai), ai);
		return false;
	}

	return true;

}


/*
 * Convert bracketed AI syntax data to regular AI data string with ^ = FNC1
 *
 */
bool gs1_parseAIdata(gs1_encoder* const ctx, const char* const aiData, char* const dataStr) {

	const char *p = aiData;
	bool fnc1req = true;

	assert(ctx);
	assert(aiData);

	*dataStr = '\0';
	ctx->err = gs1_encoder_eNO_ERROR;
	*ctx->errMsg = '\0';
	ctx->linterErr = GS1_LINTER_OK;
	*ctx->linterErrMarkup = '\0';

	DEBUG_PRINT("\nParsing AI data: %s\n", p);

	while (*p) {

		const struct aiEntry *entry;
		const char *outai, *outval, *r, *ai;
		size_t ailen;

		if (*p++ != '(') goto fail; 			// Expect start of AI
		if (!(r = strchr(p, ')'))) goto fail;		// Find end of A
		ailen = (size_t)(r-p);
		entry = gs1_lookupAIentry(ctx, p, ailen);
		if (entry == NULL) {
			SET_ERR_V(AI_UNRECOGNISED, (int)ailen, p);
			goto fail;
		}
		ai = p;

		if (fnc1req)
			writeDataStr("^");			// Write FNC1, if required
		outai = dataStr + strlen(dataStr);		// Record the current start of the output AI
		nwriteDataStr(p, ailen);			// Write AI
		fnc1req = entry->fnc1;				// Record whether FNC1 required before next AI

		if (!*++r) goto fail;				// Advance to start of AI value and fail if at end

		outval = dataStr + strlen(dataStr);		// Record the current start of the output value

again:

		if ((p = strchr(r, '(')) == NULL)
			p = r + strlen(r);			// Move the pointer to the end if no more AIs

		if (*p != '\0' && *(p-1) == '\\') {		// This bracket is an escaped data character
			nwriteDataStr(r, (size_t)(p-r-1));	// Write up to the escape character
			writeDataStr("(");			// Write the data bracket
			r = p+1;				// And keep going
			goto again;
		}

		nwriteDataStr(r, (size_t)(p-r));		// Write the remainder of the value

		// Perform certain checks at parse time, before processing the
		// components with the linters
		if (!gs1_aiValLengthContentCheck(ctx, ai, entry, outval, strlen(outval)))
			goto fail;

		// Update the AI data
		if (ctx->numAIs >= MAX_AIS) {
			SET_ERR(TOO_MANY_AIS);
			goto fail;
		}

		ctx->aiData[ctx->numAIs++] = (struct aiValue) {
			.kind = aiValue_aival,
			.aiEntry = entry,
			.ai = outai,
			.ailen = (uint8_t)ailen,
			.value = outval,
			.vallen = (uint8_t)strlen(outval),
			.dlPathOrder = DL_PATH_ORDER_ATTRIBUTE
		};

	}

	DEBUG_PRINT("Parsing AI data successful: %s\n", dataStr);

	// Now validate the data that we have written
	return gs1_processAIdata(ctx, dataStr, false);

fail:

	if (*ctx->errMsg == '\0')
		SET_ERR(AI_PARSE_FAILED);

	DEBUG_PRINT("Parsing AI data failed: %s\n", ctx->errMsg);

	*dataStr = '\0';
	return false;

}


/*
 *  Validate regular AI data ("^...") and optionally extract AIs
 *
 */
bool gs1_processAIdata(gs1_encoder* const ctx, const char* const dataStr, const bool extractAIs) {

	const char *p;

	assert(ctx);
	assert(dataStr);

	ctx->err = gs1_encoder_eNO_ERROR;
	*ctx->errMsg = '\0';
	ctx->linterErr = GS1_LINTER_OK;
	*ctx->linterErrMarkup = '\0';

	p = dataStr;

	// Ensure FNC1 in first
	if (!*p || *p++ != '^') {
		SET_ERR(MISSING_FNC1_IN_FIRST_POSITION);
		return false;
	}

	// Must have some AI data
	if (!*p) {
		SET_ERR(AI_DATA_EMPTY);
		return false;
	}

	while (*p) {

		const char *r, *ai;
		const struct aiEntry *entry;
		size_t vallen;

		/* Find AI that matches a prefix of our data
		 *
		 * We cannot allow unknown AIs of *unknown AI length* when
		 * extracting AIs from a raw data string because we are unable
		 * to differentiate the AI from its value without knowing a
		 * priori the AI's length.
		 *
		 */
		if ((entry = gs1_lookupAIentry(ctx, p, 0)) == NULL ||
		    (extractAIs && entry == &unknownAI)) {
			SET_ERR_V(NO_AI_FOR_PREFIX, p);
			return false;
		}

		// Save start of AI for AI data then jump over
		ai = p;
		p += strlen(entry->ai);

		// r points to the next FNC1 or end of string...
		if ((r = strchr(p, '^')) == NULL)
			r = p + strlen(p);

		// Validate and return how much was consumed
		if ((vallen = validate_ai_val(ctx, ai, entry, p, r)) == 0)
			return false;

		// Add to the aiData
		if (extractAIs) {
			if (ctx->numAIs >= MAX_AIS) {
				SET_ERR(TOO_MANY_AIS);
				return false;
			}
			ctx->aiData[ctx->numAIs++] = (struct aiValue) {
				.kind = aiValue_aival,
				.aiEntry = entry,
				.ai = ai,
				.ailen = (uint8_t)strlen(entry->ai),
				.value = p,
				.vallen = (uint8_t)vallen,
				.dlPathOrder = DL_PATH_ORDER_ATTRIBUTE
			};
		}

		// After AIs requiring FNC1, we expect to find an FNC1 or be at the end
		p += vallen;
		if (entry->fnc1 && *p != '^' && *p != '\0') {
			SET_ERR_V(AI_DATA_IS_TOO_LONG, (int)strlen(entry->ai), ai);
			return false;
		}

		// Skip FNC1, even at end of fixed-length AIs
		if (*p == '^')
			p++;

	}

	return true;

}


/*
 *  Search the AIs for any match with the given AI pattern, optionally
 *  returning the matched AI.
 *
 *  Ignore AI can be set to the current AI to avoid matching triggering on
 *  itself when matching by a self-referencing pattern.
 *
 *  Note: Given the typically small number of AIs and template matching
 *  requirement, there is little to be gained by maintaining a more advanced
 *  data structure versus the current approach of simply walking the AIs.
 *
 */
static bool aiExists(const gs1_encoder* const ctx, const char* const ai, const char* const ignoreAI, struct aiValue const **matchedAI) {

	int i;
	const size_t prefixlen = strspn(ai, "0123456789");

	for (i = 0; i < ctx->numAIs; i++) {

		const struct aiValue* const ai2 = &ctx->aiData[i];

		if (ai2->kind != aiValue_aival ||
		    strncmp(ai2->ai, ai, prefixlen) != 0 ||
		    (ignoreAI && strncmp(ai2->ai, ignoreAI, strlen(ai)) == 0)
		   )
			continue;

		if (matchedAI)
			*matchedAI = ai2;
		return true;

	}

	if (matchedAI)
		*matchedAI = NULL;
	return false;

}


/*
 * AI validation routine that process the "ex" attributes of an AI table entry
 * to ensure that AIs that are mutually exclusive do not appear in the data.
 *
 */
static bool validateAImutex(gs1_encoder* const ctx) {

	int i;

	assert(ctx);
	assert(ctx->numAIs <= MAX_AIS);

	for (i = 0; i < ctx->numAIs; i++) {

		const struct aiValue* const ai = &ctx->aiData[i];
		char attrs[MAX_AI_ATTR_LEN + 1] = { 0 };
		const char *token;
		char *saveptr = NULL;

		if (ai->kind != aiValue_aival)
			continue;

		assert(ai->aiEntry);

		*attrs = '\0';
		strncat(attrs, ai->aiEntry->attrs, MAX_AI_ATTR_LEN);

		for (token = strtok_r(attrs, " ", &saveptr); token; token = strtok_r(NULL, " ", &saveptr)) {

			char *saveptr2 = NULL;

			if (strncmp(token, "ex=", 3) != 0)
				continue;

			for (token = strtok_r((char*)(token+3), ",", &saveptr2); token; token = strtok_r(NULL, ",", &saveptr2)) {

				const struct aiValue *matchedAI;

				if (!aiExists(ctx, token, ai->ai, &matchedAI))
					continue;

				SET_ERR_V(INVALID_AI_PAIRS, ai->ailen, ai->ai, matchedAI->ailen, matchedAI->ai);
				return false;

			}

		}

	}

	return true;

}


/*
 * AI validation routine that process the "req" attributes of an AI table entry
 * to ensure that all AIs required to satisfy some other AI exist in the data.
 *
 */
static bool validateAIrequisites(gs1_encoder* const ctx) {

	int i;

	assert(ctx);
	assert(ctx->numAIs <= MAX_AIS);

	for (i = 0; i < ctx->numAIs; i++) {

		const struct aiValue* const ai = &ctx->aiData[i];
		char attrs[MAX_AI_ATTR_LEN + 1] = { 0 };
		const char *token;
		char *saveptr = NULL;

		if (ai->kind != aiValue_aival)
			continue;

		assert(ai->aiEntry);

		*attrs = '\0';
		strncat(attrs, ai->aiEntry->attrs, MAX_AI_ATTR_LEN);

		for (token = strtok_r(attrs, " ", &saveptr); token; token = strtok_r(NULL, " ", &saveptr)) {

			bool satisfied = true;
			char *saveptr2 = NULL;
			char reqErr[MAX_AI_ATTR_LEN - 4 + 1] = { 0 };

			if (strncmp(token, "req=", 4) != 0)
				continue;

			strncat(reqErr, token+4, MAX_AI_ATTR_LEN - 4);

			for (token = strtok_r((char*)(token+4), ",", &saveptr2); token; token = strtok_r(NULL, ",", &saveptr2)) {

				char *saveptr3 = NULL;
				satisfied = true;

				// All members of a group (e.g. "01+21") must be present
				for (token = strtok_r((char*)token, "+", &saveptr3); token; token = strtok_r(NULL, ",", &saveptr3))
					if (!aiExists(ctx, token, ai->ai, NULL))
						satisfied = false;

				if (satisfied)		// Any wholly satisfied group is sufficient for req
					break;

			}

			if (!satisfied) {	/* Loop finished without satisfying one of the AI groups in "req" */
				SET_ERR_V(REQUIRED_AIS_NOT_SATISFIED, ai->ailen, ai->ai, reqErr);
				return false;
			}

		}

	}

	return true;

}


/*
 * AI validation routine that ensure that any repeated AIs in the data have the
 * same value. (Repeated AIs may occur when the AI data from reads of multiple
 * symbol carriers on the same label is concatenated.)
 *
 */
static bool validateAIrepeats(gs1_encoder* const ctx) {

	int i;

	assert(ctx);
	assert(ctx->numAIs <= MAX_AIS);

	for (i = 0; i < ctx->numAIs; i++) {

		const struct aiValue* const ai = &ctx->aiData[i];
		int j;

		if (ai->kind != aiValue_aival)
			continue;

		for (j = i + 1; j < ctx->numAIs; j++) {

			const struct aiValue* const ai2 = &ctx->aiData[j];

			if (ai2->kind != aiValue_aival)
				continue;

			if (ai->ailen == ai2->ailen && strncmp(ai->ai, ai2->ai, ai->ailen) == 0 &&
			   (ai->vallen != ai2->vallen || strncmp(ai->value, ai2->value, ai->vallen) != 0)) {
				SET_ERR_V(INSTANCES_OF_AI_HAVE_DIFFERENT_VALUES, ai->ailen, ai->ai);
				return false;
			}

		}

	}

	return true;

}


/*
 * AI validation routine that enforces that AIs (253), (255) and (8003) include
 * a serial component when used with a (8030) digital signature.
 *
 */
static bool validateDigSigRequiresSerialisedKey(gs1_encoder* const ctx) {

	int i;

	assert(ctx);
	assert(ctx->numAIs <= MAX_AIS);

	if (!aiExists(ctx, "8030", NULL, NULL))
		return true;

	for (i = 0; i < ctx->numAIs; i++) {

		const struct aiValue* ai = &ctx->aiData[i];

		if (ai->kind != aiValue_aival ||
		        (strcmp(ai->aiEntry->ai, "253") != 0 &&
		         strcmp(ai->aiEntry->ai, "255") != 0 &&
		         strcmp(ai->aiEntry->ai, "8003") != 0)
		   )
			continue;

		if (ai->vallen == aiEntryMinLength(ai->aiEntry)) {
			SET_ERR_V(SERIAL_NOT_PRESENT, ai->ailen, ai->ai);
			return false;
		}

	}

	return true;

}


/*
 *  Execute each enabled validation function in turn
 *
 */
bool gs1_validateAIs(gs1_encoder* const ctx) {

	int i;

	for (i = 0; i < gs1_encoder_vNUMVALIDATIONS; i++) {

		const struct validationEntry v = ctx->validationTable[i];

		if (v.enabled && v.fn && !v.fn(ctx))
			return false;

	}

	return true;

}


void gs1_loadValidationTable(gs1_encoder* const ctx) {

#define ENTRY(n,l,e,f) ctx->validationTable[n] =			\
		(struct validationEntry){ .locked = l, .enabled = e, .fn = f };

	//     enum exported in public API           locked  enabled  fn
	ENTRY( gs1_encoder_vMUTEX_AIS,               true,   true,    validateAImutex                     );
	ENTRY( gs1_encoder_vREQUISITE_AIS,           false,  true,    validateAIrequisites                );
	ENTRY( gs1_encoder_vREPEATED_AIS,            true,   true,    validateAIrepeats                   );
	ENTRY( gs1_encoder_vDIGSIG_SERIAL_KEY,       true,   true,    validateDigSigRequiresSerialisedKey );
	ENTRY( gs1_encoder_vUNKNOWN_AI_NOT_DL_ATTR,  false,  true,    NULL                                );

#undef ENTRY

}



#ifdef UNIT_TESTS

#define TEST_NO_MAIN
#include "acutest.h"


void test_ai_lookupAIentry(void) {

	gs1_encoder* ctx;
	TEST_ASSERT((ctx = gs1_encoder_init(NULL)) != NULL);
	assert(ctx);

	TEST_CHECK(strcmp(gs1_lookupAIentry(ctx, "01",     2)->ai, "01") == 0);		// Exact lookup, data following
	TEST_CHECK(strcmp(gs1_lookupAIentry(ctx, "011234", 2)->ai, "01") == 0);		// Exact lookup, data following
	TEST_CHECK(strcmp(gs1_lookupAIentry(ctx, "011234", 0)->ai, "01") == 0);		// Prefix lookup, data following
	TEST_CHECK(strcmp(gs1_lookupAIentry(ctx, "8012",   0)->ai, "8012") == 0);	// Prefix lookup, data following

	TEST_CHECK(gs1_lookupAIentry(ctx, "2345XX", 4) == NULL);			// No such AI (2345)
	TEST_CHECK(gs1_lookupAIentry(ctx, "234XXX", 3) == NULL);			// No such AI (234)
	TEST_CHECK(gs1_lookupAIentry(ctx, "23XXXX", 2) == NULL);			// No such AI (23)
	TEST_CHECK(gs1_lookupAIentry(ctx, "2XXXXX", 1) == NULL);			// No such AI (2)
	TEST_CHECK(gs1_lookupAIentry(ctx, "XXXXXX", 0) == NULL);			// No matching prefix
	TEST_CHECK(gs1_lookupAIentry(ctx, "234567", 0) == NULL);			// No matching prefix

	TEST_CHECK(strcmp(gs1_lookupAIentry(ctx, "235XXX", 0)->ai, "235") == 0);	// Matching prefix
	TEST_CHECK(gs1_lookupAIentry(ctx, "235XXX", 2) == NULL);			// No such AI (23), even though data starts 235
	TEST_CHECK(gs1_lookupAIentry(ctx, "235XXX", 1) == NULL);			// No such AI (2), even though data starts 235

	TEST_CHECK(strcmp(gs1_lookupAIentry(ctx, "37123", 2)->ai, "37") == 0);		// Exact lookup
	TEST_CHECK(gs1_lookupAIentry(ctx, "37123", 3) == NULL);				// No such AI (371), even though there is AI (37)
	TEST_CHECK(gs1_lookupAIentry(ctx, "37123", 1) == NULL);				// No such AI (3), even though there is AI (37)

	gs1_encoder_setPermitUnknownAIs(ctx, true);
	TEST_CHECK(gs1_lookupAIentry(ctx, "89", 2) == &unknownAI);			// No such AI (89), but permitting unknown AIs so we vivify it requiring FNC1
	TEST_CHECK(gs1_lookupAIentry(ctx, "011", 3) == NULL);				// Ditto for (011), but we can't vivify it since known (01) is a prefix match

	TEST_CHECK(gs1_lookupAIentry(ctx, "800", 3) == NULL);				// Don't vivify (800) which is a prefix of existing (8001)
	TEST_CHECK(gs1_lookupAIentry(ctx, "80", 2) == NULL);				// Nor (80) for the same reason

	TEST_CHECK(gs1_lookupAIentry(ctx, "399", 3) == NULL);				// Don't vivify (399) since AI prefix "39" is defined as having length 4
	TEST_CHECK(gs1_lookupAIentry(ctx, "3999", 4) == &unknownAI4);			// So (3999) is okay

	TEST_CHECK(gs1_lookupAIentry(ctx, "2367", 4) == NULL);				// Don't vivify (2367) since AI prefix "23" is defined as having length 3
	TEST_CHECK(gs1_lookupAIentry(ctx, "236", 3) == &unknownAI3);			// So (236) is okay, requiring FNC1

	TEST_CHECK(gs1_lookupAIentry(ctx, "4199", 4) == NULL);				// Don't vivify (4199) since AI prefix "41" is defined as having length 3
	TEST_CHECK(gs1_lookupAIentry(ctx, "419", 3) == &unknownAI3fixed13);		// So (419) is okay, not requiring FNC1

	gs1_encoder_free(ctx);

}


void test_ai_checkAIlengthByPrefix(void) {

	gs1_encoder* ctx;
	TEST_ASSERT((ctx = gs1_encoder_init(NULL)) != NULL);
	assert(ctx);

	TEST_CHECK(aiLengthByPrefix(ctx, "00") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "01") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "02") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "10") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "11") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "12") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "13") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "15") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "16") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "17") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "20") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "21") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "22") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "23") == 3);
	TEST_CHECK(aiLengthByPrefix(ctx, "24") == 3);
	TEST_CHECK(aiLengthByPrefix(ctx, "25") == 3);
	TEST_CHECK(aiLengthByPrefix(ctx, "30") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "31") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "32") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "33") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "34") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "35") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "36") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "37") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "39") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "40") == 3);
	TEST_CHECK(aiLengthByPrefix(ctx, "41") == 3);
	TEST_CHECK(aiLengthByPrefix(ctx, "42") == 3);
	TEST_CHECK(aiLengthByPrefix(ctx, "43") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "70") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "71") == 3);
	TEST_CHECK(aiLengthByPrefix(ctx, "72") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "80") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "81") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "82") == 4);
	TEST_CHECK(aiLengthByPrefix(ctx, "90") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "91") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "92") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "93") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "94") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "95") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "96") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "97") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "98") == 2);
	TEST_CHECK(aiLengthByPrefix(ctx, "99") == 2);

	gs1_encoder_free(ctx);

}


void test_ai_AItableVsPrefixLength(void) {
	const struct aiEntry *entry;

	gs1_encoder* ctx;
	TEST_ASSERT((ctx = gs1_encoder_init(NULL)) != NULL);
	assert(ctx);

	for (entry = ctx->aiTable; *entry->ai; entry++) {
		TEST_CASE(entry->ai);
		TEST_CHECK(strlen(entry->ai) == aiLengthByPrefix(ctx, entry->ai));
		TEST_MSG("Expected %d; Got %d", aiLengthByPrefix(ctx, entry->ai), strlen(entry->ai));
	}

	gs1_encoder_free(ctx);

}

void test_ai_AItableVsIsFNC1required(void) {
	const struct aiEntry *entry;

	gs1_encoder* ctx;
	TEST_ASSERT((ctx = gs1_encoder_init(NULL)) != NULL);
	assert(ctx);

	for (entry = ctx->aiTable; *entry->ai; entry++) {
		TEST_CASE(entry->ai);
		TEST_CHECK(entry->fnc1 == (valLengthByPrefix(entry->ai) == 0));
		TEST_MSG("Prefix list: %d; AI table: %d", (valLengthByPrefix(entry->ai) == 0), entry->fnc1);
	}

	gs1_encoder_free(ctx);

}


static void do_test_parseAIdata(gs1_encoder* const ctx, const char* const file, const int line, const bool should_succeed, const char* const aiData, const char* const expect) {

	char out[256];
	char casename[256];

	snprintf(casename, sizeof(casename), "%s:%d: %s => %s", file, line, aiData, expect);
	TEST_CASE(casename);

	ctx->numAIs = 0;
	TEST_CHECK(gs1_parseAIdata(ctx, aiData, out) ^ (!should_succeed));
	if (should_succeed)
		TEST_CHECK(strcmp(out, expect) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected: %s; Err: %s", aiData, out, expect, ctx->errMsg);

}


/*
 *  Convert a bracketed AI string to a regular AI string "^..."
 *
 */
void test_ai_parseAIdata(void) {

	gs1_encoder* ctx;
	TEST_ASSERT((ctx = gs1_encoder_init(NULL)) != NULL);
	assert(ctx);

#define test_parseAIdata(s, d, e) do {					\
	do_test_parseAIdata(ctx, __FILE__, __LINE__, s, d, e);		\
} while (0)

	test_parseAIdata(true,  "(01)12345678901231", "^0112345678901231");
	test_parseAIdata(true,  "(10)12345", "^1012345");
	test_parseAIdata(true,  "(01)12345678901231(10)12345", "^01123456789012311012345");		// No FNC1 after (01)
	test_parseAIdata(true,  "(3100)123456(10)12345", "^31001234561012345");				// No FNC1 after (3100)
	test_parseAIdata(true,  "(10)12345(11)991225", "^1012345^11991225");				// FNC1 after (10)
	test_parseAIdata(true,  "(3900)12345(11)991225", "^390012345^11991225");				// FNC1 after (3900)
	test_parseAIdata(true,  "(10)12345\\(11)991225", "^1012345(11)991225");				// Escaped bracket
	test_parseAIdata(true,  "(10)12345\\(", "^1012345(");						// At end if fine

	test_parseAIdata(false, "(10)(11)98765", "");							// Value must not be empty
	test_parseAIdata(false, "(10)12345(11)", "");							// Value must not be empty
	test_parseAIdata(false, "(1A)12345", "");								// AI must be numeric
	test_parseAIdata(false, "1(12345", "");								// Must start with AI
	test_parseAIdata(false, "12345", "");								// Must start with AI
	test_parseAIdata(false, "()12345", "");								// AI too short
	test_parseAIdata(false, "(1)12345", "");								// AI too short
	test_parseAIdata(false, "(12345)12345", "");							// AI too long
	test_parseAIdata(false, "(15", "");								// AI must terminate
	test_parseAIdata(false, "(1", "");									// AI must terminate
	test_parseAIdata(false, "(", "");									// AI must terminate
	test_parseAIdata(false, "(01)123456789012312(10)12345", "");					// Fixed-length AI too long
	test_parseAIdata(false, "(10)12345^", "");								// Reject "^": Conflated with FNC1
	test_parseAIdata(false, "(17)9(90)217", "");							// Should not parse to ^7990217

	gs1_encoder_free(ctx);

#undef test_parseAIdata

}


static void test_linters(gs1_encoder* const ctx, const char* const aiData, gs1_lint_err_t expect) {

	char out[256];
	char casename[256];

	strcpy(casename, aiData);
	TEST_CASE(casename);

	ctx->numAIs = 0;
	TEST_CHECK(gs1_parseAIdata(ctx, aiData, out) || ctx->linterErr != GS1_LINTER_OK);
	TEST_MSG("Parse failed for non-linter reasons. Err: %s", ctx->errMsg);

	TEST_CHECK(ctx->linterErr == expect);
	TEST_MSG("Got: %d; Expected: %d", ctx->linterErr, expect);

}

void test_ai_linters(void) {

	size_t i;

	struct linter_test_s {
		char *aiData;
		gs1_lint_err_t linterErr;
	};

	/*
	 *  The linter functions are fully exercised by their own test
	 *  framework. Here we just trigger each linter errors using a real
	 *  AI.
	 *
	 */
	static const struct linter_test_s tests[] = {
		{ "(00)123456789012345675",				GS1_LINTER_OK },
		{ "(00)A23456789012345675",				GS1_LINTER_NON_DIGIT_CHARACTER },
		{ "(10) ",						GS1_LINTER_INVALID_CSET82_CHARACTER },
		{ "(8010)123456_",					GS1_LINTER_INVALID_CSET39_CHARACTER },
		{ "(8013)123456ABOO",					GS1_LINTER_INVALID_CSET32_CHARACTER },
		{ "(8030)ABC:123",					GS1_LINTER_INVALID_CSET64_CHARACTER },
		{ "(8030)123=",						GS1_LINTER_INVALID_CSET64_PADDING },
		{ "(00)123456789012345670",				GS1_LINTER_INCORRECT_CHECK_DIGIT },
		{ "(00)123456789012345675",				GS1_LINTER_OK },
//		GS1_LINTER_TOO_SHORT_FOR_CHECK_DIGIT			No variable-length csum components at present
		{ "(8013)123456ABXX",					GS1_LINTER_INCORRECT_CHECK_PAIR },
		{ "(8013)A",						GS1_LINTER_TOO_SHORT_FOR_CHECK_PAIR },
//		GS1_LINTER_TOO_LONG_FOR_CHECK_PAIR_IMPLEMENTATION	Parse-time length check prevent this
//		GS1_LINTER_GCP_DATASOURCE_OFFLINE			Not possible to trigger with default implementation
		{ "(401)123",						GS1_LINTER_TOO_SHORT_FOR_KEY },
		{ "(7023)12A4",						GS1_LINTER_INVALID_GCP_PREFIX },
//		GS1_LINTER_IMPORTER_IDX_MUST_BE_ONE_CHARACTER		Parse-time length checks prevent this
		{ "(7040)1AB=",						GS1_LINTER_INVALID_IMPORT_IDX_CHARACTER },
		{ "(8001)12340000012311",				GS1_LINTER_ILLEGAL_ZERO_VALUE },
		{ "(8003)112345678901281234567890123456",		GS1_LINTER_NOT_ZERO },
		{ "(8011)023456789012",					GS1_LINTER_ILLEGAL_ZERO_PREFIX },
		{ "(4321)2",						GS1_LINTER_NOT_ZERO_OR_ONE },
		{ "(8001)12341234512321",				GS1_LINTER_INVALID_WINDING_DIRECTION },
		{ "(426)987",						GS1_LINTER_NOT_ISO3166 },
		{ "(7030)987ABC",					GS1_LINTER_NOT_ISO3166_OR_999 },
		{ "(4307)AA",						GS1_LINTER_NOT_ISO3166_ALPHA2 },
		{ "(3910)9870",						GS1_LINTER_NOT_ISO4217 },
		{ "(8007)AB1234",					GS1_LINTER_IBAN_TOO_SHORT },
		{ "(8007)FR12_45678901234",				GS1_LINTER_INVALID_IBAN_CHARACTER },
		{ "(8007)AB12345678901234",				GS1_LINTER_ILLEGAL_IBAN_COUNTRY_CODE },
		{ "(8007)FR12345678901234",				GS1_LINTER_INCORRECT_IBAN_CHECKSUM },
//		GS1_LINTER_DATE_TOO_SHORT				Parse-time length checks prevent this
//		GS1_LINTER_DATE_TOO_LONG				Parse-time length checks prevent this
//		GS1_LINTER_DATE_WITH_HOUR_TOO_SHORT			Parse-time length checks prevent this
//		GS1_LINTER_DATE_WITH_HOUR_TOO_LONG			Parse-time length checks prevent this
//		GS1_LINTER_HOUR_WITH_MINUTE_TOO_SHORT			Parse-time length checks prevent this
//		GS1_LINTER_HOUR_WITH_MINUTE_TOO_LONG			Parse-time length checks prevent this
//		GS1_LINTER_HOUR_TOO_SHORT				Parse-time length checks prevent this
//		GS1_LINTER_HOUR_TOO_LONG				Parse-time length checks prevent this
//		GS1_LINTER_MINUTE_TOO_SHORT				Parse-time length checks prevent this
//		GS1_LINTER_MINUTE_TOO_LONG				Parse-time length checks prevent this
//		GS1_LINTER_SECOND_TOO_SHORT				Parse-time length checks prevent this
//		GS1_LINTER_SECOND_TOO_LONG				Parse-time length checks prevent this
		{ "(4326)201300",					GS1_LINTER_ILLEGAL_MONTH },
		{ "(4326)201200",					GS1_LINTER_ILLEGAL_DAY },
		{ "(4324)2012252400",					GS1_LINTER_ILLEGAL_HOUR },
		{ "(4324)2012252360",					GS1_LINTER_ILLEGAL_MINUTE },
		{ "(8008)201225230060",					GS1_LINTER_ILLEGAL_SECOND },
//		GS1_LINTER_INVALID_LENGTH_FOR_PIECE_OF_TOTAL		Parse-time length checks prevent this
		{ "(8026)123456789012310099",				GS1_LINTER_ZERO_PIECE_NUMBER },
		{ "(8026)123456789012310100",				GS1_LINTER_ZERO_TOTAL_PIECES },
		{ "(8026)123456789012310302",				GS1_LINTER_PIECE_NUMBER_EXCEEDS_TOTAL },
		{ "(4300)ABC%0g",					GS1_LINTER_INVALID_PERCENT_SEQUENCE },
//		GS1_LINTER_COUPON_MISSING_FORMAT_CODE			Parse-time length checks prevent this
		{ "(8112)201234561234560123456",			GS1_LINTER_COUPON_INVALID_FORMAT_CODE },
		{ "(8112)0",						GS1_LINTER_COUPON_MISSING_FUNDER_VLI },
		{ "(8112)07",						GS1_LINTER_COUPON_INVALID_FUNDER_LENGTH },
		{ "(8112)01123456",					GS1_LINTER_COUPON_TRUNCATED_FUNDER },
		{ "(8112)00123456",					GS1_LINTER_COUPON_TRUNCATED_OFFER_CODE },
		{ "(8112)00123456123456",				GS1_LINTER_COUPON_MISSING_SERIAL_NUMBER_VLI },
		{ "(8112)001234561234560",				GS1_LINTER_COUPON_TRUNCATED_SERIAL_NUMBER },
//		GS1_LINTER_COUPON_MISSING_GCP_VLI			Parse-time length checks prevent this
		{ "(8110)71234567890123",				GS1_LINTER_COUPON_INVALID_GCP_LENGTH },
		{ "(8110)012345",					GS1_LINTER_COUPON_TRUNCATED_GCP },
		{ "(8110)0123456123456",				GS1_LINTER_COUPON_MISSING_SAVE_VALUE_VLI },
		{ "(8110)01234561234560",				GS1_LINTER_COUPON_INVALID_SAVE_VALUE_LENGTH },
		{ "(8110)01234561234561",				GS1_LINTER_COUPON_TRUNCATED_SAVE_VALUE },
		{ "(8110)012345612345611",				GS1_LINTER_COUPON_MISSING_1ST_PURCHASE_REQUIREMENT_VLI },
		{ "(8110)0123456123456116123456",			GS1_LINTER_COUPON_INVALID_1ST_PURCHASE_REQUIREMENT_LENGTH },
		{ "(8110)0123456123456111",				GS1_LINTER_COUPON_TRUNCATED_1ST_PURCHASE_REQUIREMENT },
		{ "(8110)01234561234561111",				GS1_LINTER_COUPON_MISSING_1ST_PURCHASE_REQUIREMENT_CODE },
		{ "(8110)012345612345611115",				GS1_LINTER_COUPON_INVALID_1ST_PURCHASE_REQUIREMENT_CODE },
		{ "(8110)012345612345611119",				GS1_LINTER_COUPON_TRUNCATED_1ST_PURCHASE_FAMILY_CODE },
		{ "(8110)0123456123456111101231",			GS1_LINTER_COUPON_MISSING_ADDITIONAL_PURCHASE_RULES_CODE },
		{ "(8110)01234561234561111012314",			GS1_LINTER_COUPON_INVALID_ADDITIONAL_PURCHASE_RULES_CODE },
		{ "(8110)01234561234561111012310",			GS1_LINTER_COUPON_MISSING_2ND_PURCHASE_REQUIREMENT_VLI },
		{ "(8110)012345612345611110123106123456",		GS1_LINTER_COUPON_INVALID_2ND_PURCHASE_REQUIREMENT_LENGTH },
		{ "(8110)012345612345611110123101",			GS1_LINTER_COUPON_TRUNCATED_2ND_PURCHASE_REQUIREMENT },
		{ "(8110)0123456123456111101231011",			GS1_LINTER_COUPON_MISSING_2ND_PURCHASE_REQUIREMENT_CODE },
		{ "(8110)01234561234561111012310115",			GS1_LINTER_COUPON_INVALID_2ND_PURCHASE_REQUIREMENT_CODE },
		{ "(8110)01234561234561111012310119",			GS1_LINTER_COUPON_TRUNCATED_2ND_PURCHASE_FAMILY_CODE },
		{ "(8110)01234561234561111012310110123",		GS1_LINTER_COUPON_MISSING_2ND_PURCHASE_GCP_VLI },
		{ "(8110)0123456123456111101231011012371234567890123",	GS1_LINTER_COUPON_INVALID_2ND_PURCHASE_GCP_LENGTH },
		{ "(8110)012345612345611110123101101230",		GS1_LINTER_COUPON_TRUNCATED_2ND_PURCHASE_GCP },
		{ "(8110)0123456123456111101232",			GS1_LINTER_COUPON_MISSING_3RD_PURCHASE_REQUIREMENT_VLI },
		{ "(8110)01234561234561111012320",			GS1_LINTER_COUPON_INVALID_3RD_PURCHASE_REQUIREMENT_LENGTH },
		{ "(8110)01234561234561111012321",			GS1_LINTER_COUPON_TRUNCATED_3RD_PURCHASE_REQUIREMENT },
		{ "(8110)012345612345611110123211",			GS1_LINTER_COUPON_MISSING_3RD_PURCHASE_REQUIREMENT_CODE },
		{ "(8110)0123456123456111101232115",			GS1_LINTER_COUPON_INVALID_3RD_PURCHASE_REQUIREMENT_CODE },
		{ "(8110)0123456123456111101232110",			GS1_LINTER_COUPON_TRUNCATED_3RD_PURCHASE_FAMILY_CODE },
		{ "(8110)0123456123456111101232110123",			GS1_LINTER_COUPON_MISSING_3RD_PURCHASE_GCP_VLI },
		{ "(8110)012345612345611110123211012371234567890123",	GS1_LINTER_COUPON_INVALID_3RD_PURCHASE_GCP_LENGTH },
		{ "(8110)01234561234561111012321101230",		GS1_LINTER_COUPON_TRUNCATED_3RD_PURCHASE_GCP },
		{ "(8110)0123456123456111101233",			GS1_LINTER_COUPON_TOO_SHORT_FOR_EXPIRATION_DATE },
		{ "(8110)0123456123456111101233200010",			GS1_LINTER_COUPON_INVALID_EXIPIRATION_DATE },
		{ "(8110)0123456123456111101234",			GS1_LINTER_COUPON_TOO_SHORT_FOR_START_DATE },
		{ "(8110)0123456123456111101234200010",			GS1_LINTER_COUPON_INVALID_START_DATE },
		{ "(8110)01234561234561111012335006064500607",		GS1_LINTER_COUPON_EXPIRATION_BEFORE_START },
		{ "(8110)0123456123456111101236",			GS1_LINTER_COUPON_MISSING_RETAILER_GCP_OR_GLN_VLI },
		{ "(8110)01234561234561111012360",			GS1_LINTER_COUPON_INVALID_RETAILER_GCP_OR_GLN_LENGTH },
		{ "(8110)01234561234561111012361",			GS1_LINTER_COUPON_TRUNCATED_RETAILER_GCP_OR_GLN },
		{ "(8110)0123456123456111101239",			GS1_LINTER_COUPON_MISSING_SAVE_VALUE_CODE },
		{ "(8110)01234561234561111012393",			GS1_LINTER_COUPON_INVALID_SAVE_VALUE_CODE },
		{ "(8110)01234561234561111012390",			GS1_LINTER_COUPON_MISSING_SAVE_VALUE_APPLIES_TO_ITEM },
		{ "(8110)012345612345611110123903",			GS1_LINTER_COUPON_INVALID_SAVE_VALUE_APPLIES_TO_ITEM },
		{ "(8110)012345612345611110123900",			GS1_LINTER_COUPON_MISSING_STORE_COUPON_FLAG },
		{ "(8110)0123456123456111101239000",			GS1_LINTER_COUPON_MISSING_DONT_MULTIPLY_FLAG },
		{ "(8110)01234561234561111012390002",			GS1_LINTER_COUPON_INVALID_DONT_MULTIPLY_FLAG },
		{ "(8110)012345612345611110123900000",			GS1_LINTER_COUPON_EXCESS_DATA },
//		GS1_LINTER_LATLONG_INVALID_LENGTH			Parse-time length checks prevent this
		{ "(4309)18000000010000000000",				GS1_LINTER_INVALID_LATITUDE },
		{ "(4309)00000000003600000001",				GS1_LINTER_INVALID_LONGITUDE },
		{ "(4330)000000X",					GS1_LINTER_NOT_HYPHEN },
		{ "(7252)5",						GS1_LINTER_INVALID_BIOLOGICAL_SEX_CODE },
		{ "(7258)111",						GS1_LINTER_POSITION_IN_SEQUENCE_MALFORMED },
		{ "(7258)0/3",						GS1_LINTER_ILLEGAL_ZERO_PREFIX },
		{ "(7258)2/1",						GS1_LINTER_POSITION_EXCEEDS_END },

		// Multiple AIs
		{ "(01)95012345678903(3103)000123",			GS1_LINTER_OK },
		{ "(01)95012345678902(3103)000123",			GS1_LINTER_INCORRECT_CHECK_DIGIT },
		{ "(01)95012345678903(11)131313",			GS1_LINTER_ILLEGAL_MONTH },
	};

	gs1_encoder* ctx;
	TEST_ASSERT((ctx = gs1_encoder_init(NULL)) != NULL);
	assert(ctx);

	for (i = 0; i < SIZEOF_ARRAY(tests); i++)
		test_linters(ctx, tests[i].aiData, tests[i].linterErr);

	gs1_encoder_free(ctx);

}

static void do_test_processAIdata(gs1_encoder* const ctx, const char* const file, const int line, const bool should_succeed, const char* const dataStr) {

	char casename[256];

	snprintf(casename, sizeof(casename), "%s:%d: %s", file, line, dataStr);
	TEST_CASE(casename);

	// Process and extract AIs
	TEST_CHECK(gs1_processAIdata(ctx, dataStr, true) ^ (!should_succeed));
	TEST_MSG(ctx->errMsg);

}

void test_ai_processAIdata(void) {

	gs1_encoder* ctx;
	TEST_ASSERT((ctx = gs1_encoder_init(NULL)) != NULL);
	assert(ctx);

#define test_processAIdata(s, d) do {					\
	do_test_processAIdata(ctx, __FILE__, __LINE__, s, d);		\
} while (0)

	test_processAIdata(false, "");						// No FNC1 in first position
	test_processAIdata(false, "991234");					// No FNC1 in first position
	test_processAIdata(false, "^");						// FNC1 in first but no AIs
	test_processAIdata(false, "^891234");					// No such AI

	test_processAIdata(true,  "^991234");

	test_processAIdata(false, "^99~ABC");					// Bad CSET82 character
 	test_processAIdata(false, "^99ABC~");					// Bad CSET82 character

	test_processAIdata(true,  "^0112345678901231");				// N14, no FNC1 required
	test_processAIdata(false, "^01A2345678901231");				// Bad numeric character
	test_processAIdata(false, "^011234567890123A");				// Bad numeric character
	test_processAIdata(false, "^0112345678901234");				// Incorrect check digit (csum linter)
	test_processAIdata(false, "^011234567890123");				// Too short
	test_processAIdata(false, "^01123456789012312");				// No such AI (2). Can't be "too long" since FNC1 not required

	test_processAIdata(true,  "^0112345678901231^");				// Tolerate superflous FNC1
	test_processAIdata(false, "^011234567890123^");				// Short, with superflous FNC1
	test_processAIdata(false, "^01123456789012345^");				// Long, with superflous FNC1 (no following AIs)
	test_processAIdata(false, "^01123456789012345^991234");			// Long, with superflous FNC1 and meaningless AI (5^..)

	test_processAIdata(true,  "^0112345678901231991234");			// Fixed-length, run into next AI (01)...(99)...
	test_processAIdata(true,  "^0112345678901231^991234");			// Tolerate superflous FNC1

	test_processAIdata(true,  "^2421");					// N1..6; FNC1 required
	test_processAIdata(true,  "^24212");
	test_processAIdata(true,  "^242123");
	test_processAIdata(true,  "^2421234");
	test_processAIdata(true,  "^24212345");
	test_processAIdata(true,  "^242123456");
	test_processAIdata(true,  "^242123456^10ABC123");				// Limit, then following AI
	test_processAIdata(true,  "^242123456^");					// Tolerant of FNC1 at end of data
	test_processAIdata(false, "^2421234567");					// Data too long

	test_processAIdata(true,  "^81111234");					// N4; FNC1 required
	test_processAIdata(false, "^8111123");					// Too short
	test_processAIdata(false, "^811112345");					// Too long
	test_processAIdata(true,  "^81111234^10ABC123");				// Followed by another AI

	test_processAIdata(true,  "^800112341234512398");				// N4-5-3-1-1; FNC1 required
	test_processAIdata(false, "^80011234123451239");				// Too short
	test_processAIdata(false, "^8001123412345123981");				// Too long
	test_processAIdata(true,  "^800112341234512398^0112345678901231");
	test_processAIdata(false, "^80011234123451239^0112345678901231");		// Too short
	test_processAIdata(false, "^8001123412345123981^01123456789012312");	// Too long

	test_processAIdata(true,  "^7007211225211231");				// N6 [N6]; FNC1 required
	test_processAIdata(true,  "^7007211225");					// No optional component
	test_processAIdata(false, "^70072112252");					// Incorrect length
	test_processAIdata(false, "^700721122521");				// Incorrect length
	test_processAIdata(false, "^7007211225211");				// Incorrect length
	test_processAIdata(false, "^70072112252112");				// Incorrect length
	test_processAIdata(false, "^700721122521123");				// Incorrect length
	test_processAIdata(false, "^70072112252212311");				// Too long

	test_processAIdata(true,  "^800302112345678900ABC");			// N1 N13,csum X0..16; FNC1 required
	test_processAIdata(false, "^800302112345678901ABC");			// Bad check digit on N13 component
	test_processAIdata(true,  "^800302112345678900");				// Empty final component
	test_processAIdata(true,  "^800302112345678900^10ABC123");			// Empty final component and following AI
	test_processAIdata(true,  "^800302112345678900ABCDEFGHIJKLMNOP");		// Empty final component and following AI
	test_processAIdata(false, "^800302112345678900ABCDEFGHIJKLMNOPQ");		// Empty final component and following AI

	test_processAIdata(true,  "^7230121234567890123456789012345678");		// X2 X1..28; FNC1 required
	test_processAIdata(false, "^72301212345678901234567890123456789");		// Too long
	test_processAIdata(true,  "^7230123");					// Shortest
	test_processAIdata(false, "^723012");					// Too short

	test_processAIdata(false, "^423");						// List of 3-digit ISO-3166 codes
	test_processAIdata(false, "^4235");
	test_processAIdata(false, "^42352");
	test_processAIdata(true,  "^423528");
	test_processAIdata(false, "^4235285");
	test_processAIdata(false, "^42352852");
	test_processAIdata(true,  "^423528528");
	test_processAIdata(false, "^4235285285");
	test_processAIdata(false, "^42352852852");
	test_processAIdata(true,  "^423528528528");
	test_processAIdata(false, "^4235285285285");
	test_processAIdata(false, "^42352852852852");
	test_processAIdata(true,  "^423528528528528");
	test_processAIdata(false, "^4235285285285285");
	test_processAIdata(false, "^42352852852852852");
	test_processAIdata(true,  "^423528528528528528");
	test_processAIdata(false,  "^4235285285285285285");			// Too long

	// Unlike parsed data input, we cannot vivify unknown AIs when
	// extracting AI data from a raw string
	gs1_encoder_setPermitUnknownAIs(ctx, true);
	test_processAIdata(false, "^891234");

#undef test_processAIdata

	gs1_encoder_free(ctx);

}

static void do_test_validateAIs(gs1_encoder* const ctx, const char* const file, const int line, const bool should_succeed, gs1_encoder_validation_func_t fn, const char* const aiData) {

	bool ret;
	char out[256];
	char casename[256];

	snprintf(casename, sizeof(casename), "%s:%d: %s", file, line, aiData);
	TEST_CASE(casename);

	ctx->numAIs = 0;
	TEST_CHECK((ret = gs1_parseAIdata(ctx, aiData, out)) == true);
	TEST_MSG("Parse failed for non-pair validation reasons. Err: %s", ctx->errMsg);
	if (!ret)
		return;

	if (!should_succeed) {
		TEST_CHECK(!fn(ctx));
		return;
	}

	TEST_CHECK(fn(ctx));
	TEST_MSG("Expected success. Got: %s", ctx->errMsg);

}

void test_ai_validateAIs(void) {

	gs1_encoder* ctx;
	TEST_ASSERT((ctx = gs1_encoder_init(NULL)) != NULL);
	assert(ctx);

#define test_validateAIs(s, f, d) do {					\
	do_test_validateAIs(ctx, __FILE__, __LINE__, s, f, d);		\
} while (0)

	gs1_encoder_setPermitUnknownAIs(ctx, true);
	assert(ctx);

	/*
	 * Test for repeated attributes
	 *
	 */
	test_validateAIs(true,  validateAIrepeats, "(400)ABC");
	test_validateAIs(true,  validateAIrepeats, "(400)ABC(400)ABC");
	test_validateAIs(true,  validateAIrepeats, "(400)ABC(99)DEF(400)ABC");
	test_validateAIs(true,  validateAIrepeats, "(99)ABC(400)XYZ(400)XYZ");
	test_validateAIs(false, validateAIrepeats, "(400)ABC(400)AB");
	test_validateAIs(false, validateAIrepeats, "(400)ABC(400)ABCD");
	test_validateAIs(false, validateAIrepeats, "(400)ABC(400)ABC(400)XYZ");
	test_validateAIs(false, validateAIrepeats, "(400)ABC(400)XYZ(400)ABC");
	test_validateAIs(false, validateAIrepeats, "(400)ABC(400)XYZ(400)XYZ");
	test_validateAIs(false, validateAIrepeats, "(400)ABC(99)DEF(400)XYZ");
	test_validateAIs(false, validateAIrepeats, "(99)ABC(400)ABC(400)XYZ");
	test_validateAIs(true,  validateAIrepeats, "(89)ABC(89)ABC(89)ABC");
	test_validateAIs(false, validateAIrepeats, "(89)ABC(89)ABC(89)XYZ");
	test_validateAIs(false, validateAIrepeats, "(89)ABC(89)XYZ(89)ABC");
	test_validateAIs(false, validateAIrepeats, "(89)ABC(89)XYZ(89)XYZ");
	test_validateAIs(false, validateAIrepeats, "(89)ABC(89)AB(89)ABC");
	test_validateAIs(false, validateAIrepeats, "(89)ABC(89)ABCD(89)ABC");


	/*
	 * "Ex" attribute
	 *
	 */
	test_validateAIs(false, validateAImutex, "(01)12345678901231(02)12345678901231");
	test_validateAIs(false, validateAImutex, "(99)ABC123(01)12345678901231(02)12345678901231");
	test_validateAIs(false, validateAImutex, "(01)12345678901231(99)ABC123(02)12345678901231");
	test_validateAIs(false, validateAImutex, "(01)12345678901231(02)12345678901231(99)ABC123");
	test_validateAIs(false, validateAImutex, "(01)12345678901231(255)5412345000150");
	test_validateAIs(false, validateAImutex, "(01)12345678901231(37)123");
	test_validateAIs(false, validateAImutex, "(21)ABC123(235)XYZ");
	test_validateAIs(false, validateAImutex, "(3940)1234(8111)9999");
	test_validateAIs(false, validateAImutex, "(3940)1234(3941)9999");	// Match by "394n", ignoring self
	test_validateAIs(false, validateAImutex, "(3955)123456(3929)123");	// Match by "392n"


	/*
	 * "Req" attributes
	 *
	 */

	// (02) req=37; (37) req=02,8026
	test_validateAIs(false, validateAIrequisites, "(02)12345678901231");
	test_validateAIs(false, validateAIrequisites, "(02)12345678901231(37)123");
	test_validateAIs(false, validateAIrequisites, "(99)AAA(02)12345678901231(37)123");
	test_validateAIs(false, validateAIrequisites, "(02)12345678901231(99)AAA(37)123");
	test_validateAIs(false, validateAIrequisites, "(02)12345678901231(37)123(99)AAA");
	test_validateAIs(true,  validateAIrequisites, "(02)12345678901231(37)123(00)123456789012345675");
	test_validateAIs(true,  validateAIrequisites, "(91)XXX(02)12345678901231(92)YYY(37)123(93)ZZZ(00)123456789012345675");

	// (21) req=01,8006
	test_validateAIs(false, validateAIrequisites, "(21)ABC123");
	test_validateAIs(true,  validateAIrequisites, "(21)ABC123(01)12345678901231");
	test_validateAIs(true,  validateAIrequisites, "(21)ABC123(8006)123456789012310510");

	// (250) req=01,8006 req=21
	test_validateAIs(false, validateAIrequisites, "(01)12345678901231(250)ABC123");
	test_validateAIs(true,  validateAIrequisites, "(01)12345678901231(21)XYZ999(250)ABC123");

	// (392n) req=01 req=30,31nn,32nn,35nn,36nn
	test_validateAIs(false, validateAIrequisites, "(01)12345678901231(3925)12599");
	test_validateAIs(true,  validateAIrequisites, "(01)12345678901231(3925)12599(30)123");
	test_validateAIs(true,  validateAIrequisites, "(01)12345678901231(3925)12599(3100)654321");
	test_validateAIs(true,  validateAIrequisites, "(01)12345678901231(3925)12599(3105)654321");
	test_validateAIs(true,  validateAIrequisites, "(01)12345678901231(3925)12599(3160)654321");
	test_validateAIs(true,  validateAIrequisites, "(01)12345678901231(3925)12599(3165)654321");
	test_validateAIs(true,  validateAIrequisites, "(01)12345678901231(3925)12599(3295)654321");
	test_validateAIs(true,  validateAIrequisites, "(01)12345678901231(3925)12599(3500)654321");
	test_validateAIs(true,  validateAIrequisites, "(01)12345678901231(3925)12599(3575)654321");
	test_validateAIs(true,  validateAIrequisites, "(01)12345678901231(3925)12599(3600)654321");
	test_validateAIs(true,  validateAIrequisites, "(01)12345678901231(3925)12599(3695)654321");

	// (8030) req=00,01+21,253,255,8003,8004,8006+21,8010+8011,8017,8018
	test_validateAIs(false, validateAIrequisites, "(8030)DIGSIG");
	test_validateAIs(true,  validateAIrequisites, "(8030)DIGSIG(00)123456789012345675");
	test_validateAIs(false, validateAIrequisites, "(8030)DIGSIG(01)12345678901231");
	test_validateAIs(true,  validateAIrequisites, "(8030)DIGSIG(01)12345678901231(21)ABC123");
	test_validateAIs(true,  validateAIrequisites, "(8030)DIGSIG(253)1234567890128X");
	test_validateAIs(true,  validateAIrequisites, "(8030)DIGSIG(255)12345678901280");
	test_validateAIs(true,  validateAIrequisites, "(8030)DIGSIG(8003)01234567890128X");
	test_validateAIs(true,  validateAIrequisites, "(8030)DIGSIG(8004)01234567890");
	test_validateAIs(false, validateAIrequisites, "(8030)DIGSIG(8006)123456789012310102");
	test_validateAIs(true,  validateAIrequisites, "(8030)DIGSIG(8006)123456789012310102(21)ABC123");
	test_validateAIs(false, validateAIrequisites, "(8030)DIGSIG(8010)1234567890");
	test_validateAIs(true,  validateAIrequisites, "(8030)DIGSIG(8010)1234567890(8011)123456789012");
	test_validateAIs(true,  validateAIrequisites, "(8030)DIGSIG(8017)123456789012345675");
	test_validateAIs(true,  validateAIrequisites, "(8030)DIGSIG(8018)123456789012345675");


	/*
	 * AI (8030) digital signatures require serialised components with AIs (253), (255) and (8003)
	 *
	 */

	test_validateAIs(true,  validateDigSigRequiresSerialisedKey, "(253)1234567890128");
	test_validateAIs(false, validateDigSigRequiresSerialisedKey, "(253)1234567890128(8030)ABC123");
	test_validateAIs(true,  validateDigSigRequiresSerialisedKey, "(253)1234567890128X(8030)ABC123");

	test_validateAIs(true,  validateDigSigRequiresSerialisedKey, "(255)1234567890128");
	test_validateAIs(false, validateDigSigRequiresSerialisedKey, "(255)1234567890128(8030)ABC123");
	test_validateAIs(true,  validateDigSigRequiresSerialisedKey, "(255)12345678901280(8030)ABC123");

	test_validateAIs(true,  validateDigSigRequiresSerialisedKey, "(8003)01234567890128");
	test_validateAIs(false, validateDigSigRequiresSerialisedKey, "(8003)01234567890128(8030)ABC123");
	test_validateAIs(true,  validateDigSigRequiresSerialisedKey, "(8003)01234567890128X(8030)ABC123");

#undef test_validateAIs

	gs1_encoder_free(ctx);

}


#endif  /* UNIT_TESTS */

