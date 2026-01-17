/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef HTTP_H
#define HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

/* HTTP Content Types */
/* HTML document. Use this for web pages rendered by browsers. */
#define cHTTPX_CTYPE_HTML   "text/html"
/* Plain text. Use for simple text responses or logs. */
#define cHTTPX_CTYPE_TEXT   "text/plain"
/* XML document. Use for XML-based APIs or configurations. */
#define cHTTPX_CTYPE_XML    "application/xml"
/* CSS stylesheet. Use when returning CSS files for web pages. */
#define cHTTPX_CTYPE_CSS    "text/css"
/* CSV file. Use for spreadsheet-style data exports. */
#define cHTTPX_CTYPE_CSV    "text/csv"
/* JSON data. Use for REST API responses and requests. */
#define cHTTPX_CTYPE_JSON   "application/json"
/* URL-encoded form data. Typical for HTML form submissions. */
#define cHTTPX_CTYPE_FORM   "application/x-www-form-urlencoded"
/* Multipart form data. Used for file uploads via forms. */
#define cHTTPX_CTYPE_MULTI  "multipart/form-data"
/* Raw binary stream. Use when content type is unknown. */
#define cHTTPX_CTYPE_OCTET  "application/octet-stream"
/* JavaScript script file. Used for web applications. */
#define cHTTPX_CTYPE_JS     "application/javascript"
/* PNG image format. Lossless compressed image. */
#define cHTTPX_CTYPE_PNG    "image/png"
/* JPEG image format. Common for photos. */
#define cHTTPX_CTYPE_JPEG   "image/jpeg"
/* GIF image format. Supports simple animations. */
#define cHTTPX_CTYPE_GIF    "image/gif"
/* WebP image format. Modern compressed image format. */
#define cHTTPX_CTYPE_WEBP   "image/webp"
/* SVG vector image format. */
#define cHTTPX_CTYPE_SVG    "image/svg+xml"
/* BMP bitmap image format. Rarely used on the web. */
#define cHTTPX_CTYPE_BMP    "image/bmp"
/* MP3 audio format. Common compressed audio. */
#define cHTTPX_CTYPE_MP3    "audio/mpeg"
/* WAV audio format. Uncompressed audio. */
#define cHTTPX_CTYPE_WAV    "audio/wav"
/* OGG audio format. Open-source audio container. */
#define cHTTPX_CTYPE_OGG    "audio/ogg"
/* MP4 video format. Most common video container. */
#define cHTTPX_CTYPE_MP4    "video/mp4"
/* WebM video format. Open-source video format. */
#define cHTTPX_CTYPE_WEBM   "video/webm"
/* AVI video format. Older Microsoft video format. */
#define cHTTPX_CTYPE_AVI    "video/x-msvideo"
/* ZIP archive file. */
#define cHTTPX_CTYPE_ZIP    "application/zip"
/* RAR archive file. */
#define cHTTPX_CTYPE_RAR    "application/vnd.rar"
/* 7-Zip archive file. */
#define cHTTPX_CTYPE_7Z     "application/x-7z-compressed"
/* PDF document file. */
#define cHTTPX_CTYPE_PDF    "application/pdf"
/* Microsoft Word DOC document. */
#define cHTTPX_CTYPE_DOC    "application/msword"
/* Microsoft Word DOCX document. */
#define cHTTPX_CTYPE_DOCX   "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
/* Microsoft Excel XLS spreadsheet. */
#define cHTTPX_CTYPE_XLS    "application/vnd.ms-excel"
/* Microsoft Excel XLSX spreadsheet. */
#define cHTTPX_CTYPE_XLSX   "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
/* Web Open Font Format. */
#define cHTTPX_CTYPE_WOFF   "font/woff"
/* Web Open Font Format 2. */
#define cHTTPX_CTYPE_WOFF2  "font/woff2"
/* TrueType font. */
#define cHTTPX_CTYPE_TTF    "font/ttf"
/* OpenType font. */
#define cHTTPX_CTYPE_OTF    "font/otf"

/* HTTP methods */
#define cHTTPX_MethodGet     "GET"
#define cHTTPX_MethodPost    "POST"
#define cHTTPX_MethodPut     "PUT"
#define cHTTPX_MethodPatch   "PATCH"
#define cHTTPX_MethodDelete  "DELETE"
#define cHTTPX_MethodOptions "OPTIONS"

/* HTTP statuses */
// 1xx
#define cHTTPX_StatusContinue 100
#define cHTTPX_StatusSwitchingProtocols 101
#define cHTTPX_StatusProcessing 102
#define cHTTPX_StatusEarlyHints 103
// 2xx
#define cHTTPX_StatusOK 200
#define cHTTPX_StatusCreated 201
#define cHTTPX_StatusAccepted 202
#define cHTTPX_StatusNonAuthoritativeInformation 203
#define cHTTPX_StatusNoContent 204
#define cHTTPX_StatusResetContent 205
#define cHTTPX_StatusPartialContent 206
#define cHTTPX_StatusMultiStatus 207
#define cHTTPX_StatusAlreadyReported 208
#define cHTTPX_StatusIMUsed 226
// 3xx
#define cHTTPX_StatusMultipleChoices 300
#define cHTTPX_StatusMovedPermanently 301
#define cHTTPX_StatusFound 302
#define cHTTPX_StatusSeeOther 303
#define cHTTPX_StatusNotModified 304
#define cHTTPX_StatusUseProxy 305
#define cHTTPX_StatusNone 306
#define cHTTPX_StatusTemporaryRedirect 307
#define cHTTPX_StatusPermanentRedirect 308
// 4xx
#define cHTTPX_StatusBadRequest 400
#define cHTTPX_StatusUnauthorized 401
#define cHTTPX_StatusPaymentRequired 402
#define cHTTPX_StatusForbidden 403
#define cHTTPX_StatusNotFound 404
#define cHTTPX_StatusMethodNotAllowed 405
#define cHTTPX_StatusNotAcceptable 406
#define cHTTPX_StatusProxyAuthenticationRequired 407
#define cHTTPX_StatusRequestTimeout 408
#define cHTTPX_StatusConflict 409
#define cHTTPX_StatusGone 410
#define cHTTPX_StatusLengthRequired 411
#define cHTTPX_StatusPreconditionFailed 412
#define cHTTPX_StatusPayloadTooLarge 413
#define cHTTPX_StatusURITooLong 414
#define cHTTPX_StatusUnsupportedMediaType 415
#define cHTTPX_StatusRangeNotSatisfiable 416
#define cHTTPX_StatusExpectationFailed 417
#define cHTTPX_StatusImATeapot 418
#define cHTTPX_StatusAuthenticationTimeout 419
#define cHTTPX_StatusMisdirectedRequest 421
#define cHTTPX_StatusUnprocessableEntity 422
#define cHTTPX_StatusLocked 423
#define cHTTPX_StatusFailedDependency 424
#define cHTTPX_StatusTooEarly 425
#define cHTTPX_StatusUpgradeRequired 426
#define cHTTPX_StatusPreconditionRequired 428
#define cHTTPX_StatusTooManyRequests 429
#define cHTTPX_StatusRequestHeaderFieldsTooLarge 431
#define cHTTPX_StatusRetryWith 449
#define cHTTPX_StatusUnavailableForLegalReasons 451
#define cHTTPX_StatusClientClosedRequest 499
// 5xx
#define cHTTPX_StatusInternalServerError 500
#define cHTTPX_StatusNotImplemented 501
#define cHTTPX_StatusBadGateway 502
#define cHTTPX_StatusServiceUnavailable 503
#define cHTTPX_StatusGatewayTimeout 504
#define cHTTPX_StatusHTTPVersionNotSupported 505
#define cHTTPX_StatusVariantAlsoNegotiates 506
#define cHTTPX_StatusInsufficientStorage 507
#define cHTTPX_StatusLoopDetected 508
#define cHTTPX_StatusBandwidthLimitExceeded 509
#define cHTTPX_StatusNotExtended 510
#define cHTTPX_StatusNetworkAuthenticationRequired 511
#define cHTTPX_StatusUnknownError 520
#define cHTTPX_StatusWebServerIsDown 521
#define cHTTPX_StatusConnectionTimedOut 522
#define cHTTPX_StatusOriginIsUnreachable 523
#define cHTTPX_StatusATimeoutOccurred 524
#define cHTTPX_StatusSSLHandshakeFailed 525
#define cHTTPX_StatusInvalidSSLCertificate 526

#ifdef __cplusplus
extern }
#endif

#endif