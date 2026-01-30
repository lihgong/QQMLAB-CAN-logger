#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

// The safest place to start server is when WIFI manager gets the IP
// So we expose this API, and let WIFI manager calls it

void http_server_start(void);

#endif // __HTTP_SERVER_H__
