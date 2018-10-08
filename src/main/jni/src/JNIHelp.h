#ifndef JNIHELP_H_
#define JNIHELP_H_
#include "jni.h"
#include <errno.h>
#include <unistd.h>
#include <android/log.h>
#define LOGI(...)   __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...)   __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...)   __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

inline int jniThrowException(JNIEnv* env, const char* className, const char* msg) {
    jclass Exception = env->FindClass(className);
    return (env)->ThrowNew( Exception, msg );
}

template<class string_type>
inline typename string_type::value_type *WriteInto(string_type *str, size_t length_with_null) {
    str->reserve(length_with_null);
    str->resize(length_with_null - 1);
    return &((*str)[0]);
}

#endif