# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2019 Red Hat, Inc.
# Copyright (c) 2019 Tomáš Mráz <tmraz@fedoraproject.org>

import os
from subprocess import CalledProcessError, check_output
from tempfile import mkstemp

from .configgenerator import ConfigGenerator


class BindGenerator(ConfigGenerator):
    CONFIG_NAME = 'bind'

    RELOAD_CMD = ('systemctl try-reload-or-restart bind.service 2>/dev/null '
                  '|| :\n')

    sign_not_map = {
        'DSA-SHA1': ('DSA', 'NSEC3DSA'),
        'RSA-SHA1': ('RSASHA1', 'NSEC3RSASHA1'),
        'RSA-SHA2-256': ('RSASHA256',),
        'RSA-SHA2-512': ('RSASHA512',),
        'GOSTR341001': ('ECCGOST',),
        'ECDSA-SHA2-256': ('ECDSAP256SHA256',),  # + custom handling below
        'ECDSA-SHA2-384': ('ECDSAP384SHA384',),  # + custom handling below
        'EDDSA-ED25519': ('ED25519',),
        'EDDSA-ED448': ('ED448',),
    }

    hash_not_map = {
        'SHA1': 'SHA-1',
        'SHA2-256': 'SHA-256',
        'SHA2-384': 'SHA-384',
        'GOSTR94': 'GOST',
    }

    @classmethod
    def generate_config(cls, unscoped_policy):
        policy = unscoped_policy.scoped({'dnssec', 'bind'})
        ip = policy.disabled
        s = ''

        s += 'disable-algorithms "." {\n'
        s += 'RSAMD5;\n'  # deprecated, disabled unconditionally
        for i in ip['sign']:
            try:
                for disabled_sign in cls.sign_not_map[i]:
                    s += f'{disabled_sign};\n'
            except KeyError:
                pass
        if 'ECDSA-SHA2-256' not in ip['sign'] and 'SECP256R1' in ip['group']:
            s += 'ECDSAP256SHA256;\n'  # additionally disabled on lack of P-256
        if 'ECDSA-SHA2-384' not in ip['sign'] and 'SECP384R1' in ip['group']:
            s += 'ECDSAP384SHA384;\n'  # additionally disabled on lack of P-384
        s += '};\n'

        s += 'disable-ds-digests "." {\n'
        for i in ip['hash']:
            try:
                s += f'{cls.hash_not_map[i]};\n'
            except KeyError:
                pass
        s += '};\n'

        return s

    @classmethod
    def test_config(cls, config):
        fd, path = mkstemp()

        try:
            with os.fdopen(fd, 'w') as f:
                f.write('options {\n')
                f.write(config)
                f.write('\n};\n')
            try:
                _ = check_output(['/usr/sbin/named-checkconf', path])
            except CalledProcessError:
                cls.eprint('There is an error in bind generated policy')
                cls.eprint(f'Policy:\n{config}')
                return False
            except OSError:
                # Ignore missing check command
                pass
        finally:
            os.unlink(path)

        return True
