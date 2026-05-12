# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2019 Red Hat, Inc.
# Copyright (c) 2019 Tomáš Mráz <tmraz@fedoraproject.org>

from subprocess import CalledProcessError, check_output

from .configgenerator import ConfigGenerator

RH_SHA1_SECTION = '''
[openssl_init]
alg_section = evp_properties

[evp_properties]
rh-allow-sha1-signatures = {}
'''

FIPS_MODULE_CONFIG = '''
[fips_sect]
tls1-prf-ems-check = {}
activate = 1
'''


class OpenSSLGenerator(ConfigGenerator):
    CONFIG_NAME = 'openssl'

    cipher_not_map = {
        'AES-256-CTR': '',
        'AES-128-CTR': '',
        'AES-256-GCM': '-AES256',
        'AES-128-GCM': '-AES128',
        'AES-256-CBC': '-SHA256',
        'AES-128-CBC': '',
        'CHACHA20-POLY1305': '-CHACHA20',
        'SEED-CBC': '-SEED',
        'IDEA-CBC': '!IDEA',
        'DES-CBC': '!DES',
        'RC4-40': '',
        'DES40-CBC': '',
        '3DES-CBC': '-3DES',
        'RC4-128': '!RC4',
        'RC2-CBC': '!RC2',
        'NULL': '!eNULL:!aNULL'
    }
    cipher_notany_multimap = {
        # CBC is documented as SSL_DES, SSL_3DES, SSL_RC2, SSL_IDEA,
        #                      SSL_AES128, SSL_AES256,
        #                      SSL_CAMELLIA128, SSL_CAMELLIA256, SSL_SEED
        '-CBC': {'DES-CBC', '3DES-CBC', 'RC2-CBC', 'IDEA-CBC',
                 'AES-128-CBC', 'AES-256-CBC',
                 'CAMELLIA-128-CBC', 'CAMELLIA-256-CBC', 'SEED-CBC'},
        '-AESCCM': {'AES-128-CCM', 'AES-256-CCM'},
        '-AESGCM': {'AES-128-GCM', 'AES-256-GCM'},
    }

    key_exchange_map = {
        'RSA': 'kRSA',
        'ECDHE': 'kEECDH',
        'PSK': 'kPSK',
        'DHE-PSK': 'kDHEPSK',
        'DHE-RSA': 'kEDH',
        'DHE-DSS': '',
        'ECDHE-PSK': 'kECDHEPSK',
        'RSA-PSK': 'kRSAPSK',
        'VKO-GOST-2012': 'kGOST'
    }

    key_exchange_not_map = {
        'ANON': '',
        'DH': '',
        'ECDH': '',
        'RSA': '-kRSA',
        'ECDHE': '-kEECDH',
        'DHE-RSA': '-aRSA',
        'DHE-DSS': '-aDSS',
        'PSK': '-kPSK',
        'DHE-PSK': '-kDHEPSK',
        'ECDHE-PSK': '-kECDHEPSK',
        'RSA-PSK': '-kRSAPSK'
    }

    mac_not_map = {
        'HMAC-MD5': '!MD5',
        'HMAC-SHA1': '-SHA1'
    }

    ciphersuite_map = {
        'TLS_AES_256_GCM_SHA384': {
            'cipher': {'AES-256-GCM'},
            'hash': {'SHA2-384'},
            'protocol': {'TLS1.3', 'DTLS1.3'},
        },
        'TLS_AES_128_GCM_SHA256': {
            'cipher': {'AES-128-GCM'},
            'hash': {'SHA2-256'},
            'protocol': {'TLS1.3', 'DTLS1.3'},
        },
        'TLS_CHACHA20_POLY1305_SHA256': {
            'cipher': {'CHACHA20-POLY1305'},
            'hash': {'SHA2-256'},
            'protocol': {'TLS1.3', 'DTLS1.3'},
        },
        'TLS_AES_128_CCM_SHA256': {
            'cipher': {'AES-128-CCM'},
            'hash': {'SHA2-256'},
            'protocol': {'TLS1.3', 'DTLS1.3'},
        },
        # This one is not enableable since c-p does not expose CCM8 ciphers:
        # 'TLS_AES_128_CCM_8_SHA256': {
        #     'cipher': {'AES-128-CCM8'},  # this is not a thing in c-p
        #     'hash': {'SHA2-256'},
        #     'protocol': {'TLS1.3', 'DTLS1.3'},
        # },
        'TLS_SHA256_SHA256': {
            'cipher': {'NULL'},
            'hash': {'SHA2-256'},
            'mac': {'SHA2-256'},
            'protocol': {'TLS1.3', 'DTLS1.3'},
        },
        'TLS_SHA256_SHA384': {
            'cipher': {'NULL'},
            'hash': {'SHA2-384'},
            'mac': {'SHA2-384'},
            'protocol': {'TLS1.3', 'DTLS1.3'},
        },
    }

    @classmethod
    def generate_ciphers(cls, policy):
        s = ''
        p = policy.enabled
        ip = policy.disabled
        # We cannot separate RSA strength from DH params.
        min_dh_size = policy.integers['min_dh_size']
        min_rsa_size = policy.integers['min_rsa_size']
        if min_dh_size < 1023 or min_rsa_size < 1023:
            s = cls.append(s, '@SECLEVEL=0')
        elif min_dh_size < 2048 or min_rsa_size < 2048:
            s = cls.append(s, '@SECLEVEL=1')
        elif min_dh_size < 3072 or min_rsa_size < 3072:
            s = cls.append(s, '@SECLEVEL=2')
        else:
            s = cls.append(s, '@SECLEVEL=3')

        for i in p['key_exchange']:
            try:
                s = cls.append(s, cls.key_exchange_map[i])
            except KeyError:
                pass

        for i in ip['key_exchange']:
            try:
                s = cls.append(s, cls.key_exchange_not_map[i])
            except KeyError:
                pass

        for i in ip['cipher']:
            try:
                s = cls.append(s, cls.cipher_not_map[i])
            except KeyError:
                pass
        for keyword, cipherset in cls.cipher_notany_multimap.items():
            if all(c in ip['cipher'] for c in cipherset):
                s = cls.append(s, keyword)

        for i in ip['mac']:
            try:
                s = cls.append(s, cls.mac_not_map[i])
            except KeyError:
                pass

        # These ciphers are not necessary for any
        # policy level, and only increase the attack surface.
        # FIXME! must be fixed for custom policies
        for c in ('-SHA384', '-CAMELLIA', '-ARIA', '-AESCCM8'):
            s = cls.append(s, c)

        return s

    @classmethod
    def generate_ciphersuites(cls, policy):
        s = ''
        p = policy.enabled
        # we need the output sorted by order of `cipher`
        # finer sorting nuances are currently ignored
        for c in p['cipher']:
            cipher_submap = {name: spec for name, spec
                             in cls.ciphersuite_map.items()
                             if spec['cipher'] == {c}}
            for ciphersuite_name, ciphersuite_spec in cipher_submap.items():
                if all(any(val in algvalues for val in p[algclass])
                       for algclass, algvalues in ciphersuite_spec.items()):
                    s = cls.append(s, ciphersuite_name)
        return s

    @classmethod
    def generate_config(cls, unscoped_policy):
        policy = unscoped_policy.scoped({'tls', 'ssl', 'openssl'})
        return cls.generate_ciphers(policy) + '\n'

    @classmethod
    def test_config(cls, config):
        output = b''
        assert config.endswith('\n')  # noqa: S101
        try:
            output = check_output(['openssl',  # noqa: S607
                                   'ciphers', config[:-1]])
        except CalledProcessError:
            cls.eprint('There is an error in openssl generated policy')
            cls.eprint(f'Policy:\n{config}')
            return False
        except OSError:
            # Ignore missing openssl
            return True
        if b'NULL' in output or b'ADH' in output:
            cls.eprint('There is NULL or ADH in openssl generated policy')
            cls.eprint(f'Policy:\n{config}')
            return False
        return True


class OpenSSLConfigGenerator(OpenSSLGenerator):
    CONFIG_NAME = 'opensslcnf'

    # has to cover everything c-p has
    protocol_map = {
        'SSL3.0': 'SSLv3',
        'TLS1.0': 'TLSv1',
        'TLS1.1': 'TLSv1.1',
        'TLS1.2': 'TLSv1.2',
        'TLS1.3': 'TLSv1.3',
        'DTLS0.9': 'DTLSv0.9',
        'DTLS1.0': 'DTLSv1',
        'DTLS1.2': 'DTLSv1.2'
    }

    sign_map = {
        'RSA-SHA1': 'RSA+SHA1',
        'DSA-SHA1': 'DSA+SHA1',
        'ECDSA-SHA1': 'ECDSA+SHA1',
        'RSA-SHA2-224': 'RSA+SHA224',
        'DSA-SHA2-224': 'DSA+SHA224',
        'ECDSA-SHA2-224': 'ECDSA+SHA224',
        'RSA-SHA2-256': 'RSA+SHA256',
        'DSA-SHA2-256': 'DSA+SHA256',
        'ECDSA-SHA2-256': 'ECDSA+SHA256',
        'RSA-SHA2-384': 'RSA+SHA384',
        'DSA-SHA2-384': 'DSA+SHA384',
        'ECDSA-SHA2-384': 'ECDSA+SHA384',
        'RSA-SHA2-512': 'RSA+SHA512',
        'DSA-SHA2-512': 'DSA+SHA512',
        'ECDSA-SHA2-512': 'ECDSA+SHA512',
        'RSA-PSS-SHA2-256': 'rsa_pss_pss_sha256',
        'RSA-PSS-SHA2-384': 'rsa_pss_pss_sha384',
        'RSA-PSS-SHA2-512': 'rsa_pss_pss_sha512',
        'RSA-PSS-RSAE-SHA2-256': 'rsa_pss_rsae_sha256',
        'RSA-PSS-RSAE-SHA2-384': 'rsa_pss_rsae_sha384',
        'RSA-PSS-RSAE-SHA2-512': 'rsa_pss_rsae_sha512',
        'EDDSA-ED25519': 'ed25519',
        'EDDSA-ED448': 'ed448',
        # optional to not hard-require newer openssl version
        'ECDSA-BRAINPOOLP256-SHA2-256': '?ecdsa_brainpoolP256r1_sha256',
        'ECDSA-BRAINPOOLP384-SHA2-384': '?ecdsa_brainpoolP384r1_sha384',
        'ECDSA-BRAINPOOLP512-SHA2-512': '?ecdsa_brainpoolP512r1_sha512',
        # provider-only, so, optional (openssl#23050) + marked experimental
        'MLDSA44': '?mldsa44',
        'P256-MLDSA44': '?p256_mldsa44',
        'RSA3072-MLDSA44': '?rsa3072_mldsa44',
        'MLDSA44-PSS2048': '?mldsa44_pss2048',
        'MLDSA44-RSA2048': '?mldsa44_rsa2048',
        'MLDSA44-ED25519': '?mldsa44_ed25519',
        'MLDSA44-P256': '?mldsa44_p256',
        'MLDSA44-BP256': '?mldsa44_bp256',
        'MLDSA65': '?mldsa65',
        'P384-MLDSA65': '?p384_mldsa65',
        'MLDSA65-PSS3072': '?mldsa65_pss3072',
        'MLDSA65-RSA3072': '?mldsa65_rsa3072',
        'MLDSA65-P256': '?mldsa65_p256',
        'MLDSA65-BP256': '?mldsa65_bp256',
        'MLDSA65-ED25519': '?mldsa65_ed25519',
        'MLDSA87': '?mldsa87',
        'P521-MLDSA87': '?p521_mldsa87',
        'MLDSA87-P384': '?mldsa87_p384',
        'MLDSA87-BP384': '?mldsa87_bp384',
        'MLDSA87-ED448': '?mldsa87_ed448',
        'FALCON512': '?falcon512',
        'P256-FALCON512': '?p256_falcon512',
        'RSA3072-FALCON512': '?rsa3072_falcon512',
        'FALCONPADDED512': '?falconpadded512',
        'P256-FALCONPADDED512': '?p256_falconpadded512',
        'RSA3072-FALCONPADDED512': '?rsa3072_falconpadded512',
        'FALCON1024': '?falcon1024',
        'P521-FALCON1024': '?p521_falcon1024',
        'FALCONPADDED1024': '?falconpadded1024',
        'P521-FALCONPADDED1024': '?p521_falconpadded1024',
        'SPHINCSSHA2128FSIMPLE': '?sphincssha2128fsimple',
        'P256-SPHINCSSHA2128FSIMPLE': '?p256_sphincssha2128fsimple',
        'RSA3072-SPHINCSSHA2128FSIMPLE': '?rsa3072_sphincssha2128fsimple',
        'SPHINCSSHA2128SSIMPLE': '?sphincssha2128ssimple',
        'P256-SPHINCSSHA2128SSIMPLE': '?p256_sphincssha2128ssimple',
        'RSA3072-SPHINCSSHA2128SSIMPLE': '?rsa3072_sphincssha2128ssimple',
        'SPHINCSSHA2192FSIMPLE': '?sphincssha2192fsimple',
        'P384-SPHINCSSHA2192FSIMPLE': '?p384_sphincssha2192fsimple',
        'SPHINCSSHAKE128FSIMPLE': '?sphincsshake128fsimple',
        'P256-SPHINCSSHAKE128FSIMPLE': '?p256_sphincsshake128fsimple',
        'RSA3072-SPHINCSSHAKE128FSIMPLE': '?rsa3072_sphincsshake128fsimple',
    }

    group_map = {
        'SECP224R1': 'secp224r1',
        'SECP256R1': 'secp256r1',
        'SECP384R1': 'secp384r1',
        'SECP521R1': 'secp521r1',
        'X25519': 'X25519',
        'X448': 'X448',
        'FFDHE-2048': 'ffdhe2048',
        'FFDHE-3072': 'ffdhe3072',
        'FFDHE-4096': 'ffdhe4096',
        'FFDHE-6144': 'ffdhe6144',
        'FFDHE-8192': 'ffdhe8192',
        'BRAINPOOL-P256R1': 'brainpoolP256r1:?brainpoolP256r1tls13',
        'BRAINPOOL-P384R1': 'brainpoolP384r1:?brainpoolP384r1tls13',
        'BRAINPOOL-P512R1': 'brainpoolP512r1:?brainpoolP512r1tls13',
        # provider-only, so, optional (openssl#23050) + marked experimental
        'KYBER768': '?kyber768',
        'X25519-KYBER768': '?x25519_kyber768',
        'P256-KYBER768': '?p256_kyber768',
        'MLKEM512': '?mlkem512',
        'P256-MLKEM512': '?p256_mlkem512',
        'X25519-MLKEM512': '?x25519_mlkem512',
        'MLKEM768': '?mlkem768',
        'P384-MLKEM768': '?p384_mlkem768',
        'X448-MLKEM768': '?x448_mlkem768',
        'X25519-MLKEM768': '?X25519MLKEM768:?x25519_mlkem768',  # old/new name
        'P256-MLKEM768': '?SecP256r1MLKEM768:?p256_mlkem768',  # old/new name
        'MLKEM1024': '?mlkem1024',
        'P521-MLKEM1024': '?p521_mlkem1024',
        'P384-MLKEM1024': '?p384_mlkem1024',
    }

    @classmethod
    def generate_config(cls, unscoped_policy):
        policy = unscoped_policy.scoped({'tls', 'ssl', 'openssl'})
        p = policy.enabled
        # This includes the seclevel
        s = f'CipherString = {cls.generate_ciphers(policy)}\n'
        s += f'Ciphersuites = {cls.generate_ciphersuites(policy)}\n'

        if policy.min_tls_version:
            s += 'TLS.MinProtocol ='
            s += f' {cls.protocol_map[policy.min_tls_version]}\n'
        if policy.max_tls_version:
            s += 'TLS.MaxProtocol ='
            s += f' {cls.protocol_map[policy.max_tls_version]}\n'
        if policy.min_dtls_version:
            s += 'DTLS.MinProtocol ='
            s += f' {cls.protocol_map[policy.min_dtls_version]}\n'
        if policy.max_dtls_version:
            s += 'DTLS.MaxProtocol ='
            s += f' {cls.protocol_map[policy.max_dtls_version]}\n'

        sig_algs = [cls.sign_map[i] for i in p['sign'] if i in cls.sign_map]
        s += 'SignatureAlgorithms = ' + ':'.join(sig_algs) + '\n'

        groups = [cls.group_map[i] for i in p['group'] if i in cls.group_map]
        s += 'Groups = ' + ':'.join(groups) + '\n'

        if policy.enums['__ems'] == 'RELAX':
            s += 'Options = RHNoEnforceEMSinFIPS\n'

        # In the future it'll be just
        # s += RH_SHA1_SECTION.format('yes' if 'SHA1' in p['hash'] else 'no')
        # but for now we slow down the roll-out and we have
        sha1_sig = not policy.integers['__openssl_block_sha1_signatures']
        s += RH_SHA1_SECTION.format('yes' if sha1_sig else 'no')

        return s

    @classmethod
    def test_config(cls, config):  # pylint: disable=unused-argument
        return True


class OpenSSLFIPSGenerator(ConfigGenerator):
    CONFIG_NAME = 'openssl_fips'

    @classmethod
    def generate_config(cls, unscoped_policy):
        policy = unscoped_policy.scoped({'tls', 'ssl', 'openssl'})
        # OpenSSL EMS relaxation is special
        # in that it uses a separate FIPS module config
        # and, just in case, EMS is enforcing by default.
        # It only puts `= 0` there if it's explicitly relaxed.
        # That's the reason why `__ems` is a tri-state enum.
        return FIPS_MODULE_CONFIG.format(int(policy.enums['__ems'] != 'RELAX'))

    @classmethod
    def test_config(cls, config):  # pylint: disable=unused-argument
        return True
