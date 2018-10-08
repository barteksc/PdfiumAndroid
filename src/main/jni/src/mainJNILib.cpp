#include "PdfUtils.h"
#include "JNIHelp.h"

#include "util.hpp"

extern "C" {
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

}

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/bitmap.h>
#include <utils/Mutex.h>

using namespace android;
using namespace tools;

#include <fpdfview.h>
#include <fpdf_doc.h>
#include <string>
#include <vector>
#include <fpdf_text.h>

static int sUnmatchedPdfiumInitRequestCount = 0;
static struct {
    jfieldID x;
    jfieldID y;
} gPointClassInfo;
static jclass gPointClass;
static struct {
    jfieldID left;
    jfieldID right;
    jfieldID top;
    jfieldID bottom;
} gRectFClassInfo;
static jclass gRectFClass;
static jclass gStringClass;
static jmethodID gStringMethodGetBytes;

static struct rgb {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

void JNI_OnUnload(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return;
    }
    env->DeleteGlobalRef(gPointClass);
    env->DeleteGlobalRef(gRectFClass);
    env->DeleteGlobalRef(gStringClass);
}

static bool initializeLibraryIfNeeded(JNIEnv *env) {
    if (sUnmatchedPdfiumInitRequestCount == 0) {
        FPDF_InitLibrary();
        HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, false);
    }
    sUnmatchedPdfiumInitRequestCount++;
    return true;
}

static void destroyLibraryIfNeeded(JNIEnv *env, bool handleError) {
    if (sUnmatchedPdfiumInitRequestCount == 1) {
        FPDF_DestroyLibrary();
        if (handleError) {
            HANDLE_PDFIUM_ERROR_STATE(env);
        }
    }
    sUnmatchedPdfiumInitRequestCount--;
}

static inline long getFileSize(int fd) {
    struct stat file_state;
    if (fstat(fd, &file_state) >= 0) {
        return (long) (file_state.st_size);
    } else {
        LOGE("Error getting file size");
        return 0;
    }
}

static uint16_t rgbTo565(rgb *color) {
    return ((color->red >> 3) << 11) | ((color->green >> 2) << 5) | (color->blue >> 3);
}

static void rgbBitmapTo565(void *source, int sourceStride, void *dest, AndroidBitmapInfo *info) {
    rgb *srcLine;
    uint16_t *dstLine;
    int y, x;
    for (y = 0; y < info->height; y++) {
        srcLine = (rgb *) source;
        dstLine = (uint16_t *) dest;
        for (x = 0; x < info->width; x++) {
            dstLine[x] = rgbTo565(&srcLine[x]);
        }
        source = (char *) source + sourceStride;
        dest = (char *) dest + info->stride;
    }
}

static FPDF_WIDESTRING GetStringUTF16LEChars(JNIEnv *env, jstring str) {
    jstring charsetName = env->NewStringUTF("UTF-16LE");
    auto stringBytes = (jbyteArray) env->CallObjectMethod(str, gStringMethodGetBytes, charsetName);
    env->DeleteLocalRef(charsetName);
    jsize length = env->GetArrayLength(stringBytes);
    jbyte *s = env->GetByteArrayElements(stringBytes, NULL);
    auto *ss = (jbyte *) malloc(length + 2);
    ss[length] = 0;
    ss[length + 1] = 0;
    memcpy((void *) ss, s, length);
    env->ReleaseByteArrayElements(stringBytes, s, JNI_ABORT);
    env->DeleteLocalRef(stringBytes);
    return (FPDF_WIDESTRING) ss;
}

static inline void ReleaseStringUTF16LEChars(FPDF_WIDESTRING ss) {
    free((void *) ss);
}

extern "C" { //For JNI support


JNI_FUNC(jlong, PdfDocument, nativeInit)(JNI_ARGS) {
    jclass clazz = env->FindClass((const char *) "android/graphics/Point");
    gPointClass = (jclass) env->NewGlobalRef(clazz);
    env->DeleteLocalRef(clazz);
    gPointClassInfo.x = env->GetFieldID(gPointClass, "x", "I");
    gPointClassInfo.y = env->GetFieldID(gPointClass, "y", "I");

    clazz = env->FindClass((const char *) "android/graphics/RectF");
    gRectFClass = (jclass) env->NewGlobalRef(clazz);
    env->DeleteLocalRef(clazz);
    gRectFClassInfo.left = env->GetFieldID(gRectFClass, "left", "F");
    gRectFClassInfo.right = env->GetFieldID(gRectFClass, "right", "F");
    gRectFClassInfo.top = env->GetFieldID(gRectFClass, "top", "F");
    gRectFClassInfo.bottom = env->GetFieldID(gRectFClass, "bottom", "F");

    clazz = env->FindClass((const char *) "java/lang/String");
    gStringClass = (jclass) env->NewGlobalRef(clazz);
    env->DeleteLocalRef(clazz);
    gStringMethodGetBytes = env->GetMethodID(gStringClass, "getBytes", "(Ljava/lang/String;)[B");

    return JNI_VERSION_1_6;
}


JNI_FUNC(jlong, PdfDocument, nativeGetFileSize)(JNI_ARGS, jint fd) {
    auto fileLength = (size_t) getFileSize(fd);
    if (fileLength <= 0) {
        jniThrowException(env, "java/io/IOException",
                          "File is empty");
        return -1;
    }
    return fileLength;
}

JNI_FUNC(jint, PdfDocument, nativeGetPageCount)(JNI_ARGS, jlong documentPtr) {
    auto document = reinterpret_cast<FPDF_DOCUMENT>(documentPtr);
    int pageCount = FPDF_GetPageCount(document);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1);
    return pageCount;
}

JNI_FUNC(jlong, PdfDocument, nativeOpenPageAndGetSize)(JNI_ARGS, jlong documentPtr, jint pageIndex, jobject outSize) {
    auto document = reinterpret_cast<FPDF_DOCUMENT>(documentPtr);
    FPDF_PAGE page = FPDF_LoadPage(document, pageIndex);
    if (!page) {
        jniThrowException(env, "java/lang/IllegalStateException",
                          "cannot load page");
        return -1;
    }
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    double width = 0;
    double height = 0;
    int result = FPDF_GetPageSizeByIndex(document, pageIndex, &width, &height);
    if (!result) {
        jniThrowException(env, "java/lang/IllegalStateException",
                          "cannot get page size");
        return -1;
    }
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    env->SetIntField(outSize, gPointClassInfo.x, width);
    env->SetIntField(outSize, gPointClassInfo.y, height);
    return reinterpret_cast<jlong>(page);
}

JNI_FUNC(jlong, PdfDocument, nativeOpen)(JNI_ARGS, jint fd, jlong size, jstring password) {
    bool isInitialized = initializeLibraryIfNeeded(env);
    if (!isInitialized) {
        return -1;
    }
    FPDF_FILEACCESS loader;
    loader.m_FileLen = static_cast<unsigned long>(size);
    loader.m_Param = reinterpret_cast<void *>(intptr_t(fd));
    loader.m_GetBlock = &getBlock;
    const char *cpassword = nullptr;
    if (password != nullptr) {
        cpassword = env->GetStringUTFChars(password, nullptr);
    }
    FPDF_DOCUMENT document = FPDF_LoadCustomDocument(&loader, cpassword);
    if (nullptr != cpassword) {
        env->ReleaseStringUTFChars(password, cpassword);
    }
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    if (!document) {
        forwardPdfiumError(env);
        destroyLibraryIfNeeded(env, false);
        return -1;
    }

    return reinterpret_cast<jlong>(document);
}

JNI_FUNC(jlong, PdfDocument, nativeOpenByteArray)(JNI_ARGS, jbyteArray data, jstring password) {
    const char *cpassword = nullptr;
    if (password != nullptr) {
        cpassword = env->GetStringUTFChars(password, nullptr);
    }
    jbyte *cData = env->GetByteArrayElements(data, nullptr);
    int size = (int) env->GetArrayLength(data);
    auto *cDataCopy = new jbyte[size];
    memcpy(cDataCopy, cData, static_cast<size_t>(size));
    FPDF_DOCUMENT document = FPDF_LoadMemDocument(reinterpret_cast<const void *>(cDataCopy), size, cpassword);
    env->ReleaseByteArrayElements(data, cData, JNI_ABORT);
    if (nullptr != cpassword) {
        env->ReleaseStringUTFChars(password, cpassword);
    }
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    return reinterpret_cast<jlong>(document);
}

JNI_FUNC(void, PdfDocument, nativeClose)(JNI_ARGS, jlong documentPtr) {
    auto document = reinterpret_cast<FPDF_DOCUMENT>(documentPtr);
    FPDF_CloseDocument(document);
    HANDLE_PDFIUM_ERROR_STATE(env)
    destroyLibraryIfNeeded(env, true);
}

JNI_FUNC(jboolean, PdfDocument, nativeScaleForPrinting)(JNI_ARGS, jlong documentPtr) {
    auto document = reinterpret_cast<FPDF_DOCUMENT>(documentPtr);
    FPDF_BOOL printScaling = FPDF_VIEWERREF_GetPrintScaling(document);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, false);
    return static_cast<jboolean>(printScaling);
}

JNI_FUNC(void, PdfDocument, nativeClosePage)(JNI_ARGS, jlong pagePtr) {
    auto page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    FPDF_ClosePage(page);
    HANDLE_PDFIUM_ERROR_STATE(env)
}

JNI_FUNC(void, PdfDocument, nativeGetPageSizeByIndex)(JNI_ARGS, jlong docPtr, jint pageIndex, jobject outSize) {
    auto doc = reinterpret_cast<FPDF_DOCUMENT>(docPtr);
    if (doc == nullptr) {
        LOGE("Document is null");
        jniThrowException(env, "java/lang/IllegalStateException",
                          "Document is null");
        return;
    }
    double width, height;
    FPDF_GetPageSizeByIndex(doc, pageIndex, &width, &height);
    HANDLE_PDFIUM_ERROR_STATE(env)
    env->SetIntField(outSize, gPointClassInfo.x, width);
    env->SetIntField(outSize, gPointClassInfo.y, height);
}

JNI_FUNC(void, PdfDocument, nativeRenderPage)(JNI_ARGS, jlong pagePtr, jobject objSurface, jint startX, jint startY,
                                              jint drawSizeHor, jint drawSizeVer,
                                              jboolean renderAnnot) {
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, objSurface);
    if (nativeWindow == nullptr) {
        LOGE("native window pointer null");
        return;
    }

    auto page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    if (nullptr == page) {
        LOGE("Render page pointers invalid");
        return;
    }

    if (ANativeWindow_getFormat(nativeWindow) != WINDOW_FORMAT_RGBA_8888) {
        LOGD("Set format to RGBA_8888");
        ANativeWindow_setBuffersGeometry(nativeWindow, ANativeWindow_getWidth(nativeWindow), ANativeWindow_getHeight(nativeWindow),
                                         WINDOW_FORMAT_RGBA_8888);
    }

    ANativeWindow_Buffer buffer;
    int ret;
    if ((ret = ANativeWindow_lock(nativeWindow, &buffer, nullptr)) != 0) {
        LOGE("Locking native window failed: %s", strerror(ret * -1));
        return;
    }
    FPDF_BITMAP pdfBitmap = FPDFBitmap_CreateEx(drawSizeHor, drawSizeVer,
                                                FPDFBitmap_BGRA,
                                                buffer.bits,
                                                (int) (buffer.stride) * 4);
    HANDLE_PDFIUM_ERROR_STATE(env)
    int flags = FPDF_REVERSE_BYTE_ORDER | FPDF_LCD_TEXT;

    if (renderAnnot) {
        flags |= FPDF_ANNOT;
    }
    FPDF_RenderPageBitmap(pdfBitmap, page,
                          startX, startY,
                          drawSizeHor, drawSizeVer,
                          0, flags);
    ANativeWindow_unlockAndPost(nativeWindow);
    ANativeWindow_release(nativeWindow);
    HANDLE_PDFIUM_ERROR_STATE(env)
}

JNI_FUNC(void, PdfDocument, nativeRenderPageBitmap)(JNI_ARGS, jlong pagePtr, jobject bitmap,
                                                    jint startX, jint startY,
                                                    jint drawSizeHor, jint drawSizeVer,
                                                    jlong backgroundColor,
                                                    jboolean renderAnnot) {

    auto page = reinterpret_cast<FPDF_PAGE>(pagePtr);

    if (page == nullptr || bitmap == nullptr) {
        LOGE("Render page pointers invalid");
        return;
    }

    AndroidBitmapInfo info;
    int ret;
    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE("Fetching bitmap info failed: %s", strerror(ret * -1));
        return;
    }

    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 &&
        info.format != ANDROID_BITMAP_FORMAT_RGB_565) {
        LOGE("Bitmap format must be RGBA_8888 or RGB_565");
        return;
    }

    void *addr;
    if ((ret = AndroidBitmap_lockPixels(env, bitmap, &addr)) != 0) {
        LOGE("Locking bitmap failed: %s", strerror(ret * -1));
        return;
    }

    void *tmp;
    int format;
    int sourceStride;
    int canvasHorSize = info.width;
    int canvasVerSize = info.height;
    if (info.format == ANDROID_BITMAP_FORMAT_RGB_565) {
        tmp = malloc(canvasVerSize * canvasHorSize * sizeof(rgb));
        sourceStride = canvasHorSize * sizeof(rgb);
        format = FPDFBitmap_BGR;
    } else {
        tmp = addr;
        sourceStride = info.stride;
        format = FPDFBitmap_BGRA;
    }

    FPDF_BITMAP pdfBitmap = FPDFBitmap_CreateEx(canvasHorSize, canvasVerSize, format, tmp, sourceStride);
    HANDLE_PDFIUM_ERROR_STATE(env)
    unsigned int flags = FPDF_REVERSE_BYTE_ORDER | FPDF_LCD_TEXT;

    if (renderAnnot) {
        flags |= FPDF_ANNOT;
    }

    if (backgroundColor != 0) {
        FPDFBitmap_FillRect(pdfBitmap,
                            (startX < 0) ? 0 : (int) startX,
                            (startY < 0) ? 0 : (int) startY,
                            (canvasHorSize < drawSizeHor) ? canvasHorSize : (int) drawSizeHor,
                            (canvasVerSize < drawSizeVer) ? canvasVerSize : (int) drawSizeVer,
                            backgroundColor);
    }

    FPDF_RenderPageBitmap(pdfBitmap, page, startX, startY, (int) drawSizeHor, (int) drawSizeVer, 0, flags);
    HANDLE_PDFIUM_ERROR_STATE(env)
    if (info.format == ANDROID_BITMAP_FORMAT_RGB_565) {
        rgbBitmapTo565(tmp, sourceStride, addr, &info);
        free(tmp);
    }

    AndroidBitmap_unlockPixels(env, bitmap);
}


JNI_FUNC(jstring, PdfDocument, nativeGetMetaText)(JNI_ARGS, jlong docPtr, jstring tag) {
    const char *ctag = env->GetStringUTFChars(tag, nullptr);
    if (ctag == nullptr) {
        return env->NewStringUTF("");
    }
    auto doc = reinterpret_cast<FPDF_DOCUMENT>(docPtr);
    size_t bufferLen = FPDF_GetMetaText(doc, ctag, nullptr, 0);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, nullptr)
    if (bufferLen <= 2) {
        return env->NewStringUTF("");
    }
    std::wstring text;
    FPDF_GetMetaText(doc, ctag, WriteInto(&text, bufferLen + 1), bufferLen);
    env->ReleaseStringUTFChars(tag, ctag);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, nullptr)
    return env->NewString((jchar *) text.c_str(), bufferLen / 2 - 1);
}

JNI_FUNC(jlong, PdfDocument, nativeGetFirstChildBookmark)(JNI_ARGS, jlong docPtr, jlong bookmarkPtr) {
    auto doc = reinterpret_cast<FPDF_DOCUMENT>(docPtr);
    FPDF_BOOKMARK parent;
    if (bookmarkPtr == 0) {
        parent = nullptr;
    } else {
        parent = reinterpret_cast<FPDF_BOOKMARK>(bookmarkPtr);
    }
    FPDF_BOOKMARK bookmark = FPDFBookmark_GetFirstChild(doc, parent);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    if (bookmark == nullptr) {
        return 0;
    }
    return reinterpret_cast<jlong>(bookmark);
}

JNI_FUNC(jlong, PdfDocument, nativeGetSiblingBookmark)(JNI_ARGS, jlong docPtr, jlong bookmarkPtr) {
    auto doc = reinterpret_cast<FPDF_DOCUMENT>(docPtr);
    auto parent = reinterpret_cast<FPDF_BOOKMARK>(bookmarkPtr);
    FPDF_BOOKMARK bookmark = FPDFBookmark_GetNextSibling(doc, parent);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    if (bookmark == nullptr) {
        return 0;
    }
    return reinterpret_cast<jlong>(bookmark);
}

JNI_FUNC(jstring, PdfDocument, nativeGetBookmarkTitle)(JNI_ARGS, jlong bookmarkPtr) {
    auto bookmark = reinterpret_cast<FPDF_BOOKMARK>(bookmarkPtr);
    size_t bufferLen = FPDFBookmark_GetTitle(bookmark, nullptr, 0);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, nullptr)
    if (bufferLen <= 2) {
        return env->NewStringUTF("");
    }
    std::wstring title;
    FPDFBookmark_GetTitle(bookmark, WriteInto(&title, bufferLen + 1), bufferLen);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, nullptr)
    return env->NewString((jchar *) title.c_str(), bufferLen / 2 - 1);
}

JNI_FUNC(jlong, PdfDocument, nativeGetBookmarkDestIndex)(JNI_ARGS, jlong docPtr, jlong bookmarkPtr) {
    auto doc = reinterpret_cast<FPDF_DOCUMENT>(docPtr);
    auto bookmark = reinterpret_cast<FPDF_BOOKMARK>(bookmarkPtr);

    FPDF_DEST dest = FPDFBookmark_GetDest(doc, bookmark);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    if (dest == nullptr) {
        return -1;
    }
    return (jlong) FPDFDest_GetPageIndex(doc, dest);
}

JNI_FUNC(jlongArray, PdfDocument, nativeGetPageLinks)(JNI_ARGS, jlong pagePtr) {
    auto page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    int pos = 0;
    std::vector<jlong> links;
    FPDF_LINK link;
    while (FPDFLink_Enumerate(page, &pos, &link)) {
        links.push_back(reinterpret_cast<jlong>(link));
    }
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, nullptr)
    auto size = static_cast<jsize>(links.size());
    jlongArray result = env->NewLongArray(size);
    env->SetLongArrayRegion(result, 0, size, &links[0]);
    return result;
}

JNI_FUNC(jlong, PdfDocument, nativeGetDestPageIndex)(JNI_ARGS, jlong docPtr, jlong linkPtr) {
    auto doc = reinterpret_cast<FPDF_DOCUMENT>(docPtr);
    auto link = reinterpret_cast<FPDF_LINK>(linkPtr);
    FPDF_DEST dest = FPDFLink_GetDest(doc, link);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    if (dest == nullptr) {
        return 0;
    }
    return FPDFDest_GetPageIndex(doc, dest);
}

JNI_FUNC(jstring, PdfDocument, nativeGetLinkURI)(JNI_ARGS, jlong docPtr, jlong linkPtr) {
    auto doc = reinterpret_cast<FPDF_DOCUMENT>(docPtr);
    auto link = reinterpret_cast<FPDF_LINK>(linkPtr);
    FPDF_ACTION action = FPDFLink_GetAction(link);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, nullptr)
    if (action == nullptr) {
        return nullptr;
    }
    size_t bufferLen = FPDFAction_GetURIPath(doc, action, nullptr, 0);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, nullptr)
    if (bufferLen <= 0) {
        return env->NewStringUTF("");
    }
    std::string uri;
    FPDFAction_GetURIPath(doc, action, WriteInto(&uri, bufferLen), bufferLen);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, nullptr)
    return env->NewStringUTF(uri.c_str());
}

JNI_FUNC(jboolean, PdfDocument, nativeGetLinkRect)(JNI_ARGS, jlong linkPtr, jobject outRectF) {
    auto link = reinterpret_cast<FPDF_LINK>(linkPtr);
    FS_RECTF fsRectF;
    FPDF_BOOL result = FPDFLink_GetAnnotRect(link, &fsRectF);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, JNI_FALSE)
    if (!result) {
        return JNI_FALSE;
    }
    env->SetFloatField(outRectF, gRectFClassInfo.left, fsRectF.left);
    env->SetFloatField(outRectF, gRectFClassInfo.right, fsRectF.right);
    env->SetFloatField(outRectF, gRectFClassInfo.top, fsRectF.top);
    env->SetFloatField(outRectF, gRectFClassInfo.bottom, fsRectF.bottom);
    return JNI_TRUE;
}

JNI_FUNC(void, PdfDocument, nativePageCoordsToDevice)(JNI_ARGS, jlong pagePtr, jint startX,
                                                      jint startY, jint sizeX,
                                                      jint sizeY, jint rotate, jdouble pageX,
                                                      jdouble pageY, jobject outSize) {
    auto page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    int deviceX, deviceY;
    FPDF_PageToDevice(page, startX, startY, sizeX, sizeY, rotate, pageX, pageY, &deviceX, &deviceY);
    HANDLE_PDFIUM_ERROR_STATE(env);
    env->SetIntField(outSize, gPointClassInfo.x, deviceX);
    env->SetIntField(outSize, gPointClassInfo.y, deviceY);
}

JNI_FUNC(jlong, PdfDocument, nativeTextLoadPage)(JNI_ARGS, jlong pagePtr) {
    auto pPagePtr = reinterpret_cast<FPDF_PAGE>(pagePtr);
    jlong pTextPage = reinterpret_cast<jlong>( FPDFText_LoadPage(pPagePtr));
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    return pTextPage;
}

JNI_FUNC(void, PdfDocument, nativeTextClosePage)(JNI_ARGS, jlong textPtr) {
    auto pTextPage = reinterpret_cast<FPDF_TEXTPAGE>(textPtr);
    FPDFText_ClosePage(pTextPage);
    HANDLE_PDFIUM_ERROR_STATE(env)
}

JNI_FUNC(jlong, PdfDocument, nativeTextFindStart)(JNI_ARGS, jlong textPtr, jstring query, jint flags, jint startIndex) {
    auto pTextPage = reinterpret_cast<FPDF_TEXTPAGE>(textPtr);
    FPDF_WIDESTRING ss = GetStringUTF16LEChars(env, query);
    FPDF_SCHHANDLE searchHandle = FPDFText_FindStart(pTextPage, ss, (unsigned long) flags, startIndex);
    ReleaseStringUTF16LEChars(ss);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    if (searchHandle == nullptr) {
        LOGE("FPDFTextFindStart: FPDFTextFindStart did not return success");
    }
    return reinterpret_cast<jlong>(searchHandle);
}

JNI_FUNC(jboolean, PdfDocument, nativeTextFindNext)(JNI_ARGS, jlong searchPtr) {
    auto pSearch = reinterpret_cast<FPDF_SCHHANDLE>(searchPtr);
    FPDF_BOOL hasNext = FPDFText_FindNext(pSearch);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, JNI_FALSE)
    return static_cast<jboolean>(hasNext);
}

JNI_FUNC(jboolean, PdfDocument, nativeTextFindPrev)(JNI_ARGS, jlong searchPtr) {
    auto pSearch = reinterpret_cast<FPDF_SCHHANDLE>(searchPtr);
    FPDF_BOOL hasPrev = FPDFText_FindPrev(pSearch);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, JNI_FALSE)
    return static_cast<jboolean>(hasPrev);
}

JNI_FUNC(jstring, PdfDocument, nativeTextGetText)(JNI_ARGS, jlong textPtr, jint nStart, jint nCount) {
    auto pTextPage = reinterpret_cast<FPDF_TEXTPAGE>(textPtr);
    auto *pBuff = new FPDF_WCHAR[nCount + 1];
    int ret = FPDFText_GetText(pTextPage, nStart, nCount, pBuff);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, nullptr)
    if (ret == 0) {
        LOGE("FPDFTextGetText: FPDFTextGetText did not return success");
    }
    return env->NewString(pBuff, nCount + 1);
}

JNI_FUNC(jint, PdfDocument, nativeTextCountChars)(JNI_ARGS, jlong textPtr) {
    auto pTextPage = reinterpret_cast<FPDF_TEXTPAGE>(textPtr);
    int count = 0;
    count = FPDFText_CountChars(pTextPage);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    return count;
}

JNI_FUNC(jint, PdfDocument, nativeTextCountRects)(JNI_ARGS, jlong textPtr, jint start, jint count) {
    auto pTextPage = reinterpret_cast<FPDF_TEXTPAGE>(textPtr);
    jint rectCount = FPDFText_CountRects(pTextPage, start, count);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    return rectCount;
}

JNI_FUNC(void, PdfDocument, nativeTextGetRect)(JNI_ARGS, jlong textPtr, jint index, jobject outRectF) {
    double rectLeft, rectTop, rectRight, rectBottom;
    auto pTextPage = reinterpret_cast<FPDF_TEXTPAGE>(textPtr);
    FPDFText_GetRect(pTextPage, index, &rectLeft, &rectTop, &rectRight, &rectBottom);
    HANDLE_PDFIUM_ERROR_STATE(env)
    env->SetFloatField(outRectF, gRectFClassInfo.left, rectLeft);
    env->SetFloatField(outRectF, gRectFClassInfo.right, rectRight);
    env->SetFloatField(outRectF, gRectFClassInfo.top, rectTop);
    env->SetFloatField(outRectF, gRectFClassInfo.bottom, rectBottom);
}

JNI_FUNC(jint, PdfDocument, nativeTextGetSchResultIndex)(JNI_ARGS, jlong searchPtr) {
    auto pSearch = reinterpret_cast<FPDF_SCHHANDLE>(searchPtr);
    jint index = FPDFText_GetSchResultIndex(pSearch);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    return index;
}

JNI_FUNC(jint, PdfDocument, nativeTextGetSchCount)(JNI_ARGS, jlong searchPtr) {
    auto pSearchHandle = reinterpret_cast<FPDF_SCHHANDLE>(searchPtr);
    jint count = FPDFText_GetSchCount(pSearchHandle);
    HANDLE_PDFIUM_ERROR_STATE_WITH_RET_CODE(env, -1)
    return count;
}

JNI_FUNC(void, PdfDocument, nativeTextFindClose)(JNI_ARGS, jlong searchPtr) {
    auto pSearch = reinterpret_cast<FPDF_SCHHANDLE>(searchPtr);
    FPDFText_FindClose(pSearch);
    HANDLE_PDFIUM_ERROR_STATE(env)
}

}//extern C
