#ifndef _MBEDTLS_CONFIG_H
#define _MBEDTLS_CONFIG_H

#include <limits.h>  /* évite des erreurs INT_MAX dans certains fichiers mbedtls */

/* Entropie hardware RP2350 (pico_rand), pas d'entropie plateforme */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_HAVE_TIME

/* Réduire la taille des tables AES */
#define MBEDTLS_AES_C
#define MBEDTLS_AES_FEWER_TABLES

/* Chiffrements et digests nécessaires */
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_GCM_C
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA256_SMALLER
#define MBEDTLS_SHA512_C

/* Courbes elliptiques */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED

/* RSA */
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS5_C

/* PKI / X.509 (requis par altcp_tls même sans vérification de cert) */
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* TLS 1.2 + TLS 1.3 client */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_PROTO_TLS1_3
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED
#define MBEDTLS_SSL_TLS1_3_COMPATIBILITY_MODE
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE
#define MBEDTLS_HKDF_C
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

/* PSA Crypto — requis par TLS 1.3 */
#define MBEDTLS_PSA_CRYPTO_C
#define PSA_WANT_ALG_ECDH                    1
#define PSA_WANT_ALG_HKDF                    1
#define PSA_WANT_ALG_HKDF_EXTRACT            1
#define PSA_WANT_ALG_HKDF_EXPAND             1
#define PSA_WANT_ALG_SHA_256                 1
#define PSA_WANT_ALG_SHA_384                 1
#define PSA_WANT_ALG_ECDSA                   1
#define PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY     1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC 1
#define PSA_WANT_ECC_SECP_R1_256             1
#define PSA_WANT_ECC_SECP_R1_384             1
/* Ciphers TLS 1.3 : TLS_AES_128_GCM_SHA256 + TLS_AES_256_GCM_SHA384 */
#define PSA_WANT_ALG_GCM                     1
#define PSA_WANT_KEY_TYPE_AES                1

/* Entropie / DRBG */
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C

/* Utilitaires */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_ERROR_C

/* Autoriser l'accès aux membres privés (requis par altcp_tls_mbedtls.c) */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

/* Timing milliseconde (requis par mbedtls 3.x) */
#define MBEDTLS_PLATFORM_MS_TIME_ALT

#endif /* _MBEDTLS_CONFIG_H */
