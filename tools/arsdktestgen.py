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

_DEFAULT_CLS_NAME = 'defaultcls'

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
        arsdkparser.ArArgType.BINARY: "struct arsdk_binary",
    }
    return table[argType]

def _get_arg_default_val(argType):
    table = {
        arsdkparser.ArArgType.I8: "-1",
        arsdkparser.ArArgType.U8: "1",
        arsdkparser.ArArgType.I16: "-3000",
        arsdkparser.ArArgType.U16: "3000",
        arsdkparser.ArArgType.I32: "-2000000000",
        arsdkparser.ArArgType.U32: "2000000000",
        arsdkparser.ArArgType.I64: "-500000000000",
        arsdkparser.ArArgType.U64: "500000000000",
        arsdkparser.ArArgType.FLOAT: "1.5",
        arsdkparser.ArArgType.DOUBLE: "-2.2",
        arsdkparser.ArArgType.STRING: "\"ABC\"",
	arsdkparser.ArArgType.BINARY: "&s_binary_val",
    }
    return table[argType]

def _get_ftr_cls(ftr):
    if ftr.classes:
        return ftr.classes
    else:
        defcls = arsdkparser.ArClass(_DEFAULT_CLS_NAME, 0, "", _DEFAULT_CLS_NAME)
        defcls.cmds = ftr.getMsgs()
        return [defcls]

def _get_msg_name(cls, msg):
    return _to_c_name(msg.name) if cls.name == _DEFAULT_CLS_NAME else \
            _to_c_name(cls.name)+'_'+_to_c_name(msg.name)

#===============================================================================
#===============================================================================
def gen_test_dec_cmds(ctx, ftr, cls, msg, out):
    out.write("static void test_dec_%s_%s_%s(const struct arsdk_cmd *cmd)\n",
            ftr.name, cls.name, msg.name)
    out.write("{\n")
    out.write("\tint res = 0;\n")
    for argObj in msg.args:
        if isinstance(argObj.argType, arsdkparser.ArEnum):
            out.write("\tint32_t")
        elif isinstance(argObj.argType, arsdkparser.ArBitfield):
            out.write("\t%s", _get_arg_type_c_name(argObj.argType.btfType))
        else:
            out.write("\t%s", _get_arg_type_c_name(argObj.argType))
        if argObj.argType != arsdkparser.ArArgType.STRING:
            out.write(" ")
        out.write("_%s;\n" % argObj.name)
    out.write("\n")

    msgName = _get_msg_name(cls, msg)

    out.write("\tres = arsdk_cmd_dec_%s_%s(\n",
        _to_c_name(ftr.name),
        msgName)
    out.write("\t\t\tcmd")
    for argObj in msg.args:
        out.write(",\n\t\t\t&_%s" % argObj.name)
    out.write(");\n")
    out.write("\tCU_ASSERT_EQUAL(res, 0);\n")
    out.write("\n")

    for argObj in msg.args:
        if argObj.argType == arsdkparser.ArArgType.STRING:
            out.write("\tCU_ASSERT_STRING_EQUAL(_%s, " % argObj.name)
        elif argObj.argType == arsdkparser.ArArgType.BINARY:
            pass
        else:
            out.write("\tCU_ASSERT_EQUAL(_%s, " % argObj.name)

        if isinstance(argObj.argType, arsdkparser.ArEnum):
            out.write("ARSDK_%s_%s_%s",
                _to_c_enum(ftr.name),
                _to_c_enum(argObj.argType.name),
                _to_c_enum(argObj.argType.values[0].name))
        elif isinstance(argObj.argType, arsdkparser.ArBitfield):
            out.write("%s",_get_arg_default_val(argObj.argType.btfType))
        elif argObj.argType == arsdkparser.ArArgType.BINARY:
            out.write("\tCU_ASSERT_EQUAL(_%s.len, s_binary_val.len);\n" % argObj.name)
            out.write("\tCU_ASSERT_EQUAL(memcmp(_%s.cdata, s_binary_val.cdata, s_binary_val.len), 0" % argObj.name)
        else:
            out.write("%s",_get_arg_default_val(argObj.argType))
        out.write(");\n")
    out.write("}\n")
    out.write("\n")

#===============================================================================
#===============================================================================
def gen_test_send_msg(ctx, out):
    out.write("static int test_send_msg(struct arsdk_cmd_itf *itf,\n")
    out.write("\t\tuint32_t id,\n")
    out.write("\t\tvoid *data)\n")
    out.write("{\n")
    out.write("\tint res = 0;\n")
    out.write("\tswitch (id) {\n")
    cmdid = 0
    for featureId in sorted(ctx.featuresById.keys()):
        ftr = ctx.featuresById[featureId]
        for cls in _get_ftr_cls(ftr):
            for msgObj in cls.cmds:
                out.write("\tcase %d:\n", cmdid)

                msgName = _get_msg_name(cls, msgObj)

                out.write("\t\tres = arsdk_cmd_send_%s_%s(\n",
                        _to_c_name(ftr.name),
                        msgName)
                out.write("\t\t\t\titf,\n")
                out.write("\t\t\t\t&send_status,\n")
                out.write("\t\t\t\tdata")

                for argObj in msgObj.args:
                    out.write(",\n")
                    if isinstance(argObj.argType, arsdkparser.ArEnum):
                        out.write("\t\t\t\tARSDK_%s_%s_%s",
                            _to_c_enum(ftr.name),
                            _to_c_enum(argObj.argType.name),
                            _to_c_enum(argObj.argType.values[0].name))
                    elif isinstance(argObj.argType, arsdkparser.ArBitfield):
                        out.write("\t\t\t\t%s",
                                _get_arg_default_val(argObj.argType.btfType))
                    else:
                        out.write("\t\t\t\t%s",
                            _get_arg_default_val(argObj.argType))
                out.write(");\n")
                out.write("\n")
                out.write("\t\tCU_ASSERT_EQUAL(res, 0);\n")

                cmdid +=1
                out.write("\t\tbreak;\n")
    out.write("\tcase %d:\n", cmdid)
    out.write("\t\treturn -1;\n")
    out.write("\tdefault:\n")
    out.write("\t\tCU_FAIL(\"bad cmdid\");\n")
    out.write("\t\tbreak;\n")
    out.write("\t}\n")
    out.write("\n")
    out.write("\treturn 0;\n")
    out.write("}\n")

#===============================================================================
#===============================================================================
def gen_test_rev_msg(ctx, out):
    out.write("static int test_check_rcv_msg(const struct arsdk_cmd *cmd,\n")
    out.write("\t\tuint32_t id)\n")
    out.write("{\n")
    out.write("\tswitch (id) {\n")
    cmdid = 0
    for featureId in sorted(ctx.featuresById.keys()):
        ftr = ctx.featuresById[featureId]
        for cls in _get_ftr_cls(ftr):
            for msg in cls.cmds:
                out.write("\tcase %d:\n", cmdid)

                out.write("\t\tCU_ASSERT_EQUAL(cmd->prj_id, %d);\n",
                        ftr.featureId)
                out.write("\t\tCU_ASSERT_EQUAL(cmd->cls_id, %d);\n",
                        cls.classId)
                out.write("\t\tCU_ASSERT_EQUAL(cmd->cmd_id, %d);\n", msg.cmdId)

                out.write("\t\ttest_dec_%s_%s_%s(cmd);\n",
                        ftr.name, cls.name, msg.name)

                cmdid +=1
                out.write("\t\tbreak;\n")
    out.write("\tcase %d:\n", cmdid)
    out.write("\t\treturn -1;\n")
    out.write("\tdefault:\n")
    out.write("\t\tCU_FAIL(\"bad cmdid\");\n")
    out.write("\t\tbreak;\n")
    out.write("\t}\n")
    out.write("\n")
    out.write("\treturn 0;\n")
    out.write("}\n")

#===============================================================================
#===============================================================================
def gen_protoc_c(ctx, out):
    out.write("#include <arsdk/arsdk.h>\n")
    out.write("#include \"arsdk_test.h\"\n")
    out.write("#include \"arsdk_test_protoc.h\"\n")
    out.write("\n")

    out.write("/** */\n")
    out.write("struct test_msg {\n")
    out.write("\tuint32_t msgid;\n")
    out.write("\tstruct arsdk_cmd_itf *cmd_itf;\n")
    out.write("};\n")
    out.write("\n")

    out.write("/** */\n")
    out.write("static struct test_msg s_test_msg = {\n")
    out.write("\t.msgid = 0,\n")
    out.write("\t.cmd_itf = NULL,\n")
    out.write("};\n")
    out.write("\n")

    out.write("/** */\n")
    out.write("static const uint8_t s_binary_data[] = {0x01, 0x02};\n")
    out.write("static const struct arsdk_binary s_binary_val = {\n")
    out.write("\t.cdata = s_binary_data,\n")
    out.write("\t.len = sizeof(s_binary_data),\n")
    out.write("};\n")
    out.write("\n")

    for featureId in sorted(ctx.featuresById.keys()):
        ftr = ctx.featuresById[featureId]
        for cls in _get_ftr_cls(ftr):
            for msg in cls.cmds:
                gen_test_dec_cmds(ctx, ftr, cls, msg, out)

    gen_test_rev_msg(ctx, out)

    out.write("/**\n")
    out.write(" */\n")
    out.write("static void send_status(struct arsdk_cmd_itf *itf,\n")
    out.write("\t\tconst struct arsdk_cmd *cmd,\n")
    out.write("\t\tenum arsdk_cmd_buffer_type type,\n")
    out.write("\t\tenum arsdk_cmd_itf_cmd_send_status status,\n")
    out.write("\t\tuint16_t seq,\n")
    out.write("\t\tint done,\n")
    out.write("\t\tvoid *userdata)\n")
    out.write("{\n")
    out.write("}\n")
    out.write("\n")

    gen_test_send_msg(ctx, out)

    out.write("static void test_send_next_msg(struct arsdk_cmd_itf *itf)\n")
    out.write("{\n")
    out.write("\tint res = test_send_msg(itf, s_test_msg.msgid, &s_test_msg);\n")
    out.write("\tif (res < 0) {\n")
    out.write("\t\ttest_end();\n")
    out.write("\t}\n")
    out.write("}\n")

    out.write("/**\n")
    out.write(" */\n")
    out.write("void test_dev_recv_cmd(const struct arsdk_cmd *cmd)\n")
    out.write("{\n")
    out.write("\ttest_check_rcv_msg(cmd, s_test_msg.msgid);\n")
    out.write("\ts_test_msg.msgid += 1;\n")

    out.write("\ttest_send_next_msg(s_test_msg.cmd_itf);\n")
    out.write("\n")
    out.write("}\n")

    out.write("void test_start_send_msgs(struct test_ctrl *ctrl)\n")
    out.write("{\n")
    out.write("\t/* Save cmd itf. */\n")
    out.write("\ts_test_msg.cmd_itf = test_ctrl_get_itf(ctrl);\n")
    out.write("\n")
    out.write("\tfprintf(stderr, \"send all existing commands ...\\n\");\n")
    out.write("\ttest_send_next_msg(s_test_msg.cmd_itf);\n")
    out.write("}\n")

#===============================================================================
#===============================================================================

entries = [
    {"name": "arsdk_test_protoc_gen.c", "func": gen_protoc_c},
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
