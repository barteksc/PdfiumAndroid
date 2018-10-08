package com.shockwave.pdfium;

import java.util.ArrayList;
import java.util.List;

public class PdfBookmark {
    public final List<PdfBookmark> children = new ArrayList<>();
    public final String title;
    public final long pageIdx;
    public final long nativePtr;

    PdfBookmark(long nativePtr, String title, long pageIdx) {
        this.nativePtr = nativePtr;
        this.title = title;
        this.pageIdx = pageIdx;
    }
}