#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import getpass
import hashlib
import os
import sys


def make_hash(password: str, iterations: int = 260000) -> str:
    salt = os.urandom(16).hex()
    digest = hashlib.pbkdf2_hmac(
        'sha256',
        password.encode('utf-8'),
        salt.encode('utf-8'),
        iterations,
    ).hex()
    return f'pbkdf2_sha256${iterations}${salt}${digest}'


def main() -> int:
    password = sys.argv[1] if len(sys.argv) > 1 else getpass.getpass('Password: ')
    if not password:
        print('empty password is not allowed', file=sys.stderr)
        return 1
    print(make_hash(password))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
