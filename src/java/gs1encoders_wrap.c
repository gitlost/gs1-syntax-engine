/*
 * JNI interface for accessing the GS1 Barcode Syntax Engine native library
 * from Java using the org.gs1.gs1encoders.GS1Encoder wrapper class.
 *
 * Copyright (c) 2022-2026 GS1 AISBL.
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
#include "gs1encoders.h"


#define MSG_BUF_SIZE 256


JNIEXPORT jstring JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetErrMsgJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx) {
    char *out = gs1_encoder_getErrMsg((gs1_encoder*)ctx);
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT jlong JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderInitExJNI(
        JNIEnv* env,
        jobject obj,
        jobject initOptions,
        jobjectArray outErrorMessage) {
    const char* path = NULL;
    jstring syntaxDictionary = NULL;
    jlong ret;
    char msgBuf[MSG_BUF_SIZE] = { 0 };
    gs1_encoder_init_opts_t opts = {
        .struct_size = sizeof(gs1_encoder_init_opts_t),
        .msgBuf = msgBuf,
        .msgBufSize = sizeof(msgBuf),
    };
    (void)obj;
    if (initOptions) {
        jclass cls = (*env)->GetObjectClass(env, initOptions);
        jfieldID synFid, fbFid, noEmbFid;
        synFid = (*env)->GetFieldID(env, cls, "syntaxDictionary", "Ljava/lang/String;");
        if (synFid == NULL)
            return 0;    /* NoSuchFieldError pending */
        fbFid = (*env)->GetFieldID(env, cls, "fallbackOnSyndictError", "Z");
        if (fbFid == NULL)
            return 0;    /* NoSuchFieldError pending */
        noEmbFid = (*env)->GetFieldID(env, cls, "noEmbedded", "Z");
        if (noEmbFid == NULL)
            return 0;    /* NoSuchFieldError pending */
        syntaxDictionary = (*env)->GetObjectField(env, initOptions, synFid);
        if ((*env)->GetBooleanField(env, initOptions, fbFid))    opts.flags |= gs1_encoder_iFALLBACK_ON_SYNDICT_ERROR;
        if ((*env)->GetBooleanField(env, initOptions, noEmbFid)) opts.flags |= gs1_encoder_iNO_EMBEDDED;
    }
    if (syntaxDictionary) {
        path = (*env)->GetStringUTFChars(env, syntaxDictionary, NULL);
        if (path == NULL)
            return 0;    /* OutOfMemoryError pending */
        opts.syntaxDictionary = path;
    }
    ret = (jlong)gs1_encoder_init_ex(NULL, &opts);
    if (path != NULL)
        (*env)->ReleaseStringUTFChars(env, syntaxDictionary, path);
    /* On failure (ret==0) the message is the error description; on success
     * with a non-empty msgBuf the message is a fallback warning (status
     * == GS1_ENCODERS_INIT_FALLBACK_TO_EMBEDDED_TABLE — currently the only
     * success-with-message case the C library produces). */
    if (outErrorMessage && msgBuf[0] != '\0') {
        jstring msg = (*env)->NewStringUTF(env, msgBuf);
        if (msg == NULL) {
            /* OutOfMemoryError pending; do not leak the context */
            if (ret)
                gs1_encoder_free((gs1_encoder *)ret);
            return 0;
        }
        (*env)->SetObjectArrayElement(env, outErrorMessage, 0, msg);
    }
    return ret;
}

JNIEXPORT void JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderFreeJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx) {
    gs1_encoder_free((gs1_encoder*)ctx);
}

JNIEXPORT jstring JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetVersionJNI(
        JNIEnv* env,
        jobject obj) {
    char *out = gs1_encoder_getVersion();
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT jint JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetSymJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx) {
    return gs1_encoder_getSym((gs1_encoder*)ctx);
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetSymJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx,
        jint sym) {
    return gs1_encoder_setSym((gs1_encoder*)ctx, sym);
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetAddCheckDigitJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx) {
    return gs1_encoder_getAddCheckDigit((gs1_encoder*)ctx);
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetAddCheckDigitJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx,
        jboolean value) {
    return gs1_encoder_setAddCheckDigit((gs1_encoder*)ctx, value);
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetIncludeDataTitlesInHRIJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx) {
    return gs1_encoder_getIncludeDataTitlesInHRI((gs1_encoder*)ctx);
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetIncludeDataTitlesInHRIJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx,
        jboolean value) {
    return gs1_encoder_setIncludeDataTitlesInHRI((gs1_encoder*)ctx, value);
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetPermitUnknownAIsJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx) {
    return gs1_encoder_getPermitUnknownAIs((gs1_encoder*)ctx);
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetPermitUnknownAIsJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx,
        jboolean value) {
    return gs1_encoder_setPermitUnknownAIs((gs1_encoder*)ctx, value);
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetPermitZeroSuppressedGTINinDLurisJNI(
        JNIEnv *env,
        jobject obj,
        jlong ctx) {
    return gs1_encoder_getPermitZeroSuppressedGTINinDLuris((gs1_encoder *) ctx);
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetPermitZeroSuppressedGTINinDLurisJNI(
        JNIEnv *env,
        jobject obj,
        jlong ctx,
        jboolean value) {
    return gs1_encoder_setPermitZeroSuppressedGTINinDLuris((gs1_encoder *) ctx, value);
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetValidationEnabledJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx,
        jint validation) {
    return gs1_encoder_getValidationEnabled((gs1_encoder*)ctx, (enum gs1_encoder_validations)validation);
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetValidationEnabledJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx,
        jint validation,
        jboolean value) {
    return gs1_encoder_setValidationEnabled((gs1_encoder*)ctx, (enum gs1_encoder_validations)validation, value);
}

JNIEXPORT jstring JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetDataStrJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx) {
    char *out = gs1_encoder_getDataStr((gs1_encoder*)ctx);
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetDataStrJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx,
        jstring value) {
    const char* str;
    jboolean ret;

    str = (*env)->GetStringUTFChars(env, value, NULL);
    if (str == NULL)
        return JNI_FALSE;    /* OutOfMemoryError pending */

    ret = gs1_encoder_setDataStr((gs1_encoder*)ctx, str);
    (*env)->ReleaseStringUTFChars(env, value, str);

    return ret;
}

JNIEXPORT jstring JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetAIdataStrJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx) {
    char *out = gs1_encoder_getAIdataStr((gs1_encoder*)ctx);
    return out ? (*env)->NewStringUTF(env, out) : NULL;
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetAIdataStrJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx,
        jstring value) {
    const char* str;
    jboolean ret;

    str = (*env)->GetStringUTFChars(env, value, NULL);
    if (str == NULL)
        return JNI_FALSE;    /* OutOfMemoryError pending */

    ret = gs1_encoder_setAIdataStr((gs1_encoder*)ctx, str);
    (*env)->ReleaseStringUTFChars(env, value, str);

    return ret;
}

JNIEXPORT jstring JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetScanDataJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx) {
    char *out = gs1_encoder_getScanData((gs1_encoder*)ctx);
    return out ? (*env)->NewStringUTF(env, out) : NULL;
}

JNIEXPORT jboolean JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderSetScanDataJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx,
        jstring value) {
    const char* str;
    jboolean ret;

    str = (*env)->GetStringUTFChars(env, value, NULL);
    if (str == NULL)
        return JNI_FALSE;    /* OutOfMemoryError pending */

    ret = gs1_encoder_setScanData((gs1_encoder*)ctx, str);
    (*env)->ReleaseStringUTFChars(env, value, str);

    return ret;
}

JNIEXPORT jstring JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetErrMarkupJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx) {
    char *out = gs1_encoder_getErrMarkup((gs1_encoder*)ctx);
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT jstring JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetDLuriJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx,
        jstring stem) {

    const char* out;
    const char* str;

    str = stem ? (*env)->GetStringUTFChars(env, stem, NULL) : NULL;
    if (stem != NULL && str == NULL)
        return NULL;    /* OutOfMemoryError pending */

    out = gs1_encoder_getDLuri((gs1_encoder*)ctx, (char*)str);
    if (str != NULL)
        (*env)->ReleaseStringUTFChars(env, stem, str);

    return out ? (*env)->NewStringUTF(env, out) : NULL;
}

JNIEXPORT jobjectArray JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetHRIJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx) {
    char **hri;
    jobjectArray ret;
    jclass stringClass;
    int i, numAIs;
    numAIs = gs1_encoder_getHRI((gs1_encoder*)ctx, &hri);
    stringClass = (*env)->FindClass(env, "java/lang/String");
    if (stringClass == NULL)
        return NULL;    /* exception pending */
    ret = (*env)->NewObjectArray(env, numAIs, stringClass, NULL);
    if (ret == NULL)
        return NULL;    /* exception pending */
    for (i = 0; i < numAIs; i++) {
        jstring line = (*env)->NewStringUTF(env, hri[i]);
        if (line == NULL)
            return NULL;    /* exception pending */
        (*env)->SetObjectArrayElement(env, ret, i, line);
        (*env)->DeleteLocalRef(env, line);
    }
    return ret;
}

JNIEXPORT jobjectArray JNICALL Java_org_gs1_gs1encoders_GS1Encoder_gs1encoderGetDLignoredQueryParamsJNI(
        JNIEnv* env,
        jobject obj,
        jlong ctx) {
    char **qp;
    jobjectArray ret;
    jclass stringClass;
    int i, numAIs;
    numAIs = gs1_encoder_getDLignoredQueryParams((gs1_encoder*)ctx, &qp);
    stringClass = (*env)->FindClass(env, "java/lang/String");
    if (stringClass == NULL)
        return NULL;    /* exception pending */
    ret = (*env)->NewObjectArray(env, numAIs, stringClass, NULL);
    if (ret == NULL)
        return NULL;    /* exception pending */
    for (i = 0; i < numAIs; i++) {
        jstring qpStr = (*env)->NewStringUTF(env, qp[i]);
        if (qpStr == NULL)
            return NULL;    /* exception pending */
        (*env)->SetObjectArrayElement(env, ret, i, qpStr);
        (*env)->DeleteLocalRef(env, qpStr);
    }
    return ret;
}
