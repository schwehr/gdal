#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ******************************************************************************
#
#  Project:  GDAL
#  Purpose:  Application for Google web service authentication.
#  Author:   Frank Warmerdam, warmerdam@pobox.com
#
# ******************************************************************************
#  Copyright (c) 2013, Frank Warmerdam <warmerdam@pobox.com>
#
# SPDX-License-Identifier: MIT
# ******************************************************************************


import sys
import time
import webbrowser

from osgeo import gdal

SCOPES = {
    "ft": "https://www.googleapis.com/auth/fusiontables",
    "storage": "https://www.googleapis.com/auth/devstorage.read_only",
    "storage-rw": "https://www.googleapis.com/auth/devstorage.read_write",
}


def Usage():
    print("")
    print("Usage: gdal_auth_py [-s scope]")
    print("       - interactive use.")
    print("")
    print("or:")
    print("Usage: gdal_auth.py login [-s scope] ")
    print("Usage: gdal_auth.py auth2refresh [-s scope] auth_token")
    print("Usage: gdal_auth.py refresh2access [-s scope] refresh_token")
    print("")
    print("scopes: ft/storage/storage-rw/full_url")
    print("")
    return 2


def main(argv=sys.argv):
    scope = SCOPES["ft"]
    token_in = None
    command = None

    argv = gdal.GeneralCmdLineProcessor(sys.argv)
    if argv is None:
        return 0

    # Parse command line arguments.
    i = 1
    while i < len(argv):
        arg = argv[i]

        if arg == "-s" and i < len(argv) - 1:
            if argv[i + 1] in SCOPES:
                scope = SCOPES[argv[i + 1]]
            elif argv[i + 1].startswith("http"):
                scope = argv[i + 1]
            else:
                print("Scope %s not recognised." % argv[i + 1])
                return Usage()
            i = i + 1

        elif arg[0] == "-":
            return Usage()

        elif command is None:
            command = arg

        elif token_in is None:
            token_in = arg

        else:
            return Usage()

        i = i + 1

    if command is None:
        command = "interactive"

    if command == "login":
        print(gdal.GOA2GetAuthorizationURL(scope))
    elif command == "auth2refresh":
        print(gdal.GOA2GetRefreshToken(token_in, scope))
    elif command == "refresh2access":
        print(gdal.GOA2GetAccessToken(token_in, scope))
    elif command != "interactive":
        return Usage()
    else:
        # Interactive case
        print("Authorization requested for scope:")
        print(scope)
        print("")
        print("Please login and authorize access in web browser...")

        webbrowser.open(gdal.GOA2GetAuthorizationURL(scope))

        time.sleep(2.0)

        print("")
        print("Enter authorization token:")
        auth_token = sys.stdin.readline()

        refresh_token = gdal.GOA2GetRefreshToken(auth_token, scope)

        print("Refresh Token:" + refresh_token)
        print("")
        if scope == SCOPES["ft"]:
            print("Consider setting a configuration option like:")
            print("GFT_REFRESH_TOKEN=" + refresh_token)
        elif scope in (SCOPES["storage"], SCOPES["storage-rw"]):
            print("Consider setting a configuration option like:")
            print("GS_OAUTH2_REFRESH_TOKEN=" + refresh_token)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
