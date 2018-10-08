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

#include "PdfUtils.h"
#include "jni.h"
#include "fpdfview.h"

#define LOG_TAG "PdfUtils"

namespace tools {
    static int sUnmatchedPdfiumInitRequestCount = 0;

    int getBlock(void *param, unsigned long position, unsigned char *outBuffer,
                 unsigned long size) {
        const int fd = reinterpret_cast<intptr_t>(param);
        const int readCount = pread(fd, outBuffer, size, position);
        if (readCount < 0) {
            LOGE("Cannot read from file descriptor. Error:%d", errno);
            return 0;
        }
        return 1;
    }

// Check if the last pdfium command failed and if so, forward the error to java via an exception. If
// this function returns true an exception is pending.
    bool forwardPdfiumError(JNIEnv *env) {
        long error = FPDF_GetLastError();
        switch (error) {
            case FPDF_ERR_SUCCESS:
                return false;
            case FPDF_ERR_FILE:
                jniThrowException(env, "java/io/IOException", "file not found or cannot be opened");
                break;
            case FPDF_ERR_FORMAT:
                jniThrowException(env, "java/io/IOException",
                                  "file not in PDF format or corrupted");
                break;
            case FPDF_ERR_PASSWORD:
                jniThrowException(env, "java/lang/SecurityException",
                                  "password required or incorrect password");
                break;
            case FPDF_ERR_SECURITY:
                jniThrowException(env, "java/lang/SecurityException",
                                  "unsupported security scheme");
                break;
            case FPDF_ERR_PAGE:
                jniThrowException(env, "java/io/IOException", "page not found or content error");
                break;
            case FPDF_ERR_UNKNOWN:
            default:
                jniThrowException(env, "java/lang/Exception", "unknown error");
        }

        return true;
    }

}