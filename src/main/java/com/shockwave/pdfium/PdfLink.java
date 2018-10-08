package com.shockwave.pdfium;

import android.graphics.RectF;

public class PdfLink {
    public final RectF bounds;
    public final long destPageIdx;
    public final String uri;

    PdfLink(RectF bounds, long destPageIdx, String uri) {
        this.bounds = bounds;
        this.destPageIdx = destPageIdx;
        this.uri = uri;
    }
}