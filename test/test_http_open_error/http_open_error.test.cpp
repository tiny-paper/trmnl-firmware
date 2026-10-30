#include <app_api.h>
#include <http_open_error.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

#define ESP_ERR_HTTP_CONNECT 0x7003
#define ESP_ERR_MBEDTLS_SSL_SETUP_FAILED 0x8017
#define ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED 0x8019
#define CONN ESP_ERR_HTTP_CONNECT
#define SETUP ESP_ERR_MBEDTLS_SSL_SETUP_FAILED
#define HANDSHAKE ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED

// The X's case: the handshake could not allocate its record buffer; the app hears "out of memory", not "no network"
void test_ssl_setup_alloc_failed_is_enomem() {
  TEST_ASSERT_EQUAL(APP_ENOMEM, http_open_app_error({CONN, SETUP, -0x7F00}));
}

void test_handshake_bignum_or_cert_alloc_failed_is_enomem() {
  TEST_ASSERT_EQUAL(APP_ENOMEM, http_open_app_error({CONN, HANDSHAKE, -0x0010}));
  TEST_ASSERT_EQUAL(APP_ENOMEM, http_open_app_error({CONN, HANDSHAKE, -0x4D80}));
  // combined: X509 parse error + ASN1 allocation failed
  TEST_ASSERT_EQUAL(APP_ENOMEM, http_open_app_error({CONN, HANDSHAKE, -(0x2180 + 0x6A)}));
}

void test_esp_no_mem_is_enomem() {
  TEST_ASSERT_EQUAL(APP_ENOMEM, http_open_app_error({0x101, 0, 0}));
  TEST_ASSERT_EQUAL(APP_ENOMEM, http_open_app_error({CONN, 0x101, 0}));
}

// Everything else stays a transport error: DNS, refused, certificate not trusted, timeout
void test_other_errors_stay_transport_errors() {
  TEST_ASSERT_EQUAL(APP_ETIMEDOUT, http_open_app_error({CONN, 0, 0}));
  TEST_ASSERT_EQUAL(APP_ETIMEDOUT, http_open_app_error({CONN, HANDSHAKE, -0x2700}));
  TEST_ASSERT_EQUAL(APP_ETIMEDOUT, http_open_app_error({CONN, HANDSHAKE, -0x7780}));
  TEST_ASSERT_EQUAL(APP_ETIMEDOUT, http_open_app_error({CONN, 0x8006, 0}));
  TEST_ASSERT_FALSE(http_open_mbedtls_alloc_failed(0));
  TEST_ASSERT_FALSE(http_open_mbedtls_alloc_failed(5));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ssl_setup_alloc_failed_is_enomem);
  RUN_TEST(test_handshake_bignum_or_cert_alloc_failed_is_enomem);
  RUN_TEST(test_esp_no_mem_is_enomem);
  RUN_TEST(test_other_errors_stay_transport_errors);
  return UNITY_END();
}
