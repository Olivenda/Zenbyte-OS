# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2021 Red Hat, Inc.

from .general import PolicySyntaxError


class MalformedLineError(PolicySyntaxError):
    def __init__(self, line):
        super().__init__(f'malformed line `{line}`')


class MixedDifferentialNonDifferentialError(PolicySyntaxError):
    def __init__(self, rhs):
        super().__init__('cannot initialize list and modify it at once '
                         f'(`{rhs}`)')


class IntPropertyNonIntValueError(PolicySyntaxError):
    def __init__(self, int_property_name):
        # wording follows previous versions
        super().__init__(f'Bad value of policy property `{int_property_name}`:'
                         ' value must be an integer')


class NonIntPropertyIntValueError(PolicySyntaxError):
    def __init__(self, alg_class):
        # wording follows previous versions
        super().__init__(f'Bad value of policy property `{alg_class}`:'
                         ' value must not be an integer')


class BadEnumValueError(PolicySyntaxError):
    def __init__(self, enum_name, value, acceptable_values):
        super().__init__(f'Bad value of policy property `{enum_name}`:'
                         f' {value}; must be one of {acceptable_values}')


def count_equals_signs(line):
    if line.count('=') != 1:
        raise MalformedLineError(line)


def empty_lhs(lhs, line):
    if not lhs:
        raise MalformedLineError(line)
