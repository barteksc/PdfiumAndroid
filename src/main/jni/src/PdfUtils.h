/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PDF_UTILS_H_
#define PDF_UTILS_H_

#include "jni.h"
#include "JNIHelp.h"


namespace tools {
    int getBlock(void *param, unsigned long position, unsigned char *outBuffer,
                 unsigned long size);

    bool forwardPdfiumError(JNIEnv *env);

#define HANDLE_PDFIUM_ERROR_STATE(env)                         \
        {                                                      \
            bool isExceptionPending = forwardPdfiumError(env); \
            if (isExceptionPending) {                          \
                return;                                        \
            }                                                  \
        }

#define HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, retCode)  \
        {                                                      \
            bool isExceptionPending = forwardPdfiumError(env); \
            if (isExceptionPending) {                          \
                return retCode;                                \
            }                                                  \
        }

}
#endif /* PDF_UTILS_H_ */