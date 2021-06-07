package com.shockwave.pdfium;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Point;
import android.graphics.PointF;
import android.graphics.RectF;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.view.Surface;

import com.shockwave.pdfium.util.Size;

import java.io.FileDescriptor;
import java.io.IOException;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

public class PdfiumCore {
    private static final String TAG = PdfiumCore.class.getName();
    private static final Class FD_CLASS = FileDescriptor.class;
    private static final String FD_FIELD_NAME = "descriptor";

    static {
        try {
            System.loadLibrary("c++.cr");
            System.loadLibrary("chrome_zlib.cr");
            System.loadLibrary("icui18n.cr");
            System.loadLibrary("icuuc.cr");
            System.loadLibrary("pdfium.cr");
            System.loadLibrary("jniPdfium");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Native libraries failed to load - " + e);
        }
    }

    private native long nativeOpenDocument(int fd, String password);

    private native long nativeOpenMemDocument(byte[] data, String password);

    private native void nativeCloseDocument(long docPtr);

    private native int nativeGetPageCount(long docPtr);

    private native long nativeLoadPage(long docPtr, int pageIndex);

    private native long[] nativeLoadPages(long docPtr, int fromIndex, int toIndex);

    private native void nativeClosePage(long pagePtr);

    private native void nativeClosePages(long[] pagesPtr);

    private native int nativeGetPageWidthPixel(long pagePtr, int dpi);

    private native int nativeGetPageHeightPixel(long pagePtr, int dpi);

    private native int nativeGetPageWidthPoint(long pagePtr);

    private native int nativeGetPageHeightPoint(long pagePtr);

    private native double nativeGetFontSize(long pagePtr, int charIndex);

    private native float[] nativeGetPageMediaBox(long pagePtr);
    private native float[] nativeGetPageCropBox(long pagePtr);
    private native float[] nativeGetPageBleedBox(long pagePtr);
    private native float[] nativeGetPageTrimBox(long pagePtr);
    private native float[] nativeGetPageArtBox(long pagePtr);
    private native float[] nativeGetPageBoundingBox(long pagePtr);

    //private native long nativeGetNativeWindow(Surface surface);
    //private native void nativeRenderPage(long pagePtr, long nativeWindowPtr);
    private native void nativeRenderPage(long pagePtr, Surface surface, int dpi,
                                         int startX, int startY,
                                         int drawSizeHor, int drawSizeVer,
                                         boolean renderAnnot);

    private native void nativeRenderPageBitmap(long pagePtr, Bitmap bitmap, int dpi,
                                               int startX, int startY,
                                               int drawSizeHor, int drawSizeVer,
                                               boolean renderAnnot, boolean textMask);

    private native String nativeGetDocumentMetaText(long docPtr, String tag);

    private native Long nativeGetFirstChildBookmark(long docPtr, Long bookmarkPtr);

    private native Long nativeGetSiblingBookmark(long docPtr, long bookmarkPtr);

    private native String nativeGetBookmarkTitle(long bookmarkPtr);

    private native long nativeGetBookmarkDestIndex(long docPtr, long bookmarkPtr);

    private native long nativeLoadTextPage(long docPtr, long pagePtr);

    private native void nativeCloseTextPage(long pagePtr);

    private native int nativeTextCountChars(long textPagePtr);

    private native int nativeTextGetText(long textPagePtr, int start_index, int count, short[] result);

    private native int nativeTextGetUnicode(long textPagePtr, int index);

    private native double[] nativeTextGetCharBox(long textPagePtr, int index);

    private native int nativeTextGetCharIndexAtPos(long textPagePtr, double x, double y, double xTolerance, double yTolerance);

    private native int nativeTextCountRects(long textPagePtr, int start_index, int count);

    private native double[] nativeTextGetRect(long textPagePtr, int rect_index);

    private native int nativeTextGetBoundedText(long textPagePtr, double left, double top, double right, double bottom, short[] arr);

    private native Size nativeGetPageSizeByIndex(long docPtr, int pageIndex, int dpi);

    private native long[] nativeGetPageLinks(long pagePtr);

    private native Integer nativeGetDestPageIndex(long docPtr, long linkPtr);

    private native String nativeGetLinkURI(long docPtr, long linkPtr);

    private native RectF nativeGetLinkRect(long linkPtr);

    private native Point nativePageCoordsToDevice(long pagePtr, int startX, int startY, int sizeX,
                                                  int sizeY, int rotate, double pageX, double pageY);

    private native PointF nativeDeviceCoordsToPage(long pagePtr, int startX, int startY, int sizeX,
                                                   int sizeY, int rotate, int deviceX, int deviceY);

    private native boolean nativeSaveAsCopy(long docPtr, PdfWriteCallback callback);

    /* synchronize native methods */
    private static final Object lock = new Object();
    private static Field mFdField = null;
    private int mCurrentDpi;

    public static int getNumFd(ParcelFileDescriptor fdObj) {
        try {
            if (mFdField == null) {
                mFdField = FD_CLASS.getDeclaredField(FD_FIELD_NAME);
                mFdField.setAccessible(true);
            }

            return mFdField.getInt(fdObj.getFileDescriptor());
        } catch (NoSuchFieldException e) {
            e.printStackTrace();
            return -1;
        } catch (IllegalAccessException e) {
            e.printStackTrace();
            return -1;
        }
    }


    /** Context needed to get screen density */
    public PdfiumCore(Context ctx) {
        mCurrentDpi = ctx.getResources().getDisplayMetrics().densityDpi;
        Log.d(TAG, "Starting PdfiumAndroid " + BuildConfig.BUILD_TYPE);
    }

    /** Create new document from file */
    public PdfDocument newDocument(ParcelFileDescriptor fd) throws IOException {
        return newDocument(fd, null);
    }

    /** Create new document from file with password */
    public PdfDocument newDocument(ParcelFileDescriptor fd, String password) throws IOException {
        PdfDocument document = new PdfDocument();
        document.parcelFileDescriptor = fd;
        synchronized (lock) {
            document.mNativeDocPtr = nativeOpenDocument(getNumFd(fd), password);
        }

        return document;
    }

    /** Create new document from bytearray */
    public PdfDocument newDocument(byte[] data) throws IOException {
        return newDocument(data, null);
    }

    /** Create new document from bytearray with password */
    public PdfDocument newDocument(byte[] data, String password) throws IOException {
        PdfDocument document = new PdfDocument();
        synchronized (lock) {
            document.mNativeDocPtr = nativeOpenMemDocument(data, password);
        }
        return document;
    }

    /** Get total numer of pages in document */
    public int getPageCount(PdfDocument doc) {
        synchronized (lock) {
            return nativeGetPageCount(doc.mNativeDocPtr);
        }
    }

    /** Open page and store native pointer in {@link PdfDocument} */
    public long openPage(PdfDocument doc, int pageIndex) {
        synchronized (lock) {
            if (doc.mNativePagesPtr == null) {
                return -1;
            }
            Long pagePtr = doc.mNativePagesPtr.get(pageIndex);
            if (pagePtr == null) {
                pagePtr = nativeLoadPage(doc.mNativeDocPtr, pageIndex);
                doc.mNativePagesPtr.put(pageIndex, pagePtr);
            }
            return pagePtr;
        }

    }

    public void closePage(PdfDocument doc, int pageIndex) {
        synchronized (lock) {
            final Long pagePtr = doc.mNativePagesPtr.get(pageIndex);
            if (pagePtr != null) {
                nativeClosePage(pagePtr);
                doc.mNativePagesPtr.remove(pageIndex);
            }
        }
    }

    /** Open range of pages and store native pointers in {@link PdfDocument} */
    public long[] openPage(PdfDocument doc, int fromIndex, int toIndex) {
        long[] pagesPtr;
        synchronized (lock) {
            pagesPtr = nativeLoadPages(doc.mNativeDocPtr, fromIndex, toIndex);
            int pageIndex = fromIndex;
            for (long page : pagesPtr) {
                if (pageIndex > toIndex) break;
                doc.mNativePagesPtr.put(pageIndex, page);
                pageIndex++;
            }

            return pagesPtr;
        }
    }

    /**
     * Get page width in pixels. <br>
     * This method requires page to be opened.
     */
    public int getPageWidth(PdfDocument doc, int index) {
        synchronized (lock) {
            Long pagePtr;
            if ((pagePtr = doc.mNativePagesPtr.get(index)) != null) {
                return nativeGetPageWidthPixel(pagePtr, mCurrentDpi);
            }
            return 0;
        }
    }

    /**
     * Get page height in pixels. <br>
     * This method requires page to be opened.
     */
    public int getPageHeight(PdfDocument doc, int index) {
        synchronized (lock) {
            Long pagePtr;
            if ((pagePtr = doc.mNativePagesPtr.get(index)) != null) {
                return nativeGetPageHeightPixel(pagePtr, mCurrentDpi);
            }
            return 0;
        }
    }

    /**
     * Get page width in PostScript points (1/72th of an inch).<br>
     * This method requires page to be opened.
     */
    public int getPageWidthPoint(PdfDocument doc, int index) {
        synchronized (lock) {
            Long pagePtr;
            if ((pagePtr = doc.mNativePagesPtr.get(index)) != null) {
                return nativeGetPageWidthPoint(pagePtr);
            }
            return 0;
        }
    }

    /**
     * Get page height in PostScript points (1/72th of an inch).<br>
     * This method requires page to be opened.
     */
    public int getPageHeightPoint(PdfDocument doc, int index) {
        synchronized (lock) {
            Long pagePtr;
            if ((pagePtr = doc.mNativePagesPtr.get(index)) != null) {
                return nativeGetPageHeightPoint(pagePtr);
            }
            return 0;
        }
    }

    /**
     * Get character font size in PostScript points (1/72th of an inch).<br>
     * This method requires page to be opened.
     */
    public double getFontSize(PdfDocument doc, int pageIndex, int charIndex) {
        synchronized (lock) {
            Long pagePtr;
            if ((pagePtr = doc.mNativePagesPtr.get(pageIndex)) != null) {
                return nativeGetFontSize(pagePtr, charIndex);
            }
            return 0.0;
        }
    }

    /**
     * Get page height in PostScript points (1/72th of an inch).<br>
     * This method requires page to be opened.
     */
    public RectF getPageCropBox(PdfDocument doc, int index) {
        synchronized (lock) {
            Long pagePtr;
            if ((pagePtr = doc.mNativePagesPtr.get(index)) != null) {
                float[] o =  nativeGetPageCropBox(pagePtr);

                RectF r = new RectF();
                r.left = o[0];
                r.top = o[1];
                r.right = o[2];
                r.bottom = o[3];

                return r;
            }
            return new RectF();
        }
    }

    /**
     * Get page height in PostScript points (1/72th of an inch).<br>
     * This method requires page to be opened.
     */
    public RectF getPageMediaBox(PdfDocument doc, int index) {
        synchronized (lock) {
            Long pagePtr;
            if ((pagePtr = doc.mNativePagesPtr.get(index)) != null) {
                float[] o =  nativeGetPageMediaBox(pagePtr);

                RectF r = new RectF();
                r.left = o[0];
                r.top = o[1];
                r.right = o[2];
                r.bottom = o[3];

                return r;
            }
            return new RectF();
        }
    }

    /**
     * Get page height in PostScript points (1/72th of an inch).<br>
     * This method requires page to be opened.
     */
    public RectF getPageBleedBox(PdfDocument doc, int index) {
        synchronized (lock) {
            Long pagePtr;
            if ((pagePtr = doc.mNativePagesPtr.get(index)) != null) {
                float[] o =  nativeGetPageBleedBox(pagePtr);

                RectF r = new RectF();
                r.left = o[0];
                r.top = o[1];
                r.right = o[2];
                r.bottom = o[3];

                return r;
            }
            return new RectF();
        }
    }

    /**
     * Get page height in PostScript points (1/72th of an inch).<br>
     * This method requires page to be opened.
     */
    public RectF getPageTrimBox(PdfDocument doc, int index) {
        synchronized (lock) {
            Long pagePtr;
            if ((pagePtr = doc.mNativePagesPtr.get(index)) != null) {
                float[] o =  nativeGetPageTrimBox(pagePtr);

                RectF r = new RectF();
                r.left = o[0];
                r.top = o[1];
                r.right = o[2];
                r.bottom = o[3];

                return r;
            }
            return new RectF();
        }
    }

    /**
     * Get page height in PostScript points (1/72th of an inch).<br>
     * This method requires page to be opened.
     */
    public RectF getPageArtBox(PdfDocument doc, int index) {
        synchronized (lock) {
            Long pagePtr;
            if ((pagePtr = doc.mNativePagesPtr.get(index)) != null) {
                float[] o =  nativeGetPageArtBox(pagePtr);

                RectF r = new RectF();
                r.left = o[0];
                r.top = o[1];
                r.right = o[2];
                r.bottom = o[3];

                return r;
            }
            return new RectF();
        }
    }

    /**
     * Get page height in PostScript points (1/72th of an inch).<br>
     * This method requires page to be opened.
     */
    public RectF getPageBoundingBox(PdfDocument doc, int index) {
        synchronized (lock) {
            Long pagePtr;
            if ((pagePtr = doc.mNativePagesPtr.get(index)) != null) {
                float[] o =  nativeGetPageBoundingBox(pagePtr);

                RectF r = new RectF();
                r.left = o[0];
                r.top = o[1];
                r.right = o[2];
                r.bottom = o[3];

                return r;
            }
            return new RectF();
        }
    }

    /**
     * Get size of page in pixels.<br>
     * This method does not require given page to be opened.
     */
    public Size getPageSize(PdfDocument doc, int index) {
        synchronized (lock) {
            return nativeGetPageSizeByIndex(doc.mNativeDocPtr, index, mCurrentDpi);
        }
    }

    /**
     * Render page fragment on {@link Surface}.<br>
     * Page must be opened before rendering.
     */
    public void renderPage(PdfDocument doc, Surface surface, int pageIndex,
                           int startX, int startY, int drawSizeX, int drawSizeY) {
        renderPage(doc, surface, pageIndex, startX, startY, drawSizeX, drawSizeY, false);
    }

    /**
     * Render page fragment on {@link Surface}. This method allows to render annotations.<br>
     * Page must be opened before rendering.
     */
    public void renderPage(PdfDocument doc, Surface surface, int pageIndex,
                           int startX, int startY, int drawSizeX, int drawSizeY,
                           boolean renderAnnot) {
        synchronized (lock) {
            try {
                //nativeRenderPage(doc.mNativePagesPtr.get(pageIndex), surface, mCurrentDpi);
                nativeRenderPage(doc.mNativePagesPtr.get(pageIndex), surface, mCurrentDpi,
                        startX, startY, drawSizeX, drawSizeY, renderAnnot);
            } catch (NullPointerException e) {
                Log.e(TAG, "mContext may be null");
                e.printStackTrace();
            } catch (Exception e) {
                Log.e(TAG, "Exception throw from native");
                e.printStackTrace();
            }
        }
    }

    /**
     * Render page fragment on {@link Bitmap}.<br>
     * Page must be opened before rendering.
     * <p>
     * Supported bitmap configurations:
     * <ul>
     * <li>ARGB_8888 - best quality, high memory usage, higher possibility of OutOfMemoryError
     * <li>RGB_565 - little worse quality, twice less memory usage
     * </ul>
     */
    public void renderPageBitmap(PdfDocument doc, Bitmap bitmap, int pageIndex,
                                 int startX, int startY, int drawSizeX, int drawSizeY, boolean textMask) {
        renderPageBitmap(doc, bitmap, pageIndex, startX, startY, drawSizeX, drawSizeY, false, textMask);
    }

    /**
     * Render page fragment on {@link Bitmap}. This method allows to render annotations.<br>
     * Page must be opened before rendering.
     * <p>
     * For more info see {@link PdfiumCore#renderPageBitmap(PdfDocument, Bitmap, int, int, int, int, int)}
     */
    public void renderPageBitmap(PdfDocument doc, Bitmap bitmap, int pageIndex,
                                 int startX, int startY, int drawSizeX, int drawSizeY,
                                 boolean renderAnnot, boolean textMask) {
        synchronized (lock) {
            try {
                nativeRenderPageBitmap(doc.mNativePagesPtr.get(pageIndex), bitmap, mCurrentDpi,
                        startX, startY, drawSizeX, drawSizeY, renderAnnot, textMask);
            } catch (NullPointerException e) {
                Log.e(TAG, "mContext may be null");
                e.printStackTrace();
            } catch (Exception e) {
                Log.e(TAG, "Exception throw from native");
                e.printStackTrace();
            }
        }
    }

    /** Release native resources and opened file */
    public void closeDocument(PdfDocument doc) {
        synchronized (lock) {
            if (doc.mNativePagesPtr != null) {
                for (Integer index : doc.mNativePagesPtr.keySet()) {
                    nativeClosePage(doc.mNativePagesPtr.get(index));
                }
                doc.mNativePagesPtr.clear();
            }

            nativeCloseDocument(doc.mNativeDocPtr);

            if (doc.parcelFileDescriptor != null) { //if document was loaded from file
                try {
                    doc.parcelFileDescriptor.close();
                } catch (IOException e) {
                /* ignore */
                }
                doc.parcelFileDescriptor = null;
            }
        }
    }

    /** Get metadata for given document */
    public PdfDocument.Meta getDocumentMeta(PdfDocument doc) {
        synchronized (lock) {
            PdfDocument.Meta meta = new PdfDocument.Meta();
            meta.title = nativeGetDocumentMetaText(doc.mNativeDocPtr, "Title");
            meta.author = nativeGetDocumentMetaText(doc.mNativeDocPtr, "Author");
            meta.subject = nativeGetDocumentMetaText(doc.mNativeDocPtr, "Subject");
            meta.keywords = nativeGetDocumentMetaText(doc.mNativeDocPtr, "Keywords");
            meta.creator = nativeGetDocumentMetaText(doc.mNativeDocPtr, "Creator");
            meta.producer = nativeGetDocumentMetaText(doc.mNativeDocPtr, "Producer");
            meta.creationDate = nativeGetDocumentMetaText(doc.mNativeDocPtr, "CreationDate");
            meta.modDate = nativeGetDocumentMetaText(doc.mNativeDocPtr, "ModDate");

            return meta;
        }
    }

    /** Get table of contents (bookmarks) for given document */
    public List<PdfDocument.Bookmark> getTableOfContents(PdfDocument doc) {
        synchronized (lock) {
            List<PdfDocument.Bookmark> topLevel = new ArrayList<>();
            Long first = nativeGetFirstChildBookmark(doc.mNativeDocPtr, null);
            if (first != null) {
                recursiveGetBookmark(topLevel, doc, first, 1);
            }
            return topLevel;
        }
    }

    private void recursiveGetBookmark(List<PdfDocument.Bookmark> tree, PdfDocument doc, long bookmarkPtr, long level) {
        PdfDocument.Bookmark bookmark = new PdfDocument.Bookmark();
        bookmark.mNativePtr = bookmarkPtr;
        bookmark.title = nativeGetBookmarkTitle(bookmarkPtr);
        bookmark.pageIdx = nativeGetBookmarkDestIndex(doc.mNativeDocPtr, bookmarkPtr);
        tree.add(bookmark);

        Long child = nativeGetFirstChildBookmark(doc.mNativeDocPtr, bookmarkPtr);
        if (child != null && level < 16) {
            recursiveGetBookmark(bookmark.getChildren(), doc, child, level++);
        }

        Long sibling = nativeGetSiblingBookmark(doc.mNativeDocPtr, bookmarkPtr);
        if (sibling != null && level < 16) {
            recursiveGetBookmark(tree, doc, sibling, level++);
        }
    }

    /** Get all links from given page */
    public List<PdfDocument.Link> getPageLinks(PdfDocument doc, int pageIndex) {
        synchronized (lock) {
            List<PdfDocument.Link> links = new ArrayList<>();
            Long nativePagePtr = doc.mNativePagesPtr.get(pageIndex);
            if (nativePagePtr == null) {
                return links;
            }
            long[] linkPtrs = nativeGetPageLinks(nativePagePtr);
            for (long linkPtr : linkPtrs) {
                Integer index = nativeGetDestPageIndex(doc.mNativeDocPtr, linkPtr);
                String uri = nativeGetLinkURI(doc.mNativeDocPtr, linkPtr);

                RectF rect = nativeGetLinkRect(linkPtr);
                if (rect != null && (index != null || uri != null)) {
                    links.add(new PdfDocument.Link(rect, index, uri));
                }

            }
            return links;
        }
    }

    /**
     * Map page coordinates to device screen coordinates
     *
     * @param doc       pdf document
     * @param pageIndex index of page
     * @param startX    left pixel position of the display area in device coordinates
     * @param startY    top pixel position of the display area in device coordinates
     * @param sizeX     horizontal size (in pixels) for displaying the page
     * @param sizeY     vertical size (in pixels) for displaying the page
     * @param rotate    page orientation: 0 (normal), 1 (rotated 90 degrees clockwise),
     *                  2 (rotated 180 degrees), 3 (rotated 90 degrees counter-clockwise)
     * @param pageX     X value in page coordinates
     * @param pageY     Y value in page coordinate
     * @return mapped coordinates
     */
    public Point mapPageCoordsToDevice(PdfDocument doc, int pageIndex, int startX, int startY, int sizeX,
                                       int sizeY, int rotate, double pageX, double pageY) {
        long pagePtr = doc.mNativePagesPtr.get(pageIndex);
        return nativePageCoordsToDevice(pagePtr, startX, startY, sizeX, sizeY, rotate, pageX, pageY);
    }

    /**
     * Map device screen coordinates to page coordinates
     *
     * @param doc       pdf document
     * @param pageIndex index of page
     * @param startX    left pixel position of the display area in device coordinates
     * @param startY    top pixel position of the display area in device coordinates
     * @param sizeX     horizontal size (in pixels) for displaying the page
     * @param sizeY     vertical size (in pixels) for displaying the page
     * @param rotate    page orientation: 0 (normal), 1 (rotated 90 degrees clockwise),
     *                  2 (rotated 180 degrees), 3 (rotated 90 degrees counter-clockwise)
     * @param deviceX   X value in page coordinates
     * @param deviceY   Y value in page coordinate
     * @return mapped coordinates
     */
    public PointF mapDeviceCoordsToPage(PdfDocument doc, int pageIndex, int startX, int startY, int sizeX,
                                        int sizeY, int rotate, int deviceX, int deviceY) {
        long pagePtr = doc.mNativePagesPtr.get(pageIndex);
        return nativeDeviceCoordsToPage(pagePtr, startX, startY, sizeX, sizeY, rotate, deviceX, deviceY);
    }

    /**
     * @see PdfiumCore#mapPageCoordsToDevice(PdfDocument, int, int, int, int, int, int, double, double)
     * @return mapped coordinates
     */
    public RectF mapRectToDevice(PdfDocument doc, int pageIndex, int startX, int startY, int sizeX,
                                 int sizeY, int rotate, RectF coords) {

        Point leftTop = mapPageCoordsToDevice(doc, pageIndex, startX, startY, sizeX, sizeY, rotate,
                coords.left, coords.top);
        Point rightBottom = mapPageCoordsToDevice(doc, pageIndex, startX, startY, sizeX, sizeY, rotate,
                coords.right, coords.bottom);
        return new RectF(leftTop.x, leftTop.y, rightBottom.x, rightBottom.y);
    }

    /**
     * @see PdfiumCore#mapDeviceCoordsToPage(PdfDocument, int, int, int, int, int, int, int, int)
     * @return mapped coordinates
     */
    public RectF mapRectToPage(PdfDocument doc, int pageIndex, int startX, int startY, int sizeX,
                                 int sizeY, int rotate, RectF coords) {

        PointF leftTop = mapDeviceCoordsToPage(doc, pageIndex, startX, startY, sizeX, sizeY, rotate,
                (int)coords.left, (int)coords.top);
        PointF rightBottom = mapDeviceCoordsToPage(doc, pageIndex, startX, startY, sizeX, sizeY, rotate,
                (int)coords.right, (int)coords.bottom);
        return new RectF(leftTop.x, leftTop.y, rightBottom.x, rightBottom.y);
    }

    public long openTextPage(PdfDocument doc, int pageIndex) {
        synchronized (lock) {
            long page = openPage(doc, pageIndex);
            Long textPagePtr = doc.mNativeTextPagesPtr.get(pageIndex);
            if (textPagePtr == null) {
                textPagePtr = nativeLoadTextPage(doc.mNativeDocPtr, page);
                doc.mNativeTextPagesPtr.put(pageIndex, textPagePtr);
            }
            return textPagePtr;
        }

    }

    public void closeTextPage(PdfDocument doc, int pageIndex) {
        synchronized (lock) {
            final Long nativeLoadTextPage = doc.mNativeTextPagesPtr.get(pageIndex);
            if (nativeLoadTextPage != null) {
                nativeCloseTextPage(nativeLoadTextPage);
                doc.mNativeTextPagesPtr.remove(pageIndex);
            }
        }
    }

    public long[] openTextPage(PdfDocument doc, int fromIndex, int toIndex) {
        long[] textPagesPtr;
        synchronized (lock) {
            textPagesPtr = nativeLoadPages(doc.mNativeDocPtr, fromIndex, toIndex);
            int pageIndex = fromIndex;
            for (long page : textPagesPtr) {
                if (pageIndex > toIndex) break;
                doc.mNativeTextPagesPtr.put(pageIndex, page);
                pageIndex++;
            }

            return textPagesPtr;
        }
    }

    public int textPageCountChars(PdfDocument doc, int textPageIndex) {
        synchronized (lock) {
            try {
                return nativeTextCountChars(doc.mNativeTextPagesPtr.get(textPageIndex));
            } catch (NullPointerException e) {
                Log.e(TAG, "mContext may be null");
                e.printStackTrace();
            } catch (Exception e) {
                Log.e(TAG, "Exception throw from native");
                e.printStackTrace();
            }
        }
        return -1;
    }

    public String textPageGetText(PdfDocument doc, int textPageIndex, int startIndex, int length) {
        synchronized (lock) {
            try {
                short[] buf = new short[length+1];

                int r = nativeTextGetText(doc.mNativeTextPagesPtr.get(textPageIndex), startIndex, length, buf);

                byte[] bytes = new byte[(r-1)*2];
                ByteBuffer bb = ByteBuffer.wrap(bytes);
                bb.order(ByteOrder.LITTLE_ENDIAN);

                for (int i = 0; i < r-1; i++) {
                    short s = buf[i];
                    bb.putShort(s);
                }
                return new String(bytes, "UTF-16LE");
            } catch (NullPointerException e) {
                Log.e(TAG, "mContext may be null");
                e.printStackTrace();
            } catch (Exception e) {
                Log.e(TAG, "Exception throw from native");
                e.printStackTrace();
            }
            return null;
        }
    }










    public char textPageGetUnicode(PdfDocument doc, int textPageIndex, int index) {
        synchronized (lock) {
            try {
                return (char)nativeTextGetUnicode(doc.mNativeTextPagesPtr.get(textPageIndex), index);
            } catch (NullPointerException e) {
                Log.e(TAG, "mContext may be null");
                e.printStackTrace();
            } catch (Exception e) {
                Log.e(TAG, "Exception throw from native");
                e.printStackTrace();
            }
        }
        return 0;
    }

    public RectF textPageGetCharBox(PdfDocument doc, int textPageIndex, int index) {
        synchronized (lock) {
            try {
                double[] o = nativeTextGetCharBox(doc.mNativeTextPagesPtr.get(textPageIndex), index);
                RectF r = new RectF();
                r.left = (float)o[0];
                r.right = (float)o[1];
                r.bottom = (float)o[2];
                r.top = (float)o[3];
                return r;
            } catch (NullPointerException e) {
                Log.e(TAG, "mContext may be null");
                e.printStackTrace();
            } catch (Exception e) {
                Log.e(TAG, "Exception throw from native");
                e.printStackTrace();
            }
        }
        return null;
    }

    public double textPageGetFontSize(PdfDocument doc, int textPageIndex, int index) {
        synchronized (lock) {
            try {
                return nativeGetFontSize(doc.mNativeTextPagesPtr.get(textPageIndex), index);
            } catch (NullPointerException e) {
                Log.e(TAG, "mContext may be null");
                e.printStackTrace();
            } catch (Exception e) {
                Log.e(TAG, "Exception throw from native");
                e.printStackTrace();
            }
        }
        return 0.0;
    }

    public int textPageGetCharIndexAtPos(PdfDocument doc, int textPageIndex, double x, double y, double xTolerance, double yTolerance) {
        synchronized (lock) {
            try {
                return nativeTextGetCharIndexAtPos(doc.mNativeTextPagesPtr.get(textPageIndex), x, y, xTolerance, yTolerance);
            } catch (NullPointerException e) {
                Log.e(TAG, "mContext may be null");
                e.printStackTrace();
            } catch (Exception e) {
                Log.e(TAG, "Exception throw from native");
                e.printStackTrace();
            }
        }
        return -1;
    }

    public int textPageCountRects(PdfDocument doc, int textPageIndex, int start_index, int count) {
        synchronized (lock) {
            try {
                return nativeTextCountRects(doc.mNativeTextPagesPtr.get(textPageIndex), start_index, count);
            } catch (NullPointerException e) {
                Log.e(TAG, "mContext may be null");
                e.printStackTrace();
            } catch (Exception e) {
                Log.e(TAG, "Exception throw from native");
                e.printStackTrace();
            }
        }
        return -1;
    }

    public RectF textPageGetRect(PdfDocument doc, int textPageIndex, int rect_index) {
        synchronized (lock) {
            try {
                double[] o = nativeTextGetRect(doc.mNativeTextPagesPtr.get(textPageIndex), rect_index);
                RectF r = new RectF();
                r.left = (float)o[0];
                r.top = (float)o[1];
                r.right = (float)o[2];
                r.bottom = (float)o[3];
                return r;
            } catch (NullPointerException e) {
                Log.e(TAG, "mContext may be null");
                e.printStackTrace();
            } catch (Exception e) {
                Log.e(TAG, "Exception throw from native");
                e.printStackTrace();
            }
        }
        return null;
    }

    public String textPageGetBoundedText(PdfDocument doc, int textPageIndex, RectF rect, int length) {
        synchronized (lock) {
            try {
                short[] buf = new short[length+1];

                int r = nativeTextGetBoundedText(doc.mNativeTextPagesPtr.get(textPageIndex), rect.left, rect.top, rect.right, rect.bottom, buf);

                byte[] bytes = new byte[(r-1)*2];
                ByteBuffer bb = ByteBuffer.wrap(bytes);
                bb.order(ByteOrder.LITTLE_ENDIAN);

                for (int i = 0; i < r-1; i++) {
                    short s = buf[i];
                    bb.putShort(s);
                }
                return new String(bytes, "UTF-16LE");
            } catch (NullPointerException e) {
                Log.e(TAG, "mContext may be null");
                e.printStackTrace();
            } catch (Exception e) {
                Log.e(TAG, "Exception throw from native");
                e.printStackTrace();
            }
            return null;
        }
    }

    public boolean saveAsCopy(PdfDocument doc, PdfWriteCallback callback) {
        return nativeSaveAsCopy(doc.mNativeDocPtr, callback);
    }
}
