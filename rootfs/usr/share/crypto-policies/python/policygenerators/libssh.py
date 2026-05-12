# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2019 Red Hat, Inc.
# Copyright (c) 2019 Tomáš Mráz <tmraz@fedoraproject.org>

from .configgenerator import ConfigGenerator


class LibsshGenerator(ConfigGenerator):
    CONFIG_NAME = 'libssh'

    cipher_map = {
        'AES-256-GCM': 'aes256-gcm@openssh.com',
        'AES-256-CTR': 'aes256-ctr',
        'AES-192-GCM': '',  # not supported
        'AES-192-CTR': 'aes192-ctr',
        'AES-128-GCM': 'aes128-gcm@openssh.com',
        'AES-128-CTR': 'aes128-ctr',
        'CHACHA20-POLY1305': 'chacha20-poly1305@openssh.com',
        'CAMELLIA-256-GCM': '',
        'AES-256-CCM': '',
        'AES-192-CCM': '',
        'AES-128-CCM': '',
        'CAMELLIA-128-GCM': '',
        'AES-256-CBC': 'aes256-cbc',
        'AES-192-CBC': 'aes192-cbc',
        'AES-128-CBC': 'aes128-cbc',
        'CAMELLIA-256-CBC': '',
        'CAMELLIA-128-CBC': '',
        'RC4-128': '',
        'DES-CBC': '',
        'CAMELLIA-128-CTS': '',
        '3DES-CBC': '3des-cbc'
    }

    mac_map_etm = {
        'HMAC-MD5': '',
        'UMAC-64': '',
        'UMAC-128': '',
        'HMAC-SHA1': 'hmac-sha1-etm@openssh.com',
        'HMAC-SHA2-256': 'hmac-sha2-256-etm@openssh.com',
        'HMAC-SHA2-512': 'hmac-sha2-512-etm@openssh.com'
    }

    mac_map = {
        'HMAC-MD5': '',
        'UMAC-64': '',
        'UMAC-128': '',
        'HMAC-SHA1': 'hmac-sha1',
        'HMAC-SHA2-256': 'hmac-sha2-256',
        'HMAC-SHA2-512': 'hmac-sha2-512'
    }

    kx_map = {
        ('ECDHE', 'SECP521R1', 'SHA2-512'): ('ecdh-sha2-nistp521',),
        ('ECDHE', 'SECP384R1', 'SHA2-384'): ('ecdh-sha2-nistp384',),
        ('ECDHE', 'SECP256R1', 'SHA2-256'): ('ecdh-sha2-nistp256',),
        ('ECDHE', 'X25519', 'SHA2-256'): (
            'curve25519-sha256',
            'curve25519-sha256@libssh.org',
        ),
        ('DHE', 'FFDHE-1024', 'SHA1'): ('diffie-hellman-group1-sha1',),
        ('DHE', 'FFDHE-2048', 'SHA1'): ('diffie-hellman-group14-sha1',),
        ('DHE', 'FFDHE-2048', 'SHA2-256'): ('diffie-hellman-group14-sha256',),
        ('DHE', 'FFDHE-4096', 'SHA2-512'): ('diffie-hellman-group16-sha512',),
        ('DHE', 'FFDHE-8192', 'SHA2-512'): ('diffie-hellman-group18-sha512',),
    }

    gx_map = {
        ('DHE', 'SHA1'): ('diffie-hellman-group-exchange-sha1',),
        ('DHE', 'SHA2-256'): ('diffie-hellman-group-exchange-sha256',),
    }

    sign_map = {
        'RSA-SHA1': 'ssh-rsa',
        'DSA-SHA1': 'ssh-dss',
        'RSA-SHA2-256': 'rsa-sha2-256',
        'RSA-SHA2-512': 'rsa-sha2-512',
        'ECDSA-SHA2-256': 'ecdsa-sha2-nistp256',
        'ECDSA-SHA2-256-FIDO': 'sk-ecdsa-sha2-nistp256@openssh.com',
        'ECDSA-SHA2-384': 'ecdsa-sha2-nistp384',
        'ECDSA-SHA2-512': 'ecdsa-sha2-nistp521',
        'EDDSA-ED25519': 'ssh-ed25519',
        'EDDSA-ED25519-FIDO': 'sk-ssh-ed25519@openssh.com',
    }

    sign_map_certs = {
        'RSA-SHA1': 'ssh-rsa-cert-v01@openssh.com',
        'DSA-SHA1': 'ssh-dss-cert-v01@openssh.com',
        'RSA-SHA2-256': 'rsa-sha2-256-cert-v01@openssh.com',
        'RSA-SHA2-512': 'rsa-sha2-512-cert-v01@openssh.com',
        'ECDSA-SHA2-256': 'ecdsa-sha2-nistp256-cert-v01@openssh.com',
        'ECDSA-SHA2-256-FIDO': 'sk-ecdsa-sha2-nistp256-cert-v01@openssh.com',
        'ECDSA-SHA2-384': 'ecdsa-sha2-nistp384-cert-v01@openssh.com',
        'ECDSA-SHA2-512': 'ecdsa-sha2-nistp521-cert-v01@openssh.com',
        'EDDSA-ED25519': 'ssh-ed25519-cert-v01@openssh.com',
        'EDDSA-ED25519-FIDO': 'sk-ssh-ed25519-cert-v01@openssh.com',
    }

    @classmethod
    def generate_config(cls, unscoped_policy):
        policy = unscoped_policy.scoped({'ssh', 'libssh'})
        p = policy.enabled
        cfg = ''
        sep = ','

        s = ''
        for i in p['cipher']:
            try:
                s = cls.append(s, cls.cipher_map[i], sep)
            except KeyError:
                pass

        if s:
            cfg += f'Ciphers {s}\n'

        s = ''
        if policy.enums['etm'] != 'DISABLE_ETM':
            for i in p['mac']:
                try:
                    s = cls.append(s, cls.mac_map_etm[i], sep)
                except KeyError:
                    pass
        if policy.enums['etm'] != 'DISABLE_NON_ETM':
            for i in p['mac']:
                try:
                    s = cls.append(s, cls.mac_map[i], sep)
                except KeyError:
                    pass

        if s:
            cfg += f'MACs {s}\n'

        kxs = []
        for kx in p['key_exchange']:
            for h in p['hash']:
                if policy.integers['arbitrary_dh_groups']:
                    try:
                        kxs.extend(cls.gx_map[(kx, h)])
                    except KeyError:
                        pass
                for g in p['group']:
                    try:
                        kxs.extend(cls.kx_map[(kx, g, h)])
                    except KeyError:
                        pass

        if kxs:
            cfg += f'KexAlgorithms {",".join(kxs)}\n'

        s = ''
        for i in p['sign']:
            try:
                s = cls.append(s, cls.sign_map[i], sep)
            except KeyError:
                pass
            if policy.integers['ssh_certs']:
                try:
                    s = cls.append(s, cls.sign_map_certs[i], sep)
                except KeyError:
                    pass

        if s:
            cfg += f'HostKeyAlgorithms {s}\n'
            cfg += f'PubkeyAcceptedKeyTypes {s}\n'

        return cfg

    @classmethod
    def test_config(cls, config):  # pylint: disable=unused-argument
        return True
