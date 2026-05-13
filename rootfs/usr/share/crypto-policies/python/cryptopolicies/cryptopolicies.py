# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2019 Red Hat, Inc.
# Copyright (c) 2019 Tomáš Mráz <tmraz@fedoraproject.org>

import collections
import enum
import fnmatch
import os
import re
import warnings

from . import (
    alg_lists,
    validation,  # moved out of the way to not obscure the flow
)

# Dunder (underscore-prefixed) options are ones
# we don't advertise for direct usage, specific to a narrow set of policies.
# We'd rather have users build up upon policies we ship that set them.


# Defaults of integer property values (doubles as an allowlist)

INT_DEFAULTS = dict.fromkeys((
    'arbitrary_dh_groups',
    'min_dh_size', 'min_dsa_size', 'min_rsa_size',
    '__openssl_block_sha1_signatures',  # FUTURE/TEST-FEDORA39/NO-SHA1
    'sha1_in_certs',
    'ssh_certs',
), 0) | {'min_ec_size': 256}


# For enum values, first value works as default,

ENUMS = {
    'etm': ('ANY', 'DISABLE_ETM', 'DISABLE_NON_ETM'),
    '__ems': ('DEFAULT', 'ENFORCE', 'RELAX'),  # FIPS/NO-ENFORCE-EMS
}


# Scopes (`@!ipsec`) and matching them

SCOPE_ANY = '*'

ALL_SCOPES = (  # defined explicitly to catch typos / globbing nothing
    'tls', 'ssl',
    'openssl', 'gnutls', 'java-tls',
    'nss', 'nss-tls',
    'pkcs12', 'pkcs12-import', 'nss-pkcs12', 'nss-pkcs12-import',
    'smime', 'smime-import', 'nss-smime', 'nss-smime-import',
    'ssh', 'openssh', 'openssh-server', 'openssh-client', 'libssh',
    'ipsec', 'ike', 'libreswan',
    'kerberos', 'krb5',
    'dnssec', 'bind',
    'sequoia', 'rpm', 'rpm-sequoia',
)
DUMPABLE_SCOPES = {  # TODO: fix duplication, backends specify same things
    # scope we dump under: (relative parent scope, set of scopes applied)
    'bind': (None, {'bind', 'dnssec'}),
    'gnutls': (None, {'gnutls', 'tls', 'ssl'}),
    'java-tls': (None, {'java-tls', 'tls', 'ssl'}),
    'krb5': (None, {'krb5', 'kerberos'}),
    'libreswan': (None, {'ipsec', 'ike', 'libreswan'}),
    'libssh': (None, {'libssh', 'ssh'}),
    'nss': (None, {'nss'}),
    'nss-tls': ('nss', {'nss', 'nss-tls', 'tls', 'ssl'}),
    'nss-pkcs12': ('nss', {'nss', 'pkcs12', 'nss-pkcs12'}),
    'nss-pkcs12-import': ('nss-pkcs12', {'nss', 'pkcs12', 'pkcs12-import',
                                         'nss-pkcs12', 'nss-pkcs12-import'}),
    'nss-smime': ('nss', {'nss', 'smime', 'nss-smime'}),
    'nss-smime-import': ('nss-smime', {'nss', 'smime', 'smime-import',
                                       'nss-smime', 'nss-smime-import'}),
    'openssh': (None, {'openssh', 'ssh'}),
    'openssh-client': ('openssh', {'openssh-client', 'openssh', 'ssh'}),
    'openssh-server': ('openssh', {'openssh-server', 'openssh', 'ssh'}),
    'openssl': (None, {'openssl', 'tls', 'ssl'}),
    'sequoia': (None, {'sequoia'}),
    'rpm': (None, {'rpm', 'rpm-sequoia'}),
}


class ScopeSelector:
    def __init__(self, pattern=SCOPE_ANY):
        """
        Initialize a scope selector.
        An example would be `ssh` in `ciphers@ssh = -NULL`.
        When openssh backend will request the configuration,
        it'll offer (`{'ssh', 'openssh'}`) as scopes
        and the rule above will be taken into account.
        Both patterns and scopes are cast to lowercase.
        For more examples, refer to tests/unit/parsing/test_scope_selector.py
        >>> ss = ScopeSelector('!{SSH,IPsec}')
        >>> ss.matches({'ipsec', 'libreswan'})
        False
        >>> ss.matches({'tls', 'openssl'})
        True
        """
        self.pattern = pattern = pattern.lower()
        self._positive = not pattern.startswith('!')
        p = pattern if self._positive else pattern[1:]

        validation.scope.illegal_characters(p, original_pattern=self.pattern)
        validation.scope.curly_brackets(p, original_pattern=self.pattern)

        self._globs = p[1:-1].split(',') if p.startswith('{') else [p]

        validation.scope.resulting_globs(self._globs, ALL_SCOPES,
                                         original_pattern=self.pattern)

    def __str__(self):
        return f'<ScopeSelector pattern={self.pattern!r}>'

    def matches(self, scopes):
        """
        Checks whether ScopeSelector matches one of the scopes.
        For more examples, refer to tests/unit/parsing/test_scope_selector.py
        >>> ScopeSelector('{SSH,IPsec}').matches({'ipsec', 'libreswan'})
        True
        >>> ScopeSelector('!{SSH,IPsec}').matches({'ipsec', 'libreswan'})
        False
        """
        if self.pattern == SCOPE_ANY:  # matches even an empty set
            return True
        scopes = [s.lower() for s in scopes]
        assert all(s in ALL_SCOPES for s in scopes)  # supplied by backends
        if self._positive:
            return any(fnmatch.filter(scopes, g) for g in self._globs)
        return all(not fnmatch.filter(scopes, g) for g in self._globs)


# Operations: interpreting right hand sides of (sub)policy files

class Operation(enum.Enum):
    """An operation that comes with the right-hand value of the directive."""

    RESET = 1     # cipher =
    PREPEND = 2   # cipher = +NULL
    APPEND = 3    # cipher = NULL+
    OMIT = 4      # cipher = -NULL
    SET_INT = 5   # sha1_in_certs = 0; setting to something that's all digits
    SET_ENUM = 6  # __ems = ENFORCE

    def __repr__(self):  # to unify the output between Python versions
        return f'Operation.{self.name}'


def parse_rhs(rhs, prop_name):
    """
    Parses right-hand parts of the directives
    into lists of operation/value pairs.
    For more examples, refer to tests/unit/test_parsing.py
    >>> parse_rhs('', 'cipher')
    [(Operation.RESET, None)]
    >>> parse_rhs('IDEA-CBC SEED-CBC', 'cipher')
    [(Operation.RESET, None),
     (Operation.APPEND, 'IDEA-CBC'),
     (Operation.APPEND, 'SEED-CBC')]
    >>> # 3DES-CBC gets prepended last for higher prio
    >>> parse_rhs('+*DES-CBC', 'cipher')
    [(Operation.PREPEND, 'DES-CBC'),
     (Operation.PREPEND, '3DES-CBC')]
    >>> parse_rhs('ENFORCE', '__ems')
    [(Operation.SET_ENUM, 'ENFORCE')]
    """
    def differential(v):
        return v.startswith(('+', '-')) or v.endswith('+')

    if rhs.isdigit():
        if prop_name not in alg_lists.ALL and prop_name in INT_DEFAULTS:
            return [(Operation.SET_INT, int(rhs))]
        if prop_name in alg_lists.ALL or prop_name in ENUMS:
            raise validation.rules.NonIntPropertyIntValueError(prop_name)
        assert prop_name not in alg_lists.ALL
        assert prop_name not in INT_DEFAULTS
        assert prop_name not in ENUMS
        # pass for now, it's gonna be caught as non-existing algclass
    else:
        if prop_name in INT_DEFAULTS:
            raise validation.rules.IntPropertyNonIntValueError(prop_name)
        if prop_name in ENUMS:
            if rhs not in ENUMS[prop_name]:
                raise validation.rules.BadEnumValueError(prop_name, rhs,
                                                         ENUMS[prop_name])
            return [(Operation.SET_ENUM, rhs)]

    values = rhs.split()

    if not any(differential(v) for v in values):  # Setting something anew
        values = [x for v in values for x in alg_lists.glob(v, prop_name)]
        return ([(Operation.RESET, None)]
                + [(Operation.APPEND, v) for v in values])
    if all(differential(v) for v in values):  # Modifying an existing list
        operations = []
        for value in values:
            if value.startswith('+'):
                op = Operation.PREPEND
                unglob = alg_lists.glob(value[1:], prop_name)[::-1]
            elif value.endswith('+'):
                op = Operation.APPEND
                unglob = alg_lists.glob(value[:-1], prop_name)[::-1]
            else:
                assert value.startswith('-')
                op = Operation.OMIT
                unglob = alg_lists.glob(value[1:], prop_name)
            operations.extend([(op, v) for v in unglob])
        return operations
    # Forbidden to mix them on one line
    raise validation.rules.MixedDifferentialNonDifferentialError(rhs)


# Directives: interpreting lines of (sub)policy files

Directive = collections.namedtuple('Directive', (
    'prop_name', 'scope', 'operation', 'value',
))


def parse_line(line):
    """
    Parses configuration lines into tuples of directives.
    For more examples, refer to tests/unit/test_parsing.py
    >>> parse_line('cipher@TLS = RC4* NULL')
    [Directive(prop_name='cipher', scope='tls',
               operation=Operation.RESET, value=None),
     Directive(prop_name='cipher', scope='tls',
               operation=Operation.APPEND, value='RC4-40'),
     Directive(prop_name='cipher', scope='tls',
               operation=Operation.APPEND, value='RC4-128'),
     Directive(prop_name='cipher', scope='tls',
               operation=Operation.APPEND, value='NULL')]
    """
    if not line.strip():
        return []
    validation.rules.count_equals_signs(line)

    lhs, rhs = line.split('=')
    lhs, rhs = lhs.strip(), rhs.strip()
    validation.rules.empty_lhs(lhs, line)

    prop_name, scope = lhs.split('@', 1) if '@' in lhs else (lhs, SCOPE_ANY)

    return [Directive(prop_name=prop_name, scope=scope.lower(),
                      operation=operation, value=value)
            for operation, value in parse_rhs(rhs, prop_name)]


def syntax_check_line(line, warn=False):
    try:
        l = parse_line(line)
        for d in l:
            ScopeSelector(d.scope)  # attempt parsing
    except validation.PolicySyntaxError as ex:
        if not warn:
            raise
        warnings.warn(ex)


class PolicySyntaxDeprecationWarning(FutureWarning, validation.PolicyWarning):
    def __init__(self, deprecated, replacement):
        replacement = replacement.replace('\n', ' and ')
        msg = f'option {deprecated} is deprecated'
        msg += f', please rewrite your rules using {replacement}; '
        msg += 'be advised that it is not always a 1-1 replacement'
        super().__init__(msg)


def preprocess_text(text):
    r"""
    Preprocesses text before parsing.
    Fixes line breaks, handles backwards compatibility.
    >>> preprocess_text('cipher = c1 \\ \nc2#x')
    'cipher = c1 c2'
    >>> with warnings.catch_warnings():
    ...     warnings.simplefilter("ignore")
    ...     preprocess_text('ike_protocol = IKEv2')
    'protocol@IKE = IKEv2'
    >>> with warnings.catch_warnings():
    ...     warnings.simplefilter("ignore")
    ...     preprocess_text('min_tls_version=TLS1.3')
    'protocol@TLS = -SSL2.0 -SSL3.0 -TLS1.0 -TLS1.1 -TLS1.2'
    """
    text = re.sub(r'#.*', '', text)
    text = text.replace('=', ' = ')
    text = '\n'.join(l.strip() for l in text.split('\n'))
    text = text.replace('\\\n', '')
    text = '\n'.join(l.strip() for l in text.split('\n'))
    text = '\n'.join(re.sub(r'\s+', ' ', l) for l in text.split('\n'))
    text = re.sub('\n+', '\n', text).strip()

    if re.findall(r'\bprotocol\s*=', text):
        warnings.warn(PolicySyntaxDeprecationWarning('protocol',
                                                     'protocol@TLS'))

    POSTFIX_REPLACEMENTS = {
        'tls_cipher': 'cipher@TLS',
        'ssh_cipher': 'cipher@SSH',
        'ssh_group': 'group@SSH',
        'ike_protocol': 'protocol@IKE',
    }
    for fr, to in POSTFIX_REPLACEMENTS.items():
        regex = r'\b' + fr + r'\s*=(.*)'
        ms = re.findall(regex, text)
        if ms:
            warnings.warn(PolicySyntaxDeprecationWarning(fr, to))
        text = re.sub(regex, '', text)
        for m in ms:
            text += f'\n\n{to} ={m}'
    text = re.sub('\n+', '\n', text).strip()

    PLAIN_REPLACEMENTS = {
        'sha1_in_dnssec = 0':
            'hash@DNSSec = -SHA1\nsign@DNSSec = -RSA-SHA1 -ECDSA-SHA1',
        'sha1_in_dnssec = 1':
            'hash@DNSSec = SHA1+\nsign@DNSSec = RSA-SHA1+ ECDSA-SHA1+',
        'ssh_etm = 0': 'etm@SSH = DISABLE_ETM',
        'ssh_etm = 1': 'etm@SSH = ANY',
        'ssh_etm@([^= ]+) = 0':
            'etm@\\1 = DISABLE_ETM',
        'ssh_etm@([^= ]+) = 1':
            'etm@\\1 = ANY',
    }
    for fr, to in PLAIN_REPLACEMENTS.items():
        regex = r'\b' + fr + r'\b'
        matches = {}
        for match in re.finditer(regex, text):
            matches[match.group(0)] = re.sub(regex, to, match.group(0))
        for match_fr, match_to in matches.items():
            warnings.warn(PolicySyntaxDeprecationWarning(match_fr, match_to))
        text = re.sub(regex, to, text)

    dtls_versions = list(alg_lists.DTLS_PROTOCOLS[::-1])
    while dtls_versions:
        neg = " ".join("-" + v for v in dtls_versions[:-1])
        text = re.sub(r'\bmin_dtls_version = ' + dtls_versions[-1] + r'\b',
                      f'protocol@TLS = {neg}' if neg else '', text)
        dtls_versions.pop()
    text = re.sub(r'\bmin_dtls_version = 0\b', '', text)

    tls_versions = list(alg_lists.TLS_PROTOCOLS[::-1])
    while tls_versions:
        neg = " ".join("-" + v for v in tls_versions[:-1])
        text = re.sub(r'\bmin_tls_version = ' + tls_versions[-1] + r'\b',
                      f'protocol@TLS = {neg}' if neg else '', text)
        tls_versions.pop()
    return re.sub(r'\bmin_tls_version = 0\b', '', text)


# Finally, constructing a policy

class ScopedPolicy:
    """
    An entity constructing lists of what's `.enabled` and what's `.disabled`
    when the given scopes are active.
    >>> sp = ScopedPolicy(parse_line('cipher@TLS = RC4* NULL'), {'tls'})
    >>> 'AES-192-GCM' in sp.disabled['cipher']
    True
    >>> sp.enabled['cipher']
    ['RC4-40', 'RC4-128', 'NULL']
    >>> ScopedPolicy(parse_line('min_dh_size=2048')).integers['min_dh_size']
    2048
    """

    def __init__(self, directives, relevant_scopes=None):
        relevant_scopes = relevant_scopes or set()
        self.integers = INT_DEFAULTS.copy()
        self.enums = {k: v[0] for k, v in ENUMS.items()}
        self.enabled = {prop_name: [] for prop_name in alg_lists.ALL}

        for directive in directives:
            # TODO: validate that the target exists
            ss = ScopeSelector(directive.scope)
            if ss.matches(relevant_scopes):
                if directive.operation == Operation.RESET:
                    self.enabled[directive.prop_name] = []
                elif directive.operation == Operation.APPEND:
                    enabled = self.enabled[directive.prop_name]
                    if directive.value not in enabled:
                        enabled.append(directive.value)
                elif directive.operation == Operation.PREPEND:
                    enabled = self.enabled[directive.prop_name]
                    # in case of duplicates, remove the latter, lower-prio ones
                    if directive.value in enabled:
                        enabled.remove(directive.value)
                    enabled.insert(0, directive.value)
                elif directive.operation == Operation.OMIT:
                    self.enabled[directive.prop_name] = [
                        e for e in self.enabled[directive.prop_name]
                        if e != directive.value
                    ]
                elif directive.operation == Operation.SET_INT:
                    self.integers[directive.prop_name] = directive.value
                else:
                    assert directive.operation == Operation.SET_ENUM
                    self.enums[directive.prop_name] = directive.value
        assert len(self.enabled) == len(set(self.enabled))

        self.disabled = {prop_name: [e for e in alg_list
                                     if e not in self.enabled[prop_name]]
                         for prop_name, alg_list in alg_lists.ALL.items()}

    @property
    def min_tls_version(self):
        return alg_lists.min_tls_version(self.enabled['protocol'])

    @property
    def max_tls_version(self):
        return alg_lists.max_tls_version(self.enabled['protocol'])

    @property
    def min_dtls_version(self):
        return alg_lists.min_dtls_version(self.enabled['protocol'])

    @property
    def max_dtls_version(self):
        return alg_lists.max_dtls_version(self.enabled['protocol'])


# Locating policy files

def lookup_file(policyname, fname, paths):
    for d in paths:
        p = os.path.join(d, fname)
        if os.access(p, os.R_OK):
            return p
    raise validation.PolicyFileNotFoundError(policyname, fname, paths)


# main class

class UnscopedCryptoPolicy:
    CONFIG_DIR = '/etc/crypto-policies'

    SHARE_DIR = '/usr/share/crypto-policies'

    def __init__(self, policy_name, *subpolicy_names, policydir=None):
        self.policydir = policydir
        self.policyname = ':'.join((policy_name, *subpolicy_names))

        self.lines = []

        directives = self.read_policy_file(policy_name)
        for subpolicy_name in subpolicy_names:
            directives += self.read_policy_file(subpolicy_name, subpolicy=True)
        self._directives = directives

    def is_empty(self):
        return not self._directives

    def scoped(self, scopes=None):
        return ScopedPolicy(self._directives, scopes or {})

    def read_policy_file(self, name, subpolicy=False):
        pdir = self.policydir or 'policies'
        if subpolicy:
            pdir = os.path.join(pdir, 'modules')
        p = lookup_file(name,
                        name + ('.pol' if not subpolicy else '.pmod'), (
                            os.path.curdir,
                            pdir,
                            os.path.join(self.CONFIG_DIR, pdir),
                            os.path.join(self.SHARE_DIR, pdir),
                        ))
        # TODO: error handling
        with open(p, encoding='utf-8') as f:
            text = f.read()
        text = preprocess_text(text)
        lines = text.split('\n')
        for l in lines:  # display several warnings at once
            syntax_check_line(l, warn=True)
        for l in lines:  # crash
            syntax_check_line(l)
        return [x for l in lines for x in parse_line(l)]

    def __str__(self):
        def fmt(key, value):
            s = ' '.join(value) if isinstance(value, list) else str(value)
            return f'{key} = {s}'.rstrip() + '\n'

        generic_scoped = self.scoped()
        s = f'# Policy {self.policyname} dump\n'
        s += '#\n'
        s += '# Do not parse the contents of this file with automated tools,\n'
        s += '# it is provided for review convenience only.\n'
        s += '#\n'
        s += '# Baseline values for all scopes:\n'
        generic_all = {**generic_scoped.enabled,
                       **generic_scoped.integers,
                       **generic_scoped.enums}
        for prop_name, value in generic_all.items():
            s += fmt(prop_name, value)
        anything_scope_specific = False
        for scope_name, (parent_scope, scope_set) in DUMPABLE_SCOPES.items():
            specific_scoped = self.scoped(scopes=scope_set)
            specific_all = {**specific_scoped.enabled,
                            **specific_scoped.integers,
                            **specific_scoped.enums}
            if parent_scope is not None:
                _, parent_scope_set = DUMPABLE_SCOPES[parent_scope]
                parent_scoped = self.scoped(scopes=parent_scope_set)
                parent_all = {**parent_scoped.enabled,
                              **parent_scoped.integers,
                              **parent_scoped.enums}
            else:
                parent_scoped, parent_all = generic_scoped, generic_all
            for prop_name, value in specific_all.items():
                if value != parent_all[prop_name]:
                    if not anything_scope_specific:
                        s += ('# Scope-specific properties '
                              'derived for select backends:\n')
                        anything_scope_specific = True
                    s += fmt(f'{prop_name}@{scope_name}', value)
        if not anything_scope_specific:
            s += '# No scope-specific properties found.\n'
        return s
