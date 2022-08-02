# Copyright (c) 2020 Parrot Drones SAS

# Export C string to Python str function (generated in pybinding-macro)
from libarsdk import string_cast


def iter_ptr(table):
    """Iterate over a pointer until NULL value is found"""
    if table:
        for elem in table:
            if not elem:
                break
            yield elem
