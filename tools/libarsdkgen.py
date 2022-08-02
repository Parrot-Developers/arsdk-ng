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

#===============================================================================
#===============================================================================
def gen_cmd_desc_c(ctx, out):
    out.write("#include \"arsdk/arsdk.h\"\n\n")

    out.write("/* Disable compilation warnings*/\n")
    out.write("#ifdef __clang__\n")
    out.write("#pragma clang diagnostic push\n")
    out.write("#pragma clang diagnostic ignored \"-Wunused-const-variable\"\n")
    out.write("#elif defined(__GNUC__)\n")
    out.write("#pragma GCC diagnostic push\n")
    out.write("#if __GNUC__ > 5\n")
    out.write("#pragma GCC diagnostic ignored \"-Wunused-const-variable\"\n")
    out.write("#else\n")
    out.write("#pragma GCC diagnostic ignored \"-Wunused-variable\"\n")
    out.write("#endif\n")
    out.write("#endif\n")

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

    out.write("#ifdef __clang__\n")
    out.write("#pragma clang diagnostic pop\n")
    out.write("#elif defined(__GNUC__)\n")
    out.write("#pragma GCC diagnostic pop\n")
    out.write("#endif\n\n")

    out.write("const struct arsdk_cmd_desc * const * const **arsdk_get_cmd_table(void)\n")
    out.write("{\n")
    out.write("\treturn g_arsdk_cmd_desc_table;\n")
    out.write("}\n")

#===============================================================================
#===============================================================================
def gen_cmd_dec_h(ctx, out):
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]

        for msgObj in featureObj.getMsgs():
            msgName = _to_c_name(msgObj.name) if msgObj.cls == None else \
                    _to_c_name(msgObj.cls.name)+'_'+_to_c_name(msgObj.name)

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
                elif argObj.argType == arsdkparser.ArArgType.BINARY:
                    out.write(",\n\t\tstruct arsdk_binary")
                else:
                    out.write(",\n\t\t%s", _get_arg_type_c_name(argObj.argType))
                if argObj.argType != arsdkparser.ArArgType.STRING:
                    out.write(" ")
                out.write("*_%s" % argObj.name)

            out.write(") {\n")
            out.write("\treturn arsdk_cmd_dec(cmd,\n")
            out.write("\t\t\t&g_arsdk_cmd_desc_%s_%s",
                    _to_c_name(featureObj.name),
                    msgName)
            for argObj in msgObj.args:
                out.write(",\n\t\t\t_%s" % argObj.name)
            out.write(");\n")
            out.write("}\n\n")

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

#===============================================================================
#===============================================================================
def gen_cmd_enc_h(ctx, out):
    for featureId in sorted(ctx.featuresById.keys()):
        featureObj = ctx.featuresById[featureId]
        for msgObj in featureObj.getMsgs():
            msgName = _to_c_name(msgObj.name) if msgObj.cls == None else \
                    _to_c_name(msgObj.cls.name)+'_'+_to_c_name(msgObj.name)

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
                elif argObj.argType == arsdkparser.ArArgType.BINARY:
                    out.write(",\n\t\tconst struct arsdk_binary *")
                else:
                    out.write(",\n\t\t%s", _get_arg_type_c_name(argObj.argType))

                if argObj.argType != arsdkparser.ArArgType.STRING:
                    out.write(" ")
                out.write("_%s" % argObj.name)
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
            out.write("\t\tarsdk_cmd_itf_cmd_send_status_cb_t send_status,\n")
            out.write("\t\tvoid *userdata")
            for argObj in msgObj.args:
                if isinstance(argObj.argType, arsdkparser.ArEnum):
                    # FIXME: use a real enum type
                    out.write(",\n\t\tint32_t")
                elif isinstance(argObj.argType, arsdkparser.ArBitfield):
                    out.write(",\n\t\t%s",
                        _get_arg_type_c_name(argObj.argType.btfType))
                elif argObj.argType == arsdkparser.ArArgType.BINARY:
                    out.write(",\n\t\tconst struct arsdk_binary *")
                else:
                    out.write(",\n\t\t%s", _get_arg_type_c_name(argObj.argType))
                if argObj.argType != arsdkparser.ArArgType.STRING :
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
