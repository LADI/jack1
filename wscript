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
    opt.load('compiler_c')
    opt.load('waf_autooptions')

    opt.set_auto_options_define('HAVE_%s')
    opt.set_auto_options_style('yesno_and_hack')

    alsa = opt.add_auto_option(
            'alsa',
            help='Enable ALSA driver',
            conf_dest='BUILD_DRIVER_ALSA')
    alsa.check_cfg(
            package='alsa >= 1.0.18',
            args='--cflags --libs')

    opt.add_auto_option(
        'debug',
        help='Enable debug symbols',
        conf_dest='BUILD_DEBUG',
        default=False,
    )

    opt.add_auto_option(
        'devmode',
        help='Enable devmode', # enable warnings and treat them as errors
        conf_dest='BUILD_DEVMODE',
        default=False,
    )

    # this must be called before the configure phase
    opt.apply_auto_options_hack()

def display_feature(conf, msg, build):
    if build:
        conf.msg(msg, 'yes', color='GREEN')
    else:
        conf.msg(msg, 'no', color='YELLOW')

def configure(conf):
    conf.load('waf_autooptions')

    flags = WafToolchainFlags(conf)

    conf.env['JACK_DRIVER_DIR'] = os.path.normpath(
        os.path.join(conf.env['PREFIX'],
                     'libexec',
                     'jack-driver'))
    conf.env['JACK_INTERNAL_DIR'] = os.path.normpath(
        os.path.join(conf.env['PREFIX'],
                     'libexec',
                     'jack-internal'))
    conf.define('JACK_DRIVER_DIR', conf.env['JACK_DRIVER_DIR'])
    conf.define('JACK_INTERNAL_DIR', conf.env['JACK_INTERNAL_DIR'])

    flags.add_c('-std=gnu99')
    if conf.env['BUILD_DEVMODE']:
        flags.add_c(['-Wall', '-Wextra'])
        flags.add_c('-Wpedantic')
        flags.add_c('-Werror')
        flags.add_c(['-Wno-variadic-macros', '-Wno-gnu-zero-variadic-macro-arguments'])

        # https://wiki.gentoo.org/wiki/Modern_C_porting
        if conf.env['CC'] == 'clang':
            flags.add_c('-Wno-unknown-argumemt')
            flags.add_c('-Werror=implicit-function-declaration')
            flags.add_c('-Werror=incompatible-function-pointer-types')
            flags.add_c('-Werror=deprecated-non-prototype')
            flags.add_c('-Werror=strict-prototypes')
            if int(conf.env['CC_VERSION'][0]) < 16:
                flags.add_c('-Werror=implicit-int')
        else:
            flags.add_c('-Wno-unknown-warning-option')
            flags.add_c('-Werror=implicit-function-declaration')
            flags.add_c('-Werror=implicit-int')
            flags.add_c('-Werror=incompatible-pointer-types')
            flags.add_c('-Werror=strict-prototypes')
    if conf.env['BUILD_DEBUG']:
        flags.add_c(['-O0', '-g', '-fno-omit-frame-pointer'])
        flags.add_link('-g')

    flags.flush()

    print()
    version_msg = APPNAME + "-" + VERSION
    if os.access('version.h', os.R_OK):
        data = open('version.h').read()
        m = re.match(r'^#define GIT_VERSION "([^"]*)"$', data)
        if m != None:
            version_msg += " exported from " + m.group(1)
    elif os.access('.git', os.R_OK):
        version_msg += " git revision will be checked and eventually updated during build"
    print(version_msg)

    conf.msg('Install prefix', conf.env['PREFIX'], color='CYAN')
    conf.msg('Library directory', conf.all_envs['']['LIBDIR'], color='CYAN')
    conf.msg('Drivers directory', conf.env['JACK_DRIVER_DIR'], color='CYAN')
    conf.msg('Internal clients directory', conf.env['JACK_INTERNAL_DIR'], color='CYAN')
    display_feature(conf, 'Build debuggable binaries', conf.env['BUILD_DEBUG'])

    tool_flags = [
        ('C compiler flags',   ['CFLAGS', 'CPPFLAGS']),
        ('Linker flags',       ['LINKFLAGS', 'LDFLAGS'])
    ]
    for name, vars in tool_flags:
        flags = []
        for var in vars:
            flags += conf.all_envs[''][var]
        conf.msg(name, repr(flags), color='NORMAL')

    conf.summarize_auto_options()

    print()

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
