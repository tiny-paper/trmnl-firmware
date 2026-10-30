#pragma once

#include <stdbool.h>

// Why an app's http_request could not open its connection (docs/apps.md "Memory for HTTPS"): out of memory
// (APP_ENOMEM: the TLS handshake or the client could not allocate) or a transport error (APP_ETIMEDOUT).
// Pure: src/app_api_table.cpp passes the codes of esp_http_client_open and
// esp_http_client_get_and_clear_last_tls_error.

// The ESP-IDF and mbedTLS codes this file knows (checked against their headers in src/app_api_table.cpp)
#define HTTP_OPEN_ESP_ERR_NO_MEM 0x101
// mbedTLS: high-level parts (0x1000..0x7F80) and low-level parts (0x01..0x7F) of a combined code
#define HTTP_OPEN_MBEDTLS_ERR_SSL_ALLOC_FAILED -0x7F00
#define HTTP_OPEN_MBEDTLS_ERR_PK_ALLOC_FAILED -0x3F80
#define HTTP_OPEN_MBEDTLS_ERR_X509_ALLOC_FAILED -0x2880
#define HTTP_OPEN_MBEDTLS_ERR_ECP_ALLOC_FAILED -0x4D80
#define HTTP_OPEN_MBEDTLS_ERR_CIPHER_ALLOC_FAILED -0x6180
#define HTTP_OPEN_MBEDTLS_ERR_MD_ALLOC_FAILED -0x5180
#define HTTP_OPEN_MBEDTLS_ERR_PEM_ALLOC_FAILED -0x1180
#define HTTP_OPEN_MBEDTLS_ERR_PKCS7_ALLOC_FAILED -0x5780
#define HTTP_OPEN_MBEDTLS_ERR_DHM_ALLOC_FAILED -0x3400
#define HTTP_OPEN_MBEDTLS_ERR_MPI_ALLOC_FAILED -0x0010
#define HTTP_OPEN_MBEDTLS_ERR_ASN1_ALLOC_FAILED -0x006A
#define HTTP_OPEN_MBEDTLS_ERR_LMS_ALLOC_FAILED -0x0017

typedef struct {
  int open_err; // esp_http_client_open's esp_err_t
  int tls_err; // the last ESP-TLS esp_err_t (0: none)
  int mbedtls_err; // the last mbedTLS code (0: none)
} http_open_codes_t;

// True when a code is an mbedTLS "allocation failed" (high- or low-level part of a combined code)
bool http_open_mbedtls_alloc_failed(int mbedtls_err);
// APP_ENOMEM or APP_ETIMEDOUT
int http_open_app_error(http_open_codes_t c);
