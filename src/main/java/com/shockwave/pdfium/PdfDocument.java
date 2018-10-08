package com.shockwave.pdfium;

import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Point;
import android.graphics.RectF;
import android.os.ParcelFileDescriptor;
import android.support.annotation.NonNull;
import android.support.annotation.Nullable;
import android.util.Log;
import android.view.Surface;

import java.io.Closeable;
import java.io.FileDescriptor;
import java.io.IOException;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class PdfDocument implements Closeable {
    private static final String TAG = PdfDocument.class.getName();
    private static final Class FD_CLASS = FileDescriptor.class;
    private static final String FD_FIELD_NAME = "descriptor";
    private static final Object lock = new Object();
    private static Field mFdField = null;

    static {
        try {
            System.loadLibrary("c++_shared");
            System.loadLibrary("modpng");
            System.loadLibrary("modft2");
            System.loadLibrary("modpdfium");
            System.loadLibrary("jniPdfium");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Native libraries failed to load - " + e);
        }
        nativeInit();
    }

    private final Point mTempPoint = new Point();
    private ParcelFileDescriptor mFileDescriptor;
    private int mPageCount;
    private long mNativePtr;

    public PdfDocument(@NonNull ParcelFileDescriptor input, @Nullable String password) {
        mFileDescriptor = input;
        synchronized (lock) {
            long size = nativeGetFileSize(getNumFd(input));
            initDocument(nativeOpen(mFileDescriptor.getFd(), size, password));
        }
    }

    public PdfDocument(@NonNull byte[] data, @Nullable String password) {
        synchronized (lock) {
            initDocument(nativeOpenByteArray(data, password));
        }
    }

    private static int getNumFd(ParcelFileDescriptor fdObj) {
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

    private void initDocument(long nativePtr) {
        synchronized (lock) {
            mNativePtr = nativePtr;
            try {
                mPageCount = nativeGetPageCount(mNativePtr);
            } catch (Throwable t) {
                nativeClose(mNativePtr);
                mNativePtr = 0;
                throw t;
            }
        }
    }

    public boolean shouldScaleForPritning() {
        synchronized (lock) {
            return nativeScaleForPrinting(mNativePtr);
        }
    }

    public Point getPageSize(int pageIndex) {
        Point point = new Point();
        synchronized (lock) {
            nativeGetPageSizeByIndex(mNativePtr, pageIndex, point);
        }
        return point;
    }

    public List<Point> getAllPageSizes() {
        List<Point> sizes = new ArrayList<>(mPageCount);
        synchronized (lock) {
            for (int i = 0; i < mPageCount; i++) {
                Point point = new Point();
                nativeGetPageSizeByIndex(mNativePtr, i, point);
                sizes.add(point);
            }
        }
        return sizes;
    }

    public PdfPage openPage(int index) {
        return new PdfPage(mNativePtr, index);
    }

    public int getPageCount() {
        return mPageCount;
    }

    /**
     * Get metadata for given document
     */
    public PdfMetaInfo getDocumentMeta() {
        synchronized (lock) {
            return new PdfMetaInfo(
                    nativeGetMetaText(mNativePtr, "Title"),
                    nativeGetMetaText(mNativePtr, "Author"),
                    nativeGetMetaText(mNativePtr, "Subject"),
                    nativeGetMetaText(mNativePtr, "Keywords"),
                    nativeGetMetaText(mNativePtr, "Creator"),
                    nativeGetMetaText(mNativePtr, "Producer"),
                    nativeGetMetaText(mNativePtr, "CreationDate"),
                    nativeGetMetaText(mNativePtr, "ModDate"));
        }
    }

    /**
     * Get table of contents (bookmarks) for given document
     */
    public List<PdfBookmark> getBookmarks() {
        synchronized (lock) {
            List<PdfBookmark> root = new ArrayList<>();
            long first = nativeGetFirstChildBookmark(mNativePtr, 0);
            if (first != 0) {
                recursiveGetBookmark(root, first);
            }
            return root;
        }
    }

    private void recursiveGetBookmark(List<PdfBookmark> tree, long bookmarkPtr) {
        PdfBookmark pdfBookmark = new PdfBookmark(bookmarkPtr, nativeGetBookmarkTitle(bookmarkPtr), nativeGetBookmarkDestIndex(mNativePtr, bookmarkPtr));
        tree.add(pdfBookmark);
        long child = nativeGetFirstChildBookmark(mNativePtr, bookmarkPtr);
        if (child != 0) {
            recursiveGetBookmark(pdfBookmark.children, child);
        }
        long sibling = nativeGetSiblingBookmark(mNativePtr, bookmarkPtr);
        if (sibling != 0) {
            recursiveGetBookmark(tree, sibling);
        }
    }

    @Override
    public void close() {
        throwIfClosed();
        doClose();
    }

    private void doClose() {
        if (mNativePtr != 0) {
            synchronized (lock) {
                nativeClose(mNativePtr);
                if (mFileDescriptor != null) {
                    try {
                        mFileDescriptor.close();
                    } catch (IOException e) {
                        /*no op*/
                    }
                    mFileDescriptor = null;
                }
            }
            mNativePtr = 0;
        }
    }

    private void throwIfClosed() {
        if (mNativePtr == 0) {
            throw new IllegalStateException("Already closed");
        }
    }

    public static class PdfSearchResult {
        public final int startIndex;
        public final int length;

        PdfSearchResult(long searchPtr) {
            length = nativeTextGetSchCount(searchPtr);
            startIndex = nativeTextGetSchResultIndex(searchPtr);
        }
    }

    public final class PdfTextSearch implements Closeable {
        private long mNativePtr;

        PdfTextSearch(long textPtr, String query, int startIndex, int flags) {
            synchronized (lock) {
                mNativePtr = nativeTextFindStart(textPtr, query, flags, startIndex);
            }
        }

        @Nullable
        public PdfSearchResult findNext() {
            synchronized (lock) {
                if (nativeTextFindNext(mNativePtr)) {
                    return new PdfSearchResult(mNativePtr);
                }
                return null;
            }
        }

        @Nullable
        public PdfSearchResult findPrev() {
            synchronized (lock) {
                if (nativeTextFindPrev(mNativePtr)) {
                    return new PdfSearchResult(mNativePtr);
                }
                return null;
            }
        }

        @Override
        public void close() {
            throwIfClosed();
            doClose();
        }

        private void doClose() {
            if (mNativePtr != 0) {
                synchronized (lock) {
                    nativeTextFindClose(mNativePtr);
                }
                mNativePtr = 0;
            }
        }

        private void throwIfClosed() {
            if (mNativePtr == 0) {
                throw new IllegalStateException("Already closed");
            }
        }
    }

    public final class PdfText implements Closeable {
        // If not set, it will not match case by default.
        public final static int SEARCH_FLAG_MATCHCASE = 0x00000001;
        // If not set, it will not match the whole word by default.
        public final static int SEARCH_MATCH_WHOLE_WORD = 0x00000002;

        private long mNativePtr;

        PdfText(long pagePtr) {
            synchronized (lock) {
                mNativePtr = nativeTextLoadPage(pagePtr);
            }
        }

        public String getText() {
            synchronized (lock) {
                int length = nativeTextCountChars(mNativePtr);
                return length > 0 ? nativeTextGetText(mNativePtr, 0, length) : "";
            }
        }

        public List<RectF> getTextRects(int start, int count) {
            synchronized (lock) {
                int rectsCount = nativeTextCountRects(mNativePtr, start, count);
                if (rectsCount > 0) {
                    List<RectF> result = new ArrayList<>(rectsCount);
                    for (int i = 0; i < rectsCount; i++) {
                        RectF rect = new RectF();
                        nativeTextGetRect(mNativePtr, i, rect);
                        result.add(rect);
                    }
                    return result;
                }
            }
            return Collections.emptyList();
        }

        /**
         * Start a search
         *
         * @param query      A match pattern
         * @param startIndex Start from this character. -1 for end of the page.
         * @param flags      Option flags.
         * @return Search result
         */
        public PdfTextSearch search(@NonNull String query, int startIndex, int flags) {
            return new PdfTextSearch(mNativePtr, query, startIndex, flags);
        }

        public long getTextLength() {
            synchronized (lock) {
                return nativeTextCountChars(mNativePtr);
            }
        }

        @Override
        public void close() {
            throwIfClosed();
            doClose();
        }

        private void doClose() {
            if (mNativePtr != 0) {
                synchronized (lock) {
                    nativeTextClosePage(mNativePtr);
                }
                mNativePtr = 0;
            }
        }

        private void throwIfClosed() {
            if (mNativePtr == 0) {
                throw new IllegalStateException("Already closed");
            }
        }
    }

    public final class PdfPage implements Closeable {
        private final int index;
        private final int width;
        private final int height;
        private long mNativePtr;

        PdfPage(long documentPtr, int index) {
            Point size = mTempPoint;
            synchronized (lock) {
                mNativePtr = nativeOpenPageAndGetSize(documentPtr, index, size);
            }
            this.index = index;
            this.width = size.x;
            this.height = size.y;
        }

        public void render(@NonNull Bitmap bitmap, int startX, int startY, int drawSizeX, int drawSizeY) {
            render(bitmap, startX, startY, drawSizeX, drawSizeY, Color.WHITE, false);
        }

        public void render(@NonNull Bitmap bitmap, int startX, int startY, int drawSizeX, int drawSizeY, long backgroundColor, boolean renderAnnot) {
            synchronized (lock) {
                try {
                    nativeRenderPageBitmap(mNativePtr, bitmap, startX, startY, drawSizeX, drawSizeY, backgroundColor, renderAnnot);
                } catch (Exception e) {
                    Log.e(TAG, "Exception throw from native");
                    e.printStackTrace();
                }
            }
        }

        public void render(@NonNull Surface surface, int startX, int startY, int drawSizeX, int drawSizeY, boolean renderAnnot) {
            synchronized (lock) {
                try {
                    nativeRenderPage(mNativePtr, surface, startX, startY, drawSizeX, drawSizeY, renderAnnot);
                } catch (Exception e) {
                    Log.e(TAG, "Exception throw from native");
                    e.printStackTrace();
                }
            }
        }

        public PdfText openText() {
            return new PdfText(mNativePtr);
        }

        public List<PdfLink> getPageLinks() {
            synchronized (lock) {
                List<PdfLink> pdfLinks = new ArrayList<>();
                long[] linkPtrs = nativeGetPageLinks(mNativePtr);
                if (linkPtrs != null) {
                    for (int size = linkPtrs.length, i = 0; i < size; i++) {
                        long linkPtr = linkPtrs[i];
                        long index = nativeGetDestPageIndex(mNativePtr, linkPtr);
                        String uri = nativeGetLinkURI(mNativePtr, linkPtr);
                        RectF rect = new RectF();
                        if (nativeGetLinkRect(linkPtr, rect) && (index != 0 || uri != null)) {
                            pdfLinks.add(new PdfLink(rect, index, uri));
                        }
                    }
                }
                return pdfLinks;
            }
        }


        /**
         * Map page coordinates to device screen coordinates
         *
         * @param startX left pixel position of the display area in device coordinates
         * @param startY top pixel position of the display area in device coordinates
         * @param sizeX  horizontal size (in pixels) for displaying the page
         * @param sizeY  vertical size (in pixels) for displaying the page
         * @param rotate page orientation: 0 (normal), 1 (rotated 90 degrees clockwise),
         *               2 (rotated 180 degrees), 3 (rotated 90 degrees counter-clockwise)
         * @param pageX  X value in page coordinates
         * @param pageY  Y value in page coordinate
         * @return mapped coordinates
         */
        public Point mapPageCoordsToDevice(int startX, int startY, int sizeX, int sizeY, int rotate, double pageX, double pageY) {
            Point outPoint = new Point();
            synchronized (lock) {
                nativePageCoordsToDevice(mNativePtr, startX, startY, sizeX, sizeY, rotate, pageX, pageY, outPoint);
            }
            return outPoint;
        }

        /**
         * @return mapped coordinates
         * @see PdfPage#mapPageCoordsToDevice(int, int, int, int, int, double, double)
         */
        public RectF mapRectToDevice(int startX, int startY, int sizeX, int sizeY, int rotate, RectF coords) {
            synchronized (lock) {
                Point leftTop = mapPageCoordsToDevice(startX, startY, sizeX, sizeY, rotate,
                        coords.left, coords.top);
                Point rightBottom = mapPageCoordsToDevice(startX, startY, sizeX, sizeY, rotate,
                        coords.right, coords.bottom);
                return new RectF(leftTop.x, leftTop.y, rightBottom.x, rightBottom.y);
            }
        }

        @Override
        public void close() {
            throwIfClosed();
            doClose();
        }

        private void doClose() {
            if (mNativePtr != 0) {
                synchronized (lock) {
                    nativeClosePage(mNativePtr);
                }
                mNativePtr = 0;
            }
        }

        private void throwIfClosed() {
            if (mNativePtr == 0) {
                throw new IllegalStateException("Already closed");
            }
        }
    }

    private static native long nativeInit();

    private static native long nativeGetFileSize(int fd);

    private static native long nativeOpen(int fd, long size, String password);

    private static native void nativeClose(long documentPtr);

    private static native long nativeOpenPageAndGetSize(long documentPtr, int pageIndex, Point outSize);

    private static native boolean nativeScaleForPrinting(long documentPtr);

    private static native int nativeGetPageCount(long docPtr);

    private static native long nativeTextLoadPage(long page);

    private static native long nativeTextFindStart(long textPage, String findWhat, int flags, int startIndex);

    private static native boolean nativeTextFindNext(long handle);

    private static native boolean nativeTextFindPrev(long handle);

    private static native int nativeTextGetSchResultIndex(long handle);

    private static native int nativeTextGetSchCount(long handle);

    private static native String nativeTextGetText(long textPage, int start, int count);

    private static native void nativeTextGetRect(long textPage, int index, RectF outRect);

    private static native int nativeTextCountRects(long textPage, int start, int count);

    private static native int nativeTextCountChars(long textPage);

    private static native void nativeTextFindClose(long handle);

    private static native void nativeTextClosePage(long textPage);

    private native long nativeOpenByteArray(byte[] data, String password);

    private native void nativeClosePage(long pagePtr);

    private native void nativeRenderPage(long pagePtr, Surface surface, int startX, int startY, int drawSizeHor, int drawSizeVer, boolean renderAnnot);

    private native void nativeRenderPageBitmap(long pagePtr, Bitmap bitmap, int startX, int startY, int drawSizeHor, int drawSizeVer, long backgroundColor, boolean renderAnnot);

    private native String nativeGetMetaText(long docPtr, String tag);

    private native long nativeGetFirstChildBookmark(long docPtr, long bookmarkPtr);

    private native long nativeGetSiblingBookmark(long docPtr, long bookmarkPtr);

    private native String nativeGetBookmarkTitle(long bookmarkPtr);

    private native long nativeGetBookmarkDestIndex(long docPtr, long bookmarkPtr);

    private native void nativeGetPageSizeByIndex(long docPtr, int pageIndex, Point outSize);

    private native long[] nativeGetPageLinks(long pagePtr);

    private native long nativeGetDestPageIndex(long docPtr, long linkPtr);

    private native String nativeGetLinkURI(long docPtr, long linkPtr);

    private native boolean nativeGetLinkRect(long linkPtr, RectF outRect);

    private native void nativePageCoordsToDevice(long pagePtr, int startX, int startY, int sizeX, int sizeY, int rotate, double pageX, double pageY, Point outSize);

}
