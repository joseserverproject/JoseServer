/*
Copyright (c) <2013> <joseprojectteam>

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef _JS_HTTPMSG_H_
#define _JS_HTTPMSG_H_

#define JS_HTTPSERVER_HTML_OK_TEMPLATE	"HTTP/1.1 200 OK\r\n\
Server: JoseServer 1.0\r\n\
Accept-Ranges: bytes\r\n\
Content-Length: %u\r\n\
Connection: Keep-Alive\r\n\
Cahce-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0\r\n\
Pragma: no-cache\r\n\
Content-Type: text/html\r\n\r\n"

#define JS_HTTPSERVER_JSON_OK_TEMPLATE	"HTTP/1.1 200 OK\r\n\
Server: JoseServer 1.0\r\n\
Accept-Ranges: bytes\r\n\
Content-Length: %u\r\n\
Connection: Keep-Alive\r\n\
Cahce-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0\r\n\
Pragma: no-cache\r\n\
Content-Type: application/json\r\n\r\n"

#define JS_HTTPSERVER_XML_OK_TEMPLATE	"HTTP/1.1 200 OK\r\n\
Server: JoseServer 1.0\r\n\
Accept-Ranges: bytes\r\n\
Content-Length: %u\r\n\
Connection: Keep-Alive\r\n\
Cahce-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0\r\n\
Pragma: no-cache\r\n\
Content-Type: application/xml\r\n\r\n"

#define JS_HTTPSERVER_FULL_OK_TEMPLATE	"HTTP/1.1 200 OK\r\n\
Date: %s\r\n\
Server: JoseServer 1.0\r\n\
Last-Modified: %s\r\n\
ETag: %s\r\n\
Accept-Ranges: bytes\r\n\
Content-Length: %llu\r\n\
Connection: Keep-Alive\r\n\
Content-Type: %s\r\n\r\n"

#define JS_HTTPSERVER_PARTIAL_OK_TEMPLATE	"HTTP/1.1 206 OK\r\n\
Date: %s\r\n\
Server: JoseServer 1.0\r\n\
Last-Modified: %s\r\n\
ETag: %s\r\n\
Accept-Ranges: bytes\r\n\
Content-Range: bytes=%llu-%llu\r\n\
Connection: Keep-Alive\r\n\
Content-Type: %s\r\n\r\n"

#define JS_HTTPSERVER_ERROR_HEADER_TEMPLATE	"HTTP/1.1 %d %s\r\n\
Server: JoseServer 1.0\r\n\
Accept-Ranges: bytes\r\n\
Content-Length: %u\r\n\
Connection: Close\r\n\
Content-Type: text/html\r\n\r\n"

#define JS_HTTPSERVER_ERROR_PAGE "<!DOCTYPE html>\n\
<html lang=en>\n\
  <meta charset=utf-8>\n\
  <meta name=viewport content=\"initial-scale=1, minimum-scale=1, width=device-width\">\n\
  <title>Error Page</title>\n\
  <body><p><b>Error %d.</b> <ins>That is an error.</ins>\n\
<p>please check the error code <a href='http://en.wikipedia.org/wiki/List_of_HTTP_status_codes'>HTTP error</a>\n\
</body></html>"

#define JS_HTTPSERVER_ALLOWED_METHODS	"OPTIONS, GET, HEAD, POST"

#define JS_HTTPSERVER_OPTION_RSP "HTTP/1.1 200 OK\r\n\
Allow: %s\r\n\
Content-Length: 0\r\n\r\n"


#define JS_HTTPSERVER_DEFAULT_MIME "text/html html htm shtm shtml\r\n\
text/css css\r\n\
application/x-javascript js\r\n\
image/gif gif\r\n\
image/jpeg jpg jpeg\r\n\
image/png	png\r\n\
image/bmp	bmp\r\n\
text/plain	txt\r\n\
audio/x-wav	wav\r\n\
audio/x-mp3	mp3\r\n\
audio/x-mpegurl	m3u m3u8\r\n\
audio/ogg	ogg\r\n\
application/xml	xml xslt xsl\r\n\
application/json json\r\n\
application/ms-excel xls\r\n\
application/ms-powerpoint ppt\r\n\
application/msword doc\r\n\
application/x-zip-compressed zip\r\n\
application/pdf pdf\r\n\
application/x-shockwave-flash swf\r\n\
video/mpeg mpeg mpg\r\n\
video/m2ts	m2ts\r\n\
video/quicktime mov\r\n\
video/mp4 mp4\r\n\
video/x-m4v m4v\r\n\
video/x-ms-asf asf\r\n\
video/x-ms-wma wma\r\n\
video/x-ms-wmv wmv\r\n\
video/x-msvideo avi"

#define JS_DEFAULT_XML_DEC	"<?xml version=\"1.0\"?>\n"

#endif