#include <app_api.h>
#include <http_open_error.h>

// The "allocation failed" parts; a combined code is a high-level part plus a low-level one
static const int high_alloc[] = {
    -HTTP_OPEN_MBEDTLS_ERR_SSL_ALLOC_FAILED,
    -HTTP_OPEN_MBEDTLS_ERR_PK_ALLOC_FAILED,
    -HTTP_OPEN_MBEDTLS_ERR_X509_ALLOC_FAILED,
    -HTTP_OPEN_MBEDTLS_ERR_ECP_ALLOC_FAILED,
    -HTTP_OPEN_MBEDTLS_ERR_CIPHER_ALLOC_FAILED,
    -HTTP_OPEN_MBEDTLS_ERR_MD_ALLOC_FAILED,
    -HTTP_OPEN_MBEDTLS_ERR_PEM_ALLOC_FAILED,
    -HTTP_OPEN_MBEDTLS_ERR_PKCS7_ALLOC_FAILED,
    -HTTP_OPEN_MBEDTLS_ERR_DHM_ALLOC_FAILED,
};
static const int low_alloc[] = {
    -HTTP_OPEN_MBEDTLS_ERR_MPI_ALLOC_FAILED,
    -HTTP_OPEN_MBEDTLS_ERR_ASN1_ALLOC_FAILED,
    -HTTP_OPEN_MBEDTLS_ERR_LMS_ALLOC_FAILED,
};

bool http_open_mbedtls_alloc_failed(int mbedtls_err) {
  if (mbedtls_err >= 0) return false;
  int e = -mbedtls_err, high = e & 0xFF80, low = e & 0x007F;
  for (int h : high_alloc)
    if (high == h) return true;
  for (int l : low_alloc)
    if (low == l) return true;
  return false;
}

int http_open_app_error(http_open_codes_t c) {
  if (c.open_err == HTTP_OPEN_ESP_ERR_NO_MEM || c.tls_err == HTTP_OPEN_ESP_ERR_NO_MEM ||
      http_open_mbedtls_alloc_failed(c.mbedtls_err))
    return APP_ENOMEM;
  return APP_ETIMEDOUT;
}
