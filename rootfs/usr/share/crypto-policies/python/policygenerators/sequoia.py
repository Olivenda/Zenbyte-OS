# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2022 Red Hat, Inc.
# Copyright (c) 2022 Alexander Sosedkin <asosedkin@redhat.com>

import os
import subprocess
from tempfile import mkstemp

try:
    import tomllib as toml
    toml_error = toml.TOMLDecodeError
except ModuleNotFoundError:
    import toml
    toml_error = toml.decoder.TomlDecodeError

from .configgenerator import ConfigGenerator


class SequoiaGenerator(ConfigGenerator):
    # Limitation: controls only
    # * `hash_algorithms`,
    # * `symmetric_algorithms` and
    # * partially, `asymmetric_algorithms`, deduced from `sign` and `group`

    CONFIG_NAME = 'sequoia'

    # sequoia display name to c-p name, taken from sequoia_openpgp/types/mod.rs
    hash_backwards_map = {
        'md5': 'MD5',
        'sha1': 'SHA1',
        'ripemd160': None,
        'sha224': 'SHA2-224',
        'sha256': 'SHA2-256',
        'sha384': 'SHA2-384',
        'sha512': 'SHA2-512',
        'sha3-256': 'SHA3-256',
        'sha3-512': 'SHA3-512',
    }

    symmetric_backwards_map = {
        'idea': 'IDEA-CFB',
        'tripledes': '3DES-CFB',
        'cast5': None,
        'blowfish': None,
        'aes128': 'AES-128-CFB',
        'aes192': 'AES-192-CFB',
        'aes256': 'AES-256-CFB',
        'twofish': None,
        'camellia128': 'CAMELLIA-128-CFB',
        'camellia192': 'CAMELLIA-192-CFB',
        'camellia256': 'CAMELLIA-256-CFB',
        # 'unencrypted': 'NULL',  # can't be set
    }

    asymmetric_group_backwards_map = {
        'nistp256': 'SECP256R1',
        'nistp384': 'SECP384R1',
        'nistp521': 'SECP521R1',
        'cv25519': 'X25519',
        'x25519': 'X25519',
        'x448': 'X448',
        'mlkem768-x25519': 'MLKEM768-X25519',
        'mlkem1024-x448': 'MLKEM1024-X448',
    }

    asymmetric_sign_backwards_map = {
        'ed25519': 'EDDSA-ED25519',
        'ed448': 'EDDSA-ED448',
        'mldsa65-ed25519': 'MLDSA65-ED25519',
        'mldsa87-ed448': 'MLDSA87-ED448',
    }

    asymmetric_always_disabled = (
        'elgamal1024',
        'elgamal2048',
        'elgamal3072',
        'elgamal4096',
        'brainpoolp256',
        'brainpoolp512',
        # 'unknown',  # can't be set
    )

    aead_backwards_map = {
        'eax': {'AES-256-EAX', 'AES-128-EAX'},
        'ocb': {'AES-256-OCB', 'AES-128-OCB'},
        'gcm': {'AES-256-GCM', 'AES-128-GCM'},
    }

    # listing new algorithms here would let old sequoia ignore unknown values
    ignore_invalid = {  # c-p property name -> tuple[sequoia algorithm names]
        # sequoia-openpgp 2, rpm-sequoia 1.8
        'hash': ('sha3-256', 'sha3-512'),
        'group': ('x25519', 'x448', 'mlkem768-x25519', 'mlkem1024-x448'),
        'sign': ('ed25519', 'ed448', 'mldsa65-ed25519', 'mldsa87-ed448'),
        'aead': ('gcm',),
    }

    @classmethod
    def _generate_ignore_invalid(cls, *kinds):
        values = [v for k in kinds for v in cls.ignore_invalid.get(k, [])]
        if values:
            values = ', '.join(f'"{v}"' for v in values)
            return f'ignore_invalid = [ {values} ]\n'
        return ''

    @classmethod
    def generate_config(cls, unscoped_policy):
        return cls._generate_config(unscoped_policy.scoped({'sequoia'}))

    @classmethod
    def _generate_config(cls, policy):
        p = policy.enabled

        cfg = '[hash_algorithms]\n'
        cfg += cls._generate_ignore_invalid('hash')
        for seqoia_name, c_p_name in cls.hash_backwards_map.items():
            v = 'always' if c_p_name in p['hash'] else 'never'
            cfg += f'{seqoia_name}.collision_resistance = "{v}"\n'
            cfg += f'{seqoia_name}.second_preimage_resistance = "{v}"\n'
        cfg += 'default_disposition = "never"\n\n'

        cfg += '[symmetric_algorithms]\n'
        cfg += cls._generate_ignore_invalid('cipher')
        for seqoia_name, c_p_name in cls.symmetric_backwards_map.items():
            v = 'always' if c_p_name in p['cipher'] else 'never'
            cfg += f'{seqoia_name} = "{v}"\n'
        cfg += 'default_disposition = "never"\n\n'

        cfg += '[asymmetric_algorithms]\n'
        cfg += cls._generate_ignore_invalid('group', 'sign')
        # ugly inference from various lists: rsa/dsa is sign + min_size
        any_rsa = any(s.startswith('RSA-') for s in p['sign'])
        any_dsa = any(s.startswith('DSA-') for s in p['sign'])
        min_rsa = policy.integers['min_rsa_size']
        for l in 1024, 2048, 3072, 4096:
            v = 'always' if l >= min_rsa and any_rsa else 'never'
            cfg += f'rsa{l} = "{v}"\n'
        min_dsa = policy.integers['min_dsa_size']
        for l in 1024, 2048, 3072, 4096:
            v = 'always' if l >= min_dsa and any_dsa else 'never'
            cfg += f'dsa{l} = "{v}"\n'
        # groups
        for seq_name, group in cls.asymmetric_group_backwards_map.items():
            v = 'always' if group in p['group'] else 'never'
            cfg += f'{seq_name} = "{v}"\n'
        # sign
        for seq_name, sign in cls.asymmetric_sign_backwards_map.items():
            v = 'always' if sign in p['sign'] else 'never'
            cfg += f'{seq_name} = "{v}"\n'
        # always disabled
        for seq_name in cls.asymmetric_always_disabled:
            cfg += f'{seq_name} = "never"\n'
        cfg += 'default_disposition = "never"\n'

        # aead algorithms
        cfg += '\n[aead_algorithms]\n'
        cfg += 'default_disposition = "never"\n'
        cfg += cls._generate_ignore_invalid('aead')
        for seq_name, c_p_names in cls.aead_backwards_map.items():
            v = 'always' if c_p_names.intersection(p['cipher']) else 'never'
            cfg += f'{seq_name} = "{v}"\n'

        return cfg

    @classmethod
    def _lint_config(cls, linter, config,
                     stricter=False, linter_missing_ok=False):
        policy_descr = 'the generated sequoia policy'
        if stricter:
            stricter_config = '\n'.join(
                l for l in config.split('\n')
                if not l.startswith('ignore_invalid = ')
            )
            if config != stricter_config:
                config = stricter_config
                policy_descr = 'a tightened sequoia policy'
        fd, path = mkstemp()
        try:
            with os.fdopen(fd, 'w') as f:
                f.write(config)
            r = subprocess.run([linter, path], check=False, encoding='utf-8',
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
            cls.eprint(f'{linter} returns {r.returncode} for {policy_descr}'
                       + (f': `{r.stdout}`' if r.stdout else ''))
            if (r.returncode, r.stdout) == (0, ''):
                return True
            cls.eprint(f'There is an error in {policy_descr}')
        except FileNotFoundError:
            if linter_missing_ok:
                cls.eprint(f'{linter} not found, skipping...')
                return True
            cls.eprint(f'{linter} not found!')
        finally:
            os.unlink(path)
        return False

    @classmethod
    def test_config(cls, config):
        # check for TOML validity
        toml.loads(config)
        try:
            toml.loads(config)
            cls.eprint('the generated sequoia policy is valid TOML')
        except toml_error as ex:
            cls.eprint('There is a syntax error in generated sequoia policy')
            cls.eprint(f'Invalid TOML: {type(ex)} {ex}')
            cls.eprint(f'Policy:\n{config}')
            return False

        if os.getenv('OLD_SEQUOIA') == '1':
            return True

        loose = os.getenv('SEQUOIA_POLICY_CONFIG_CHECK_LOOSE')
        strict = os.getenv('SEQUOIA_POLICY_CONFIG_CHECK_STRICT')
        if loose is None and strict is None:
            return cls._lint_config('sequoia-policy-config-check', config,
                                    stricter=True, linter_missing_ok=True)
        for linter in loose.split():
            if not cls._lint_config(linter, config, stricter=False):
                return False
        for linter in strict.split():
            if not cls._lint_config(linter, config, stricter=True):
                return False
        return True


class RPMSequoiaGenerator(SequoiaGenerator):
    CONFIG_NAME = 'rpm-sequoia'

    @classmethod
    def generate_config(cls, unscoped_policy):
        return cls._generate_config(unscoped_policy.scoped({'rpm',
                                                            'rpm-sequoia'}))
