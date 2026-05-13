# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2021 Red Hat, Inc.

class PolicyWarning(UserWarning):
    pass


class PolicySyntaxError(ValueError, PolicyWarning):
    pass


class PolicyFileNotFoundError(FileNotFoundError):
    def __init__(self, pname, fname, paths):
        super().__init__(f'Unknown policy `{pname}`: '
                         f'file `{fname}` not found in ({", ".join(paths)})')
