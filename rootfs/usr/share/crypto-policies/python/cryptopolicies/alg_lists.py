# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2019 Red Hat, Inc.
# Copyright (c) 2019 Tomáš Mráz <tmraz@fedoraproject.org>

"""Lists of algorithms and globbing among them."""

import fnmatch
import warnings

from . import validation

ALL_CIPHERS = (
    'AES-256-GCM', 'AES-256-CCM',
    'AES-192-GCM', 'AES-192-CCM',
    'AES-128-GCM', 'AES-128-CCM',
    'CHACHA20-POLY1305',
    'CAMELLIA-256-GCM', 'CAMELLIA-128-GCM',
    'AES-256-CTR', 'AES-256-CBC',
    'AES-192-CTR', 'AES-192-CBC',
    'AES-128-CTR', 'AES-128-CBC',
    'AES-256-OCB', 'AES-256-EAX',
    'AES-192-OCB', 'AES-192-EAX',
    'AES-128-OCB', 'AES-128-EAX',
    'CAMELLIA-256-CBC', 'CAMELLIA-192-CBC', 'CAMELLIA-128-CBC',
    '3DES-CBC', 'DES-CBC', 'RC4-40', 'RC4-128',
    'DES40-CBC', 'RC2-CBC', 'IDEA-CBC', 'SEED-CBC',
    'AES-256-CFB', 'AES-192-CFB', 'AES-128-CFB',
    'CAMELLIA-256-CFB', 'CAMELLIA-192-CFB', 'CAMELLIA-128-CFB',
    '3DES-CFB', 'IDEA-CFB',
    'GOST28147-TC26Z-CFB', 'GOST28147-CPA-CFB',
    'GOST28147-CPB-CFB', 'GOST28147-CPC-CFB',
    'GOST28147-CPD-CFB', 'GOST28147-TC26Z-CNT',
    'MAGMA-CTR-ACPKM', 'KUZNYECHIK-CTR-ACPKM',
    'NULL',
)

ALL_MACS = (
    'AEAD', 'UMAC-128', 'HMAC-SHA1', 'HMAC-SHA2-256',
    'HMAC-SHA2-384', 'HMAC-SHA2-512', 'UMAC-64', 'HMAC-MD5',
    'HMAC-STREEBOG-256', 'HMAC-STREEBOG-512',
    'GOST28147-CPA-IMIT', 'GOST28147-TC26Z-IMIT',
    'MAGMA-OMAC', 'KUZNYECHIK-OMAC',
    'MAGMA-OMAC-ACPKM', 'KUZNYECHIK-OMAC-ACPKM',
)

ALL_HASHES = (
    'SHA2-256', 'SHA2-384', 'SHA2-512',
    'SHA3-256', 'SHA3-384', 'SHA3-512',
    'SHA2-224', 'SHA3-224',
    'SHAKE-256', 'SHAKE-128',
    'SHA1', 'MD5',
    'STREEBOG-256', 'STREEBOG-512', 'GOSTR94',
)

# we disable curves <= 256 bits by default in Fedora
EXPERIMENTAL_GROUPS = (
    'KYBER768',
    'X25519-KYBER768', 'P256-KYBER768',
    'MLKEM512',
    'P256-MLKEM512', 'X25519-MLKEM512',
    'MLKEM768',
    'P384-MLKEM768', 'X448-MLKEM768', 'X25519-MLKEM768', 'P256-MLKEM768',
    'MLKEM1024',
    'P521-MLKEM1024', 'P384-MLKEM1024',
    'MLKEM768-X25519',
    'MLKEM1024-X448',
)
ALL_GROUPS = (
    'X25519', 'SECP256R1', 'SECP384R1', 'SECP521R1', 'X448',
    'FFDHE-1536', 'FFDHE-2048', 'FFDHE-3072', 'FFDHE-4096',
    'FFDHE-6144', 'FFDHE-8192', 'FFDHE-1024',
    'GOST-GC256A', 'GOST-GC256B', 'GOST-GC256C', 'GOST-GC256D',
    'GOST-GC512A', 'GOST-GC512B', 'GOST-GC512C',
    'BRAINPOOL-P256R1', 'BRAINPOOL-P384R1', 'BRAINPOOL-P512R1',
    *EXPERIMENTAL_GROUPS,
)

EXPERIMENTAL_SIGN = (
    'MLDSA44',
    'P256-MLDSA44', 'RSA3072-MLDSA44', 'MLDSA44-PSS2048', 'MLDSA44-RSA2048',
    'MLDSA44-ED25519', 'MLDSA44-P256', 'MLDSA44-BP256',
    'MLDSA65',
    'P384-MLDSA65', 'MLDSA65-PSS3072', 'MLDSA65-RSA3072',
    'MLDSA65-P256', 'MLDSA65-BP256', 'MLDSA65-ED25519',
    'MLDSA87',
    'P521-MLDSA87', 'MLDSA87-P384', 'MLDSA87-BP384', 'MLDSA87-ED448',
    'FALCON512',
    'P256-FALCON512', 'RSA3072-FALCON512',
    'FALCONPADDED512',
    'P256-FALCONPADDED512', 'RSA3072-FALCONPADDED512',
    'FALCON1024',
    'P521-FALCON1024',
    'FALCONPADDED1024',
    'P521-FALCONPADDED1024',
    'SPHINCSSHA2128FSIMPLE',
    'P256-SPHINCSSHA2128FSIMPLE', 'RSA3072-SPHINCSSHA2128FSIMPLE',
    'SPHINCSSHA2128SSIMPLE',
    'P256-SPHINCSSHA2128SSIMPLE', 'RSA3072-SPHINCSSHA2128SSIMPLE',
    'SPHINCSSHA2192FSIMPLE',
    'P384-SPHINCSSHA2192FSIMPLE',
    'SPHINCSSHAKE128FSIMPLE',
    'P256-SPHINCSSHAKE128FSIMPLE', 'RSA3072-SPHINCSSHAKE128FSIMPLE',
)
ALL_SIGN = (
    'RSA-MD5', 'RSA-SHA1', 'DSA-SHA1', 'ECDSA-SHA1',
    'RSA-SHA2-224', 'DSA-SHA2-224', 'ECDSA-SHA2-224',
    'RSA-SHA2-256', 'DSA-SHA2-256', 'ECDSA-SHA2-256',
    'ECDSA-SHA2-256-FIDO',
    'RSA-SHA2-384', 'DSA-SHA2-384', 'ECDSA-SHA2-384',
    'RSA-SHA2-512', 'DSA-SHA2-512', 'ECDSA-SHA2-512',
    'RSA-SHA3-224', 'DSA-SHA3-224', 'ECDSA-SHA3-224',
    'RSA-SHA3-256', 'DSA-SHA3-256', 'ECDSA-SHA3-256',
    'RSA-SHA3-384', 'DSA-SHA3-384', 'ECDSA-SHA3-384',
    'RSA-SHA3-512', 'DSA-SHA3-512', 'ECDSA-SHA3-512',
    'EDDSA-ED25519', 'EDDSA-ED25519-FIDO', 'EDDSA-ED448',
    'RSA-PSS-SHA1', 'RSA-PSS-SHA2-224', 'RSA-PSS-SHA2-256',
    'RSA-PSS-SHA2-384', 'RSA-PSS-SHA2-512', 'RSA-PSS-RSAE-SHA1',
    'RSA-PSS-RSAE-SHA2-224', 'RSA-PSS-RSAE-SHA2-256',
    'RSA-PSS-RSAE-SHA2-384', 'RSA-PSS-RSAE-SHA2-512',
    'RSA-PSS-SHA3-224', 'RSA-PSS-SHA3-256',
    'RSA-PSS-SHA3-384', 'RSA-PSS-SHA3-512',
    'RSA-PSS-RSAE-SHA3-256', 'RSA-PSS-RSAE-SHA3-384',
    'RSA-PSS-RSAE-SHA3-512',
    'ECDSA-BRAINPOOLP256-SHA2-256',
    'ECDSA-BRAINPOOLP384-SHA2-384',
    'ECDSA-BRAINPOOLP512-SHA2-512',
    'GOSTR341012-512', 'GOSTR341012-256', 'GOSTR341001',
    *EXPERIMENTAL_SIGN,
)

EXPERIMENTAL_KEY_EXCHANGES = (
    'SNTRUP', 'KEM-ECDH',
)
ALL_KEY_EXCHANGES = (
    'PSK', 'DHE-PSK', 'ECDHE-PSK', 'RSA-PSK', 'ECDHE',
    'RSA', 'DHE', 'DHE-RSA', 'DHE-DSS', 'EXPORT', 'ANON',
    'DH', 'ECDH',
    'VKO-GOST-2001', 'VKO-GOST-2012', 'VKO-GOST-KDF',
    'DHE-GSS', 'ECDHE-GSS',
    *EXPERIMENTAL_KEY_EXCHANGES,
)

# Order matters, see preprocess_text
TLS_PROTOCOLS = ('TLS1.3', 'TLS1.2', 'TLS1.1', 'TLS1.0', 'SSL3.0', 'SSL2.0')
DTLS_PROTOCOLS = ('DTLS1.2', 'DTLS1.0', 'DTLS0.9')
IKE_PROTOCOLS = ('IKEv2', 'IKEv1')
ALL_PROTOCOLS = TLS_PROTOCOLS + DTLS_PROTOCOLS + IKE_PROTOCOLS


ALL = {
    'cipher': ALL_CIPHERS,
    'group': ALL_GROUPS,
    'hash': ALL_HASHES,
    'key_exchange': ALL_KEY_EXCHANGES,
    'mac': ALL_MACS,
    'protocol': ALL_PROTOCOLS,
    'sign': ALL_SIGN,
}

EXPERIMENTAL = {
    'group': EXPERIMENTAL_GROUPS,
    'sign': EXPERIMENTAL_SIGN,
    'key_exchange': EXPERIMENTAL_KEY_EXCHANGES,
}


def glob(pattern, alg_class):
    """
    Lists algorithms matching a glob, in order of appearance in ALL[alg_class].
    For more examples, refer to tests/unit/parsing/test_alg_lists.py
    >>> glob('RC4-*', 'cipher')
    ['RC4-40', 'RC4-128']
    """
    if alg_class not in ALL:
        raise validation.alg_lists.AlgorithmClassUnknownError(alg_class)

    r = fnmatch.filter(ALL[alg_class], pattern)

    if alg_class in EXPERIMENTAL:
        experimental_values = [v for v in r if v in EXPERIMENTAL[alg_class]]
        if experimental_values:
            warnings.warn(validation.alg_lists.ExperimentalValueWarning(
                alg_class, experimental_values,
            ))

    if not r:
        raise validation.alg_lists.AlgorithmEmptyMatchError(pattern, alg_class)
    return r


def earliest_occurrence(needles, ordered_haystack):
    """
    >>> earliest_occurrence('test', 'abcdefghijklmnopqrstuvwxyz')
    'e'
    """
    intersection = [n for n in needles if n in ordered_haystack]
    if not intersection:
        return None
    indices = (ordered_haystack.index(n) for n in intersection)
    return ordered_haystack[min(indices)]


def min_tls_version(versions):
    """
    >>> min_tls_version(['SSL3.0', 'TLS1.2'])
    'SSL3.0'
    """
    return earliest_occurrence(versions, TLS_PROTOCOLS[::-1])


def min_dtls_version(versions):
    """
    >>> min_dtls_version(['DTLS1.2', 'DTLS1.0'])
    'DTLS1.0'
    """
    return earliest_occurrence(versions, DTLS_PROTOCOLS[::-1])


def max_tls_version(versions):
    """
    >>> max_tls_version(['SSL3.0', 'TLS1.2'])
    'TLS1.2'
    """
    return earliest_occurrence(versions, TLS_PROTOCOLS)


def max_dtls_version(versions):
    """
    >>> max_dtls_version(['DTLS1.2', 'DTLS1.0'])
    'DTLS1.2'
    """
    return earliest_occurrence(versions, DTLS_PROTOCOLS)
