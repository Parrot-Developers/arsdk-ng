##
# Copyright (c) 2019 Parrot Drones SAS
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#   * Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#   * Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#   * Neither the name of the Parrot Drones SAS Company nor the
#     names of its contributors may be used to endorse or promote products
#     derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE PARROT DRONES SAS COMPANY BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
##

import sys, os
import arsdkparser

#===============================================================================
#===============================================================================
class Writer(object):
    def __init__(self, fileobj):
        self.fileobj = fileobj

    def write(self, fmt, *args):
        if args:
            self.fileobj.write(fmt % (args))
        else:
            self.fileobj.write(fmt % ())

#===============================================================================
#===============================================================================
def _to_c_enum(val):
    return val.upper()

def _to_c_name(val):
    return val[0].upper() + val[1:]

def _get_multiset_c_name(ftr, multiset):
    return "struct arsdk_%s_%s" % (ftr.name.lower(), multiset.name.lower())

def _get_multiset_msg_c_name(msg):
    if msg.cls:
        return "%s_%s_%s" % (msg.ftr.name, msg.cls.name, msg.name)
    else:
        return "%s_%s" % (msg.ftr.name, msg.name)

def _get_arg_type_c_name(argType):
    table = {
        arsdkparser.ArArgType.I8: "int8_t",
        arsdkparser.ArArgType.U8: "uint8_t",
        arsdkparser.ArArgType.I16: "int16_t",
        arsdkparser.ArArgType.U16: "uint16_t",
        arsdkparser.ArArgType.I32: "int32_t",
        arsdkparser.ArArgType.U32: "uint32_t",
        arsdkparser.ArArgType.I64: "int64_t",
        arsdkparser.ArArgType.U64: "uint64_t",
        arsdkparser.ArArgType.FLOAT: "float",
        arsdkparser.ArArgType.DOUBLE: "double",
        arsdkparser.ArArgType.STRING: "const char *",
    }
    return table[argType]

def _get_msg_name(msg):
    return _to_c_name(msg.name) if msg.cls == None else \
            _to_c_name(msg.cls.name)+'_'+_to_c_name(msg.name)

#===============================================================================
#===============================================================================

def _get_all_msgs(ctx):
    # container: project or feature
    # project: old fashion container (containing cmds)
    # feature: new fashion container (containing msgs: evts & cmds)
    # currently ctx.features contains both projects & features (= containers)
    for container in ctx.features:
        if container.classes:
            # container is a project
            for cls in container.classes:
                for cmd in cls.cmds:
                    yield cmd
        else:
            # container is a feature
            for msg in container.getMsgs():
                yield msg


def _get_max_args_count(ctx):
    max_count = 0

    for msg in _get_all_msgs(ctx):
        count = len(msg.args)
        max_count = max(count, max_count)

    return max_count

def _get_msgs_with_multiset(msgs):
    for msg in msgs:
        if list(_get_args_multiset(msg.args)):
            yield msg

def _get_args_without_multiset(args):
    for arg in args:
        if not isinstance(arg.argType, arsdkparser.ArMultiSetting):
            yield arg

def _get_args_multiset(args):
    for arg in args:
        if isinstance(arg.argType, arsdkparser.ArMultiSetting):
            yield arg

#===============================================================================
#===============================================================================
def gen_ids_h(ctx, out):
    # Feature Ids
    if len(ctx.features) != 0:
        out.write("enum {\n")
        for featureId in sorted(ctx.featuresById.keys()):
            featureObj = ctx.featuresById[featureId]
            out.write("\tARSDK_PRJ_%s = %d,\n",
                    _to_c_enum(featureObj.name),
                    featureObj.featureId)
        out.write("};\n\n")

    # Class Ids
    out.write("#define ARSDK_CLS_DEFAULT 0\n\n")

    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        if featureObj.classes and len(featureObj.classes) != 0:
            out.write("enum {\n")
            for classId in sorted(featureObj.classesById.keys()):
                classObj = featureObj.classesById[classId]
                out.write("\tARSDK_CLS_%s_%s = %d,\n",
                        _to_c_enum(featureObj.name),
                        _to_c_enum(classObj.name),
                        classObj.classId)
            out.write("};\n\n")

    # Msg Ids
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        if featureObj.classes and len(featureObj.classes) != 0:
            for classId in sorted(featureObj.classesById.keys()):
                classObj = featureObj.classesById[classId]
                out.write("enum {\n")
                if len(classObj.cmds) != 0:
                    for cmdId in sorted(classObj.cmdsById.keys()):
                        cmdObj = classObj.cmdsById[cmdId]
                        out.write("\tARSDK_CMD_%s_%s_%s = %d,\n",
                                _to_c_enum(featureObj.name),
                                _to_c_enum(classObj.name),
                                _to_c_enum(cmdObj.name),
                                cmdObj.cmdId)
                    out.write("};\n\n")
        elif len(featureObj.getMsgs()) != 0:
            out.write("enum {\n")
            for msgId in sorted(featureObj.getMsgsById().keys()):
                msgObj = featureObj.getMsgsById()[msgId]
                out.write("\tARSDK_CMD_%s_%s = %d,\n",
                        _to_c_enum(featureObj.name),
                        _to_c_enum(msgObj.name),
                        msgObj.cmdId)
            out.write("};\n\n")

    # Full Ids
    out.write("enum {\n")
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        if featureObj.classes and len(featureObj.classes) != 0:
            for classId in sorted(featureObj.classesById.keys()):
                classObj = featureObj.classesById[classId]
                for cmdId in sorted(classObj.cmdsById.keys()):
                    cmdObj = classObj.cmdsById[cmdId]
                    out.write("\tARSDK_ID_%s_%s_%s = 0x%08x,\n",
                        _to_c_enum(featureObj.name),
                        _to_c_enum(classObj.name),
                        _to_c_enum(cmdObj.name),
                        featureId << 24 | classId << 16 | cmdId)
        elif len(featureObj.getMsgs()) != 0:
            for msgId in sorted(featureObj.getMsgsById().keys()):
                msgObj = featureObj.getMsgsById()[msgId]
                out.write("\tARSDK_ID_%s_%s = 0x%08x,\n",
                        _to_c_enum(featureObj.name),
                        _to_c_enum(msgObj.name),
                        featureId << 24 | msgId)
    out.write("};\n\n")

#===============================================================================
#===============================================================================
def gen_enums_h(ctx, out):
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        for enum in featureObj.enums:
            out.write("enum {\n")
            for enumVal in enum.values:
                out.write("\tARSDK_%s_%s_%s = %d,\n",
                        _to_c_enum(featureObj.name),
                        _to_c_enum(enum.name),
                        _to_c_enum(enumVal.name),
                        enumVal.value)
            out.write("};\n")
            out.write("#define ARSDK_%s_%s_COUNT %d\n\n",
                      _to_c_enum(featureObj.name),
                      _to_c_enum(enum.name),
                      len(enum.values))

#===============================================================================
#===============================================================================
def _gen_multiset_msg_h(ctx, out, multiset_msg):
    out.write("\tstruct {\n")
    out.write("\t\tuint8_t is_set;\n")
    for arg in multiset_msg.args:
        if isinstance(arg.argType, arsdkparser.ArEnum):
            # FIXME: use a real enum type
            out.write("\t\tint32_t")
        elif isinstance(arg.argType, arsdkparser.ArBitfield):
            out.write("\t\t%s", _get_arg_type_c_name(arg.argType.btfType))
        else:
            out.write("\t\t%s", _get_arg_type_c_name(arg.argType))
        if arg.argType != arsdkparser.ArArgType.STRING:
            out.write(" ")
        out.write("%s;\n" % arg.name)
    out.write("\t} %s;\n", _get_multiset_msg_c_name(multiset_msg))

def gen_multisets_h(ctx, out):
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        for multiset in featureObj.multisets:
            out.write(_get_multiset_c_name(featureObj, multiset)+" {\n")
            for msg in multiset.msgs:
                _gen_multiset_msg_h(ctx, out, msg)
            out.write("};\n")
#===============================================================================
#===============================================================================
def gen_cmd_desc_h(ctx, out):
    out.write("#define ARSDK_MAX_ARGS_COUNT %d\n", _get_max_args_count(ctx))

    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        if featureObj.classes:
            for classId in sorted(featureObj.classesById.keys()):
                classObj = featureObj.classesById[classId]
                for cmdId in sorted(classObj.cmdsById.keys()):
                    cmdObj = classObj.cmdsById[cmdId]
                    out.write("extern ARSDK_API const struct arsdk_cmd_desc g_arsdk_cmd_desc_%s_%s_%s;\n",
                            _to_c_name(featureObj.name),
                            _to_c_name(classObj.name),
                            _to_c_name(cmdObj.name))
        else:
            for msgId in sorted(featureObj.getMsgsById().keys()):
                msgObj = featureObj.getMsgsById()[msgId]
                out.write("extern ARSDK_API const struct arsdk_cmd_desc g_arsdk_cmd_desc_%s_%s;\n",
                        _to_c_name(featureObj.name),
                        _to_c_name(msgObj.name))
    out.write("extern ARSDK_API const struct arsdk_cmd_desc * const * const *g_arsdk_cmd_desc_table[];\n")
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        for multiset in featureObj.multisets:
            out.write("extern const struct arsdk_cmd_desc * const g_arsdk_cmd_desc_multiset_%s_table[];\n",
                    _to_c_name(multiset.name))

#===============================================================================
#===============================================================================
def gen_cmd_desc_c(ctx, out):
    out.write("#include \"arsdk/arsdk.h\"\n")
    out.write("\n")
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]

        # Enum descriptions
        for enum in featureObj.enums:
            out.write("static const struct arsdk_enum_desc s_enum_desc_%s_%s[] = {\n",
                        _to_c_name(featureObj.name),
                        _to_c_name(enum.name))
            for enumVal in enum.values:
                out.write("\t{\"%s\", ARSDK_%s_%s_%s},\n",
                        _to_c_enum(enumVal.name),
                        _to_c_enum(featureObj.name),
                        _to_c_enum(enum.name),
                        _to_c_enum(enumVal.name))
            out.write("};\n\n")

        for msgObj in featureObj.getMsgs():
            # Generate msg part names
            msgName = _to_c_name(msgObj.name) if msgObj.cls == None else \
                    _to_c_name(msgObj.cls.name)+'_'+_to_c_name(msgObj.name)
            msgEnum = _to_c_enum(msgObj.name) if msgObj.cls == None  else \
                    _to_c_enum(msgObj.cls.name)+'_'+_to_c_enum(msgObj.name)
            msgDec = _to_c_name(msgObj.name) if msgObj.cls == None  else \
                    _to_c_name(msgObj.cls.name)+'.'+_to_c_name(msgObj.name)

            # Argument descriptions
            if len(msgObj.args) != 0:
                out.write("static const struct arsdk_arg_desc s_arg_desc_%s_%s[] = {\n",
                        _to_c_name(featureObj.name),
                        msgName)
                for argObj in msgObj.args:
                    enumDescTable = "NULL"
                    enumDescCount = "0"
                    if isinstance(argObj.argType, arsdkparser.ArEnum):
                        enumDescTable = "s_enum_desc_%s_%s" % (
                                _to_c_name(featureObj.name),
                                _to_c_name(argObj.argType.name))
                        enumDescCount = "sizeof(%s) / sizeof(%s[0])" % (
                                enumDescTable, enumDescTable)
                    elif isinstance(argObj.argType, arsdkparser.ArBitfield):
                        featureName = featureObj.name
                        if argObj.argType.enum.name == "list_flags":
                            featureName = "generic"
                        enumDescTable = "s_enum_desc_%s_%s" % (
                            _to_c_name(featureName),
                            _to_c_name(argObj.argType.enum.name))
                        enumDescCount = "sizeof(%s) / sizeof(%s[0])" % (
                            enumDescTable, enumDescTable)

                    if isinstance(argObj.argType, arsdkparser.ArEnum):
                        typestr = "ENUM"
                    elif isinstance(argObj.argType, arsdkparser.ArBitfield):
                        typestr = _to_c_enum(arsdkparser.ArArgType.TO_STRING[argObj.argType.btfType])
                    elif isinstance(argObj.argType, arsdkparser.ArMultiSetting):
                        typestr = "MULTISET"
                    else:
                        typestr = _to_c_enum(arsdkparser.ArArgType.TO_STRING[argObj.argType])
                    out.write("\t{\n\t\t\"%s\",\n\t\tARSDK_ARG_TYPE_%s,\n\t\t%s,\n\t\t%s\n\t},\n",
                            _to_c_name(argObj.name),
                            typestr,
                            enumDescTable,
                            enumDescCount)
                out.write("};\n\n")
            # Command description
            out.write("/*extern*/ const struct arsdk_cmd_desc g_arsdk_cmd_desc_%s_%s = {\n",
                    _to_c_name(featureObj.name),
                    msgName)
            argDescTable = "NULL"
            argDescCount = "0"
            if len(msgObj.args) != 0:
                argDescTable = "s_arg_desc_%s_%s" % (
                        _to_c_name(featureObj.name),
                        msgName)
                argDescCount = "sizeof(%s) / sizeof(%s[0])" % (
                        argDescTable, argDescTable)
            out.write("\t\"%s.%s\",\n",
                    _to_c_name(featureObj.name),
                    msgDec)
            out.write("\tARSDK_PRJ_%s,\n",
                    _to_c_enum(featureObj.name))
            if msgObj.cls:
                out.write("\tARSDK_CLS_%s_%s,\n",
                        _to_c_enum(featureObj.name),
                        _to_c_enum(msgObj.cls.name))
            else:
                out.write("\tARSDK_CLS_DEFAULT,\n")
            out.write("\tARSDK_CMD_%s_%s,\n",
                    _to_c_enum(featureObj.name),
                    msgEnum)
            out.write("\tARSDK_CMD_LIST_TYPE_%s,\n",
                    _to_c_enum(arsdkparser.ArCmdListType.TO_STRING[msgObj.listType]))
            out.write("\tARSDK_CMD_BUFFER_TYPE_%s,\n",
                    _to_c_enum(arsdkparser.ArCmdBufferType.TO_STRING[msgObj.bufferType]))
            out.write("\tARSDK_CMD_TIMEOUT_POLICY_%s,\n",
                    _to_c_enum(arsdkparser.ArCmdTimeoutPolicy.TO_STRING[msgObj.timeoutPolicy]))
            out.write("\t%s,\n\t%s\n", argDescTable, argDescCount)
            out.write("};\n\n")

    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]

        if featureObj.classes:
            for classId in sorted(featureObj.classesById.keys()):
                classObj = featureObj.classesById[classId]
                out.write("static const struct arsdk_cmd_desc * const s_arsdk_cmd_desc_%s_%s_table[] = {\n",
                        _to_c_name(featureObj.name),
                        _to_c_name(classObj.name))
                for cmdId in sorted(classObj.cmdsById.keys()):
                    cmdObj = classObj.cmdsById[cmdId]
                    out.write("\t&g_arsdk_cmd_desc_%s_%s_%s,\n",
                            _to_c_name(featureObj.name),
                            _to_c_name(classObj.name),
                            _to_c_name(cmdObj.name))
                out.write("\tNULL,\n")
                out.write("};\n\n")
        else:
             out.write("static const struct arsdk_cmd_desc * const s_arsdk_cmd_desc_%s_Default_table[] = {\n",
                        _to_c_name(featureObj.name))
             for cmdId in sorted(featureObj.cmdsById.keys()):
                    cmdObj = featureObj.cmdsById[cmdId]
                    out.write("\t&g_arsdk_cmd_desc_%s_%s,\n",
                            _to_c_name(featureObj.name),
                            _to_c_name(cmdObj.name))
             for cmdId in sorted(featureObj.evtsById.keys()):
                    cmdObj = featureObj.evtsById[cmdId]
                    out.write("\t&g_arsdk_cmd_desc_%s_%s,\n",
                            _to_c_name(featureObj.name),
                            _to_c_name(cmdObj.name))
             out.write("\tNULL,\n")
             out.write("};\n\n")

    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        out.write("static const struct arsdk_cmd_desc * const *s_arsdk_cmd_desc_%s_table[] = {\n",
                _to_c_name(featureObj.name))
        if featureObj.classes:
            for classId in sorted(featureObj.classesById.keys()):
                classObj = featureObj.classesById[classId]
                out.write("\ts_arsdk_cmd_desc_%s_%s_table,\n",
                        _to_c_name(featureObj.name),
                        _to_c_name(classObj.name))
        else:
             out.write("\ts_arsdk_cmd_desc_%s_Default_table,\n",
                        _to_c_name(featureObj.name))
        out.write("\tNULL,\n")
        out.write("};\n\n")

    out.write("/*extern*/ const struct arsdk_cmd_desc * const * const *g_arsdk_cmd_desc_table[] = {\n")
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        out.write("\ts_arsdk_cmd_desc_%s_table,\n",
                _to_c_name(featureObj.name))
    out.write("\tNULL,\n")
    out.write("};\n\n")

    # Multiset descriptions
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        for multiset in featureObj.multisets:
            out.write("/*extern*/ const struct arsdk_cmd_desc * const g_arsdk_cmd_desc_multiset_%s_table[] = {\n",
                    _to_c_name(multiset.name))
            for msgObj in multiset.msgs:
                if msgObj.cls:
                    out.write("\t&g_arsdk_cmd_desc_%s_%s_%s,\n",
                            _to_c_name(msgObj.ftr.name),
                            _to_c_name(msgObj.cls.name),
                            _to_c_name(msgObj.name))
                else:
                    out.write("\t&g_arsdk_cmd_desc_%s_%s,\n",
                            _to_c_name(msgObj.ftr.name),
                            _to_c_name(msgObj.name))
            out.write("\tNULL,\n")
            out.write("};\n\n")

#===============================================================================
#===============================================================================
def gen_cmd_dec_h(ctx, out):
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]

        for msgObj in featureObj.getMsgs():
            msgName = _to_c_name(msgObj.name) if msgObj.cls == None else \
                    _to_c_name(msgObj.cls.name)+'_'+_to_c_name(msgObj.name)

            if list(_get_args_multiset(msgObj.args)):
                out.write("ARSDK_API int\n")
            else:
                out.write("static inline int\n")
            out.write("__attribute__ ((warn_unused_result))\n")
            out.write("arsdk_cmd_dec_%s_%s(\n",
                    _to_c_name(featureObj.name),
                    msgName)
            out.write("\t\tconst struct arsdk_cmd *cmd")
            for argObj in msgObj.args:

                if isinstance(argObj.argType, arsdkparser.ArEnum):
                    # FIXME: use a real enum type
                    out.write(",\n\t\tint32_t")
                elif isinstance(argObj.argType, arsdkparser.ArBitfield):
                    out.write(",\n\t\t%s",
                            _get_arg_type_c_name(argObj.argType.btfType))
                elif isinstance(argObj.argType, arsdkparser.ArMultiSetting):
                    out.write(",\n\t\t%s",
                            _get_multiset_c_name(featureObj, argObj.argType))
                else:
                    out.write(",\n\t\t%s", _get_arg_type_c_name(argObj.argType))
                if argObj.argType != arsdkparser.ArArgType.STRING:
                    out.write(" ")
                out.write("*_%s" % argObj.name)

            if list(_get_args_multiset(msgObj.args)):
                out.write(");\n\n")
            else:
                out.write(") {\n")
                out.write("\treturn arsdk_cmd_dec(cmd,\n")
                out.write("\t\t\t&g_arsdk_cmd_desc_%s_%s",
                        _to_c_name(featureObj.name),
                        msgName)
                for argObj in msgObj.args:
                    out.write(",\n\t\t\t_%s" % argObj.name)
                out.write(");\n")
                out.write("}\n\n")

        for msgObj in _get_msgs_with_multiset(featureObj.getMsgs()):
            msgName = _to_c_name(msgObj.name) if msgObj.cls == None else \
                    _to_c_name(msgObj.cls.name)+'_'+_to_c_name(msgObj.name)

            out.write("ARSDK_API int arsdk_cmd_dec_%s_%s_in_cmds(\n",
                    _to_c_name(featureObj.name), msgName)
            out.write("\t\tconst struct arsdk_cmd *cmd,\n")
            out.write("\t\tvoid (*cb)(const struct arsdk_cmd *cmd, "
                    "void *userdata),\n")
            out.write("\t\tvoid *userdata);\n\n")

#===============================================================================
#===============================================================================
def gen_cmd_dec_c(ctx, out):
    out.write("#include \"arsdk/arsdk.h\"\n")
    out.write("/* Log header */\n")
    out.write("#define ULOG_TAG arsdk\n")
    out.write("#include \"arsdk/internal/arsdk_log.h\"\n")
    out.write("\n")
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]

        for msgObj in _get_msgs_with_multiset(featureObj.getMsgs()):
            msgName = _to_c_name(msgObj.name) if msgObj.cls == None else \
                    _to_c_name(msgObj.cls.name)+'_'+_to_c_name(msgObj.name)

            out.write("int arsdk_cmd_dec_%s_%s(\n",
                    _to_c_name(featureObj.name),
                    msgName)
            out.write("\t\tconst struct arsdk_cmd *cmd")
            for argObj in msgObj.args:

                if isinstance(argObj.argType, arsdkparser.ArEnum):
                    # FIXME: use a real enum type
                    out.write(",\n\t\tint32_t")
                elif isinstance(argObj.argType, arsdkparser.ArBitfield):
                    out.write(",\n\t\t%s",
                            _get_arg_type_c_name(argObj.argType.btfType))
                elif isinstance(argObj.argType, arsdkparser.ArMultiSetting):
                    out.write(",\n\t\t%s",
                            _get_multiset_c_name(featureObj, argObj.argType))
                else:
                    out.write(",\n\t\t%s", _get_arg_type_c_name(argObj.argType))
                if argObj.argType != arsdkparser.ArArgType.STRING:
                    out.write(" ")
                out.write("*_%s" % argObj.name)
            out.write(") {\n")

            arg = list(_get_args_multiset(msgObj.args))[0]
            multiset = arg.argType
            out.write("\tint res = 0;\n")
            out.write("\tsize_t cmd_i = 0;\n")
            out.write("\tstruct arsdk_cmd cmds[%d];\n", len(multiset.msgs))
            out.write("\tstruct arsdk_cmd *single_cmd = NULL;\n")
            out.write("\tstruct arsdk_multiset multi = {\n")
            out.write("\t\t.descs = g_arsdk_cmd_desc_multiset_%s_table,\n",
                _to_c_name(argObj.argType.name))
            out.write("\t\t.n_descs = %d,\n", len(multiset.msgs))
            out.write("\t\t.cmds = cmds,\n")
            out.write("\t\t.n_cmds = 0,\n")
            out.write("\t};\n")
            out.write("\n")

            out.write("\tres = arsdk_cmd_dec(cmd,\n")
            out.write("\t\t\t&g_arsdk_cmd_desc_%s_%s,\n",
                    _to_c_name(featureObj.name),
                    msgName)
            out.write("\t\t\t&multi);\n")
            out.write("\tif (res < 0)\n")
            out.write("\t\treturn res;\n")
            out.write("\n")

            out.write("\t/* reset multiset */\n")
            out.write("\tmemset(_settings, 0, sizeof(*_settings));\n")
            out.write("\n")

            out.write("\tfor (cmd_i = 0; cmd_i < multi.n_cmds; cmd_i++) {\n")
            out.write("\t\tsingle_cmd = &multi.cmds[cmd_i];\n")
            out.write("\n")

            out.write("\t\tswitch (single_cmd->id) {\n")
            for mset_msg in multiset.msgs:
                if mset_msg.cls:
                    mset_msg_name = "%s_%s" % \
                            (_to_c_name(mset_msg.cls.name),
                            _to_c_name(mset_msg.name))
                    mset_msg_id = "ARSDK_ID_%s_%s_%s" % \
                             (_to_c_enum(mset_msg.ftr.name),
                             _to_c_enum(mset_msg.cls.name),
                             _to_c_enum(mset_msg.name))
                else:
                    mset_msg_name = _to_c_name(mset_msg.name)
                    mset_msg_id = "ARSDK_ID_%s_%s" % \
                             (_to_c_enum(mset_msg.ftr.name),
                             _to_c_enum(mset_msg.name))

                out.write("\t\tcase %s:\n", mset_msg_id)
                out.write("\t\t\tres = arsdk_cmd_dec_%s_%s(single_cmd,\n",
                        _to_c_name(mset_msg.ftr.name),
                        mset_msg_name)
                for mset_msg_arg in mset_msg.args:
                    out.write("\t\t\t\t\t&_%s->%s.%s",
                            arg.name,
                            _get_multiset_msg_c_name(mset_msg),
                            mset_msg_arg.name)
                    if mset_msg_arg == mset_msg.args[-1]:
                        out.write(");\n")
                    else:
                        out.write(",\n")
                out.write("\t\t\tif (res < 0)\n")
                out.write('\t\t\t\tARSDK_LOGE("arsdk_cmd_dec_%s_%s failed :'
                        ' err=%%d(%%s)", -res, strerror(-res));\n',
                        _to_c_name(mset_msg.ftr.name),
                        mset_msg_name)
                out.write("\t\t\t_%s->%s.is_set = 1;\n",
                        arg.name,
                        _get_multiset_msg_c_name(mset_msg))

                out.write("\t\t\tbreak;\n")
                out.write("\n")
            out.write("\t\tdefault:\n")
            out.write("\t\t\t/* Message not expected */\n")
            out.write("\t\t\tbreak;\n")
            out.write("\t\t}\n")
            out.write("\n")


            out.write("\t\t/* Cleanup command */\n")
            out.write("\t\tarsdk_cmd_clear(single_cmd);\n")
            out.write("\t}\n")
            out.write("\n")

            out.write("\treturn res;\n")
            out.write("}\n\n")

        for msgObj in _get_msgs_with_multiset(featureObj.getMsgs()):
            msgName = _to_c_name(msgObj.name) if msgObj.cls == None else \
                    _to_c_name(msgObj.cls.name)+'_'+_to_c_name(msgObj.name)

            out.write("int arsdk_cmd_dec_%s_%s_in_cmds(\n",
                    _to_c_name(featureObj.name), msgName)
            out.write("\t\tconst struct arsdk_cmd *cmd,\n")
            out.write("\t\tvoid (*cb)(const struct arsdk_cmd *cmd, "
                      "void *userdata),\n")
            out.write("\t\tvoid *userdata) {\n")

            arg = list(_get_args_multiset(msgObj.args))[0]
            multiset = arg.argType
            out.write("\tint res = 0;\n")
            out.write("\tsize_t cmd_i = 0;\n")
            out.write("\tstruct arsdk_cmd cmds[%d];\n", len(multiset.msgs))
            out.write("\tstruct arsdk_cmd *single_cmd = NULL;\n")
            out.write("\tstruct arsdk_multiset multi = {\n")
            out.write("\t\t.descs = g_arsdk_cmd_desc_multiset_%s_table,\n",
                _to_c_name(argObj.argType.name))
            out.write("\t\t.n_descs = %d,\n", len(multiset.msgs))
            out.write("\t\t.cmds = cmds,\n")
            out.write("\t\t.n_cmds = 0,\n")
            out.write("\t};\n")
            out.write("\n")

            out.write("\tARSDK_RETURN_ERR_IF_FAILED(cmd != NULL, -EINVAL);\n")
            out.write("\tARSDK_RETURN_ERR_IF_FAILED(cb != NULL, -EINVAL);\n")

            out.write("\tres = arsdk_cmd_dec(cmd,\n")
            out.write("\t\t\t&g_arsdk_cmd_desc_%s_%s,\n",
                    _to_c_name(featureObj.name), msgName)
            out.write("\t\t\t&multi);\n")
            out.write("\tif (res < 0)\n")
            out.write("\t\treturn res;\n")
            out.write("\n")

            out.write("\tfor (cmd_i = 0; cmd_i < multi.n_cmds; cmd_i++) {\n")
            out.write("\t\tsingle_cmd = &multi.cmds[cmd_i];\n")
            out.write("\n")

            out.write("\t\tif (")

            for mset_msg in multiset.msgs:
                if mset_msg.cls:
                    mset_msg_name = "%s_%s" % \
                            (_to_c_name(mset_msg.cls.name),
                            _to_c_name(mset_msg.name))
                    mset_msg_id = "ARSDK_ID_%s_%s_%s" % \
                             (_to_c_enum(mset_msg.ftr.name),
                             _to_c_enum(mset_msg.cls.name),
                             _to_c_enum(mset_msg.name))
                else:
                    mset_msg_name = _to_c_name(mset_msg.name)
                    mset_msg_id = "ARSDK_ID_%s_%s" % \
                             (_to_c_enum(mset_msg.ftr.name),
                             _to_c_enum(mset_msg.name))
                if mset_msg is not multiset.msgs[0]:
                    out.write(" ||\n\t\t    ")
                out.write("(single_cmd->id == %s)", mset_msg_id)
            out.write(") {\n")
            out.write("\t\t\t(*cb)(single_cmd, userdata);\n")
            out.write("\t\t}\n")
            out.write("\n")
            out.write("\t\t/* Cleanup command */\n")
            out.write("\t\tarsdk_cmd_clear(single_cmd);\n")
            out.write("\t}\n")
            out.write("\n")

            out.write("\treturn res;\n")
            out.write("}\n\n")

#===============================================================================
#===============================================================================
def gen_cmd_enc_h(ctx, out):
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        for msgObj in featureObj.getMsgs():
            msgName = _to_c_name(msgObj.name) if msgObj.cls == None else \
                    _to_c_name(msgObj.cls.name)+'_'+_to_c_name(msgObj.name)

            if list(_get_args_multiset(msgObj.args)):
                out.write("ARSDK_API int\n")
            else:
                out.write("static inline int\n")
            out.write("arsdk_cmd_enc_%s_%s(\n",
                    _to_c_name(featureObj.name),
                    msgName)
            out.write("\t\tstruct arsdk_cmd *cmd")
            for argObj in msgObj.args:
                if isinstance(argObj.argType, arsdkparser.ArEnum):
                    # FIXME: use a real enum type
                    out.write(",\n\t\tint32_t")
                elif isinstance(argObj.argType, arsdkparser.ArBitfield):
                    out.write(",\n\t\t%s",
                            _get_arg_type_c_name(argObj.argType.btfType))
                elif isinstance(argObj.argType, arsdkparser.ArMultiSetting):
                    out.write(",\n\t\t%s *",
                            _get_multiset_c_name(featureObj, argObj.argType))
                else:
                    out.write(",\n\t\t%s", _get_arg_type_c_name(argObj.argType))

                if argObj.argType != arsdkparser.ArArgType.STRING or \
                   isinstance(argObj.argType, arsdkparser.ArMultiSetting):
                    out.write(" ")
                out.write("_%s" % argObj.name)
            if list(_get_args_multiset(msgObj.args)):
                out.write(");\n\n")
            else:
                out.write(") {\n")
                out.write("\treturn arsdk_cmd_enc(cmd,\n")
                out.write("\t\t\t&g_arsdk_cmd_desc_%s_%s",
                        _to_c_name(featureObj.name),
                        msgName)
                for argObj in msgObj.args:
                    out.write(",\n\t\t\t_%s" % argObj.name)
                out.write(");\n")
                out.write("}\n\n")

#===============================================================================
#===============================================================================
def gen_cmd_enc_c(ctx, out):
    out.write('#include "arsdk/arsdk.h"\n')
    out.write("\n")
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        for msgObj in _get_msgs_with_multiset(featureObj.getMsgs()):
            msgName = _to_c_name(msgObj.name) if msgObj.cls == None else \
                    _to_c_name(msgObj.cls.name)+'_'+_to_c_name(msgObj.name)

            out.write("int\narsdk_cmd_enc_%s_%s(\n",
                    _to_c_name(featureObj.name),
                    msgName)
            out.write("\t\tstruct arsdk_cmd *cmd")
            for argObj in msgObj.args:
                if isinstance(argObj.argType, arsdkparser.ArEnum):
                    # FIXME: use a real enum type
                    out.write(",\n\t\tint32_t")
                elif isinstance(argObj.argType, arsdkparser.ArBitfield):
                    out.write(",\n\t\t%s",
                            _get_arg_type_c_name(argObj.argType.btfType))
                elif isinstance(argObj.argType, arsdkparser.ArMultiSetting):
                    out.write(",\n\t\t%s *",
                            _get_multiset_c_name(featureObj, argObj.argType))
                else:
                    out.write(",\n\t\t%s", _get_arg_type_c_name(argObj.argType))

                if argObj.argType != arsdkparser.ArArgType.STRING or \
                   isinstance(argObj.argType, arsdkparser.ArMultiSetting):
                    out.write(" ")
                out.write("_%s" % argObj.name)
            out.write(") {\n")
            arg = list(_get_args_multiset(msgObj.args))[0]
            multiset = arg.argType
            out.write("\tstruct arsdk_cmd cmds[%d];\n", len(multiset.msgs))
            out.write("\tint res = 0;\n")
            out.write("\tsize_t cmd_i = 0;\n")
            out.write("\tstruct arsdk_multiset multi = {\n")
            out.write("\t\t.descs = g_arsdk_cmd_desc_multiset_%s_table,\n",
                    _to_c_name(argObj.argType.name))
            out.write("\t\t.n_descs = %d,\n", len(multiset.msgs))
            out.write("\t\t.cmds = cmds,\n")
            out.write("\t\t.n_cmds = 0,\n")
            out.write("\t};\n")
            out.write("\n")

            for mset_msg in multiset.msgs:
                out.write("\tif (_%s->%s.is_set) {\n",
                        arg.name,
                        _get_multiset_msg_c_name(mset_msg))
                out.write("\t\tres = arsdk_cmd_enc_%s_%s(\n",
                        _to_c_name(mset_msg.ftr.name),
                        _get_msg_name(mset_msg))
                out.write("\t\t\t\t&multi.cmds[multi.n_cmds],\n")
                for mset_msg_arg in mset_msg.args:
                    out.write("\t\t\t\t_%s->%s.%s", arg.name,
                            _get_multiset_msg_c_name(mset_msg),
                           mset_msg_arg.name)
                    if mset_msg_arg == mset_msg.args[-1]:
                        out.write(");\n")
                    else:
                        out.write(",\n")
                out.write("\t\tif (res < 0)\n")
                out.write("\t\t\tgoto error;\n")
                out.write("\n")
                out.write("\t\tmulti.n_cmds++;\n")
                out.write("\t}\n")
                out.write("\n")

            out.write("\tres = arsdk_cmd_enc(cmd,\n")
            out.write("\t\t\t&g_arsdk_cmd_desc_%s_%s,\n",
                    _to_c_name(featureObj.name),
                    msgName)
            out.write("\t\t\t&multi);\n")
            out.write("\tif (res < 0)\n")
            out.write("\t\tgoto error;\n")
            out.write("\n")
            out.write("error:\n")
            out.write("\tfor (cmd_i = 0; cmd_i < multi.n_cmds; cmd_i++) {\n")
            out.write("\t\tarsdk_cmd_clear(&multi.cmds[cmd_i]);\n")
            out.write("\t}\n")
            out.write("\n")
            out.write("\treturn res;\n")
            out.write("}\n\n")

#===============================================================================
#===============================================================================
def gen_cmd_send_h(ctx, out):
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        for msgObj in featureObj.getMsgs():
            msgName = _to_c_name(msgObj.name) if msgObj.cls == None else \
                    _to_c_name(msgObj.cls.name)+'_'+_to_c_name(msgObj.name)
            out.write("static inline int\narsdk_cmd_send_%s_%s(\n",
                    _to_c_name(featureObj.name),
                    msgName)
            out.write("\t\tstruct arsdk_cmd_itf *itf,\n")
            out.write("\t\tarsdk_cmd_itf_send_status_cb_t send_status,\n")
            out.write("\t\tvoid *userdata")
            for argObj in msgObj.args:
                if isinstance(argObj.argType, arsdkparser.ArEnum):
                    # FIXME: use a real enum type
                    out.write(",\n\t\tint32_t")
                elif isinstance(argObj.argType, arsdkparser.ArBitfield):
                    out.write(",\n\t\t%s",
                        _get_arg_type_c_name(argObj.argType.btfType))
                elif isinstance(argObj.argType, arsdkparser.ArMultiSetting):
                    out.write(",\n\t\t%s *",
                        _get_multiset_c_name(featureObj, argObj.argType))
                else:
                    out.write(",\n\t\t%s", _get_arg_type_c_name(argObj.argType))
                if argObj.argType != arsdkparser.ArArgType.STRING and \
                   not isinstance(argObj.argType, arsdkparser.ArMultiSetting):
                    out.write(" ")
                out.write("_%s" % argObj.name)
            out.write(") {\n")
            out.write("\tint res = 0;\n")
            out.write("\tstruct arsdk_cmd cmd;\n")
            out.write("\tarsdk_cmd_init(&cmd);\n")
            out.write("\tres = arsdk_cmd_enc_%s_%s(\n",
                    _to_c_name(featureObj.name),
                    msgName)
            out.write("\t\t\t&cmd")
            for argObj in msgObj.args:
                out.write(",\n\t\t\t_%s" % argObj.name)
            out.write(");\n")
            out.write("\tif (res == 0)\n")
            out.write("\t\tres = arsdk_cmd_itf_send(itf, &cmd, send_status, userdata);\n")
            out.write("\tarsdk_cmd_clear(&cmd);\n")
            out.write("\treturn res;\n")
            out.write("}\n\n")

#===============================================================================
#===============================================================================

entries = [
    {"name": "arsdk_ids.h", "func": gen_ids_h},
    {"name": "arsdk_enums.h", "func": gen_enums_h},
    {"name": "arsdk_multisettings.h", "func": gen_multisets_h},
    {"name": "arsdk_cmd_desc.h", "func": gen_cmd_desc_h},
    {"name": "arsdk_cmd_desc.c", "func": gen_cmd_desc_c},
    {"name": "arsdk_cmd_dec.h", "func": gen_cmd_dec_h},
    {"name": "arsdk_cmd_dec.c", "func": gen_cmd_dec_c},
    {"name": "arsdk_cmd_enc.h", "func": gen_cmd_enc_h},
    {"name": "arsdk_cmd_enc.c", "func": gen_cmd_enc_c},
    {"name": "arsdk_cmd_send.h", "func": gen_cmd_send_h},
]

#===============================================================================
#===============================================================================
def list_files(ctx, outdir, extra):
    for entry in entries:
        print(os.path.join(outdir, entry["name"]))

#===============================================================================
#===============================================================================
def generate_files(ctx, outdir, extra):
    print("generate files")
    for entry in entries:
        filepath = os.path.join(outdir, entry["name"])
        func = entry["func"]
        with open(filepath, "w") as fileobj:
            func(ctx, Writer(fileobj))
