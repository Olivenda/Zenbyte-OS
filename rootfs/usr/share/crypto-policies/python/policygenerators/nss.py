# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2019 Red Hat, Inc.
# Copyright (c) 2019 Tomáš Mráz <tmraz@fedoraproject.org>

import collections
import ctypes
import ctypes.util
import os
from subprocess import CalledProcessError, call
from tempfile import mkstemp

from .configgenerator import ConfigGenerator

NSS_P11_KIT_PROXY = '''
library=p11-kit-proxy.so
name=p11-kit-proxy
'''

# policy mapping as of 3.101 (lib/pk11wrap/pk11pars.c):
#
# ssl: NSS_USE_ALG_IN_SSL
# ssl-key-exchange: NSS_USE_ALG_IN_SSL_KX
# key-exchange: NSS_USE_ALG_IN_KEY_EXCHANGE
# cert-signature: NSS_USE_ALG_IN_CERT_SIGNATURE
# smime-signature, cms-signature: NSS_USE_ALG_IN_SMIME_SIGNATURE
# all-signature: NSS_USE_ALG_IN_SIGNATURE (cert-, smime-, signature)
# pkcs12: NSS_USE_ALG_IN_PKCS12  # (-legacy | encrypt)
# pkcs12-legacy: NSS_USE_ALG_IN_PKCS12_DECRYPT  # for use in allow
# pkcs12-encrypt: NSS_USE_ALG_IN_PKCS12_ENCRYPT  # for use in disallow
# smime: NSS_USE_ALG_IN_SMIME  # (-legacy | -encrypt)
# smime-legacy: NSS_USE_ALG_IN_SMIME_LEGACY  # for use in allow
# smime-encrypt: NSS_USE_ALG_IN_SMIME_ENCRYPT  # for use in disallow
# smime-key-exchange: NSS_USE_ALG_IN_SMIME_KX  # (-legacy | -encrypt})
# smime-key-exchange-legacy: NSS_USE_ALG_IN_SMIME_KX_LEGACY  # for use in allow
# smime-key-exchange-encrypt: NSS_USE_ALG_IN_SMIME_KX_ENCRYPT  # for disallow
# signature: NSS_USE_ALG_IN_ANY_SIGNATURE  # same as all-signature in disallow
#                                          # is needed for smime and cert
# legacy: NSS_USE_ALG_IN_PKCS12_DECRYPT
#       | NSS_USE_ALG_IN_SMIME_LEGACY
#       | NSS_USE_ALG_IN_SMIME_KX_LEGACY
# all: NSS_USE_ALG_IN_SSL        # (ssl)
#    | NSS_USE_ALG_IN_SSL_KX     # (ssl-key-exchange)
#    | NSS_USE_ALG_IN_PKCS12     # (pkcs12)
#    | NSS_USE_ALG_IN_SMIME      # (smime)
#    | NSS_USE_ALG_IN_SIGNATURE  # (signature)
#    | NSS_USE_ALG_IN_SMIME_KX   # (smime-key-exchange)
# none: 0


class PurposeDeduplicator:
    """Shorten the list of enabled algorithm/purpose pairs.

    For example, given a rule set of
    * ('pkcs12', 'pkcs12-legacy') -> 'pkcs12'
    * ('ssl', 'pkcs12') -> 'all'
    it'll shorten
      ('hmac-md5', 'pkcs12'), ('hmac-md5', 'pkcs12-legacy'), 'hmac-md5', 'ssl')
    to just 'hmac-md5'.
    The order of the first insertion is preserved.

    It was decided to be OK to merge all currently known purposes into '/all',
    even though in the future there could be more purposes added, since:
    1. the list of purposes doesn't change often, and then we should update c-p
    2. this will only result in overenablement for algorithms
       that the administrator already trusts for all currently known purposes,
       and it's likely that they trust it for the new purpose as well
    """

    def __init__(self, rulemap):
        self._alg_purpose_map = collections.defaultdict(list)
        self._rulemap = rulemap

    def add(self, alg, purpose):
        self._alg_purpose_map[alg].append(purpose)

    @staticmethod
    def _deduplicate_single_rule(purpose_list,
                                 purposes_separate, purpose_combined):
        new_purpose_list = []
        for purpose in purpose_list:
            if 'all' in new_purpose_list:
                continue
            if purpose in new_purpose_list:
                continue
            match = (purpose in purposes_separate
                     and all(p in purpose_list for p in purposes_separate))
            if match:
                if purpose_combined in new_purpose_list:
                    continue
                new_purpose_list.append(purpose_combined)
            else:
                new_purpose_list.append(purpose)
        return new_purpose_list

    def _deduplicate_purpose_list(self, purpose_list):
        prev_purpose_list = None
        while purpose_list != prev_purpose_list:
            for purposes_separate, purpose_combined in self._rulemap.items():
                purpose_list = self._deduplicate_single_rule(purpose_list,
                                                             purposes_separate,
                                                             purpose_combined)
            prev_purpose_list = purpose_list
        return purpose_list

    def deduplicated(self):
        deduplicated_alg_purpose_map = {
            alg: self._deduplicate_purpose_list(purpose_list)
            for alg, purpose_list in self._alg_purpose_map.items()
        }
        return (f'{alg}/{",".join(purposes)}' if purposes != ['all'] else alg
                for alg, purposes in deduplicated_alg_purpose_map.items())


class NSSGenerator(ConfigGenerator):
    CONFIG_NAME = 'nss'

    mac_map = {
        'HMAC-SHA1': 'HMAC-SHA1',
        'HMAC-MD5': 'HMAC-MD5',
        'HMAC-SHA2-224': 'HMAC-SHA224',
        'HMAC-SHA2-256': 'HMAC-SHA256',
        'HMAC-SHA2-384': 'HMAC-SHA384',
        'HMAC-SHA2-512': 'HMAC-SHA512',
        'HMAC-SHA3-224': 'HMAC-SHA3-224',
        'HMAC-SHA3-256': 'HMAC-SHA3-256',
        'HMAC-SHA3-384': 'HMAC-SHA3-384',
        'HMAC-SHA3-512': 'HMAC-SHA3-512',
    }

    hash_map = {
        'MD2': 'MD2',
        'MD4': 'MD4',
        'MD5': 'MD5',
        'SHA1': 'SHA1',
        'SHA2-224': 'SHA224',
        'SHA2-256': 'SHA256',
        'SHA2-384': 'SHA384',
        'SHA2-512': 'SHA512',
        'SHA3-224': 'SHA3-224',
        'SHA3-256': 'SHA3-256',
        'SHA3-384': 'SHA3-384',
        'SHA3-512': 'SHA3-512',
        'SHAKE-128': None,  # not present as of 3.101
        'SHAKE-256': None,  # not present as of 3.101
    }

    curve_map = {
        # PRIME*, SECP<256, SECP*K1, C2TNB*, SECT*: skip
        'X25519': 'CURVE25519',
        'X448': None,  # not present as of 3.101
        'SECP256R1': 'SECP256R1',
        'SECP384R1': 'SECP384R1',
        'SECP521R1': 'SECP521R1',
        'X25519-MLKEM768': 'mlkem768x25519',
        # not yet recognized as of nss-3.105.0-1.fc42
        # 'P256-MLKEM768': 'mlkem768secp256r1',
    }

    cipher_map = {
        'AES-256-GCM': ('aes256-gcm',),
        'AES-192-GCM': ('aes192-gcm',),
        'AES-128-GCM': ('aes128-gcm',),
        'AES-256-CBC': ('aes256-cbc',),
        'AES-192-CBC': ('aes192-cbc',),
        'AES-128-CBC': ('aes128-cbc',),
        'CAMELLIA-256-CBC': ('camellia256-cbc',),
        'CAMELLIA-192-CBC': ('camellia192-cbc',),
        'CAMELLIA-128-CBC': ('camellia128-cbc',),
        'CHACHA20-POLY1305': ('chacha20-poly1305',),
        'SEED-CBC': ('seed-cbc',),
        '3DES-CBC': ('des-ede3-cbc',),
        'DES40-CBC': ('des-40-cbc',),
        'DES-CBC': ('des-cbc',),
        'RC4-128': ('rc4',),
        'RC2-CBC': ('rc2', 'rc2-40-cbc', 'rc2-64-cbc', 'rc2-128-cbc'),
        'IDEA': ('idea',),
        'NULL': (),  # hope nobody ever needs it
    }

    key_exchange_ssl_map = {
        'RSA': ('RSA',),
        'DHE-RSA': ('DHE-RSA',),
        'DHE-DSS': ('DHE-DSS',),
        'ECDHE': ('ECDHE-RSA', 'ECDHE-ECDSA'),
        'ECDH': ('ECDH-RSA', 'ECDH-ECDSA'),
        'DH': ('DH-RSA', 'DH-DSS'),
    }
    key_exchange_smime_map = {
        'RSA': ('RSA-PKCS', 'RSA-OAEP'),
        'ECDH': ('ECDH',),
        'DH': ('DH',),
    }

    protocol_map = {
        'SSL3.0': 'ssl3.0',
        'TLS1.0': 'tls1.0',
        'TLS1.1': 'tls1.1',
        'TLS1.2': 'tls1.2',
        'TLS1.3': 'tls1.3',
        'DTLS1.0': 'dtls1.0',
        'DTLS1.2': 'dtls1.2'
    }

    # Depends on a dict being ordered
    sign_prefix_ordmap = {
        'RSA-PSS-': 'RSA-PSS',  # must come before RSA-
        'RSA-': 'RSA-PKCS',
        'ECDSA-': 'ECDSA',
        'DSA-': 'DSA',
        'EDDSA-ED25519': 'ED25519',
    }
    sign_suffix_ordmap = {
        '-MD5': 'MD5',
        '-SHA1': 'SHA1',
        '-SHA2-224': 'SHA224',
        '-SHA2-256': 'SHA256',
        '-SHA2-384': 'SHA384',
        '-SHA2-512': 'SHA512',
        '-SHA3-224': 'SHA3-224',
        '-SHA3-256': 'SHA3-256',
        '-SHA3-384': 'SHA3-384',
        '-SHA3-512': 'SHA3-512',
    }

    @classmethod
    def generate_config(cls, unscoped_policy):
        ssl_policy = unscoped_policy.scoped({'tls', 'ssl', 'nss', 'nss-tls'})
        pkcs12_import_policy = unscoped_policy.scoped({
            'nss', 'pkcs12-import', 'nss-pkcs12-import', 'pkcs12', 'nss-pkcs12'
        })
        pkcs12_export_import_policy = unscoped_policy.scoped({
            'nss', 'pkcs12', 'nss-pkcs12'
        })
        smime_import_policy = unscoped_policy.scoped({
            'nss', 'smime-import', 'nss-smime-import', 'smime', 'nss-smime'
        })
        smime_export_import_policy = unscoped_policy.scoped({
            'nss', 'smime', 'nss-smime'
        })

        # including it unconditionally, because Fedora NSS depends on p11-kit
        cfg = NSS_P11_KIT_PROXY.lstrip() + '\n\n'

        cfg += 'library=\n'
        cfg += 'name=Policy\n'
        cfg += 'NSS=flags=policyOnly,moduleDB\n'
        cfg += 'config="disallow=ALL allow='

        r = []
        macs_and_purposes = PurposeDeduplicator({
            # possible uses as of 3.101: ssl, pkcs12{,-legacy}
            ('pkcs12', 'pkcs12-legacy'): 'pkcs12',
            ('ssl', 'pkcs12'): 'all',
        })
        for i in ssl_policy.enabled['mac']:
            if (mac_alg := cls.mac_map.get(i)) is not None:
                macs_and_purposes.add(mac_alg, 'ssl')
        for i in pkcs12_export_import_policy.enabled['mac']:
            if (mac_alg := cls.mac_map.get(i)) is not None:
                macs_and_purposes.add(mac_alg, 'pkcs12')
        for i in pkcs12_import_policy.enabled['mac']:
            if (mac_alg := cls.mac_map.get(i)) is not None:
                macs_and_purposes.add(mac_alg, 'pkcs12-legacy')
        r.extend(macs_and_purposes.deduplicated())

        groups_and_purposes = PurposeDeduplicator({
            # possible uses as of 3.101: ssl-key-exchange, cert-signature
            # and cert-signature requires signature
            ('ssl-key-exchange', 'cert-signature', 'signature'): 'all',
        })
        for i in ssl_policy.enabled['group']:
            if (group_alg := cls.curve_map.get(i)) is not None:
                groups_and_purposes.add(group_alg, 'ssl-key-exchange')
                groups_and_purposes.add(group_alg, 'cert-signature')
                groups_and_purposes.add(group_alg, 'signature')
        r.extend(groups_and_purposes.deduplicated())

        ciphers_and_purposes = PurposeDeduplicator({
            # possible uses as of 3.101: ssl, pkcs12{,-legacy}, smime{,-legacy}
            ('pkcs12', 'pkcs12-legacy'): 'pkcs12',
            ('smime', 'smime-legacy'): 'smime',
            ('ssl', 'pkcs12', 'smime'): 'all',
        })
        for i in ssl_policy.enabled['cipher']:
            for cipher_alg in cls.cipher_map.get(i, ()):
                ciphers_and_purposes.add(cipher_alg, 'ssl')
        for i in pkcs12_export_import_policy.enabled['cipher']:
            for cipher_alg in cls.cipher_map.get(i, ()):
                ciphers_and_purposes.add(cipher_alg, 'pkcs12')
        for i in pkcs12_import_policy.enabled['cipher']:
            for cipher_alg in cls.cipher_map.get(i, ()):
                ciphers_and_purposes.add(cipher_alg, 'pkcs12-legacy')
        for i in smime_export_import_policy.enabled['cipher']:
            for cipher_alg in cls.cipher_map.get(i, ()):
                ciphers_and_purposes.add(cipher_alg, 'smime')
        for i in smime_import_policy.enabled['cipher']:
            for cipher_alg in cls.cipher_map.get(i, ()):
                ciphers_and_purposes.add(cipher_alg, 'smime-legacy')
        r.extend(ciphers_and_purposes.deduplicated())

        hashes_and_purposes = PurposeDeduplicator({
            # possible uses as of 3.101:
            # ssl-key-exchange, {cert-,smime-,}signature, pkcs12{,-legacy}
            # either of cert-signature and smime-signature requires signature
            ('pkcs12', 'pkcs12-legacy'): 'pkcs12',
            ('cert-signature', 'smime-signature', 'signature'):
                'all-signature',
            ('ssl-key-exchange', 'all-signature', 'pkcs12'): 'all',
        })
        for i in ssl_policy.enabled['hash']:
            if (hash_alg := cls.hash_map.get(i)) is not None:
                hashes_and_purposes.add(hash_alg, 'ssl-key-exchange')
        for i in pkcs12_export_import_policy.enabled['hash']:
            if (hash_alg := cls.hash_map.get(i)) is not None:
                hashes_and_purposes.add(hash_alg, 'pkcs12')
        for i in pkcs12_import_policy.enabled['hash']:
            if (hash_alg := cls.hash_map.get(i)) is not None:
                hashes_and_purposes.add(hash_alg, 'pkcs12-legacy')
        # but for signature purposes, we'd better look at `sign`
        for i in ssl_policy.enabled['sign']:
            for suffix, sighashalg in cls.sign_suffix_ordmap.items():
                if i.endswith(suffix):
                    hashes_and_purposes.add(sighashalg, 'cert-signature')
                    hashes_and_purposes.add(sighashalg, 'signature')
        for i in smime_export_import_policy.enabled['sign']:
            for suffix, sighashalg in cls.sign_suffix_ordmap.items():
                if i.endswith(suffix):
                    hashes_and_purposes.add(sighashalg, 'smime-signature')
        for i in smime_import_policy.enabled['key_exchange']:
            for suffix, sighashalg in cls.sign_suffix_ordmap.items():
                if i.endswith(suffix):
                    hashes_and_purposes.add(sighashalg,
                                            'smime-signature-legacy')
        r.extend(hashes_and_purposes.deduplicated())

        kex_and_purposes = PurposeDeduplicator({
            # possible uses as of 3.101:
            # ssl-key-exchange, smime-key-exchange{,-legacy}
            ('smime-key-exchange', 'smime-key-exchange-legacy'):
                'smime-key-exchange',
            ('ssl-key-exchange', 'smime-key-exchange'): 'all',
        })
        for i in ssl_policy.enabled['key_exchange']:
            for kex_alg in cls.key_exchange_ssl_map.get(i, ()):
                kex_and_purposes.add(kex_alg, 'ssl-key-exchange')
        for i in smime_import_policy.enabled['key_exchange']:
            for kex_alg in cls.key_exchange_smime_map.get(i, ()):
                kex_and_purposes.add(kex_alg, 'smime-key-exchange-legacy')
        for i in smime_export_import_policy.enabled['key_exchange']:
            for kex_alg in cls.key_exchange_smime_map.get(i, ()):
                kex_and_purposes.add(kex_alg, 'smime-key-exchange')
        r.extend(kex_and_purposes.deduplicated())

        sigalgs_and_purposes = PurposeDeduplicator({
            # possible uses as of 3.101:
            # ssl-key-exchange, {cert-,smime-,}signature
            ('cert-signature', 'smime-signature', 'signature'):
                'all-signature',
            ('ssl-key-exchange', 'all-signature'): 'all',
        })
        for i in ssl_policy.enabled['sign']:
            for prefix, sigalg in cls.sign_prefix_ordmap.items():
                if i.startswith(prefix):
                    sigalgs_and_purposes.add(sigalg, 'ssl-key-exchange')
                    sigalgs_and_purposes.add(sigalg, 'cert-signature')
                    sigalgs_and_purposes.add(sigalg, 'signature')
        for i in smime_export_import_policy.enabled['sign']:
            for prefix, sigalg in cls.sign_prefix_ordmap.items():
                if i.startswith(prefix):
                    sigalgs_and_purposes.add(sigalg, 'smime-signature')
                    sigalgs_and_purposes.add(sigalg, 'signature')
        r.extend(sigalgs_and_purposes.deduplicated())

        # option not in Fedora yet, default to True
        no_tls_require_ems = os.getenv('NSS_NO_TLS_REQUIRE_EMS', '1') == '1'
        if ssl_policy.enums['__ems'] == 'ENFORCE' and not no_tls_require_ems:
            r.append('TLS-REQUIRE-EMS')

        if ssl_policy.min_tls_version:
            minver = cls.protocol_map[ssl_policy.min_tls_version]
            r.append('tls-version-min=' + minver)
        else:  # FIXME, preserving behaviour, but this is wrong
            r.append('tls-version-min=0')

        if ssl_policy.min_dtls_version:
            minver = cls.protocol_map[ssl_policy.min_dtls_version]
            r.append('dtls-version-min=' + minver)
        else:  # FIXME, preserving behaviour, but this is wrong
            r.append('dtls-version-min=0')

        r.append(f'DH-MIN={ssl_policy.integers["min_dh_size"]}')
        r.append(f'DSA-MIN={ssl_policy.integers["min_dsa_size"]}')
        r.append(f'RSA-MIN={ssl_policy.integers["min_rsa_size"]}')

        cfg += ':'.join(r) + '"\n'

        return cfg

    @classmethod
    def test_config(cls, config):
        nss_path = ctypes.util.find_library('nss3')
        nss_lib = ctypes.CDLL(nss_path)

        nss_lax = os.getenv('NSS_LAX', '0') == '1'
        nss_is_lax_by_default = True
        try:
            if not nss_lib.NSS_VersionCheck(b'3.80'):
                # NSS older than 3.80 uses strict config checking.
                # 3.80 and newer ignores new keywords by default
                # and needs extra switches to be strict.
                nss_is_lax_by_default = False
        except AttributeError:
            cls.eprint('Cannot determine nss version with ctypes, '
                       'assuming >=3.80')
        options = ('-f value -f identifier'
                   if nss_is_lax_by_default and not nss_lax else '')

        fd, path = mkstemp()

        ret = 255
        try:
            with os.fdopen(fd, 'w') as f:
                f.write(config)
            try:
                ret = call(f'/usr/bin/nss-policy-check {options} {path}'
                           '>/dev/null',
                           shell=True)
            except CalledProcessError:
                cls.eprint("/usr/bin/nss-policy-check: Execution failed")
        finally:
            os.unlink(path)

        if ret == 2:
            cls.eprint("There is a warning in NSS generated policy")
            cls.eprint(f'Policy:\n{config}')
            return False
        if ret:
            cls.eprint("There is an error in NSS generated policy")
            cls.eprint(f'Policy:\n{config}')
            return False
        return True
