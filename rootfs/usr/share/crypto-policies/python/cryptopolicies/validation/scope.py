# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2021 Red Hat, Inc.

import fnmatch

from .general import PolicySyntaxError


class ScopeSyntaxError(PolicySyntaxError):
    pass


class ScopeUnknownError(ScopeSyntaxError):
    def __init__(self, scope_glob):
        super().__init__(f'unknown scope {scope_glob}')


class ScopeSelectorEmptyError(ScopeSyntaxError):
    def __init__(self):
        super().__init__('empty scope selector')


class ScopeSelectorIllegalCharacterError(ScopeSyntaxError):
    def __init__(self, selector):
        super().__init__(f'illegal character in scope selector `{selector}`')


class ScopeSelectorCurlyBracketsError(ScopeSyntaxError):
    def __init__(self, pattern):
        super().__init__(f'unsupported curly brackets usage in `{pattern}`')


class ScopeSelectorCommaError(ScopeSyntaxError):
    def __init__(self, pattern):
        super().__init__(f'unsupported comma usage in `{pattern}`')


class ScopeSelectorMatchedNothingError(ScopeSyntaxError):
    def __init__(self, pattern):
        super().__init__(f'scope selector `{pattern}` matches no scope')


def illegal_characters(p, original_pattern):
    if not all(c.isalnum() or c in '{,}*_-' for c in p):
        raise ScopeSelectorIllegalCharacterError(original_pattern)


def curly_brackets(p, original_pattern):
    if ((p.count('{'), p.count('}')) not in {(0, 0), (1, 1)}
            or p.startswith('{') and not p.endswith('}')
            or not p.startswith('{') and p.endswith('}')):
        raise ScopeSelectorCurlyBracketsError(original_pattern)


def resulting_globs(globs, all_scopes, original_pattern):
    if any(',' in g for g in globs):
        raise ScopeSelectorCommaError(original_pattern)
    for g in globs:
        if not g:
            raise ScopeSelectorEmptyError
        if not fnmatch.filter(all_scopes, g):
            if '*' in g:
                raise ScopeSelectorMatchedNothingError(g)
            raise ScopeUnknownError(g)
