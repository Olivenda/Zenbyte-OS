# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2021 Red Hat, Inc.

from .general import PolicySyntaxError, PolicyWarning


class AlgorithmClassSyntaxError(PolicySyntaxError):
    pass


class AlgorithmClassUnknownError(AlgorithmClassSyntaxError):
    def __init__(self, alg_class):
        # The wording follows the previous versions
        super().__init__(f'Unknown policy property: `{alg_class}`')


class AlgorithmEmptyMatchError(AlgorithmClassSyntaxError):
    def __init__(self, glob, alg_class):
        # The wording follows the previous versions
        super().__init__(f'Bad value of policy property `{alg_class}`: '
                         f'`{glob}`')


class ExperimentalValueWarning(PolicyWarning):
    def __init__(self, alg_class, values):
        msg = f'`{alg_class}` '
        if len(values) == 1:
            msg += f'value `{values[0]}` is '
        else:
            msg += f'values {", ".join(f"`{value}`" for value in values)} are '
        msg += 'experimental and might go away in the future'
        super().__init__(msg)
