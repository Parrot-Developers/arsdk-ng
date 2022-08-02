# Copyright (c) 2020 Parrot Drones SAS

import ctypes
from enum import IntEnum

import libarsdk
from .utils import iter_ptr, string_cast

ARG_TYPE_CONVERSION = {
    libarsdk.ARSDK_ARG_TYPE_I8: (int),
    libarsdk.ARSDK_ARG_TYPE_U8: (int),
    libarsdk.ARSDK_ARG_TYPE_I16: (int),
    libarsdk.ARSDK_ARG_TYPE_U16: (int),
    libarsdk.ARSDK_ARG_TYPE_I32: (int),
    libarsdk.ARSDK_ARG_TYPE_U32: (int),
    libarsdk.ARSDK_ARG_TYPE_I64: (int),
    libarsdk.ARSDK_ARG_TYPE_U64: (int),
    libarsdk.ARSDK_ARG_TYPE_FLOAT: (float, int),
    libarsdk.ARSDK_ARG_TYPE_DOUBLE: (float, int),
    libarsdk.ARSDK_ARG_TYPE_STRING: (str),
    libarsdk.ARSDK_ARG_TYPE_ENUM: (int),
    libarsdk.ARSDK_ARG_TYPE_BINARY: (bytes),
}

CTYPES_ARG_CONVERSION = {
    libarsdk.ARSDK_ARG_TYPE_I8: ctypes.c_int,
    libarsdk.ARSDK_ARG_TYPE_U8: ctypes.c_int,
    libarsdk.ARSDK_ARG_TYPE_I16: ctypes.c_int,
    libarsdk.ARSDK_ARG_TYPE_U16: ctypes.c_int,
    libarsdk.ARSDK_ARG_TYPE_I32: ctypes.c_int,
    libarsdk.ARSDK_ARG_TYPE_U32: ctypes.c_int,
    libarsdk.ARSDK_ARG_TYPE_I64: ctypes.c_int,
    libarsdk.ARSDK_ARG_TYPE_U64: ctypes.c_int,
    libarsdk.ARSDK_ARG_TYPE_FLOAT: ctypes.c_float,
    libarsdk.ARSDK_ARG_TYPE_DOUBLE: ctypes.c_double,
    libarsdk.ARSDK_ARG_TYPE_STRING: ctypes.c_char_p,
    libarsdk.ARSDK_ARG_TYPE_ENUM: ctypes.c_int,
    libarsdk.ARSDK_ARG_TYPE_BINARY: ctypes.POINTER(libarsdk.struct_arsdk_binary),
}


class ArsdkCommand:
    def __init__(self, name, desc):
        self._name = name
        self._desc = desc
        self._fullname = string_cast(desc.contents.name)
        self._register_subtypes()

        # Full command identifier used for comparing received messages
        # See libarsdk/arsdk/arsdk_cmd_itf.h
        self._id = (self._desc.contents.prj_id << 24 |
                    self._desc.contents.cls_id << 16 |
                    self._desc.contents.cmd_id)

    def _register_subtypes(self):
        """Register IntEnum subtypes for each enum argument"""
        for i in range(self._desc.contents.arg_desc_count):
            arg = self._desc.contents.arg_desc_table[i]
            if arg.type == libarsdk.ARSDK_ARG_TYPE_ENUM:
                arg_name = string_cast(arg.name)
                enum_values = {}
                for i in range(arg.enum_desc_count):
                    value_name = string_cast(arg.enum_desc_table[i].name)
                    enum_values[value_name] = arg.enum_desc_table[i].value
                setattr(self, arg_name, IntEnum(arg_name, enum_values))

    def fullname(self):
        return self._fullname

    def __hash__(self):
        return self._id

    def __eq__(self, other):
        return self._id == other

    def __call__(self, *args):
        # Check argument number
        if len(args) != self._desc.contents.arg_desc_count:
            raise TypeError(
                "{}() takes exactly {} argument(s) ({} given)".format(
                    self._fullname,
                    self._desc.contents.arg_desc_count, len(args)))

        # Check argument types
        for i in range(len(args)):
            expected_type = self._desc.contents.arg_desc_table[i].type
            expected_type = ARG_TYPE_CONVERSION[expected_type]
            if not isinstance(args[i], expected_type):
                raise TypeError(
                    "{}: expected {} for argument {} (got {})".format(
                        self._fullname, expected_type.__name__,
                        i, type(args[i]).__name__))

        # Build C argument list
        c_args = []
        c_args_types = [ctypes.POINTER(libarsdk.struct_arsdk_cmd),
                        ctypes.POINTER(libarsdk.struct_arsdk_cmd_desc)]
        for i in range(self._desc.contents.arg_desc_count):
            arg_desc = self._desc.contents.arg_desc_table[i]
            if arg_desc.type == libarsdk.ARSDK_ARG_TYPE_STRING:
                c_args.append(args[i].encode('utf-8'))
            elif arg_desc.type == libarsdk.ARSDK_ARG_TYPE_BINARY:
                c_args.append(libarsdk.struct_arsdk_binary())
                c_args[-1].len = len(args[i])
                c_args[-1].cdata = ctypes.cast(
                    ctypes.c_char_p(args[i]), ctypes.c_void_p)
            elif (arg_desc.type == libarsdk.ARSDK_ARG_TYPE_FLOAT or
                  arg_desc.type == libarsdk.ARSDK_ARG_TYPE_DOUBLE):
                c_args.append(float(args[i]))
            else:
                c_args.append(int(args[i]))

            c_args_types.append(CTYPES_ARG_CONVERSION[arg_desc.type])

        # Explicitely specify argument types to prevent ctypes errors
        libarsdk.arsdk_cmd_enc.argtypes = c_args_types

        # Encode message
        cmd = libarsdk.struct_arsdk_cmd()
        res = libarsdk.arsdk_cmd_enc(ctypes.pointer(cmd), self._desc, *c_args)
        if res < 0:
            raise RuntimeError(f"arsdk_cmd_enc: {res}")

        return cmd

    def decode(self, cmd):
        # Create output argument list
        args = []
        for i in range(self._desc.contents.arg_desc_count):
            arg_desc = self._desc.contents.arg_desc_table[i]
            if arg_desc.type == libarsdk.ARSDK_ARG_TYPE_BINARY:
                args.append(libarsdk.struct_arsdk_binary())
            else:
                args.append(CTYPES_ARG_CONVERSION[arg_desc.type]())
        args_ptrs = [ctypes.byref(a) for a in args]

        cmd = ctypes.cast(cmd, ctypes.POINTER(libarsdk.struct_arsdk_cmd))
        res = libarsdk.arsdk_cmd_dec(cmd, self._desc, *args_ptrs)
        if res < 0:
            raise RuntimeError(f"arsdk_cmd_dec: {res}")

        class ArsdkCommandParameters:
            """Used as a container for arguments when receiving commands"""
            pass

        py_args = ArsdkCommandParameters()
        for i in range(len(args)):
            arg_desc = self._desc.contents.arg_desc_table[i]
            arg_name = string_cast(arg_desc.name)
            if arg_desc.type == libarsdk.ARSDK_ARG_TYPE_STRING:
                arg_value = string_cast(args[i])
            elif (arg_desc.type == libarsdk.ARSDK_ARG_TYPE_FLOAT or
                  arg_desc.type == libarsdk.ARSDK_ARG_TYPE_DOUBLE):
                arg_value = float(args[i].value)
            elif arg_desc.type == libarsdk.ARSDK_ARG_TYPE_ENUM:
                arg_value = getattr(self, arg_name)(int(args[i].value))
            elif arg_desc.type == libarsdk.ARSDK_ARG_TYPE_BINARY:
                tmp = (ctypes.c_char * args[i].len).from_address(args[i].cdata)
                arg_value = bytes(tmp)
            else:
                arg_value = int(args[i].value)
            setattr(py_args, arg_name, arg_value)
        return py_args


# First level of the table is 'Class'
class ArsdkClsFactory:
    def __init__(self, name, table):
        self._name = name
        self._table = table
        self._children = dict()

    def __getattr__(self, key):
        if key not in self._children:
            self._children[key] = self._get_child(key)
        return self._children[key]

    def _get_child(self, key):
        for cmd_desc in iter_ptr(self._table):
            fullname = string_cast(cmd_desc.contents.name)
            parts = fullname.split('.')
            if key == parts[-1]:
                return ArsdkCommand(key, cmd_desc)
        raise RuntimeError(f"Key {key} not found in factory for {self._name}")


# First level of the table is 'Project'
class ArsdkPrjFactory:
    def __init__(self, name, table):
        self._name = name
        self._table = table
        self._children = dict()

    def __getattr__(self, key):
        if key not in self._children:
            self._children[key] = self._get_child(key)
        return self._children[key]

    def _get_child(self, key):
        for cls_table in iter_ptr(self._table):
            for cmd_desc in iter_ptr(cls_table):
                fullname = string_cast(cmd_desc.contents.name)
                parts = fullname.split('.')
                if key == parts[1]:
                    return ArsdkClsFactory(key, cls_table)
        raise RuntimeError(f"Key {key} not found in factory for {self._name}")


class ArsdkMessageFactory:
    def __init__(self):
        self._table = libarsdk.arsdk_get_cmd_table()
        self._children = dict()

    def __getattr__(self, key):
        if key not in self._children:
            self._children[key] = self._get_child(key)
        return self._children[key]

    def _get_child(self, key):
        # The root table is a 3-level array leading to command description
        for prj_table in iter_ptr(self._table):
            for cls_table in iter_ptr(prj_table):
                for cmd_desc in iter_ptr(cls_table):
                    fullname = string_cast(cmd_desc.contents.name)
                    parts = fullname.split('.')
                    if key == parts[0]:
                        # New features skip the intermediate level
                        if len(parts) == 2:
                            return ArsdkClsFactory(key, cls_table)
                        else:
                            return ArsdkPrjFactory(key, prj_table)
        raise RuntimeError(f"Key {key} not found in factory")
