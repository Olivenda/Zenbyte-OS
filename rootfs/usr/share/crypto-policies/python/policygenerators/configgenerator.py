# SPDX-License-Identifier: LGPL-2.1-or-later

# Copyright (c) 2019 Red Hat, Inc.
# Copyright (c) 2019 Tomáš Mráz <tmraz@fedoraproject.org>

import sys


class ConfigGenerator:
    RELOAD_CMD = ''

    @staticmethod
    def append(s, val, sep=':'):
        if s:
            if val:
                return s + sep + val

            return s

        return val

    @staticmethod
    def eprint(*args, **kwargs):
        print(*args, file=sys.stderr, **kwargs)
