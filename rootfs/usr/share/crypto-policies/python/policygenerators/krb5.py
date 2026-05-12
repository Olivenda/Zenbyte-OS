# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2019 Red Hat, Inc.
# Copyright (c) 2019 Tomáš Mráz <tmraz@fedoraproject.org>

from .configgenerator import ConfigGenerator


class KRB5Generator(ConfigGenerator):
    CONFIG_NAME = 'krb5'

    cipher_map = {
        'CAMELLIA-256-CBC': 'camellia256-cts-cmac',
        'CAMELLIA-128-CBC': 'camellia128-cts-cmac',
        'CAMELLIA-128-CTS': 'camellia128-cts-cmac',
        # RC4 is enabled separately
    }

    cipher_mac_map = {
        'AES-256-CBC-HMAC-SHA1': 'aes256-cts-hmac-sha1-96',
        'AES-256-CBC-HMAC-SHA2-384': 'aes256-cts-hmac-sha384-192',
        'AES-128-CBC-HMAC-SHA1': 'aes128-cts-hmac-sha1-96',
        'AES-128-CBC-HMAC-SHA2-256': 'aes128-cts-hmac-sha256-128',
        # RC4 is enabled separately
    }

    @classmethod
    def generate_config(cls, unscoped_policy):
        policy = unscoped_policy.scoped({'kerberos', 'krb5'})
        p = policy.enabled
        sep = ' '

        cfg = '[libdefaults]\n'
        cfg += 'permitted_enctypes = '
        s = ''
        for j in p['mac']:
            for i in p['cipher']:
                try:
                    s = cls.append(s, cls.cipher_mac_map[i + '-' + j], sep)
                except KeyError:
                    pass

        for i in p['cipher']:
            try:
                s = cls.append(s, cls.cipher_map[i], sep)
            except KeyError:
                pass

        if 'RC4-128' in p['cipher'] and 'MD5' in p['hash']:
            s = cls.append(s, 'arcfour-hmac-md5', sep)

        cfg += s + '\n'

        # By default libkrb5 sets the min_bits to 2048, don't
        # go lower than that.
        if policy.integers['min_dh_size'] > 2048:
            # $string .= "pkinit_dh_min_bits=$min_dh_size\n";
            # krb5.conf only accepts 2048 or 4096
            cfg += 'pkinit_dh_min_bits=4096\n'

        return cfg

    @classmethod
    def test_config(cls, config):  # pylint: disable=unused-argument
        return True
