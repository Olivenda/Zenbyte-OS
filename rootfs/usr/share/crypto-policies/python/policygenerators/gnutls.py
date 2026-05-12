# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2019 Red Hat, Inc.
# Copyright (c) 2019 Tomáš Mráz <tmraz@fedoraproject.org>
#
# This generator targets GnuTLS with allowlisting support (newer than 3.7.2)

import os
import textwrap
from subprocess import CalledProcessError, call
from tempfile import mkstemp

from .configgenerator import ConfigGenerator


class GnuTLSGenerator(ConfigGenerator):
    CONFIG_NAME = 'gnutls'

    mac_map = {
        'AEAD': 'AEAD',
        'HMAC-SHA1': 'SHA1',
        'HMAC-MD5': 'MD5',
        'HMAC-SHA2-256': None,  # not allowlisted over concerns that
        'HMAC-SHA2-384': None,  # implementation might be vulnerable to Lucky13
                                # https://gitlab.com/gnutls/gnutls/-/issues/503
        'HMAC-SHA2-512': 'SHA512',
    }

    hash_map = {
        'AEAD': 'AEAD',
        'SHA1': 'SHA1',
        'MD5': 'MD5',
        'SHA2-224': 'SHA224',
        'SHA2-256': 'SHA256',
        'SHA2-384': 'SHA384',
        'SHA2-512': 'SHA512',
        'SHA3-224': 'SHA3-224',
        'SHA3-256': 'SHA3-256',
        'SHA3-384': 'SHA3-384',
        'SHA3-512': 'SHA3-512',
        'SHAKE-128': 'SHAKE-128',
        'SHAKE-256': 'SHAKE-256',
    }

    group_map = {
        'X448': 'GROUP-X448',
        'X25519': 'GROUP-X25519',
        'SECP256R1': 'GROUP-SECP256R1',
        'SECP384R1': 'GROUP-SECP384R1',
        'SECP521R1': 'GROUP-SECP521R1',
        'FFDHE-6144': 'GROUP-FFDHE6144',
        'FFDHE-2048': 'GROUP-FFDHE2048',
        'FFDHE-3072': 'GROUP-FFDHE3072',
        'FFDHE-4096': 'GROUP-FFDHE4096',
        'FFDHE-8192': 'GROUP-FFDHE8192',
        'X25519-MLKEM768': 'GROUP-X25519-MLKEM768',
        'P256-MLKEM768': 'GROUP-SECP256R1-MLKEM768',
    }

    group_curve_map = {
        'X448': 'X448',
        'X25519': 'X25519',
        'SECP224R1': 'SECP224R1',
        'SECP256R1': 'SECP256R1',
        'SECP384R1': 'SECP384R1',
        'SECP521R1': 'SECP521R1',
    }

    sign_curve_map = {
        'EDDSA-ED448': 'Ed448',
        'EDDSA-ED25519': 'Ed25519',
    }

    sign_map = {
        'RSA-MD5': ['RSA-MD5'],
        'RSA-SHA1': ['RSA-SHA1'],
        'DSA-SHA1': ['DSA-SHA1'],
        'ECDSA-SHA1': ['ECDSA-SHA1'],
        'RSA-SHA2-224': ['RSA-SHA224'],
        'DSA-SHA2-224': ['DSA-SHA224'],
        'ECDSA-SHA2-224': ['ECDSA-SHA224'],
        'RSA-SHA2-256': ['RSA-SHA256'],
        'DSA-SHA2-256': ['DSA-SHA256'],
        'ECDSA-SHA2-256': ['ECDSA-SHA256', 'ECDSA-SECP256R1-SHA256'],
        'RSA-SHA2-384': ['RSA-SHA384'],
        'DSA-SHA2-384': ['DSA-SHA384'],
        'ECDSA-SHA2-384': ['ECDSA-SHA384', 'ECDSA-SECP384R1-SHA384'],
        'RSA-SHA2-512': ['RSA-SHA512'],
        'DSA-SHA2-512': ['DSA-SHA512'],
        'ECDSA-SHA2-512': ['ECDSA-SHA512', 'ECDSA-SECP521R1-SHA512'],

        # These are only available under 3.6.3+
        'RSA-PSS-SHA2-256': ['RSA-PSS-SHA256'],
        'RSA-PSS-SHA2-384': ['RSA-PSS-SHA384'],
        'RSA-PSS-SHA2-512': ['RSA-PSS-SHA512'],
        'RSA-PSS-RSAE-SHA2-256': ['RSA-PSS-RSAE-SHA256'],
        'RSA-PSS-RSAE-SHA2-384': ['RSA-PSS-RSAE-SHA384'],
        'RSA-PSS-RSAE-SHA2-512': ['RSA-PSS-RSAE-SHA512'],

        'RSA-SHA3-224': ['RSA-SHA3-224'],
        'DSA-SHA3-224': ['DSA-SHA3-224'],
        'ECDSA-SHA3-224': ['ECDSA-SHA3-224'],
        'RSA-SHA3-256': ['RSA-SHA3-256'],
        'DSA-SHA3-256': ['DSA-SHA3-256'],
        'ECDSA-SHA3-256': ['ECDSA-SHA3-256'],
        'RSA-SHA3-384': ['RSA-SHA3-384'],
        'DSA-SHA3-384': ['DSA-SHA3-384'],
        'ECDSA-SHA3-384': ['ECDSA-SHA3-384'],
        'RSA-SHA3-512': ['RSA-SHA3-512'],
        'DSA-SHA3-512': ['DSA-SHA3-512'],
        'ECDSA-SHA3-512': ['ECDSA-SHA3-512'],

        'EDDSA-ED448': ['EdDSA-Ed448'],
        'EDDSA-ED25519': ['EdDSA-Ed25519'],
    }

    cipher_map = {
        'AES-256-CTR': '',
        'AES-128-CTR': '',
        'AES-256-GCM': 'AES-256-GCM',
        'AES-128-GCM': 'AES-128-GCM',
        'AES-256-CCM': 'AES-256-CCM',
        'AES-128-CCM': 'AES-128-CCM',
        'AES-256-CBC': 'AES-256-CBC',
        'AES-128-CBC': 'AES-128-CBC',
        'CAMELLIA-256-GCM': 'CAMELLIA-256-GCM',
        'CAMELLIA-128-GCM': 'CAMELLIA-128-GCM',
        'CAMELLIA-256-CBC': 'CAMELLIA-256-CBC',
        'CAMELLIA-128-CBC': 'CAMELLIA-128-CBC',
        'CHACHA20-POLY1305': 'CHACHA20-POLY1305',
        '3DES-CBC': '3DES-CBC',
        'RC4-128': 'ARCFOUR-128',
    }

    key_exchange_map = {
        # ECDHE is handled separately as it splits to ECDHE-ECDSA
        # and ECDHE-RSA.
        'ECDHE': ('ECDHE-RSA', 'ECDHE-ECDSA'),
        'RSA': ('RSA',),
        'DHE-RSA': ('DHE-RSA',),
        'DHE-DSS': ('DHE-DSS',),
        # PSK kexes are not allowlisted because enabling them "forces them":
        # * RSA-PSK precludes using TLS 1.3
        # * others make gnutls-cli ask for PSK identity,
        #   users should enable them explicitly anyway
        #   (https://gitlab.com/gnutls/gnutls/-/issues/680)
        # 'PSK': 'PSK',
        # 'DHE-PSK': 'DHE-PSK',
        # 'ECDHE-PSK': 'ECDHE-PSK',
        # 'RSA-PSK': 'RSA-PSK',
    }

    protocol_map = {
        'SSL3.0': 'SSL3.0',
        'TLS1.0': 'TLS1.0',
        'TLS1.1': 'TLS1.1',
        'TLS1.2': 'TLS1.2',
        'TLS1.3': 'TLS1.3',
        'DTLS0.9': 'DTLS0.9',
        'DTLS1.0': 'DTLS1.0',
        'DTLS1.2': 'DTLS1.2'
    }

    @classmethod
    def generate_config(cls, unscoped_policy):
        policy = unscoped_policy.scoped({'tls', 'ssl', 'gnutls'})
        p = policy.enabled

        s = textwrap.dedent('''
            [global]
            override-mode = allowlist

            [overrides]
        ''').lstrip()

        if p['hash']:
            for i in p['hash']:
                try:
                    if cls.hash_map[i]:
                        s += 'secure-hash = '
                        s += cls.hash_map[i]
                        s += '\n'
                except KeyError:
                    pass

        if p['mac']:
            for i in p['mac']:
                try:
                    if cls.mac_map[i]:
                        s += 'tls-enabled-mac = '
                        s += cls.mac_map[i]
                        s += '\n'
                except KeyError:
                    pass

        for i in p['group']:
            if i in cls.group_map:
                s += f'tls-enabled-group = {cls.group_map[i]}\n'

        sigs = [cls.sign_map[i] for i in p['sign'] if i in cls.sign_map]
        for i in sigs:
            for j in i:
                s += f'secure-sig = {j}\n'
        for i in sigs:
            for j in i:
                s += f'secure-sig-for-cert = {j}\n'

        if policy.integers['sha1_in_certs']:
            s += 'secure-sig-for-cert = rsa-sha1\n'
            s += 'secure-sig-for-cert = dsa-sha1\n'
            s += 'secure-sig-for-cert = ecdsa-sha1\n'

        # with allowlisting, curves now need to be enabled separately
        for i in p['group']:
            if i in cls.group_curve_map:
                s += f'enabled-curve = {cls.group_curve_map[i]}\n'
        for i in p['sign']:
            if i in cls.sign_curve_map:
                s += f'enabled-curve = {cls.sign_curve_map[i]}\n'

        if p['cipher']:
            for i in p['cipher']:
                try:
                    if cls.cipher_map[i]:
                        s += 'tls-enabled-cipher = '
                        s += cls.cipher_map[i]
                        s += '\n'
                except KeyError:
                    pass

        for i in p['key_exchange']:
            if i in cls.key_exchange_map:
                for kx in cls.key_exchange_map[i]:
                    s += f'tls-enabled-kx = {kx}\n'

        for i in p['protocol']:
            if i in cls.protocol_map:
                s += f'enabled-version = {cls.protocol_map[i]}\n'

        # option not in Fedora yet, default to True
        if policy.enums['__ems'] == 'ENFORCE':
            s += 'tls-session-hash = require\n'
        elif policy.enums['__ems'] == 'RELAX':
            s += 'tls-session-hash = request\n'
        elif policy.enums['__ems'] == 'DEFAULT':
            pass  # let the library determine a fitting default

        # We cannot separate RSA strength from DH params.
        min_dh_size = policy.integers['min_dh_size']
        min_rsa_size = policy.integers['min_rsa_size']
        if min_dh_size <= 768 or min_rsa_size <= 768:
            s += 'min-verification-profile = very_weak'
        elif min_dh_size <= 1024 or min_rsa_size <= 1024:
            s += 'min-verification-profile = low'
        elif min_dh_size <= 2048 or min_rsa_size <= 2048:
            s += 'min-verification-profile = medium'
        elif min_dh_size <= 3072 or min_rsa_size <= 3072:
            s += 'min-verification-profile = high'
        elif min_dh_size <= 8192 or min_rsa_size <= 8192:
            s += 'min-verification-profile = ultra'
        else:
            s += 'min-verification-profile = future'

        s += '\n\n[priorities]\nSYSTEM=NONE\n'

        return s

    @classmethod
    def test_config(cls, config):
        if os.getenv('OLD_GNUTLS') == '1':
            return True
        if not os.access('/usr/bin/gnutls-cli', os.X_OK):
            return True

        fd, path = mkstemp()

        ret = 255
        try:
            with os.fdopen(fd, 'w') as f:
                f.write(config)
            try:
                os.environ['GNUTLS_SYSTEM_PRIORITY_FILE'] = path
                os.environ['GNUTLS_DEBUG_LEVEL'] = '3'
                os.environ['GNUTLS_SYSTEM_PRIORITY_FAIL_ON_INVALID'] = '1'
                ret = call('/usr/bin/gnutls-cli -l >/dev/null',
                           shell=True)
            except CalledProcessError:
                cls.eprint("/usr/bin/gnutls-cli: Execution failed")
        finally:
            del os.environ['GNUTLS_SYSTEM_PRIORITY_FILE']
            del os.environ['GNUTLS_DEBUG_LEVEL']
            del os.environ['GNUTLS_SYSTEM_PRIORITY_FAIL_ON_INVALID']
            os.unlink(path)

        if ret:
            cls.eprint('There is an error in gnutls generated policy')
            cls.eprint(f'Policy:\n{config}')
            return False
        return True
