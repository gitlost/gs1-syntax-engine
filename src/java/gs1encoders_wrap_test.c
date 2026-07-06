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
 * Every wrapper that borrows string characters must be exercised here, in
 * each mode, and each mode must end with zero outstanding acquisitions.
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
JNIEXPORT jlong    JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderInitExJNI(JNIEnv*, jobject, jobject, jobjectArray);
JNIEXPORT void     JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderFreeJNI(JNIEnv*, jobject, jlong);
JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetDataStrJNI(JNIEnv*, jobject, jlong, jstring);
JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetAIdataStrJNI(JNIEnv*, jobject, jlong, jstring);
JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetScanDataJNI(JNIEnv*, jobject, jlong, jstring);
JNIEXPORT jstring  JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetDLuriJNI(JNIEnv*, jobject, jlong, jstring);


/*
 *  Mock environment
 *
 *  A jstring is a pointer to the UTF-8 characters themselves.
 *
 */

enum mode { MODE_COPY, MODE_DIRECT, MODE_FAIL };

static enum mode mode;
static int acquired, released, releaseErrors, fails;

#define J(s) ((jstring)(s))

static const char* JNICALL mockGetStringUTFChars(JNIEnv *env, jstring str, jboolean *isCopy) {
	(void)env;
	if (mode == MODE_FAIL) {
		/* A failed acquisition leaves *isCopy unset, as a real VM may */
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
	return (jstring)(size_t)utf;
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
	return (jclass)obj;
}

static jfieldID JNICALL mockGetFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
	(void)env; (void)clazz; (void)sig;
	if (strcmp(name, "syntaxDictionary") == 0)       return (jfieldID)&synFid;
	if (strcmp(name, "fallbackOnSyndictError") == 0) return (jfieldID)&fbFid;
	return (jfieldID)&noEmbFid;
}

static jobject JNICALL mockGetObjectField(JNIEnv *env, jobject obj, jfieldID fieldID) {
	(void)env;
	return fieldID == (jfieldID)&synFid ? (jobject)((struct fakeInitOptions*)obj)->syntaxDictionary : NULL;
}

static jboolean JNICALL mockGetBooleanField(JNIEnv *env, jobject obj, jfieldID fieldID) {
	(void)env;
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


/* Exercise every wrapper that borrows string characters */
static void runOps(JNIEnv env, const char *label) {

	int a0 = acquired, r0 = released;
	jlong ctx, ctx2;
	struct fakeInitOptions opts = { J("../c-lib/gs1-syntax-dictionary.txt"), JNI_FALSE, JNI_FALSE };

	ctx = Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderInitExJNI(&env, NULL, NULL, NULL);
	CHECK(ctx != 0);

	CHECK(Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetDataStrJNI(&env, NULL, ctx, J("^0112312312312333")) == JNI_TRUE);
	CHECK(Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetDataStrJNI(&env, NULL, ctx, J("^0112312312312334")) == JNI_FALSE);	/* bad check digit: release also required on failure */
	CHECK(Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetAIdataStrJNI(&env, NULL, ctx, J("(01)12312312312319(99)TESTING123")) == JNI_TRUE);
	CHECK(Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetScanDataJNI(&env, NULL, ctx, J("]e0011231231231233310ABC123\03599XYZ")) == JNI_TRUE);
	CHECK(Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetAIdataStrJNI(&env, NULL, ctx, J("(01)12312312312319(99)TESTING123")) == JNI_TRUE);
	CHECK(Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetDLuriJNI(&env, NULL, ctx, J("https://id.example.com/stem")) != NULL);
	CHECK(Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetDLuriJNI(&env, NULL, ctx, NULL) != NULL);

	ctx2 = Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderInitExJNI(&env, NULL, (jobject)&opts, NULL);
	CHECK(ctx2 != 0);
	Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderFreeJNI(&env, NULL, ctx2);

	Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderFreeJNI(&env, NULL, ctx);

	printf("%s: acquired %d, released %d%s\n", label,
		acquired - a0, released - r0,
		acquired - a0 == released - r0 ? "" : "  *** LEAK ***");
	if (acquired - a0 != released - r0)
		fails++;
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
	fns.GetObjectClass        = mockGetObjectClass;
	fns.GetFieldID            = mockGetFieldID;
	fns.GetObjectField        = mockGetObjectField;
	fns.GetBooleanField       = mockGetBooleanField;

	mode = MODE_COPY;
	runOps(env, "copy mode  ");

	mode = MODE_DIRECT;
	runOps(env, "direct mode");

	/* Failed acquisitions must not reach the C API or be released */
	mode = MODE_FAIL;
	ctx = Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderInitExJNI(&env, NULL, NULL, NULL);
	CHECK(ctx != 0);
	CHECK(Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetDataStrJNI(&env, NULL, ctx, J("x")) == JNI_FALSE);
	CHECK(Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetAIdataStrJNI(&env, NULL, ctx, J("x")) == JNI_FALSE);
	CHECK(Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetScanDataJNI(&env, NULL, ctx, J("x")) == JNI_FALSE);
	CHECK(Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetDLuriJNI(&env, NULL, ctx, J("x")) == NULL);
	CHECK(Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderInitExJNI(&env, NULL, (jobject)&opts, NULL) == 0);
	Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderFreeJNI(&env, NULL, ctx);
	printf("fail mode  : no crash, no release of failed acquisition\n");

	CHECK(releaseErrors == 0);

	if (fails) {
		printf("\n%d FAILURE(S)\n", fails);
		return 1;
	}

	printf("\nAll wrap tests passed\n");
	return 0;
}
