/**
 * GS1 Barcode Syntax Engine
 *
 * @author Copyright (c) 2021-2024 GS1 AISBL.
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

#include "gs1encoders.h"


// Implementation limits that can be changed
#define MAX_FNAME	120	// Maximum filename
#define MAX_DATA	8191	// Maximum input buffer size


#ifdef _MSC_VER
#define strtok_r strtok_s
#define strdup _strdup
#endif

#if defined(__GNUC__) || defined(__clang__)
#define __ATTR_CONST __attribute__ ((__const__))
#define __ATTR_PURE __attribute__ ((__pure__))
#elif _MSC_VER
#define __ATTR_CONST __declspec(noalias)
#define __ATTR_PURE
#else
#define __ATTR_CONST
#define __ATTR_PURE
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
#endif

#define SIZEOF_ARRAY(x) (sizeof(x) / sizeof(x[0]))


#include "ai.h"


struct gs1_encoder {

	gs1_encoder_symbologies_t sym;		// Symbology type

	bool addCheckDigit;			// For EAN/UPC and RSS-14/Lim, calculated if true, otherwise validated
	bool permitUnknownAIs;			// Extract AIs that are not in our AI table during AI element string and DL URI parsing
	bool permitZeroSuppressedGTINinDLuris;	// Whether to permit a path component GTIN value to be in GTIN-{8,12,13} format
	bool includeDataTitlesInHRI;		// Whether to include the Data Titles in HRI string output

	char errMsg[512];
	gs1_lint_err_t linterErr;		// Error returned by a linter
	char linterErrMarkup[512];

	char dataStr[MAX_DATA+1];		// Input data buffer passed to the encoders
	char dlAIbuffer[MAX_DATA+1];		// Populated with unbracketed AI string extracted from DL input
	char outStr[2*MAX_DATA+1];		// Buffer to return formatted data
	char *outHRI[MAX_AIS];			// Array of AI element string for HRI printing

	bool localAlloc;			// True if we malloc()ed this struct
	FILE *outfp;

	struct aiEntry *aiTable;		// Pointer to the AI table
	size_t aiTableEntries;			// Number of entries in the AI table
	bool aiTableIsDynamic;			// True if the AI table is loaded from the Syntax Dictionary

	struct aiValue aiData[MAX_AIS];		// List of AI components
	int numAIs;

	struct validationEntry validationTable[gs1_encoder_vNUMVALIDATIONS];
						// Table of all global validation functions

	uint8_t aiLengthByPrefix[100];		// AI length by two-digit prefix

	char** dlKeyQualifiers;			// List of valid DL key qualifier association strings
	int numDLkeyQualifiers;			// Number of dlKeyQualifiers strings

};


/*
 *  Utility functions
 *
 */
bool gs1_allDigits(const uint8_t *str, size_t len);


#ifdef UNIT_TESTS

void test_api_getVersion(void);
void test_api_instanceSize(void);
void test_api_init(void);
void test_api_defaults(void);
void test_api_sym(void);
void test_api_addCheckDigit(void);
void test_api_permitUnknownAIs(void);
void test_api_permitZeroSuppressedGTINinDLuris(void);
void test_api_validateAIassociations(void);
void test_api_validations(void);
void test_api_dataStr(void);
void test_api_getAIdataStr(void);
void test_api_getScanData(void);
void test_api_setScanData(void);
void test_api_getHRI(void);
void test_api_copyHRI(void);
void test_api_getDLignoredQueryParams(void);
void test_api_copyDLignoredQueryParams(void);

#endif


#endif /* ENC_PRIVATE_H */
