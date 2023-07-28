#!/usr/bin/env python
# encoding: utf-8
#
# SPDX-FileCopyrightText: Copyright © 2023 Nedko Arnaudov
# SPDX-License-Identifier: GPL-3.0-or-later

import os
from waflib import Context
from waflib import Logs, Options, TaskGen
from waflib.Build import BuildContext, CleanContext, InstallContext, UninstallContext
from waf_toolchain_flags import WafToolchainFlags

VERSION = '1.121.4'
APPNAME = 'LADI JACK'
JACK_API_VERSION = VERSION

# these variables are mandatory ('/' are converted automatically)
top = '.'
out = 'build'

def options(opt):
    opt.load('waf_autooptions')
    return

def configure(conf):
    conf.load('waf_autooptions')
    return

def git_ver(self):
    bld = self.generator.bld
    header = self.outputs[0].abspath()
    if os.access('./version.h', os.R_OK):
        header = os.path.join(os.getcwd(), out, "version.h")
        shutil.copy('./version.h', header)
        data = open(header).read()
        m = re.match(r'^#define GIT_VERSION "([^"]*)"$', data)
        if m != None:
            self.ver = m.group(1)
            Logs.pprint('BLUE', "tarball from git revision " + self.ver)
        else:
            self.ver = "tarball"
        return

    if bld.srcnode.find_node('.git'):
        self.ver = bld.cmd_and_log("LANG= git rev-parse HEAD", quiet=Context.BOTH).splitlines()[0]
        if bld.cmd_and_log("LANG= git diff-index --name-only HEAD", quiet=Context.BOTH).splitlines():
            self.ver += "-dirty"

        Logs.pprint('BLUE', "git revision " + self.ver)
    else:
        self.ver = "unknown"

    fi = open(header, 'w')
    fi.write('#define GIT_VERSION "%s"\n' % self.ver)
    fi.close()


def build(bld):
    bld(rule=git_ver, target='version.h', update_outputs=True, always=True, ext_out=['.h'])
