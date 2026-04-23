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

/* TLS 1.2 client */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_SSL_SERVER_NAME_INDICATION   /* SNI */

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
