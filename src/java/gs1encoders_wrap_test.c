/*
 * Test harness for gs1encoders_wrap.c that drives the JNI wrapper functions
 * through a mock JNIEnv, without a JVM.
 *
 * The JNI specification permits GetStringUTFChars() to return either a copy
 * (isCopy == JNI_TRUE) or a direct pointer (isCopy == JNI_FALSE); in both
 * cases every successful acquisition must be handed back with
 * ReleaseStringUTFChars().  Mainstream JVMs always copy, so a wrapper that
 * releases only copies appears leak-free under functional testing.  The mock
 * environment exercises the modes a real JVM cannot be forced into:
 *
 *   - copy mode:   acquisitions return a malloc'd copy (isCopy == JNI_TRUE)
 *   - direct mode: acquisitions return a direct pointer (isCopy == JNI_FALSE)
 *   - fail mode:   acquisitions fail (NULL return with exception pending)
 *
 * The mock additionally enforces two more JNI contracts that a functional
 * test on a tolerant JVM cannot check:
 *
 *   - Local reference budget: the spec only ensures 16 local references per
 *     native frame; the array getters must not scale their live references
 *     with the number of AIs.
 *   - Pending-exception discipline: after any failed allocation the wrapper
 *     must stop calling JNI functions (other than releases) and return.
 *
 * Every wrapper that borrows string characters or builds arrays must be
 * exercised here, in each mode, and each mode must end with zero outstanding
 * acquisitions.
 *
 * Copyright (c) 2026 GS1 AISBL.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Functions under test, defined in gs1encoders_wrap.c */
JNIEXPORT jlong        JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderInitExJNI(JNIEnv*, jobject, jobject, jobjectArray);
JNIEXPORT void         JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderFreeJNI(JNIEnv*, jobject, jlong);
JNIEXPORT jboolean     JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetDataStrJNI(JNIEnv*, jobject, jlong, jstring);
JNIEXPORT jboolean     JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetAIdataStrJNI(JNIEnv*, jobject, jlong, jstring);
JNIEXPORT jboolean     JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetScanDataJNI(JNIEnv*, jobject, jlong, jstring);
JNIEXPORT jstring      JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetDLuriJNI(JNIEnv*, jobject, jlong, jstring);
JNIEXPORT jobjectArray JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetHRIJNI(JNIEnv*, jobject, jlong);
JNIEXPORT jobjectArray JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetDLignoredQueryParamsJNI(JNIEnv*, jobject, jlong);

#define InitExJNI    Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderInitExJNI
#define FreeJNI      Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderFreeJNI
#define SetDataJNI   Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetDataStrJNI
#define SetAIdataJNI Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetAIdataStrJNI
#define SetScanJNI   Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetScanDataJNI
#define GetDLuriJNI  Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetDLuriJNI
#define GetHRIJNI    Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetHRIJNI
#define GetDLQPJNI   Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetDLignoredQueryParamsJNI


/*
 *  Mock environment
 *
 *  A jstring is a pointer to the UTF-8 characters themselves.
 *
 */

enum mode { MODE_COPY, MODE_DIRECT, MODE_FAIL };

static enum mode mode;
static int acquired, released, releaseErrors, fails;

/* Local reference accounting */
static int liveRefs, highWaterRefs;

/* Pending-exception discipline: unsafe JNI calls after a failure */
static int pendingException, disciplineErrors;

/* Fail injection: fail the Nth next allocation of each kind (0 = off) */
static int failNewStringUTFAt, failNewObjectArrayAt, failFindClassAt, failGetFieldIDAt;

#define J(s) ((jstring)(s))

static void resetMock(void) {
	liveRefs = highWaterRefs = 0;
	pendingException = 0;
	disciplineErrors = 0;
	failNewStringUTFAt = failNewObjectArrayAt = failFindClassAt = failGetFieldIDAt = 0;
}

static void unsafeCall(const char *fn) {
	if (pendingException) {
		printf("  discipline: %s called with an exception pending\n", fn);
		disciplineErrors++;
	}
}

static void newRef(void) {
	if (++liveRefs > highWaterRefs)
		highWaterRefs = liveRefs;
}

/* Returns true when this call is selected for failure injection */
static int inject(int *counter) {
	if (*counter == 0 || --*counter != 0)
		return 0;
	pendingException = 1;
	return 1;
}

static const char* JNICALL mockGetStringUTFChars(JNIEnv *env, jstring str, jboolean *isCopy) {
	(void)env;
	unsafeCall(__func__);
	if (mode == MODE_FAIL) {
		/* A failed acquisition leaves *isCopy unset, as a real VM may */
		pendingException = 1;
		return NULL;
	}
	acquired++;
	if (mode == MODE_COPY) {
		if (isCopy) *isCopy = JNI_TRUE;
		return strdup((const char*)str);
	}
	if (isCopy) *isCopy = JNI_FALSE;
	return (const char*)str;
}

static void JNICALL mockReleaseStringUTFChars(JNIEnv *env, jstring str, const char* chars) {
	(void)env;
	if (chars == NULL || (mode == MODE_DIRECT && chars != (const char*)str)) {
		releaseErrors++;
		return;
	}
	if (mode == MODE_COPY)
		free((void*)(size_t)chars);
	released++;
}

static jstring JNICALL mockNewStringUTF(JNIEnv *env, const char *utf) {
	(void)env;
	unsafeCall(__func__);
	if (inject(&failNewStringUTFAt))
		return NULL;
	newRef();
	return (jstring)(size_t)utf;
}

static jclass JNICALL mockFindClass(JNIEnv *env, const char *name) {
	static char stringClass;
	(void)env; (void)name;
	unsafeCall(__func__);
	if (inject(&failFindClassAt))
		return NULL;
	newRef();
	return (jclass)&stringClass;
}

static void JNICALL mockDeleteLocalRef(JNIEnv *env, jobject obj) {
	(void)env; (void)obj;
	liveRefs--;
}

/* Fake object arrays */

#define FAKE_ARRAY_MAX 128

struct fakeArray {
	jsize len;
	jobject elems[FAKE_ARRAY_MAX];
};

static struct fakeArray fakeArrays[8];
static int fakeArrayCount;

static jobjectArray JNICALL mockNewObjectArray(JNIEnv *env, jsize len, jclass clazz, jobject init) {
	struct fakeArray *a;
	jsize i;
	(void)env; (void)clazz;
	unsafeCall(__func__);
	if (inject(&failNewObjectArrayAt))
		return NULL;
	if (len > FAKE_ARRAY_MAX || fakeArrayCount == 8) {
		fails++;
		printf("FAIL: fake array pool exhausted\n");
		return NULL;
	}
	a = &fakeArrays[fakeArrayCount++];
	a->len = len;
	for (i = 0; i < len; i++)
		a->elems[i] = init;
	newRef();
	return (jobjectArray)a;
}

static void JNICALL mockSetObjectArrayElement(JNIEnv *env, jobjectArray array, jsize index, jobject value) {
	struct fakeArray *a = (struct fakeArray *)array;
	(void)env;
	unsafeCall(__func__);
	if (a == NULL || index < 0 || index >= a->len) {
		releaseErrors++;
		return;
	}
	a->elems[index] = value;
}

/* Field access mocks sufficient for gs1encoderInitExJNI's use of InitOptions */

struct fakeInitOptions {
	jstring syntaxDictionary;
	jboolean fallbackOnSyndictError;
	jboolean noEmbedded;
};

static char synFid, fbFid, noEmbFid;

static jclass JNICALL mockGetObjectClass(JNIEnv *env, jobject obj) {
	(void)env;
	unsafeCall(__func__);
	return (jclass)obj;
}

static jfieldID JNICALL mockGetFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
	(void)env; (void)clazz; (void)sig;
	unsafeCall(__func__);
	if (inject(&failGetFieldIDAt))
		return NULL;
	if (strcmp(name, "syntaxDictionary") == 0)       return (jfieldID)&synFid;
	if (strcmp(name, "fallbackOnSyndictError") == 0) return (jfieldID)&fbFid;
	return (jfieldID)&noEmbFid;
}

static jobject JNICALL mockGetObjectField(JNIEnv *env, jobject obj, jfieldID fieldID) {
	(void)env;
	unsafeCall(__func__);
	return fieldID == (jfieldID)&synFid ? (jobject)((struct fakeInitOptions*)obj)->syntaxDictionary : NULL;
}

static jboolean JNICALL mockGetBooleanField(JNIEnv *env, jobject obj, jfieldID fieldID) {
	(void)env;
	unsafeCall(__func__);
	if (fieldID == (jfieldID)&fbFid)    return ((struct fakeInitOptions*)obj)->fallbackOnSyndictError;
	if (fieldID == (jfieldID)&noEmbFid) return ((struct fakeInitOptions*)obj)->noEmbedded;
	return JNI_FALSE;
}


#define CHECK(cond) do {						\
	if (!(cond)) {							\
		printf("FAIL: %s\n", #cond);				\
		fails++;						\
	}								\
} while (0)

/* 36 distinct, co-permissible AIs: enough HRI lines to expose an array
 * getter whose live local references scale with the AI count */
#define MANY_AIS \
	"(01)12312312312319(10)LOT1(21)SER1(22)CPV1(240)ADDL1(241)CUST1" \
	"(243)PCN1(250)SEC1(251)REF1(91)1(92)2(93)3(94)4(95)5(96)6(97)7" \
	"(98)8(99)9(11)250101(13)250102(15)250103(16)250104(17)250105" \
	"(7001)1231231231231(7002)MEAT(7003)2501011200(7030)276X1" \
	"(7031)276X2(7032)276X3(7033)276X4(7034)276X5(7035)276X6" \
	"(7036)276X7(7037)276X8(7038)276X9(7039)276X0"

#define MANY_AIS_COUNT 36


/* Exercise every wrapper that borrows string characters */
static void runOps(JNIEnv env, const char *label) {

	int a0 = acquired, r0 = released;
	jlong ctx, ctx2;
	struct fakeInitOptions opts = { J("../c-lib/gs1-syntax-dictionary.txt"), JNI_FALSE, JNI_FALSE };

	ctx = InitExJNI(&env, NULL, NULL, NULL);
	CHECK(ctx != 0);

	CHECK(SetDataJNI(&env, NULL, ctx, J("^0112312312312333")) == JNI_TRUE);
	CHECK(SetDataJNI(&env, NULL, ctx, J("^0112312312312334")) == JNI_FALSE);	/* bad check digit: release also required on failure */
	CHECK(SetAIdataJNI(&env, NULL, ctx, J("(01)12312312312319(99)TESTING123")) == JNI_TRUE);
	CHECK(SetScanJNI(&env, NULL, ctx, J("]e0011231231231233310ABC123\03599XYZ")) == JNI_TRUE);
	CHECK(SetAIdataJNI(&env, NULL, ctx, J("(01)12312312312319(99)TESTING123")) == JNI_TRUE);
	CHECK(GetDLuriJNI(&env, NULL, ctx, J("https://id.example.com/stem")) != NULL);
	CHECK(GetDLuriJNI(&env, NULL, ctx, NULL) != NULL);

	ctx2 = InitExJNI(&env, NULL, (jobject)&opts, NULL);
	CHECK(ctx2 != 0);
	FreeJNI(&env, NULL, ctx2);

	FreeJNI(&env, NULL, ctx);

	printf("%s: acquired %d, released %d%s\n", label,
		acquired - a0, released - r0,
		acquired - a0 == released - r0 ? "" : "  *** LEAK ***");
	if (acquired - a0 != released - r0)
		fails++;
}


/* Array getters must produce fully-populated arrays while keeping the live
 * local reference count within the JNI-ensured budget of 16 */
static void runArrayGetterOps(JNIEnv env) {

	jlong ctx;
	struct fakeArray *arr;
	jsize i;

	mode = MODE_COPY;
	resetMock();

	ctx = InitExJNI(&env, NULL, NULL, NULL);
	CHECK(ctx != 0);
	CHECK(SetAIdataJNI(&env, NULL, ctx, J(MANY_AIS)) == JNI_TRUE);

	resetMock();
	fakeArrayCount = 0;
	arr = (struct fakeArray *)GetHRIJNI(&env, NULL, ctx);
	CHECK(arr != NULL);
	if (arr != NULL) {
		CHECK(arr->len == MANY_AIS_COUNT);
		for (i = 0; i < arr->len; i++)
			CHECK(arr->elems[i] != NULL);
	}
	printf("getHRI     : %d lines, local ref high-water %d%s\n",
		arr ? (int)arr->len : -1, highWaterRefs,
		highWaterRefs <= 16 ? "" : "  *** OVER BUDGET ***");
	CHECK(highWaterRefs <= 16);

	/* DL URI with non-AI query parameters exercises the other array getter */
	CHECK(SetDataJNI(&env, NULL, ctx, J("https://id.example.org/01/12312312312319?99=TESTING123&name=x&purpose=y")) == JNI_TRUE);
	resetMock();
	fakeArrayCount = 0;
	arr = (struct fakeArray *)GetDLQPJNI(&env, NULL, ctx);
	CHECK(arr != NULL);
	if (arr != NULL) {
		CHECK(arr->len == 2);
		for (i = 0; i < arr->len; i++)
			CHECK(arr->elems[i] != NULL);
	}
	CHECK(highWaterRefs <= 16);

	/* Allocation failures must return NULL without further unsafe JNI calls */
	CHECK(SetAIdataJNI(&env, NULL, ctx, J(MANY_AIS)) == JNI_TRUE);

	resetMock();
	fakeArrayCount = 0;
	failFindClassAt = 1;
	CHECK(GetHRIJNI(&env, NULL, ctx) == NULL);
	CHECK(disciplineErrors == 0);

	resetMock();
	fakeArrayCount = 0;
	failNewObjectArrayAt = 1;
	CHECK(GetHRIJNI(&env, NULL, ctx) == NULL);
	CHECK(disciplineErrors == 0);

	resetMock();
	fakeArrayCount = 0;
	failNewStringUTFAt = 3;		/* fail mid-loop */
	CHECK(GetHRIJNI(&env, NULL, ctx) == NULL);
	CHECK(disciplineErrors == 0);

	resetMock();
	CHECK(SetDataJNI(&env, NULL, ctx, J("https://id.example.org/01/12312312312319?99=TESTING123&name=x&purpose=y")) == JNI_TRUE);
	resetMock();
	fakeArrayCount = 0;
	failNewStringUTFAt = 1;
	CHECK(GetDLQPJNI(&env, NULL, ctx) == NULL);
	CHECK(disciplineErrors == 0);

	printf("array fails: allocation failures bail out cleanly\n");

	resetMock();
	FreeJNI(&env, NULL, ctx);
}


int main(void) {

	struct JNINativeInterface_ fns;
	JNIEnv env = &fns;
	jlong ctx;
	struct fakeInitOptions opts = { J("../c-lib/gs1-syntax-dictionary.txt"), JNI_FALSE, JNI_FALSE };

	setvbuf(stdout, NULL, _IONBF, 0);

	memset(&fns, 0, sizeof(fns));
	fns.GetStringUTFChars     = mockGetStringUTFChars;
	fns.ReleaseStringUTFChars = mockReleaseStringUTFChars;
	fns.NewStringUTF          = mockNewStringUTF;
	fns.FindClass             = mockFindClass;
	fns.DeleteLocalRef        = mockDeleteLocalRef;
	fns.NewObjectArray        = mockNewObjectArray;
	fns.SetObjectArrayElement = mockSetObjectArrayElement;
	fns.GetObjectClass        = mockGetObjectClass;
	fns.GetFieldID            = mockGetFieldID;
	fns.GetObjectField        = mockGetObjectField;
	fns.GetBooleanField       = mockGetBooleanField;

	resetMock();

	mode = MODE_COPY;
	runOps(env, "copy mode  ");

	mode = MODE_DIRECT;
	runOps(env, "direct mode");

	/* Failed acquisitions must not reach the C API or be released */
	mode = MODE_FAIL;
	resetMock();
	ctx = InitExJNI(&env, NULL, NULL, NULL);
	CHECK(ctx != 0);
	pendingException = 0;
	CHECK(SetDataJNI(&env, NULL, ctx, J("x")) == JNI_FALSE);
	pendingException = 0;
	CHECK(SetAIdataJNI(&env, NULL, ctx, J("x")) == JNI_FALSE);
	pendingException = 0;
	CHECK(SetScanJNI(&env, NULL, ctx, J("x")) == JNI_FALSE);
	pendingException = 0;
	CHECK(GetDLuriJNI(&env, NULL, ctx, J("x")) == NULL);
	pendingException = 0;
	CHECK(InitExJNI(&env, NULL, (jobject)&opts, NULL) == 0);
	pendingException = 0;
	FreeJNI(&env, NULL, ctx);
	printf("fail mode  : no crash, no release of failed acquisition\n");

	/* A failed field lookup during init must bail out cleanly */
	mode = MODE_COPY;
	resetMock();
	failGetFieldIDAt = 1;
	CHECK(InitExJNI(&env, NULL, (jobject)&opts, NULL) == 0);
	CHECK(disciplineErrors == 0);
	printf("field fails: failed field lookup bails out cleanly\n");

	/* Fallback-warning marshalling: delivered on success; a marshalling
	 * failure must not leak the context or continue with the exception
	 * pending */
	{
		struct fakeInitOptions fbopts = { J("no-such-file.txt"), JNI_TRUE, JNI_FALSE };
		struct fakeArray *msgArr;
		jlong ctx2;

		resetMock();
		fakeArrayCount = 0;
		msgArr = &fakeArrays[fakeArrayCount++];
		msgArr->len = 1;
		msgArr->elems[0] = NULL;
		ctx2 = InitExJNI(&env, NULL, (jobject)&fbopts, (jobjectArray)msgArr);
		CHECK(ctx2 != 0);
		CHECK(msgArr->elems[0] != NULL);
		FreeJNI(&env, NULL, ctx2);

		resetMock();
		fakeArrayCount = 0;
		msgArr = &fakeArrays[fakeArrayCount++];
		msgArr->len = 1;
		msgArr->elems[0] = NULL;
		failNewStringUTFAt = 1;
		CHECK(InitExJNI(&env, NULL, (jobject)&fbopts, (jobjectArray)msgArr) == 0);
		CHECK(disciplineErrors == 0);
		printf("warn fails : failed warning marshalling bails out cleanly\n");
	}

	runArrayGetterOps(env);

	CHECK(releaseErrors == 0);

	if (fails) {
		printf("\n%d FAILURE(S)\n", fails);
		return 1;
	}

	printf("\nAll wrap tests passed\n");
	return 0;
}
