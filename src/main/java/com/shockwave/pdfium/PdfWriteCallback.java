package com.shockwave.pdfium;

public interface PdfWriteCallback {
    int WriteBlock(byte[] data);
}
