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

#include <fpdfview.h>
#include <fpdf_doc.h>
#include <fpdf_text.h>
#include <fpdf_save.h>
#include <fpdf_transformpage.h>
#include <string>
#include <vector>

static Mutex sLibraryLock;

static int sLibraryReferenceCount = 0;

static void initLibraryIfNeed(){
    Mutex::Autolock lock(sLibraryLock);
    if(sLibraryReferenceCount == 0){
        LOGD("Init FPDF library");
        FPDF_InitLibrary();
    }
    sLibraryReferenceCount++;
}

static void destroyLibraryIfNeed(){
    Mutex::Autolock lock(sLibraryLock);
    sLibraryReferenceCount--;
    if(sLibraryReferenceCount == 0){
        LOGD("Destroy FPDF library");
        FPDF_DestroyLibrary();
    }
}

struct rgb {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

class DocumentFile {
    private:
    int fileFd;

    public:
    FPDF_DOCUMENT pdfDocument = NULL;
    size_t fileSize;

    DocumentFile() { initLibraryIfNeed(); }
    ~DocumentFile();
};
DocumentFile::~DocumentFile(){
    if(pdfDocument != NULL){
        FPDF_CloseDocument(pdfDocument);
    }

    destroyLibraryIfNeed();
}

template <class string_type>
inline typename string_type::value_type* WriteInto(string_type* str, size_t length_with_null) {
  str->reserve(length_with_null);
  str->resize(length_with_null - 1);
  return &((*str)[0]);
}

inline long getFileSize(int fd){
    struct stat file_state;

    if(fstat(fd, &file_state) >= 0){
        return (long)(file_state.st_size);
    }else{
        LOGE("Error getting file size");
        return 0;
    }
}

static char* getErrorDescription(const long error) {
    char* description = NULL;
    switch(error) {
        case FPDF_ERR_SUCCESS:
            asprintf(&description, "No error.");
            break;
        case FPDF_ERR_FILE:
            asprintf(&description, "File not found or could not be opened.");
            break;
        case FPDF_ERR_FORMAT:
            asprintf(&description, "File not in PDF format or corrupted.");
            break;
        case FPDF_ERR_PASSWORD:
            asprintf(&description, "Incorrect password.");
            break;
        case FPDF_ERR_SECURITY:
            asprintf(&description, "Unsupported security scheme.");
            break;
        case FPDF_ERR_PAGE:
            asprintf(&description, "Page not found or content error.");
            break;
        default:
            asprintf(&description, "Unknown error.");
    }

    return description;
}

int jniThrowException(JNIEnv* env, const char* className, const char* message) {
    jclass exClass = env->FindClass(className);
    if (exClass == NULL) {
        LOGE("Unable to find exception class %s", className);
        return -1;
    }

    if(env->ThrowNew(exClass, message ) != JNI_OK) {
        LOGE("Failed throwing '%s' '%s'", className, message);
        return -1;
    }

    return 0;
}

int jniThrowExceptionFmt(JNIEnv* env, const char* className, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char msgBuf[512];
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    return jniThrowException(env, className, msgBuf);
    va_end(args);
}

jobject NewLong(JNIEnv* env, jlong value) {
    jclass cls = env->FindClass("java/lang/Long");
    jmethodID methodID = env->GetMethodID(cls, "<init>", "(J)V");
    return env->NewObject(cls, methodID, value);
}

jobject NewInteger(JNIEnv* env, jint value) {
    jclass cls = env->FindClass("java/lang/Integer");
    jmethodID methodID = env->GetMethodID(cls, "<init>", "(I)V");
    return env->NewObject(cls, methodID, value);
}

uint16_t rgbTo565(rgb *color) {
    return ((color->red >> 3) << 11) | ((color->green >> 2) << 5) | (color->blue >> 3);
}

void rgbBitmapTo565(void *source, int sourceStride, void *dest, AndroidBitmapInfo *info) {
    rgb *srcLine;
    uint16_t *dstLine;
    int y, x;
    for (y = 0; y < info->height; y++) {
        srcLine = (rgb*) source;
        dstLine = (uint16_t*) dest;
        for (x = 0; x < info->width; x++) {
            dstLine[x] = rgbTo565(&srcLine[x]);
        }
        source = (char*) source + sourceStride;
        dest = (char*) dest + info->stride;
    }
}

extern "C" { //For JNI support

static int getBlock(void* param, unsigned long position, unsigned char* outBuffer,
        unsigned long size) {
    const int fd = reinterpret_cast<intptr_t>(param);
    const int readCount = pread(fd, outBuffer, size, position);
    if (readCount < 0) {
        LOGE("Cannot read from file descriptor. Error:%d", errno);
        return 0;
    }
    return 1;
}

JNI_FUNC(jlong, PdfiumCore, nativeOpenDocument)(JNI_ARGS, jint fd, jstring password){

    size_t fileLength = (size_t)getFileSize(fd);
    if(fileLength <= 0) {
        jniThrowException(env, "java/io/IOException",
                                    "File is empty");
        return -1;
    }

    DocumentFile *docFile = new DocumentFile();

    FPDF_FILEACCESS loader;
    loader.m_FileLen = fileLength;
    loader.m_Param = reinterpret_cast<void*>(intptr_t(fd));
    loader.m_GetBlock = &getBlock;

    const char *cpassword = NULL;
    if(password != NULL) {
        cpassword = env->GetStringUTFChars(password, NULL);
    }

    FPDF_DOCUMENT document = FPDF_LoadCustomDocument(&loader, cpassword);

    if(cpassword != NULL) {
        env->ReleaseStringUTFChars(password, cpassword);
    }

    if (!document) {
        delete docFile;

        const long errorNum = FPDF_GetLastError();
        if(errorNum == FPDF_ERR_PASSWORD) {
            jniThrowException(env, "com/shockwave/pdfium/PdfPasswordException",
                                    "Password required or incorrect password.");
        } else {
            char* error = getErrorDescription(errorNum);
            jniThrowExceptionFmt(env, "java/io/IOException",
                                    "cannot create document: %s", error);

            free(error);
        }

        return -1;
    }

    docFile->pdfDocument = document;

    return reinterpret_cast<jlong>(docFile);
}

JNI_FUNC(jlong, PdfiumCore, nativeOpenMemDocument)(JNI_ARGS, jbyteArray data, jstring password){
    DocumentFile *docFile = new DocumentFile();

    const char *cpassword = NULL;
    if(password != NULL) {
        cpassword = env->GetStringUTFChars(password, NULL);
    }

    jbyte *cData = env->GetByteArrayElements(data, NULL);
    int size = (int) env->GetArrayLength(data);
    jbyte *cDataCopy = new jbyte[size];
    memcpy(cDataCopy, cData, size);
    FPDF_DOCUMENT document = FPDF_LoadMemDocument( reinterpret_cast<const void*>(cDataCopy),
                                                          size, cpassword);
    env->ReleaseByteArrayElements(data, cData, JNI_ABORT);

    if(cpassword != NULL) {
        env->ReleaseStringUTFChars(password, cpassword);
    }

    if (!document) {
        delete docFile;

        const long errorNum = FPDF_GetLastError();
        if(errorNum == FPDF_ERR_PASSWORD) {
            jniThrowException(env, "com/shockwave/pdfium/PdfPasswordException",
                                    "Password required or incorrect password.");
        } else {
            char* error = getErrorDescription(errorNum);
            jniThrowExceptionFmt(env, "java/io/IOException",
                                    "cannot create document: %s", error);

            free(error);
        }

        return -1;
    }

    docFile->pdfDocument = document;

    return reinterpret_cast<jlong>(docFile);
}

JNI_FUNC(jint, PdfiumCore, nativeGetPageCount)(JNI_ARGS, jlong documentPtr){
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(documentPtr);
    return (jint)FPDF_GetPageCount(doc->pdfDocument);
}

JNI_FUNC(void, PdfiumCore, nativeCloseDocument)(JNI_ARGS, jlong documentPtr){
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(documentPtr);
    delete doc;
}

static jlong loadPageInternal(JNIEnv *env, DocumentFile *doc, int pageIndex){
    try{
        if(doc == NULL) throw "Get page document null";

        FPDF_DOCUMENT pdfDoc = doc->pdfDocument;
        if(pdfDoc != NULL){
            FPDF_PAGE page = FPDF_LoadPage(pdfDoc, pageIndex);
            if (page == NULL) {
                throw "Loaded page is null";
            }
            return reinterpret_cast<jlong>(page);
        }else{
            throw "Get page pdf document null";
        }

    }catch(const char *msg){
        LOGE("%s", msg);

        jniThrowException(env, "java/lang/IllegalStateException",
                                "cannot load page");

        return -1;
    }
}

static void closePageInternal(jlong pagePtr) { FPDF_ClosePage(reinterpret_cast<FPDF_PAGE>(pagePtr)); }

JNI_FUNC(jlong, PdfiumCore, nativeLoadPage)(JNI_ARGS, jlong docPtr, jint pageIndex){
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    return loadPageInternal(env, doc, (int)pageIndex);
}
JNI_FUNC(jlongArray, PdfiumCore, nativeLoadPages)(JNI_ARGS, jlong docPtr, jint fromIndex, jint toIndex){
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);

    if(toIndex < fromIndex) return NULL;
    jlong pages[ toIndex - fromIndex + 1 ];

    int i;
    for(i = 0; i <= (toIndex - fromIndex); i++){
        pages[i] = loadPageInternal(env, doc, (int)(i + fromIndex));
    }

    jlongArray javaPages = env -> NewLongArray( (jsize)(toIndex - fromIndex + 1) );
    env -> SetLongArrayRegion(javaPages, 0, (jsize)(toIndex - fromIndex + 1), (const jlong*)pages);

    return javaPages;
}

JNI_FUNC(void, PdfiumCore, nativeClosePage)(JNI_ARGS, jlong pagePtr){ closePageInternal(pagePtr); }
JNI_FUNC(void, PdfiumCore, nativeClosePages)(JNI_ARGS, jlongArray pagesPtr){
    int length = (int)(env -> GetArrayLength(pagesPtr));
    jlong *pages = env -> GetLongArrayElements(pagesPtr, NULL);

    int i;
    for(i = 0; i < length; i++){ closePageInternal(pages[i]); }
}

JNI_FUNC(jfloatArray, PdfiumCore, nativeGetPageCropBox)(JNI_ARGS, jlong pagePtr) {
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    jfloatArray result = env->NewFloatArray(4);
    if (result == NULL) {
        return NULL;
    }

    float rect[4];
    if (!FPDFPage_GetCropBox(page, &rect[0], &rect[1], &rect[2], &rect[3])) {
        rect[0] = -1.0f;
        rect[1] = -1.0f;
        rect[2] = -1.0f;
        rect[3] = -1.0f;
    }

    env->SetFloatArrayRegion(result, 0, 4, (jfloat*)rect);
    return result;
}

JNI_FUNC(jfloatArray, PdfiumCore, nativeGetPageMediaBox)(JNI_ARGS, jlong pagePtr) {
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    jfloatArray result = env->NewFloatArray(4);
    if (result == NULL) {
        return NULL;
    }

    float rect[4];
    if (!FPDFPage_GetMediaBox(page, &rect[0], &rect[1], &rect[2], &rect[3])) {
        rect[0] = -1.0f;
        rect[1] = -1.0f;
        rect[2] = -1.0f;
        rect[3] = -1.0f;
    }

    env->SetFloatArrayRegion(result, 0, 4, (jfloat*)rect);
    return result;
}

JNI_FUNC(jfloatArray, PdfiumCore, nativeGetPageBleedBox)(JNI_ARGS, jlong pagePtr) {
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    jfloatArray result = env->NewFloatArray(4);
    if (result == NULL) {
        return NULL;
    }

    float rect[4];
    if (!FPDFPage_GetBleedBox(page, &rect[0], &rect[1], &rect[2], &rect[3])) {
        rect[0] = -1.0f;
        rect[1] = -1.0f;
        rect[2] = -1.0f;
        rect[3] = -1.0f;
    }

    env->SetFloatArrayRegion(result, 0, 4, (jfloat*)rect);
    return result;
}

JNI_FUNC(jfloatArray, PdfiumCore, nativeGetPageTrimBox)(JNI_ARGS, jlong pagePtr) {
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    jfloatArray result = env->NewFloatArray(4);
    if (result == NULL) {
        return NULL;
    }

    float rect[4];
    if (!FPDFPage_GetTrimBox(page, &rect[0], &rect[1], &rect[2], &rect[3])) {
        rect[0] = -1.0f;
        rect[1] = -1.0f;
        rect[2] = -1.0f;
        rect[3] = -1.0f;
    }

    env->SetFloatArrayRegion(result, 0, 4, (jfloat*)rect);
    return result;
}

JNI_FUNC(jfloatArray, PdfiumCore, nativeGetPageArtBox)(JNI_ARGS, jlong pagePtr) {
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    jfloatArray result = env->NewFloatArray(4);
    if (result == NULL) {
        return NULL;
    }

    float rect[4];
    if (!FPDFPage_GetArtBox(page, &rect[0], &rect[1], &rect[2], &rect[3])) {
        rect[0] = -1.0f;
        rect[1] = -1.0f;
        rect[2] = -1.0f;
        rect[3] = -1.0f;
    }

    env->SetFloatArrayRegion(result, 0, 4, (jfloat*)rect);
    return result;
}

JNI_FUNC(jfloatArray, PdfiumCore, nativeGetPageBoundingBox)(JNI_ARGS, jlong pagePtr) {
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    jfloatArray result = env->NewFloatArray(4);
    if (result == NULL) {
        return NULL;
    }

    float rect[4];
    FS_RECTF fsRect;
    if (!FPDF_GetPageBoundingBox(page, &fsRect)) {
        rect[0] = -1.0f;
        rect[1] = -1.0f;
        rect[2] = -1.0f;
        rect[3] = -1.0f;
    } else {
        rect[0] = fsRect.left;
        rect[1] = fsRect.top;
        rect[2] = fsRect.right;
        rect[3] = fsRect.bottom;
    }

    env->SetFloatArrayRegion(result, 0, 4, (jfloat*)rect);
    return result;
}

JNI_FUNC(jint, PdfiumCore, nativeGetPageWidthPixel)(JNI_ARGS, jlong pagePtr, jint dpi){
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    return (jint)(FPDF_GetPageWidth(page) * dpi / 72);
}

JNI_FUNC(jint, PdfiumCore, nativeGetPageHeightPixel)(JNI_ARGS, jlong pagePtr, jint dpi){
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    return (jint)(FPDF_GetPageHeight(page) * dpi / 72);
}

JNI_FUNC(jint, PdfiumCore, nativeGetPageWidthPoint)(JNI_ARGS, jlong pagePtr){
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    return (jint)FPDF_GetPageWidth(page);
}
JNI_FUNC(jint, PdfiumCore, nativeGetPageHeightPoint)(JNI_ARGS, jlong pagePtr){
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    return (jint)FPDF_GetPageHeight(page);
}
JNI_FUNC(jdouble, PdfiumCore, nativeGetFontSize)(JNI_ARGS, jlong textPagePtr, jint index){
    FPDF_TEXTPAGE textPage = reinterpret_cast<FPDF_TEXTPAGE>(textPagePtr);
    return (jdouble)FPDFText_GetFontSize(textPage, index);
}
JNI_FUNC(jobject, PdfiumCore, nativeGetPageSizeByIndex)(JNI_ARGS, jlong docPtr, jint pageIndex, jint dpi){
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    if(doc == NULL) {
        LOGE("Document is null");

        jniThrowException(env, "java/lang/IllegalStateException",
                               "Document is null");
        return NULL;
    }

    double width, height;
    int result = FPDF_GetPageSizeByIndex(doc->pdfDocument, pageIndex, &width, &height);

    if (result == 0) {
        width = 0;
        height = 0;
    }

    jint widthInt = (jint) (width * dpi / 72);
    jint heightInt = (jint) (height * dpi / 72);

    jclass clazz = env->FindClass("com/shockwave/pdfium/util/Size");
    jmethodID constructorID = env->GetMethodID(clazz, "<init>", "(II)V");
    return env->NewObject(clazz, constructorID, widthInt, heightInt);
}

static void renderPageInternal( FPDF_PAGE page,
                                ANativeWindow_Buffer *windowBuffer,
                                int startX, int startY,
                                int canvasHorSize, int canvasVerSize,
                                int drawSizeHor, int drawSizeVer,
                                bool renderAnnot){

    FPDF_BITMAP pdfBitmap = FPDFBitmap_CreateEx( canvasHorSize, canvasVerSize,
                                                 FPDFBitmap_BGRA,
                                                 windowBuffer->bits, (int)(windowBuffer->stride) * 4);

    /*LOGD("Start X: %d", startX);
    LOGD("Start Y: %d", startY);
    LOGD("Canvas Hor: %d", canvasHorSize);
    LOGD("Canvas Ver: %d", canvasVerSize);
    LOGD("Draw Hor: %d", drawSizeHor);
    LOGD("Draw Ver: %d", drawSizeVer);*/

    if(drawSizeHor < canvasHorSize || drawSizeVer < canvasVerSize){
        FPDFBitmap_FillRect( pdfBitmap, 0, 0, canvasHorSize, canvasVerSize,
                             0x848484FF); //Gray
    }

    int baseHorSize = (canvasHorSize < drawSizeHor)? canvasHorSize : drawSizeHor;
    int baseVerSize = (canvasVerSize < drawSizeVer)? canvasVerSize : drawSizeVer;
    int baseX = (startX < 0)? 0 : startX;
    int baseY = (startY < 0)? 0 : startY;
    int flags = FPDF_REVERSE_BYTE_ORDER;

    if(renderAnnot) {
    	flags |= FPDF_ANNOT;
    }

    FPDFBitmap_FillRect( pdfBitmap, baseX, baseY, baseHorSize, baseVerSize,
                         0xFFFFFFFF); //White

    FPDF_RenderPageBitmap( pdfBitmap, page,
                           startX, startY,
                           drawSizeHor, drawSizeVer,
                           0, flags );
}

JNI_FUNC(void, PdfiumCore, nativeRenderPage)(JNI_ARGS, jlong pagePtr, jobject objSurface,
                                             jint dpi, jint startX, jint startY,
                                             jint drawSizeHor, jint drawSizeVer,
                                             jboolean renderAnnot){
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, objSurface);
    if(nativeWindow == NULL){
        LOGE("native window pointer null");
        return;
    }
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);

    if(page == NULL || nativeWindow == NULL){
        LOGE("Render page pointers invalid");
        return;
    }

    if(ANativeWindow_getFormat(nativeWindow) != WINDOW_FORMAT_RGBA_8888){
        LOGD("Set format to RGBA_8888");
        ANativeWindow_setBuffersGeometry( nativeWindow,
                                          ANativeWindow_getWidth(nativeWindow),
                                          ANativeWindow_getHeight(nativeWindow),
                                          WINDOW_FORMAT_RGBA_8888 );
    }

    ANativeWindow_Buffer buffer;
    int ret;
    if( (ret = ANativeWindow_lock(nativeWindow, &buffer, NULL)) != 0 ){
        LOGE("Locking native window failed: %s", strerror(ret * -1));
        return;
    }

    renderPageInternal(page, &buffer,
                       (int)startX, (int)startY,
                       buffer.width, buffer.height,
                       (int)drawSizeHor, (int)drawSizeVer,
                       (bool)renderAnnot);

    ANativeWindow_unlockAndPost(nativeWindow);
    ANativeWindow_release(nativeWindow);
}

JNI_FUNC(void, PdfiumCore, nativeRenderPageBitmap)(JNI_ARGS, jlong pagePtr, jobject bitmap,
                                             jint dpi, jint startX, jint startY,
                                             jint drawSizeHor, jint drawSizeVer,
                                             jboolean renderAnnot, jboolean textMask){

    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);

    if(page == NULL || bitmap == NULL){
        LOGE("Render page pointers invalid");
        return;
    }

    AndroidBitmapInfo info;
    int ret;
    if((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE("Fetching bitmap info failed: %s", strerror(ret * -1));
        return;
    }

    int canvasHorSize = info.width;
    int canvasVerSize = info.height;

    if(info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 && info.format != ANDROID_BITMAP_FORMAT_RGB_565){
        LOGE("Bitmap format must be RGBA_8888 or RGB_565");
        return;
    }

    void *addr;
    if( (ret = AndroidBitmap_lockPixels(env, bitmap, &addr)) != 0 ){
        LOGE("Locking bitmap failed: %s", strerror(ret * -1));
        return;
    }

    void *tmp;
    int format;
    int sourceStride;
    if (info.format == ANDROID_BITMAP_FORMAT_RGB_565) {
        tmp = malloc(canvasVerSize * canvasHorSize * sizeof(rgb));
        sourceStride = canvasHorSize * sizeof(rgb);
        format = FPDFBitmap_BGR;
    } else {
        tmp = addr;
        sourceStride = info.stride;
        format = FPDFBitmap_BGRA;
    }

    FPDF_BITMAP pdfBitmap = FPDFBitmap_CreateEx( canvasHorSize, canvasVerSize,
                                                     format, tmp, sourceStride);

    /*LOGD("Start X: %d", startX);
    LOGD("Start Y: %d", startY);
    LOGD("Canvas Hor: %d", canvasHorSize);
    LOGD("Canvas Ver: %d", canvasVerSize);
    LOGD("Draw Hor: %d", drawSizeHor);
    LOGD("Draw Ver: %d", drawSizeVer);*/

    if(drawSizeHor < canvasHorSize || drawSizeVer < canvasVerSize){
        FPDFBitmap_FillRect( pdfBitmap, 0, 0, canvasHorSize, canvasVerSize,
                             0x848484FF); //Gray
    }

    int baseHorSize = (canvasHorSize < drawSizeHor)? canvasHorSize : (int)drawSizeHor;
    int baseVerSize = (canvasVerSize < drawSizeVer)? canvasVerSize : (int)drawSizeVer;
    int baseX = (startX < 0)? 0 : (int)startX;
    int baseY = (startY < 0)? 0 : (int)startY;
    int flags = FPDF_REVERSE_BYTE_ORDER;

    if(renderAnnot) {
    	flags |= FPDF_ANNOT;
    }

    if(textMask) {
        flags |= FPDF_RENDER_TEXT_MASK;
    }

    FPDFBitmap_FillRect( pdfBitmap, baseX, baseY, baseHorSize, baseVerSize,
                         0xFFFFFFFF); //White

    FPDF_RenderPageBitmap( pdfBitmap, page,
                           startX, startY,
                           (int)drawSizeHor, (int)drawSizeVer,
                           0, flags );

    if (info.format == ANDROID_BITMAP_FORMAT_RGB_565) {
        rgbBitmapTo565(tmp, sourceStride, addr, &info);
        free(tmp);
    }

    AndroidBitmap_unlockPixels(env, bitmap);
}

JNI_FUNC(jstring, PdfiumCore, nativeGetDocumentMetaText)(JNI_ARGS, jlong docPtr, jstring tag) {
    const char *ctag = env->GetStringUTFChars(tag, NULL);
    if (ctag == NULL) {
        return env->NewStringUTF("");
    }
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);

    size_t bufferLen = FPDF_GetMetaText(doc->pdfDocument, ctag, NULL, 0);
    if (bufferLen <= 2) {
        return env->NewStringUTF("");
    }
    std::wstring text;
    FPDF_GetMetaText(doc->pdfDocument, ctag, WriteInto(&text, bufferLen + 1), bufferLen);
    env->ReleaseStringUTFChars(tag, ctag);
    return env->NewString((jchar*) text.c_str(), bufferLen / 2 - 1);
}

JNI_FUNC(jobject, PdfiumCore, nativeGetFirstChildBookmark)(JNI_ARGS, jlong docPtr, jobject bookmarkPtr) {
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    FPDF_BOOKMARK parent;
    if(bookmarkPtr == NULL) {
        parent = NULL;
    } else {
        jclass longClass = env->GetObjectClass(bookmarkPtr);
        jmethodID longValueMethod = env->GetMethodID(longClass, "longValue", "()J");

        jlong ptr = env->CallLongMethod(bookmarkPtr, longValueMethod);
        parent = reinterpret_cast<FPDF_BOOKMARK>(ptr);
    }
    FPDF_BOOKMARK bookmark = FPDFBookmark_GetFirstChild(doc->pdfDocument, parent);
    if (bookmark == NULL) {
        return NULL;
    }
    return NewLong(env, reinterpret_cast<jlong>(bookmark));
}

JNI_FUNC(jobject, PdfiumCore, nativeGetSiblingBookmark)(JNI_ARGS, jlong docPtr, jlong bookmarkPtr) {
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    FPDF_BOOKMARK parent = reinterpret_cast<FPDF_BOOKMARK>(bookmarkPtr);
    FPDF_BOOKMARK bookmark = FPDFBookmark_GetNextSibling(doc->pdfDocument, parent);
    if (bookmark == NULL) {
        return NULL;
    }
    return NewLong(env, reinterpret_cast<jlong>(bookmark));
}

JNI_FUNC(jstring, PdfiumCore, nativeGetBookmarkTitle)(JNI_ARGS, jlong bookmarkPtr) {
    FPDF_BOOKMARK bookmark = reinterpret_cast<FPDF_BOOKMARK>(bookmarkPtr);
    size_t bufferLen = FPDFBookmark_GetTitle(bookmark, NULL, 0);
    if (bufferLen <= 2) {
        return env->NewStringUTF("");
    }
    std::wstring title;
    FPDFBookmark_GetTitle(bookmark, WriteInto(&title, bufferLen + 1), bufferLen);
    return env->NewString((jchar*) title.c_str(), bufferLen / 2 - 1);
}

JNI_FUNC(jlong, PdfiumCore, nativeGetBookmarkDestIndex)(JNI_ARGS, jlong docPtr, jlong bookmarkPtr) {
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    FPDF_BOOKMARK bookmark = reinterpret_cast<FPDF_BOOKMARK>(bookmarkPtr);

    FPDF_DEST dest = FPDFBookmark_GetDest(doc->pdfDocument, bookmark);
    if (dest == NULL) {
        return -1;
    }
    return (jlong) FPDFDest_GetDestPageIndex(doc->pdfDocument, dest);
}

JNI_FUNC(jlongArray, PdfiumCore, nativeGetPageLinks)(JNI_ARGS, jlong pagePtr) {
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    int pos = 0;
    std::vector<jlong> links;
    FPDF_LINK link;
    while (FPDFLink_Enumerate(page, &pos, &link)) {
        links.push_back(reinterpret_cast<jlong>(link));
    }

    jlongArray result = env->NewLongArray(links.size());
    env->SetLongArrayRegion(result, 0, links.size(), &links[0]);
    return result;
}

JNI_FUNC(jobject, PdfiumCore, nativeGetDestPageIndex)(JNI_ARGS, jlong docPtr, jlong linkPtr) {
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    FPDF_LINK link = reinterpret_cast<FPDF_LINK>(linkPtr);
    FPDF_DEST dest = FPDFLink_GetDest(doc->pdfDocument, link);
    if (dest == NULL) {
        return NULL;
    }
    unsigned long index = FPDFDest_GetDestPageIndex(doc->pdfDocument, dest);
    return NewInteger(env, (jint) index);
}

JNI_FUNC(jstring, PdfiumCore, nativeGetLinkURI)(JNI_ARGS, jlong docPtr, jlong linkPtr){
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    FPDF_LINK link = reinterpret_cast<FPDF_LINK>(linkPtr);
    FPDF_ACTION action = FPDFLink_GetAction(link);
    if (action == NULL) {
        return NULL;
    }
    size_t bufferLen = FPDFAction_GetURIPath(doc->pdfDocument, action, NULL, 0);
    if (bufferLen <= 0) {
        return env->NewStringUTF("");
    }
    std::string uri;
    FPDFAction_GetURIPath(doc->pdfDocument, action, WriteInto(&uri, bufferLen), bufferLen);
    return env->NewStringUTF(uri.c_str());
}

JNI_FUNC(jobject, PdfiumCore, nativeGetLinkRect)(JNI_ARGS, jlong linkPtr) {
    FPDF_LINK link = reinterpret_cast<FPDF_LINK>(linkPtr);
    FS_RECTF fsRectF;
    FPDF_BOOL result = FPDFLink_GetAnnotRect(link, &fsRectF);

    if (!result) {
        return NULL;
    }

    jclass clazz = env->FindClass("android/graphics/RectF");
    jmethodID constructorID = env->GetMethodID(clazz, "<init>", "(FFFF)V");
    return env->NewObject(clazz, constructorID, fsRectF.left, fsRectF.top, fsRectF.right, fsRectF.bottom);
}

JNI_FUNC(jobject, PdfiumCore, nativePageCoordsToDevice)(JNI_ARGS, jlong pagePtr, jint startX, jint startY, jint sizeX,
                                            jint sizeY, jint rotate, jdouble pageX, jdouble pageY) {
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    int deviceX, deviceY;

    FPDF_PageToDevice(page, startX, startY, sizeX, sizeY, rotate, pageX, pageY, &deviceX, &deviceY);

    jclass clazz = env->FindClass("android/graphics/Point");
    jmethodID constructorID = env->GetMethodID(clazz, "<init>", "(II)V");
    return env->NewObject(clazz, constructorID, deviceX, deviceY);
}

JNI_FUNC(jobject, PdfiumCore, nativeDeviceCoordsToPage)(JNI_ARGS, jlong pagePtr, jint startX, jint startY, jint sizeX,
                                            jint sizeY, jint rotate, jint deviceX, jint deviceY) {
    FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
    double pageX, pageY;

    FPDF_DeviceToPage(page, startX, startY, sizeX, sizeY, rotate, deviceX, deviceY, &pageX, &pageY);

    jclass clazz = env->FindClass("android/graphics/PointF");
    jmethodID constructorID = env->GetMethodID(clazz, "<init>", "(FF)V");
    return env->NewObject(clazz, constructorID, pageX, pageY);
}


//////////////////////////////////////////
//Begin FPDF_TEXTPAGE section

static jlong loadTextPageInternal(JNIEnv *env, DocumentFile *doc, jlong pagePtr){
    try{
        if(doc == NULL) throw "Get page document null";

        FPDF_PAGE page = reinterpret_cast<FPDF_PAGE>(pagePtr);
        if(page != NULL){
            FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
            if (textPage == NULL) {
                throw "Loaded text page is null";
            }
            return reinterpret_cast<jlong>(textPage);
        }else{
            throw "Load page null";
        }
    }catch(const char *msg){
        LOGE("%s", msg);

        jniThrowException(env, "java/lang/IllegalStateException",
                                "cannot load text page");

        return -1;
    }
}

static void closeTextPageInternal(jlong textPagePtr) { FPDFText_ClosePage(reinterpret_cast<FPDF_TEXTPAGE>(textPagePtr)); }

JNI_FUNC(jlong, PdfiumCore, nativeLoadTextPage)(JNI_ARGS, jlong docPtr, jlong pagePtr){
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);
    return loadTextPageInternal(env, doc, pagePtr);
}
JNI_FUNC(jlongArray, PdfiumCore, nativeLoadTextPages)(JNI_ARGS, jlong docPtr, jint fromIndex, jint toIndex){
    DocumentFile *doc = reinterpret_cast<DocumentFile*>(docPtr);

    if(toIndex < fromIndex) return NULL;
    jlong pages[ toIndex - fromIndex + 1 ];

    int i;
    for(i = 0; i <= (toIndex - fromIndex); i++){
        pages[i] = loadTextPageInternal(env, doc, (int)(i + fromIndex));
    }

    jlongArray javaPages = env -> NewLongArray( (jsize)(toIndex - fromIndex + 1) );
    env -> SetLongArrayRegion(javaPages, 0, (jsize)(toIndex - fromIndex + 1), (const jlong*)pages);

    return javaPages;
}

JNI_FUNC(void, PdfiumCore, nativeCloseTextPage)(JNI_ARGS, jlong textPagePtr){ closeTextPageInternal(textPagePtr); }
JNI_FUNC(void, PdfiumCore, nativeCloseTextPages)(JNI_ARGS, jlongArray textPagesPtr){
    int length = (int)(env -> GetArrayLength(textPagesPtr));
    jlong *textPages = env -> GetLongArrayElements(textPagesPtr, NULL);

    int i;
    for(i = 0; i < length; i++){ closeTextPageInternal(textPages[i]); }
}

//DLLEXPORT int STDCALL FPDFText_CountChars(FPDF_TEXTPAGE text_page);
JNI_FUNC(jint, PdfiumCore, nativeTextCountChars)(JNI_ARGS, jlong textPagePtr){
    FPDF_TEXTPAGE textPage = reinterpret_cast<FPDF_TEXTPAGE>(textPagePtr);
    return (jint)FPDFText_CountChars(textPage);
}

//DLLEXPORT unsigned int STDCALL FPDFText_GetUnicode(FPDF_TEXTPAGE text_page, int index);
JNI_FUNC(jint, PdfiumCore, nativeTextGetUnicode)(JNI_ARGS, jlong textPagePtr, jint index){
    FPDF_TEXTPAGE textPage = reinterpret_cast<FPDF_TEXTPAGE>(textPagePtr);
    return (jint)FPDFText_GetUnicode(textPage, (int)index);
}

/*DLLEXPORT void STDCALL FPDFText_GetCharBox(FPDF_TEXTPAGE text_page,
                                           int index,
                                           double* left,
                                           double* right,
                                           double* bottom,
                                           double* top);*/
JNI_FUNC(jdoubleArray, PdfiumCore, nativeTextGetCharBox)(JNI_ARGS, jlong textPagePtr, jint index){
    FPDF_TEXTPAGE textPage = reinterpret_cast<FPDF_TEXTPAGE>(textPagePtr);
    jdoubleArray result = env->NewDoubleArray(4);
    if (result == NULL) {
        return NULL;
    }
    double fill[4];
    FPDFText_GetCharBox(textPage, (int)index, &fill[0], &fill[1], &fill[2], &fill[3]);
    env->SetDoubleArrayRegion(result, 0, 4, (jdouble*)fill);
    return result;
}

/*DLLEXPORT int STDCALL FPDFText_GetCharIndexAtPos(FPDF_TEXTPAGE text_page,
                                                 double x,
                                                 double y,
                                                 double xTolerance,
                                                 double yTolerance);*/
JNI_FUNC(jint, PdfiumCore, nativeTextGetCharIndexAtPos)(JNI_ARGS, jlong textPagePtr, jdouble x, jdouble y, jdouble xTolerance, jdouble yTolerance){
    FPDF_TEXTPAGE textPage = reinterpret_cast<FPDF_TEXTPAGE>(textPagePtr);
    return (jint)FPDFText_GetCharIndexAtPos(textPage, (double)x, (double)y, (double)xTolerance, (double)yTolerance);
}

/*DLLEXPORT int STDCALL FPDFText_GetText(FPDF_TEXTPAGE text_page,
                                       int start_index,
                                       int count,
                                       unsigned short* result);*/
JNI_FUNC(jint, PdfiumCore, nativeTextGetText)(JNI_ARGS, jlong textPagePtr, jint start_index, jint count, jshortArray result){
    FPDF_TEXTPAGE textPage = reinterpret_cast<FPDF_TEXTPAGE>(textPagePtr);
    jboolean isCopy = 0;
    unsigned short *arr = (unsigned short *)env->GetShortArrayElements(result, &isCopy);
    jint output = (jint)FPDFText_GetText(textPage, (int)start_index, (int)count, arr);
    if (isCopy) {
        env->SetShortArrayRegion(result, 0, output, (jshort*)arr);
        env->ReleaseShortArrayElements(result, (jshort*)arr, JNI_ABORT);
    }
    return output;
}

/*DLLEXPORT int STDCALL FPDFText_CountRects(FPDF_TEXTPAGE text_page,
                                          int start_index,
                                          int count);*/
JNI_FUNC(jint, PdfiumCore, nativeTextCountRects)(JNI_ARGS, jlong textPagePtr, jint start_index, jint count){
    FPDF_TEXTPAGE textPage = reinterpret_cast<FPDF_TEXTPAGE>(textPagePtr);
    return (jint)FPDFText_CountRects(textPage, (int)start_index, (int) count);
}

/*DLLEXPORT void STDCALL FPDFText_GetRect(FPDF_TEXTPAGE text_page,
                                        int rect_index,
                                        double* left,
                                        double* top,
                                        double* right,
                                        double* bottom);*/
JNI_FUNC(jdoubleArray, PdfiumCore, nativeTextGetRect)(JNI_ARGS, jlong textPagePtr, jint rect_index){
    FPDF_TEXTPAGE textPage = reinterpret_cast<FPDF_TEXTPAGE>(textPagePtr);
    jdoubleArray result = env->NewDoubleArray(4);
    if (result == NULL) {
        return NULL;
    }
    double fill[4];
    FPDFText_GetRect(textPage, (int)rect_index, &fill[0], &fill[1], &fill[2], &fill[3]);
    env->SetDoubleArrayRegion(result, 0, 4, (jdouble*)fill);
    return result;
}

/*DLLEXPORT int STDCALL FPDFText_GetBoundedText(FPDF_TEXTPAGE text_page,
                                              double left,
                                              double top,
                                              double right,
                                              double bottom,
                                              unsigned short* buffer,
                                              int buflen);
*/

JNI_FUNC(jint, PdfiumCore, nativeTextGetBoundedText)(JNI_ARGS, jlong textPagePtr, jdouble left, jdouble top, jdouble right, jdouble bottom, jshortArray arr){
    FPDF_TEXTPAGE textPage = reinterpret_cast<FPDF_TEXTPAGE>(textPagePtr);
    jboolean isCopy = 0;
    unsigned short *buffer = NULL;
    int bufLen = 0;
    if (arr != NULL) {
        buffer = (unsigned short *)env->GetShortArrayElements(arr, &isCopy);
        bufLen = env->GetArrayLength(arr);
    }
    jint output = (jint)FPDFText_GetBoundedText(textPage, (double)left, (double)top, (double)right, (double)bottom, buffer, bufLen);
    if (isCopy) {
        env->SetShortArrayRegion(arr, 0, output, (jshort*)buffer);
        env->ReleaseShortArrayElements(arr, (jshort*)buffer, JNI_ABORT);
    }
    return output;
}

class FileWrite : public FPDF_FILEWRITE {
	public:
	jobject callbackObject;
	jmethodID callbackMethodID;
	_JNIEnv *env;

	static int WriteBlockCallback(FPDF_FILEWRITE* pFileWrite, const void* data, unsigned long size) {
		FileWrite* pThis = static_cast<FileWrite*>(pFileWrite);
		_JNIEnv *env = pThis->env;
		//Convert the native array to Java array.
		jbyteArray a = env->NewByteArray(size);
		if (a != NULL) {
			env->SetByteArrayRegion(a, 0, size, (const jbyte *)data);
			return env->CallIntMethod(pThis->callbackObject, pThis->callbackMethodID, a);
		}
		return -1;
	}
};

/*DLLEXPORT FPDF_BOOL STDCALL FPDF_SaveAsCopy(FPDF_DOCUMENT document,
                                            FPDF_FILEWRITE* pFileWrite,
                                            FPDF_DWORD flags);
*/

JNI_FUNC(jboolean, PdfiumCore, nativeSaveAsCopy)(JNI_ARGS, jlong documentPtr, jobject callback) {
	jclass callbackClass = env->FindClass("com/shockwave/pdfium/PdfWriteCallback");
	if (callback != NULL && env->IsInstanceOf(callback, callbackClass)) {
		//Setup the callback to Java.
		FileWrite fw;
		fw.version = 1;
		fw.FPDF_FILEWRITE::WriteBlock = FileWrite::WriteBlockCallback;
		fw.callbackObject = callback;
		fw.callbackMethodID = env->GetMethodID(callbackClass, "WriteBlock", "([B)I");
		fw.env = env;

		DocumentFile *doc = reinterpret_cast<DocumentFile*>(documentPtr);
		jboolean output = (jboolean)FPDF_SaveAsCopy(doc->pdfDocument, &fw, 0);
		return output;
	}
	return false;
}

}//extern C
