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
#include <ctype.h>

#include "syntax/gs1syntaxdictionary.h"
#include "gs1encoders.h"
#include "enc-private.h"
#include "debug.h"
#include "dl.h"
#include "tr.h"


#define CANONICAL_DL_STEM "https://id.gs1.org"
#define DL_KEY_QUALIFIER_INITIAL_CAPACITY 50


/*
 *  Set of characters that are permissible in URIs, including percent
 *
 */
static const char *uriCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~:/?#[]@!$&'()*+,;=%";


/*
 *  Set of unreserved characters that do not require escaping when used in URI
 *  components (path elements and query parameter values)
 *
 */
static const char *uriUnreservedCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";


/*
 *  Characters from uriCharacters that are illegal within a domain name
 *
 */
static const char *badDomainCharacters = "_~?#@!$&'()*+,;=%";


/*
 *  Load the list of valid DL key-qualifier associations from the attrs of the
 *  AI table entries.
 *
 *  We store the set of all valid key/key-qualifier associations as a sorted
 *  array of space-separated AI sequences which we can efficiently search.
 *
 */
static bool addDLkeyQualifiers(gs1_encoder* const ctx, char*** const dlKeyQualifiers, size_t* const pos, size_t* const cap, const char* const key, const char* const qualifiers) {

	int i, j, num;
	size_t req;
	char buf[MAX_AI_ATTR_LEN + 1] = { 0 };
	char qualifiersbuf[MAX_AI_ATTR_LEN + 1] = { 0 };
	char *saveptr = NULL;
	const char *token;
	char **addedQualifiers;

	/*
	 *  Count number of qualifiers passed in
	 *
	 */
	for (i = 0, num = 0; qualifiers[i]; i++)
		if (qualifiers[i] == ',')
			num++;
	if (*qualifiers != '\0')
		num++;

	/*
	 *  Grow dlKeyQualifiers if necessary
	 *
	 */
	for (i = 0, req = 1; i < num; i++, req *= 2);
	if (*pos + req >= *cap) {
		char **reallocDLkeyQualifiers = GS1_ENCODERS_REALLOC(*dlKeyQualifiers, (*pos + req) * sizeof(char *));
		if (!reallocDLkeyQualifiers) {
			SET_ERR(FAILED_TO_REALLOC_FOR_KEY_QUALIFIERS);
			return false;
		}
		*dlKeyQualifiers = reallocDLkeyQualifiers;
		*cap = (*pos + req);

	}

	/*
	 *  Rebase at current position for remainder of function
	 *
	 */
	addedQualifiers = *dlKeyQualifiers + *pos;

	/*
	 *  Create entries for all valid choices of AIs from the full
	 *  key-qualifier sequence
	 *
	 */
	strncat(buf, key, MAX_AI_ATTR_LEN);
	*addedQualifiers = gs1_strdup_alloc(buf);
	if (!*addedQualifiers)
		return false;
	(*pos)++;

	strcpy(qualifiersbuf, qualifiers);
	for (i = 0, j = 1, token = strtok_r(qualifiersbuf, ",", &saveptr);
	     i < num;
	     i++, j *= 2, token = strtok_r(NULL, ",", &saveptr)) {

		int k;

		for (k = 0; k < j; k++) {
			snprintf(buf, sizeof(buf), "%s %s", addedQualifiers[k], token);
			addedQualifiers[k + j] = gs1_strdup_alloc(buf);
			if (!addedQualifiers[k + j])
				return false;
			(*pos)++;
		}

	}

	return true;

}

static int q_cmp(const void* const a, const void* const b) {
	return strcmp(*(const char**)a, *(const char**)b);
}

bool gs1_populateDLkeyQualifiers(gs1_encoder* const ctx) {

	int i = 0;
	size_t pos = 0, cap = DL_KEY_QUALIFIER_INITIAL_CAPACITY;

	char **dlKeyQualifiers = malloc(cap * sizeof(char *));
	if (!dlKeyQualifiers) {
		SET_ERR(FAILED_TO_MALLOC_FOR_KEY_QUALIFIERS);
		return false;
	}

	/*
	 *  Parse "dlpkey" attribute
	 *
	 */
	for (i = 0; i < (int)ctx->aiTableEntries; i++) {

		const char *token;
		char *saveptr = NULL;
		char attrs[MAX_AI_ATTR_LEN + 1] = { 0 };

		strncat(attrs, ctx->aiTable[i].attrs, MAX_AI_ATTR_LEN);
		for (token = strtok_r(attrs, " ", &saveptr);
		     token;
		     token = strtok_r(NULL, " ", &saveptr)) {
			if (strcmp(token, "dlpkey") == 0) {
				if (!addDLkeyQualifiers(ctx, &dlKeyQualifiers,
							&pos, &cap, ctx->aiTable[i].ai, ""))
					goto fail;
			} else if (strncmp(token, "dlpkey=", 7) == 0) {

				char *saveptr2 = NULL;

				for (token = strtok_r((char*)(token+7), "|", &saveptr2);
				     token;
				     token = strtok_r(NULL, " ", &saveptr2))
					if (!addDLkeyQualifiers(ctx, &dlKeyQualifiers,
							&pos, &cap, ctx->aiTable[i].ai, token))
						goto fail;

			}
		}

	}

	/*
	 *  Sort the entries so that we can lookup using a binary search
	 *
	 */
	qsort(dlKeyQualifiers, pos, sizeof(dlKeyQualifiers[0]), q_cmp);

	ctx->dlKeyQualifiers = dlKeyQualifiers;
	ctx->numDLkeyQualifiers = (int)pos;

	return true;

fail:

	/*
	 * Release what we have allocated so far
	 *
	 */
	for (i--; i >= 0; i--)
		GS1_ENCODERS_FREE(dlKeyQualifiers[i]);
	GS1_ENCODERS_FREE(dlKeyQualifiers);

	return false;

}


void gs1_freeDLkeyQualifiers(gs1_encoder* const ctx) {

	int i;

	assert(ctx);

	if (!ctx->dlKeyQualifiers)
		return;

	for (i = 0; i < ctx->numDLkeyQualifiers; i++)
		GS1_ENCODERS_FREE(ctx->dlKeyQualifiers[i]);

	GS1_ENCODERS_FREE(ctx->dlKeyQualifiers);
	ctx->dlKeyQualifiers = NULL;

}


/*
 *  Find an entry in the keyQualifier list matching the given AIs, returning
 *  the position in the list or -1 if missing
 *
 */
static int getDLpathAIseqEntry(gs1_encoder* const ctx, const char seq[MAX_AIS][MAX_AI_LEN+1], const int len) {

	char aiseq[(MAX_AI_LEN+1) * MAX_AIS] = { 0 };
	char *p = aiseq;
	int i;
	size_t s = 0;
	size_t e = (size_t)ctx->numDLkeyQualifiers;

	/*
	 *  Build a space separated AI sequence string
	 *
	 */
	for (i = 0 ; i < len; i++) {
		const int n = snprintf(p, sizeof(aiseq) - (size_t)(p - aiseq), "%s ", seq[i]);
		assert(n >= 1 && n < (int)(sizeof(aiseq) - (size_t)(p - aiseq)));
		p += n;
	}
	*--p = '\0';		// Chop stray space

	/*
	 *  Binary search for string in the list of valid key-qualifier
	 *  associations.
	 *
	 */
	while (s < e) {
		const size_t m = s + (e - s) / 2;
		const int cmp = strcmp(ctx->dlKeyQualifiers[m], aiseq);
		if (cmp == 0)
			return (int)m;
		if (cmp < 0)
			s = m + 1;
		else
			e = m;
	}

	return -1;

}

static inline bool isValidDLpathAIseq(gs1_encoder* const ctx, const char seq[MAX_AIS][MAX_AI_LEN+1], const int len) {
	return getDLpathAIseqEntry(ctx, seq, len) != -1;
}

static inline bool isDLpkey(gs1_encoder* const ctx, const char* const p) {
	char seq[MAX_AIS][MAX_AI_LEN+1] = { { 0 } };
	strcpy(seq[0], p);
	return getDLpathAIseqEntry(ctx, (const char(*)[MAX_AI_LEN+1])seq, 1) != -1;
}


static size_t URIunescape(char* const out, size_t maxlen, const char* const in, const size_t inlen, const bool is_query_component) {

	size_t i, j;

	assert(in);
	assert(out);

	for (i = 0, j = 0; i < inlen && j < maxlen; i++, j++) {
		if (i < inlen - 2 && in[i] == '%' && isxdigit(in[i+1]) && isxdigit(in[i+2])) {
			const char hex[] = { in[i+1], in[i+2], '\0' };
			out[j] = (char)strtoul(hex, NULL, 16);
			if (out[j] == 0)	// Illegal null
				return 0;
			i += 2;
		} else if (is_query_component && in[i] == '+')
			out[j] = ' ';
		else
			out[j] = in[i];
	}
	out[j] = '\0';

	return j;

}


static size_t URIescape(char* const out, const size_t maxlen, const char* const in, const size_t inlen, const bool is_query_component) {

	size_t i, j;

	assert(in);
	assert(out);

	for (i = 0, j = 0; i < inlen && j < maxlen; i++) {
		if (strchr(uriUnreservedCharacters, in[i]))
			out[j++] = in[i];
		else if (in[i] == ' ' && is_query_component)
			out[j++] = '+';
//		else if (in[i] == '+' && !is_query_component)		// Encoding '+' as '%2d' in path info is preferred
//			out[j++] = '+';
		else if (j+2 < maxlen)
			j += (size_t)snprintf(&out[j], 4, "%%%02X", in[i]);
		else
			break;		/* Out of space */
	}
	out[j] = '\0';

	return j;

}


/*
 * Parse a GS1 DL URI, validating the key to key-qualifier associations in the
 * path information, and convert it to a regular AI data string with ^ = FNC1,
 * extracting AI data for HRI purposes.
 *
 * Note: "Convenience alphas" (e.g. "/gtin/0123...", which have been
 * deprecated) are not supported.
 *
 */
bool gs1_parseDLuri(gs1_encoder* const ctx, char* const dlData, char* const dataStr) {

	char* p, *r;
	const char* pi = NULL;	// Path info
	char* qp = NULL;	// Query params
	char* fr = NULL;	// Fragment
	const char* dp = NULL;	// DL path info
	bool ret;
	bool fnc1req = true;
	char pathAIseq[MAX_AIS][MAX_AI_LEN+1] = { { 0 } };	// Sequence of AIs extracted from the path info
	int numPathAIs;

	assert(ctx);
	assert(dlData);

	*dataStr = '\0';
	ctx->err = gs1_encoder_eNO_ERROR;
	*ctx->errMsg = '\0';
	ctx->linterErr = GS1_LINTER_OK;
	*ctx->linterErrMarkup = '\0';

	DEBUG_PRINT("\nParsing DL data: %s\n", dlData);

	p = dlData;

	if (strspn(p, uriCharacters) != strlen(p)) {
		SET_ERR(URI_CONTAINS_ILLEGAL_CHARACTERS);
		goto fail;
	}

	if (strlen(p) >= 8 && strncmp(p, "https://", 8) == 0)
		p += 8;
	else if (strlen(p) >= 8 && strncmp(p, "HTTPS://", 8) == 0)
		p += 8;
	else if (strlen(p) >= 7 && strncmp(p, "http://", 7) == 0)
		p += 7;
	else if (strlen(p) >= 7 && strncmp(p, "HTTP://", 7) == 0)
		p += 7;
	else {
		SET_ERR(URI_CONTAINS_ILLEGAL_SCHEME);
		goto fail;
	}

	DEBUG_PRINT("  Scheme %.*s\n", (int)(p-dlData-3), dlData);

	if (((r = strchr(p, '/')) == NULL) || r-p < 1) {
		SET_ERR(URI_MISSING_DOMAIN_AND_PATH_INFO);
		goto fail;
	}

	if ((int)strcspn(p, badDomainCharacters) < r-p) {
		SET_ERR(DOMAIN_CONTAINS_ILLEGAL_CHARACTERS);
		goto fail;
	}

	DEBUG_PRINT("  Domain: %.*s\n", (int)(r-p), p);

	pi = p = r;					// Skip the domain name

	// Fragment character delimits end of data
	if ((fr = strchr(pi, '#')) != NULL)
		*fr++ = '\0';

	// Query parameter marker delimits end of path info
	if ((qp = strchr(pi, '?')) != NULL)
		*qp++ = '\0';

	DEBUG_PRINT("  Path info: %s\n", pi);

	// Search backwards from the end of the path info looking for an
	// "/AI/value" pair where AI is a DL primary key
	while ((r = strrchr(pi, '/')) != NULL) {

		const struct aiEntry* entry;

		*p = '/';				// Restore original pair separator
							// Clobbers first character of path
							// info on first iteration

		// Find start of AI
		*r = '\0';				// Chop off value
		p = strrchr(pi, '/'); 			// Beginning of AI
		*r = '/';				// Restore original AI/value separator
		if (!p)					// At beginning of path
			break;

		DEBUG_PRINT("      %s\n", p);

		entry = gs1_lookupAIentry(ctx, p+1, (size_t)(r-p-1));
		if (!entry)
			break;

		if (isDLpkey(ctx, entry->ai)) {		// Found root of DL path info
			dp = p;
			break;
		}

		*p = '\0';

	}

	if (!dp) {
		SET_ERR(NO_GS1_DL_KEYS_FOUND_IN_PATH_INFO);
		goto fail;
	}

	DEBUG_PRINT("  Stem: %.*s\n", (int)(dp-dlData), dlData);

	DEBUG_PRINT("  DL path info: %s\n", dp);

	// Process each AI value pair in the DL path info
	p = (char*)dp;
	numPathAIs = 0;
	while (*p) {

		const struct aiEntry* entry;
		size_t ailen, vallen;
		char aival[MAX_AI_VALUE_LEN + 1];		// Unescaped AI value
		const char *outai, *outval;
		const char *ai;

		assert(*p == '/');
		r = strchr(++p, '/');
		assert(r);

		// AI is known to be valid since we previously walked over it
		ai = p;
		ailen = (size_t)(r-p);
		entry = gs1_lookupAIentry(ctx, ai, ailen);
		assert(entry);

		if ((p = strchr(++r, '/')) == NULL)
			p = r + strlen(r);

		if (p == r) {
			SET_ERR_V(AI_VALUE_PATH_ELEMENT_IS_EMPTY, (int)strlen(entry->ai), ai);
			goto fail;
		}

		// Reverse percent encoding
		if ((vallen = URIunescape(aival, MAX_AI_VALUE_LEN, r, (size_t)(p-r), false)) == 0) {
			SET_ERR_V(DECODED_AI_FROM_DL_PATH_INFO_CONTAINS_ILLEGAL_NULL, (int)ailen, ai);
			goto fail;
		}

		// Legacy handling of AI (01) to pad up to a GTIN-14, when feature enabled
		if (ctx->permitZeroSuppressedGTINinDLuris && strcmp(entry->ai, "01") == 0 &&
		    (vallen == 13 || vallen == 12 || vallen == 8)) {
			size_t i;
			for (i = 0; i <= 13; i++)
				aival[13-i] = vallen >= i+1 ? aival[vallen-i-1] : '0';
			aival[14] = '\0';
			vallen = 14;
		}

		DEBUG_PRINT("    Extracted: (%.*s) %.*s\n", (int)ailen, ai, (int)vallen, aival);

		if (fnc1req)
			writeDataStr("^");			// Write FNC1, if required
		outai = dataStr + strlen(dataStr);		// Save start of AI for AI data
		nwriteDataStr(ai, ailen);			// Write AI
		fnc1req = entry->fnc1;				// Record if required before next AI

		outval = dataStr + strlen(dataStr);		// Save start of value for AI data
		nwriteDataStr(aival, vallen);			// Write value

		// Perform certain checks at parse time, before processing the
		// components with the linters
		if (!gs1_aiValLengthContentCheck(ctx, ai, entry, aival, vallen))
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
			.vallen = (uint8_t)vallen,
			.dlPathOrder = (uint8_t)numPathAIs
		};

		strcpy(pathAIseq[numPathAIs], entry->ai);
		numPathAIs++;

	}

	numPathAIs = ctx->numAIs;

	if (qp)
		DEBUG_PRINT("  Query params: %s\n", qp);

	p = qp;
	while (p && *p) {

		const struct aiEntry* entry = NULL;
		size_t ailen = 0, vallen;
		char aival[MAX_AI_VALUE_LEN + 1];		// Unescaped AI value
		const char *outai = NULL, *outval, *ai, *e;

		aiValueKind_t kind = alValue_dlign;

		while (*p == '&')				// Jump any & separators
			p++;
		if ((r = strchr(p, '&')) == NULL)
			r = p + strlen(p);			// Value-pair finishes at end of data

		// Discard parameters with no value
		if ((e = memchr(p, '=', (size_t)(r-p))) == NULL) {
			DEBUG_PRINT("    Skipped singleton:   %.*s\n", (int)(r-p), p);
			outval = p;
			vallen = (uint8_t)(r-p);
			goto add_query_param_to_ai_data;	// Undecoded, "non-AI" data value!
		}

		// Numeric-only query parameters not matching an AI aren't allowed
		ai = p;
		ailen = (size_t)(e-p);
		if (gs1_allDigits((uint8_t*)p, ailen) && (entry = gs1_lookupAIentry(ctx, p, ailen)) == NULL) {
			SET_ERR_V(UNKNOWN_AI_IN_QUERY_PARAMS, (int)ailen, p);
			goto fail;
		}

		// Skip non-numeric query parameters
		if (!entry) {
			DEBUG_PRINT("    Skipped:   %.*s\n", (int)(r-p), p);
			outval = p;
			vallen = (uint8_t)(r-p);
			goto add_query_param_to_ai_data;	// Undecoded, "non-AI" data value!
		}

		if (r == ++e) {
			SET_ERR_V(AI_VALUE_QUERY_ELEMENT_IN_EMPTY, (int)strlen(entry->ai), ai);
			goto fail;
		}

		// Reverse percent encoding
		if ((vallen = URIunescape(aival, MAX_AI_VALUE_LEN, e, (size_t)(r-e), true)) == 0) {
			SET_ERR_V(DECODED_AI_VALUE_FROM_QUERY_PARAMS_CONTAINS_ILLEGAL_NULL, (int)strlen(entry->ai), ai);
			goto fail;
		}

		// Special handling of AI (01) to pad up to a GTIN-14
		if (strcmp(entry->ai, "01") == 0 &&
		    (vallen == 13 || vallen == 12 || vallen == 8)) {
			size_t i;
			for (i = 0; i <= 13; i++)
				aival[13-i] = vallen >= i+1 ? aival[vallen-i-1] : '0';
			aival[14] = '\0';
			vallen = 14;
		}

		DEBUG_PRINT("    Extracted: (%.*s) %.*s\n", (int)ailen, ai, (int)vallen, aival);

		if (fnc1req)
			writeDataStr("^");			// Write FNC1, if required
		outai = dataStr + strlen(dataStr);		// Save start of AI for AI data
		nwriteDataStr(ai, ailen);			// Write AI
		fnc1req = entry->fnc1;				// Record if required before next AI

		outval = dataStr + strlen(dataStr);		// Save start of value for AI data
		nwriteDataStr(aival, vallen);			// Write value

		// Perform certain checks at parse time, before processing the
		// components with the linters
		if (!gs1_aiValLengthContentCheck(ctx, ai, entry, aival, vallen))
			goto fail;

		kind = aiValue_aival;

add_query_param_to_ai_data:

		if (ctx->numAIs >= MAX_AIS) {
			SET_ERR(TOO_MANY_AIS);
			goto fail;
		}

		ctx->aiData[ctx->numAIs++] = (struct aiValue) {
			.kind = kind,
			.aiEntry = entry,
			.ai = outai,
			.ailen = (uint8_t)ailen,
			.value = outval,
			.vallen = (uint8_t)vallen,
			.dlPathOrder = DL_PATH_ORDER_ATTRIBUTE
		};

		p = r;

	}

	if (fr)
		DEBUG_PRINT("  Fragment: %s\n", fr);

	DEBUG_PRINT("Parsing DL data successful: %s\n", dataStr);

	ret = true;

	// Validate that the AI sequence in the path info is a valid
	// key-qualifier association
	if (!isValidDLpathAIseq(ctx, (const char(*)[MAX_AI_LEN+1])pathAIseq, numPathAIs)) {
		SET_ERR(INVALID_KEY_QUALIFIER_SEQUENCE);
		ret = false;
		goto out;
	}

	// Validate that attributes in the query params are valid and do not
	// instead belong within path info
	if (numPathAIs < MAX_AIS) {
		int i;
		for (i = 0; i < ctx->numAIs; i++) {

			char seq[MAX_AIS][MAX_AI_LEN+1] = { { 0 } };
			const struct aiValue* const ai = &ctx->aiData[i];
			int j;

			if (ai->kind != aiValue_aival || ai->dlPathOrder != DL_PATH_ORDER_ATTRIBUTE)
				continue;

			assert(ai->aiEntry);

			// Forbid duplicate AIs
			for (j = 0; j < i; j++) {
				const struct aiValue* ai2 = &ctx->aiData[j];
				if (ai2->kind == aiValue_aival &&
				    ai2->ailen == ai->ailen &&
				    memcmp(ai2->ai, ai->ai, ai2->ailen) == 0) {
					SET_ERR_V(DUPLICATE_AI, ai->ailen, ai->ai);
					ret = false;
					goto out;
				}
			}

			// Check that the AI is a permitted DL URI data attribute
			if (ai->aiEntry->dlDataAttr == NO_DATA_ATTR ||
			    (ai->aiEntry->dlDataAttr == XX_DATA_ATTR && ctx->validationTable[gs1_encoder_vUNKNOWN_AI_NOT_DL_ATTR].enabled)) {
				SET_ERR_V(AI_IS_NOT_VALID_DATA_ATTRIBUTE, ai->ailen, ai->ai);
				ret = false;
				goto out;
			}

			// Trial the AI at each non-initial position of the
			// path info to see if it results in a valid
			// key-qualifier sequence
			for (j = 1; j <= numPathAIs; j++) {

				memcpy(&seq[0], &pathAIseq[0], (size_t)j * sizeof(seq[0]));
				strcpy(seq[j], ai->aiEntry->ai);
				memcpy(&seq[j+1], &pathAIseq[j], (size_t)(numPathAIs-j) * sizeof(seq[0]));

				if (getDLpathAIseqEntry(ctx, (const char(*)[MAX_AI_LEN+1])seq, numPathAIs + 1) != -1) {
					SET_ERR_V(AI_SHOULD_BE_IN_PATH_INFO, seq[j]);
					ret = false;
					goto out;
				}

			}

		}
	}

	// Validate the data that we have written
	if (!gs1_processAIdata(ctx, dataStr, false))
		ret = false;

out:

	if (qp) {			// Restore original query parameter delimiter
//		*(qp-1) = '?';
		dlData[qp - dlData - 1] = '?';  // Ugly hack generates same code as above to satiate GCC 11
	}

	if (fr) {			// Restore original fragment delimiter
//		*(fr-1) = '#';
		dlData[fr - dlData - 1] = '#';  // Ditto
	}

	return ret;

fail:

	if (*ctx->errMsg == '\0')
		SET_ERR(DL_URI_PARSE_FAILED);

	DEBUG_PRINT("Parsing DL data failed: %s\n", ctx->errMsg);

	*dataStr = '\0';
	ret = false;
	goto out;

}


/*
 *  Generate a DL URI from the AI data
 *
 */
char* gs1_generateDLuri(gs1_encoder* const ctx, const char* const stem) {

	int i, maxQualifiers, numQualifiers;
	const char *key = NULL;
	int keyEntry = -1, bestKeyEntry;
	char *p;
	char *saveptr = NULL;
	const char *token;
	char tmp[256];
	bool emitFixed;

	assert(ctx);

	/*
	 *  Select the first AI that is a valid primary key for a DL
	 *
	 */
	for (i = 0; i < ctx->numAIs; i++) {

		char seq[MAX_AIS][MAX_AI_LEN+1] = { { 0 } };
		int ke;
		const struct aiValue* const ai = &ctx->aiData[i];

		if (ai->kind != aiValue_aival)
			continue;

		assert(ai->aiEntry);

		strcpy(seq[0], ai->aiEntry->ai);
		if ((ke = getDLpathAIseqEntry(ctx, (const char(*)[MAX_AI_LEN+1])seq, 1)) != -1) {
			keyEntry = ke;
			key = ctx->dlKeyQualifiers[keyEntry];
			break;
		}

	}

	if (keyEntry == -1) {
		SET_ERR(CANNOT_CREATE_DL_URI_WITHOUT_PRIMARY_KEY_AI);
		return NULL;
	}

	/*
	 *  Pick a qualifier-key sequence starting with the chosen primary key
	 *  and having a maximum number of matching qualifier AIs
	 *
	 */
	DEBUG_PRINT("Considering DL key-qualifier sequences\n");
	bestKeyEntry = keyEntry;
	maxQualifiers = 0;
	while (++keyEntry < ctx->numDLkeyQualifiers) {

		strcpy(tmp, ctx->dlKeyQualifiers[keyEntry]);
		token = strtok_r(tmp, " ", &saveptr);
		if (strcmp(token, key) != 0)
			break;

		numQualifiers = 0;
		while ((token = strtok_r(NULL, " ", &saveptr)) != NULL)
			for (i = 0; i < ctx->numAIs; i++) {

				if (ctx->aiData[i].kind != aiValue_aival)
					continue;

				assert(ctx->aiData[i].aiEntry);
				if (strcmp(ctx->aiData[i].aiEntry->ai, token) == 0)
					numQualifiers++;

			}
		if (numQualifiers > maxQualifiers) {
			maxQualifiers = numQualifiers;
			bestKeyEntry = keyEntry;
		}
		DEBUG_PRINT("  %s: %d qualifier matches\n", ctx->dlKeyQualifiers[keyEntry], numQualifiers);

	}
	DEBUG_PRINT("  Selected '%s'\n", ctx->dlKeyQualifiers[bestKeyEntry]);

	/*
	 *  Apply the path order from the sequence to the AI elements
	 *
	 */
	strcpy(tmp, ctx->dlKeyQualifiers[bestKeyEntry]);
	for (i = 0, token = strtok_r(tmp, " ", &saveptr); token; i++, token = strtok_r(NULL, " ", &saveptr)) {
		int j;
		for (j = 0; j < ctx->numAIs; j++) {

			if (ctx->aiData[j].kind != aiValue_aival)
				continue;

			assert(ctx->aiData[j].aiEntry);
			if (strcmp(ctx->aiData[j].aiEntry->ai, token) == 0)
				ctx->aiData[j].dlPathOrder = (uint8_t)i;

		}
	}
	numQualifiers = i;

	/*
	 *  Now build the output
	 *
	 */
	p = ctx->outStr;
	p += snprintf(p, sizeof(ctx->outStr), "%s", stem ? stem : CANONICAL_DL_STEM);

	// Trim trailing slash
	if (*(p-1) == '/')
		p--;

	/*
	 *  Output the path components in priority order (i.e. primary key AI,
	 *  then possible key-qualifier AIs)
	 *
	 */
	for (i = 0; i < numQualifiers; i++) {
		int j;
		for (j = 0; j < ctx->numAIs; j++) {

			char encval[MAX_AI_VALUE_LEN*3+1];	// Assuming that we %-escape everything
			int n;
			const struct aiValue* const ai = &ctx->aiData[j];

			if (ai->kind != aiValue_aival || ai->dlPathOrder != i)
				continue;

			URIescape(encval, sizeof(encval), ai->value, ai->vallen, false);
			n = snprintf(p, sizeof(ctx->outStr) - (size_t)(p - ctx->outStr), "/%.*s/%s", ai->ailen, ai->ai, encval);
			assert(n >= 0 && n < (int)(sizeof(ctx->outStr) - (size_t)(p - ctx->outStr)));  // Satisfy analyser
			p += n;
			break;

		}
	}
	p += snprintf(p, 2, "?");

	/*
	 *  Output the query parameter components (i.e. attribute AIs) in received order (i.e. attribute AIs), fixed-length first
	 *
	 */
	emitFixed = true;
again:
	for (i = 0; i < ctx->numAIs; i++) {

		char encval[MAX_AI_VALUE_LEN*3+1];	// Assuming that we %-escape everything
		int j, n;
		bool skip = false;
		const struct aiValue* ai = &ctx->aiData[i];

		if (ai->kind != aiValue_aival ||
		    ai->dlPathOrder != DL_PATH_ORDER_ATTRIBUTE ||
		    ai->aiEntry->fnc1 == emitFixed)
			continue;

		/*
		 *  Skip duplicate AIs that we have already processed
		 *
		 */
		for (j = 0; j < i; j++) {

			const struct aiValue* ai2 = &ctx->aiData[j];

			if (ai2->kind != aiValue_aival ||
			    ai2->aiEntry->fnc1 == emitFixed ||
			    ai2->ailen != ai->ailen ||
			    memcmp(ai2->ai, ai->ai, ai2->ailen) != 0)
				continue;

			skip = true;
			break;

		}
		if (skip) continue;

		/*
		 *  Check that the AI is permitted as a data attribute
		 *
		 */
		if (ai->aiEntry->dlDataAttr == NO_DATA_ATTR ||
		    (ai->aiEntry->dlDataAttr == XX_DATA_ATTR && ctx->validationTable[gs1_encoder_vUNKNOWN_AI_NOT_DL_ATTR].enabled)) {
			SET_ERR_V(AI_IS_NOT_VALID_DATA_ATTRIBUTE, ai->ailen, ai->ai);
			*ctx->outStr = '\0';
			return NULL;
		}

		URIescape(encval, sizeof(encval), ai->value, ai->vallen, true);
		n = snprintf(p, sizeof(ctx->outStr) - (size_t)(p - ctx->outStr), "%.*s=%s&", ai->ailen, ai->ai, encval);
		assert(n >= 0 && n < (int)(sizeof(ctx->outStr) - (size_t)(p - ctx->outStr)));  // Satisfy analyser
		p += n;

	}
	if (emitFixed) {
		emitFixed = false;
		goto again;
	}

	// Trim the final character, either '?' or '&'
	*(p-1) = '\0';

	return ctx->outStr;

}


#ifdef UNIT_TESTS

#define TEST_NO_MAIN
#include "acutest.h"


static void do_test_parseDLuri(gs1_encoder* const ctx, const char* const file, const int line, bool should_succeed, const char* const dlData, const char* const expect) {

	char in[256];
	char out[256];
	char casename[256];

	snprintf(casename, sizeof(casename), "%s:%d: %s => %s", file, line, dlData, expect);
	TEST_CASE(casename);

	ctx->numAIs = 0;
	strcpy(in, dlData);
	TEST_CHECK(gs1_parseDLuri(ctx, in, out) ^ (!should_succeed));
	TEST_MSG("Err: %s", ctx->errMsg);
	if (should_succeed) {
		TEST_CHECK(strcmp(out, expect) == 0);
		TEST_MSG("Given: %s; Got: %s; Expected: %s; Err: %s", dlData, out, expect, ctx->errMsg);
	}

	TEST_CHECK(strcmp(dlData, in) == 0);
	TEST_MSG("Input data was erroneously clobbered: %s", in);

}

/*
 *  Convert a DL URI to a regular AI string "^..."
 *
 */
void test_dl_parseDLuri(void) {

	gs1_encoder* ctx;
	TEST_ASSERT((ctx = gs1_encoder_init(NULL)) != NULL);
	assert(ctx);

#define test_parseDLuri(s, d, e) do {					\
	do_test_parseDLuri(ctx, __FILE__, __LINE__, s, d, e);		\
} while (0)

	test_parseDLuri(false, "", "");
	test_parseDLuri(false, "ftp://", "");
	test_parseDLuri(false, "http://", "");
	test_parseDLuri(false, "http:///", "");				// No domain
	test_parseDLuri(false, "http://a", "");				// No path info
	test_parseDLuri(false, "http://a/", "");			// Pathelogical minimal domain but no AI info
	test_parseDLuri(false, "http://a/b", "");			// No path info
	test_parseDLuri(false, "http://a/b/", "");			// Pathelogical minimal domain but no AI info

	test_parseDLuri(true,					// http
		"http://a/00/006141411234567890",
		"^00006141411234567890");

	test_parseDLuri(true,					// HTTP
		"HTTP://a/00/006141411234567890",
		"^00006141411234567890");

	test_parseDLuri(true,					// https
		"https://a/00/006141411234567890",
		"^00006141411234567890");

	test_parseDLuri(true,					// HTTPS
		"HTTPS://a/00/006141411234567890",
		"^00006141411234567890");

	test_parseDLuri(false, "HtTp://a/b/00/006141411234567890", "");	// Mixed-case scheme forbidden

	test_parseDLuri(false,					// No domain
		"https://00/006141411234567890",
		"");

	test_parseDLuri(true,
		"https://a/01/12312312312333",
		"^0112312312312333");

	/*
	 *  To prevent ossification, we don't strictly validate the form of the
	 *  domain (IPv4/6, ports, etc.), only the characterset
	 *
	 */

	test_parseDLuri(true,					// Puny code
		"https://xn--fsq.xn--0zwm56d/01/12312312312333",
		"^0112312312312333");

	test_parseDLuri(true,					// Explicit FQDN
		"https://a./01/12312312312333",
		"^0112312312312333");

	test_parseDLuri(true,					// Port specified
		"https://a:65535/01/12312312312333",
		"^0112312312312333");

	test_parseDLuri(true,					// IPv4
		"https://192.0.2.1/01/12312312312333",
		"^0112312312312333");

	test_parseDLuri(true,					// Valid IPv4 in decimal form!
		"https://3232235777/01/12312312312333",
		"^0112312312312333");

	test_parseDLuri(true,					// Valid IPv4 in octal form!
		"https://0300.0250.01.01/01/12312312312333",
		"^0112312312312333");

	test_parseDLuri(true,					// Valid IPv4 in octal form!
		"https://0xC0.0xA8.0x01.0x01/01/12312312312333",
		"^0112312312312333");

	test_parseDLuri(true,
		"https://[2001:db8::1]/01/12312312312333",	// IPv6
		"^0112312312312333");

	test_parseDLuri(false,
		"https://[fe80::1%25lo]/01/12312312312333",	// IPv6 zone identifiers not useful
		"");

	test_parseDLuri(false,					// Bad character in domain
		"https://$a/006141411234567890",
		"");

	test_parseDLuri(false,					// Bad character in domain
		"https://a$/006141411234567890",
		"");

	/*
	 * Custom stem
	 *
	 */

	test_parseDLuri(true,
		"https://a/stem/00/006141411234567890",
		"^00006141411234567890");

	test_parseDLuri(true,
		"https://a/more/stem/00/006141411234567890",
		"^00006141411234567890");

	test_parseDLuri(true,					// Fake AI in stem, stop at rightmost key
		"https://a/00/faux/00/006141411234567890",
		"^00006141411234567890");


	/*
	 * Test legacy expansion of GTIN-{8,12,13} in AI (01) path component
	 *
	 */
	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, true);
	test_parseDLuri(true,					// GTIN-13 -> GTIN-14
		"https://a/01/2112345678900",
		"^0102112345678900");
	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, false);

	test_parseDLuri(false,
		"https://a/01/2112345678900",
		"");

	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, true);
	test_parseDLuri(true,					// GTIN-12 -> GTIN-14
		"https://a/01/416000336108",
		"^0100416000336108");
	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, false);

	test_parseDLuri(false,
		"https://a/01/416000336108",
		"");

	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, true);
	test_parseDLuri(true,					// GTIN-8 -> GTIN-14
		"https://a/01/02345673",
		"^0100000002345673");
	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, false);

	test_parseDLuri(false,
		"https://a/01/02345673",
		"");


	test_parseDLuri(true,
		"https://a/01/12312312312333/22/TEST/10/ABC/21/XYZ",
		"^011231231231233322TEST^10ABC^21XYZ");

	test_parseDLuri(true,
		"https://a/01/12312312312333/235/TEST",
		"^0112312312312333235TEST");

	test_parseDLuri(true,
		"https://a/253/1231231231232",
		"^2531231231231232");

	test_parseDLuri(true,
		"https://a/253/1231231231232TEST5678901234567",
		"^2531231231231232TEST5678901234567");

	test_parseDLuri(false,
		"https://a/253/1231231231232TEST56789012345678", "");	// Too long N13 X0..17

	test_parseDLuri(true,
		"https://a/8018/123456789012345675/8019/123",
		"^8018123456789012345675^8019123");

	test_parseDLuri(false,
		"https://a/stem/00/006141411234567890/", ""); 		// Can't end in slash

	test_parseDLuri(true,
		"https://a/stem/00/006141411234567890?",
		"^00006141411234567890"); 				// Empty query params

	test_parseDLuri(true,
		"https://a/stem/00/006141411234567890?99=ABC",		// Query params; no FNC1 req after pathinfo
		 "^0000614141123456789099ABC");

	test_parseDLuri(true,
		"https://a/stem/401/12345678?99=ABC",			// Query params; FNC1 req after pathinfo
		 "^40112345678^99ABC");

	test_parseDLuri(true,
		"https://a/01/12312312312333?99=ABC&98=XYZ",
		"^011231231231233399ABC^98XYZ");

	test_parseDLuri(false,
		"https://a/01/12312312312333?99=", ""); 		// Empty AI value in query parameter

	test_parseDLuri(false,
		"https://a/01/12312312312333?99=ABC&999=faux", "");	// Non-AI, numeric-only query param

	test_parseDLuri(true,
		"https://a/01/12312312312333?&&&99=ABC&&&&&&98=XYZ&&&",	// Extraneous query param separators
		"^011231231231233399ABC^98XYZ");

	test_parseDLuri(true,
		"https://a/01/12312312312333?99=ABC&unknown=666&98=XYZ",
		"^011231231231233399ABC^98XYZ");

	test_parseDLuri(true,
		"https://a/01/12312312312333?unknown=666&99=ABC&98=XYZ",
		"^011231231231233399ABC^98XYZ");

	test_parseDLuri(true,
		"https://a/01/12312312312333?99=ABC&98=XYZ&unknown=666",
		"^011231231231233399ABC^98XYZ");

	test_parseDLuri(true,
		"https://a/01/12312312312333?unknown1=555&99=ABC&unknown2=666&98=XYZ&unknown3=777",
		"^011231231231233399ABC^98XYZ");

	test_parseDLuri(true,
		"https://a/01/12312312312333?99=ABC&singleton&98=XYZ",
		"^011231231231233399ABC^98XYZ");

	test_parseDLuri(true,
		"https://a/01/12312312312333?singleton&99=ABC&98=XYZ",
		"^011231231231233399ABC^98XYZ");

	test_parseDLuri(true,
		"https://a/01/12312312312333?99=ABC&98=XYZ&singleton",
		"^011231231231233399ABC^98XYZ");

	test_parseDLuri(true,
		"https://a/01/12312312312333?singleton1&99=ABC&singleton2&98=XYZ&singleton3",
		"^011231231231233399ABC^98XYZ");

	test_parseDLuri(true,
		"https://a/01/12312312312333?singleton1&unknown1=555&99=ABC&singleton2&unknown2=6666&98=XYZ&unknown3=777&singleton3",
		"^011231231231233399ABC^98XYZ");

	test_parseDLuri(true,
		"https://a/01/12312312312333/22/ABC%2d123?99=ABC&98=XYZ%2f987",	// Percent escaped values
		"^011231231231233322ABC-123^99ABC^98XYZ/987");

	test_parseDLuri(true,
		"https://a/01/12312312312333/22/ABC+123?99=ABC&98=XYZ%2f987",	// "+" means "+" in path info
		"^011231231231233322ABC+123^99ABC^98XYZ/987");

	test_parseDLuri(false,
		"https://a/01/12312312312333/22/ABC%2d123?99=ABC&98=XYZ+987",	// "+" means " " in path info
		"");

	test_parseDLuri(true,					// Empty fragment after path info
		"https://a/01/12312312312333/22/test/10/abc/21/xyz#",
		"^011231231231233322test^10abc^21xyz");

	test_parseDLuri(true,					// Ignore fragment after path info
		"https://a/01/12312312312333/22/test/10/abc/21/xyz#fragment",
		"^011231231231233322test^10abc^21xyz");

	test_parseDLuri(true,					// Empty fragment after query info
		"https://a/stem/00/006141411234567890?99=ABC#",
		"^0000614141123456789099ABC");

	test_parseDLuri(true,					// Ignore fragment after query info
		"https://a/stem/00/006141411234567890?99=ABC#fragment",
		"^0000614141123456789099ABC");


	/*
	 * Examples from DL specification
	 *
	 */

	test_parseDLuri(true,
		"https://id.gs1.org/01/09520123456788",
		"^0109520123456788");

	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, true);
	test_parseDLuri(true,
		"https://brand.example.com/01/9520123456788",
		"^0109520123456788");
	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, false);

	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, true);
	test_parseDLuri(true,
		"https://brand.example.com/some-extra/pathinfo/01/9520123456788",
		"^0109520123456788");
	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, false);

	test_parseDLuri(true,
		"https://id.gs1.org/01/09520123456788/22/2A",
		"^0109520123456788222A");

	test_parseDLuri(true,
		"https://id.gs1.org/01/09520123456788/10/ABC123",
		"^010952012345678810ABC123");

	test_parseDLuri(true,
		"https://id.gs1.org/01/09520123456788/21/12345",
		"^01095201234567882112345");

	test_parseDLuri(true,
		"https://id.gs1.org/01/09520123456788/10/ABC1/21/12345?17=180426",
		"^010952012345678810ABC1^2112345^17180426");
	// Specification sorts (17) before (10) and (21):
	//   "^01095201234567881718042610ABC1^2112345"

	test_parseDLuri(true,
		"https://id.gs1.org/01/09520123456788?3103=000195",
		"^01095201234567883103000195");

	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, true);
	test_parseDLuri(true,
		"https://example.com/01/9520123456788?3103=000195&3922=0299&17=201225",
		"^0109520123456788310300019539220299^17201225");
	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, false);

	test_parseDLuri(true,
		"https://example.com/01/09520123456788?3103=000195&3922=0299&17=201225",
		"^0109520123456788310300019539220299^17201225");

	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, true);
	test_parseDLuri(true,
		"https://example.com/01/9520123456788?3103=000195&3922=0299&17=201225",
		"^0109520123456788310300019539220299^17201225");
	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, false);

	test_parseDLuri(true,
		"https://example.com/01/09520123456788?3103=000195&3922=0299&17=201225",
		"^0109520123456788310300019539220299^17201225");

	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, true);
	test_parseDLuri(true,
		"https://id.gs1.org/01/9520123456788?3103=000195&3922=0299&17=201225",
		"^0109520123456788310300019539220299^17201225");
	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, false);

	test_parseDLuri(true,
		"https://id.gs1.org/01/09520123456788?3103=000195&3922=0299&17=201225",
		"^0109520123456788310300019539220299^17201225");

	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, true);
	test_parseDLuri(true,
		"https://id.gs1.org/01/9520123456788?17=201225&3103=000195&3922=0299",
		"^010952012345678817201225310300019539220299");
	gs1_encoder_setPermitZeroSuppressedGTINinDLuris(ctx, false);

	test_parseDLuri(true,
		"https://id.gs1.org/01/09520123456788?17=201225&3103=000195&3922=0299",
		"^010952012345678817201225310300019539220299");

	test_parseDLuri(true,
		"https://id.gs1.org/00/952012345678912345",
		"^00952012345678912345");

	test_parseDLuri(true,
		"https://id.gs1.org/00/952012345678912345?02=09520123456788&37=25&10=ABC123",
		"^0095201234567891234502095201234567883725^10ABC123");

	test_parseDLuri(true,
		"https://id.gs1.org/414/9520123456788",
		"^4149520123456788");

	test_parseDLuri(true,
		"https://id.gs1.org/414/9520123456788/254/32a%2Fb",
		"^414952012345678825432a/b");

	test_parseDLuri(true,
		"https://example.com/8004/9520614141234567?01=9520123456788",
		"^80049520614141234567^0109520123456788");

	// AI in query params that should be in the path info
	test_parseDLuri(false,
		"https://example.com/01/09520123456788?10=ABC123",
		"");

	// This is fine because we chose an alternate key qualifier
	test_parseDLuri(true,
		"https://id.gs1.org/01/09520123456788/235/XYZ?10=ABC123",
		"^0109520123456788235XYZ^10ABC123");

	// Forbid duplicate AIs
	test_parseDLuri(false,
		"https://id.gs1.org/01/09520123456788/10/ABC123?99=XYZ789&01=09520123456788",
		"");

	test_parseDLuri(false,
		"https://id.gs1.org/01/09520123456788/10/ABC123?99=XYZ789&10=ABC123",
		"");

	test_parseDLuri(false,
		"https://id.gs1.org/01/09520123456788/10/ABC123?99=XYZ789&99=XYZ789",
		"");

	// Examples with unknown AIs, not permitted
	test_parseDLuri(false,
		"https://example.com/01/09520123456788/89/ABC123?99=XYZ",
		"");

	test_parseDLuri(false,
		"https://example.com/01/09520123456788?99=XYZ&89=ABC123",
		"");

	// Examples with unknown AIs
	gs1_encoder_setPermitUnknownAIs(ctx, true);
	test_parseDLuri(false,								// Unknown AIs are not permitted data attributes
		"https://example.com/01/09520123456788?99=XYZ&89=ABC123",
		"");
	gs1_encoder_setValidationEnabled(ctx, gs1_encoder_vUNKNOWN_AI_NOT_DL_ATTR, false);	// ... unless when explicitly permitted
	test_parseDLuri(true,
		"https://example.com/01/09520123456788?99=XYZ&89=ABC123",
		"^010952012345678899XYZ^89ABC123");
	gs1_encoder_setValidationEnabled(ctx, gs1_encoder_vUNKNOWN_AI_NOT_DL_ATTR, true);
	gs1_encoder_setPermitUnknownAIs(ctx, false);

#undef test_parseDLuri

	gs1_encoder_free(ctx);

}


static void do_test_URIunescape(const char* const file, const int line, const char* const in, const char* const expect_path, const char* const expect_query) {

	char out[MAX_AI_VALUE_LEN+1];
	char casename[256];

	snprintf(casename, sizeof(casename), "%s:%d: %s => %s | %s", file, line, in, expect_path, expect_query);
	TEST_CASE(casename);

	TEST_CHECK(URIunescape(out, sizeof(out)-1, in, strlen(in), false) == strlen(expect_path));
	TEST_CHECK(strcmp(out, expect_path) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected query component: %s", in, out, expect_path);

	TEST_CHECK(URIunescape(out, sizeof(out)-1, in, strlen(in), true) == strlen(expect_query));
	TEST_CHECK(strcmp(out, expect_query) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected path component: %s", in, out, expect_query);

}


void test_dl_URIunescape(void) {

	char out[MAX_AI_VALUE_LEN+1];

#define test_URIunescape(i, p, q) do {				\
	do_test_URIunescape(__FILE__, __LINE__, i, p, q);	\
} while (0)

	test_URIunescape("", "", "");
	test_URIunescape("test", "test", "test");
	test_URIunescape("+", "+", " ");				// "+" means space in query info
	test_URIunescape("%20", " ", " ");
	test_URIunescape("%20AB", " AB", " AB");
	test_URIunescape("A%20B", "A B", "A B");
	test_URIunescape("AB%20", "AB ", "AB ");
	test_URIunescape("ABC%2", "ABC%2", "ABC%2");			// Off end
	test_URIunescape("ABCD%", "ABCD%", "ABCD%");
	test_URIunescape("A%20%20B", "A  B",  "A  B");			// Run together
	test_URIunescape("A%01B", "A" "\x01" "B", "A" "\x01" "B");	// "Minima", we check \0 below
	test_URIunescape("A%ffB", "A" "\xFF" "B", "A" "\xFF" "B");	// Maxima
	test_URIunescape("A%FfB", "A" "\xFF" "B", "A" "\xFF" "B");	// Case mixing
	test_URIunescape("A%fFB", "A" "\xFF" "B", "A" "\xFF" "B");	// Case mixing
	test_URIunescape("A%FFB", "A" "\xFF" "B", "A" "\xFF" "B");	// Case mixing
	test_URIunescape("A%4FB", "AOB", "AOB");
	test_URIunescape("A%4fB", "AOB", "AOB");
	test_URIunescape("A%4gB", "A%4gB", "A%4gB");			// Non hex digit
	test_URIunescape("A%4GB", "A%4GB", "A%4GB");			// Non hex digit
	test_URIunescape("A%g4B", "A%g4B", "A%g4B");			// Non hex digit
	test_URIunescape("A%G4B", "A%G4B", "A%G4B");			// Non hex digit

	// Check that \0 is trapped as an illegal character
	TEST_CHECK(URIunescape(out, MAX_AI_VALUE_LEN, "A%00B", 5, false) == 0);

	// Truncated input
	TEST_CHECK(URIunescape(out, MAX_AI_VALUE_LEN, "ABCD", 2, false) == 2);
	TEST_CHECK(memcmp(out, "AB", 3) == 0);				// Includes \0

	// Truncated output
	TEST_CHECK(URIunescape(out, 2, "ABCD", 4, false) == 2);
	TEST_CHECK(memcmp(out, "AB", 3) == 0);				// Includes \0

	TEST_CHECK(URIunescape(out, 1, "ABCD", 4, false) == 1);
	TEST_CHECK(memcmp(out, "A", 2) == 0);				// Includes \0

	TEST_CHECK(URIunescape(out, 0, "ABCD", 4, false) == 0);
	TEST_CHECK(memcmp(out, "", 1) == 0);				// Includes \0

#undef test_URIunescape

}


static void do_test_URIescape(const char* const file, const int line, const char* const in, const char* const expect_path, const char* const expect_query) {

	char out[MAX_AI_VALUE_LEN*3+1];
	char casename[256];

	snprintf(casename, sizeof(casename), "%s:%d: %s => %s | %s", file, line, in, expect_path, expect_query);
	TEST_CASE(casename);

	TEST_CHECK(URIescape(out, sizeof(out)-1, in, strlen(in), false) == strlen(expect_path));
	TEST_CHECK(strcmp(out, expect_path) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected path component: %s", in, out, expect_path);

	TEST_CHECK(URIescape(out, sizeof(out)-1, in, strlen(in), true) == strlen(expect_query));
	TEST_CHECK(strcmp(out, expect_query) == 0);
	TEST_MSG("Given: %s; Got: %s; Expected query component: %s", in, out, expect_query);

}

void test_dl_URIescape(void) {

	char out[MAX_AI_VALUE_LEN*3+1];

#define test_URIescape(i, p, q) do {				\
	do_test_URIescape(__FILE__, __LINE__, i, p, q);		\
} while (0)

	// Reserved characters that do not need escaping
	test_URIescape("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	test_URIescape("abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz");
	test_URIescape("0123456789-._~", "0123456789-._~", "0123456789-._~");

	// Other characters that may appear in AIs that must be escaped
	test_URIescape("!\"#%&'()*+,/:;<=>?", "%21%22%23%25%26%27%28%29%2A%2B%2C%2F%3A%3B%3C%3D%3E%3F", "%21%22%23%25%26%27%28%29%2A%2B%2C%2F%3A%3B%3C%3D%3E%3F");

	test_URIescape("test", "test", "test");
	test_URIescape(" ", "%20", "+");
	test_URIescape(" AB", "%20AB", "+AB");
	test_URIescape("A B", "A%20B", "A+B");
	test_URIescape("AB ", "AB%20", "AB+");
	test_URIescape("A  B", "A%20%20B", "A++B");			// Run together

	// Truncated input
	TEST_CHECK(URIescape(out, MAX_AI_VALUE_LEN, "ABCD", 2, false) == 2);
	TEST_CHECK(memcmp(out, "AB", 3) == 0);			// Includes \0

	// Truncated output
	TEST_CHECK(URIescape(out, 2, "ABCD", 4, false) == 2);
	TEST_CHECK(memcmp(out, "AB", 3) == 0);			// Includes \0

	TEST_CHECK(URIescape(out, 5, "A!B", 3, false) == 5);
	TEST_CHECK(memcmp(out, "A%21B", 6) == 0);		// Includes \0

	TEST_CHECK(URIescape(out, 4, "A!B", 3, false) == 4);
	TEST_CHECK(memcmp(out, "A%21", 5) == 0);		// Includes \0

	TEST_CHECK(URIescape(out, 3, "A!B", 3, false) == 1);
	TEST_CHECK(memcmp(out, "A", 2) == 0);			// Includes \0

	TEST_CHECK(URIescape(out, 2, "A!B", 3, false) == 1);
	TEST_CHECK(memcmp(out, "A", 2) == 0);			// Includes \0

	TEST_CHECK(URIescape(out, 1, "A!B", 3, false) == 1);
	TEST_CHECK(memcmp(out, "A", 2) == 0);			// Includes \0

	TEST_CHECK(URIescape(out, 0, "A!B", 3, false) == 0);
	TEST_CHECK(memcmp(out, "", 1) == 0);			// Includes \0

#undef test_URIescape

}


void test_dl_testValidateDLpathAIseq(void) {

	const char seq[][MAX_AIS][MAX_AI_LEN+1] = {

		// SSCC
		{ "00" },

		// GTIN; qualifiers 22, 10, 21
		{ "01" },
		{ "01", "21" },
		{ "01", "10" },
		{ "01", "10", "21" },
		{ "01", "22" },
		{ "01", "22", "21" },
		{ "01", "22", "10" },
		{ "01", "22", "10", "21" },

		// GTIN; qualifiers 235
		{ "01" },
		{ "01", "235" },

		// GDTI
		{ "253" },

		// GCN
		{ "255" },

		// GINC
		{ "401" },

		// GSIN
		{ "402" },

		// LOC NO.; qualifiers 254
		{ "414" },
		{ "414", "254" },

		// LOC NO.; qualifiers 7040
		{ "414" },
		{ "414", "7040" },

		// PARTY; qualifiers 7040
		{ "417" },
		{ "417", "7040" },

		// GRAI
		{ "8003" },

		// GIAI; qualifiers 7040
		{ "8004" },
		{ "8004", "7040" },

		// ITIP; qualifiers 22, 10, 21
		{ "8006" },
		{ "8006", "21" },
		{ "8006", "10" },
		{ "8006", "10", "21" },
		{ "8006", "22" },
		{ "8006", "22", "21" },
		{ "8006", "22", "10" },
		{ "8006", "22", "10", "21" },

		// CPID; qualifiers=8011
		{ "8010" },
		{ "8010", "8011" },

		// GMN
		{ "8013" },

		// GSRN - PROVIDER; qualifiers=8019
		{ "8017" },
		{ "8017", "8019" },

		// GSRN - RECIPIENT; qualifiers=8019
		{ "8018" },
		{ "8018", "8019" },

	};

	size_t i;

	gs1_encoder* ctx;
	TEST_ASSERT((ctx = gs1_encoder_init(NULL)) != NULL);
	assert(ctx);

	for (i = 0; i < SIZEOF_ARRAY(seq); i++) {
		int num;
		char casename[256] = { 0 };
		for (num = 0; *seq[i][num]; num++) {
			strcat(casename, seq[i][num]);
			strcat(casename, " ");
		}
		TEST_CASE(casename);
		TEST_CHECK(isValidDLpathAIseq(ctx, seq[i], num));
	}

	gs1_encoder_free(ctx);

}


static void do_test_testGenerateDLuri(gs1_encoder* const ctx, const char* const file, const int line, const bool should_succeed, const char* const stem, const char* const aiData, const char* const expect) {

	char out[256];
	char casename[256];
	const char *uri;
	bool ret;

	strcpy(casename, aiData);
	snprintf(casename, sizeof(casename), "%s:%d: %s", file, line, aiData);
	TEST_CASE(casename);

	ctx->numAIs = 0;
	TEST_CHECK((ret = gs1_parseAIdata(ctx, aiData, out)) == true);
	TEST_MSG("Parse failed for non-pair validation reasons. Err: %s", ctx->errMsg);
	if (!ret)
		return;

	if (!should_succeed) {
		TEST_CHECK(gs1_generateDLuri(ctx, stem) == NULL);
		return;
	}

	TEST_CHECK((uri = gs1_generateDLuri(ctx, stem)) != NULL);
	TEST_MSG("Expected success. Got error: %s", ctx->errMsg);

	if (!uri)
		return;

	TEST_CHECK(strcmp(uri, expect) == 0);
	TEST_MSG("Expected: '%s'. Got: '%s'", expect, uri);

}

void test_dl_generateDLuri(void) {

	gs1_encoder* ctx;
	TEST_ASSERT((ctx = gs1_encoder_init(NULL)) != NULL);
	assert(ctx);

#define test_testGenerateDLuri(s, t, d, e) do {					\
	do_test_testGenerateDLuri(ctx, __FILE__, __LINE__, s, t, d, e);		\
} while (0)

	test_testGenerateDLuri(true, NULL, "(01)12312312312326(21)abc123", "https://id.gs1.org/01/12312312312326/21/abc123");	// Canonical
	test_testGenerateDLuri(true, "https://example.com", "(01)12312312312326(21)abc123", "https://example.com/01/12312312312326/21/abc123");
	test_testGenerateDLuri(true, "https://example.com", "(01)12312312312326(22)ABC(10)DEF(21)GHI", "https://example.com/01/12312312312326/22/ABC/10/DEF/21/GHI");
	test_testGenerateDLuri(true, "https://example.com", "(01)12312312312326(22)ABC(10)DEF(21)GHI(95)INT", "https://example.com/01/12312312312326/22/ABC/10/DEF/21/GHI?95=INT");
	test_testGenerateDLuri(true, "https://example.com", "(21)XYZ(01)12312312312333(10)ABC123(99)XYZ", "https://example.com/01/12312312312333/10/ABC123/21/XYZ?99=XYZ");

	/*
	 * "+" represents space in query info but not path components
	 *
	 */
	test_testGenerateDLuri(true, "https://example.com", "(01)12312312312333(10)ABC+123(99)XYZ+QWERTY", "https://example.com/01/12312312312333/10/ABC%2B123?99=XYZ%2BQWERTY");

	/*
	 * Multiple candidate primary keys; first should be chosen and
	 * subsequent ones relegated to attributes.
	 *
	 */
	test_testGenerateDLuri(true, "https://example.com", "(8017)795260646688514634(99)000001(253)9526064000028000001", "https://example.com/8017/795260646688514634?99=000001&253=9526064000028000001");
	test_testGenerateDLuri(true, "https://example.com", "(253)9526064000028000001(99)000001(8017)795260646688514634", "https://example.com/253/9526064000028000001?99=000001&8017=795260646688514634");
	test_testGenerateDLuri(true, "https://example.com", "(98)ABC(253)9526064000028000001(99)000001(8017)795260646688514634", "https://example.com/253/9526064000028000001?98=ABC&99=000001&8017=795260646688514634");
	test_testGenerateDLuri(true, "https://example.com", "(253)9526064000028000001(99)000001(01)12312312312326(10)DEF(95)INT", "https://example.com/253/9526064000028000001?01=12312312312326&99=000001&10=DEF&95=INT");

	/*
	 * Duplicate AIs in element data
	 *
	 */
	test_testGenerateDLuri(true, "https://example.com", "(01)12312312312326(01)12312312312326(10)ABC123(99)XYZ789", "https://example.com/01/12312312312326/10/ABC123?99=XYZ789");
	test_testGenerateDLuri(true, "https://example.com", "(01)12312312312326(10)ABC123(10)ABC123(99)XYZ789", "https://example.com/01/12312312312326/10/ABC123?99=XYZ789");
	test_testGenerateDLuri(true, "https://example.com", "(01)12312312312326(10)ABC123(99)XYZ789(99)XYZ789", "https://example.com/01/12312312312326/10/ABC123?99=XYZ789");
	test_testGenerateDLuri(true, "https://example.com", "(8010)0200(3133)333333(3300)000000(99)57(3133)333333(3300)000000(01)04065093955756(10)0(3133)333333(3300)000000(01)04065093955756","https://example.com/8010/0200?3133=333333&3300=000000&01=04065093955756&99=57&10=0");


	/*
	 * AI data containing invalid DL URI data attributes
	 *
	 */
	test_testGenerateDLuri(false, "https://example.com", "(01)12312312312326(99)000001(8200)http://example.com(95)INT","");			// (8200) is invalid as a data attribute
	test_testGenerateDLuri(false, "https://example.com", "(01)12312312312326(235)TPX9526064(99)000001(22)ABC(95)INT","");			// (235) used as qualifier for (01), therefore (22) is invalid as a data attribute
	test_testGenerateDLuri(false, "https://example.com", "(01)12312312312326(22)ABC(10)DEF(99)000001(235)TPX9526064(95)INT","");		// (22)+(10) are qualifiers for (01), therefore (235) is invalid as a data attribute

	/*
	 * AI data containing unknown AIs
	 *
	 */
	gs1_encoder_setPermitUnknownAIs(ctx, true);
	test_testGenerateDLuri(false, "https://example.com", "(01)12312312312326(99)000001(89)XXX(95)INT","");	// Unknown AIs not permitted as DL URI data attributes...
	gs1_encoder_setValidationEnabled(ctx, gs1_encoder_vUNKNOWN_AI_NOT_DL_ATTR, false);				// ... unless when explicitly permitted
	test_testGenerateDLuri(true, "https://example.com", "(01)12312312312326(99)000001(89)XXX(95)INT","https://example.com/01/12312312312326?99=000001&89=XXX&95=INT");
	gs1_encoder_setValidationEnabled(ctx, gs1_encoder_vUNKNOWN_AI_NOT_DL_ATTR, true);
	gs1_encoder_setPermitUnknownAIs(ctx, false);

#undef test_testGenerateDLuri

	gs1_encoder_free(ctx);

}


#endif  /* UNIT_TESTS */

