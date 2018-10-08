package com.shockwave.pdfium;

public class PdfMetaInfo {
    public final String title;
    public final String author;
    public final String subject;
    public final String keywords;
    public final String creator;
    public final String producer;
    public final String creationDate;
    public final String modDate;
    PdfMetaInfo(String title,
                String author,
                String subject,
                String keywords,
                String creator,
                String producer,
                String creationDate,
                String modDate) {
        this.title = title;
        this.author = author;
        this.subject = subject;
        this.keywords = keywords;
        this.creator = creator;
        this.producer = producer;
        this.creationDate = creationDate;
        this.modDate = modDate;
    }
}