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

APPNAME = 'LADI JACK'
JACK_VERSION_MAJOR = 1
JACK_VERSION_MINOR = 121
JACK_VERSION_PATCH = 4
JACK_API_REVISION=28

VERSION = str(JACK_VERSION_MAJOR) + '.' + str(JACK_VERSION_MINOR) + '.' + str(JACK_VERSION_PATCH)

# (LADI) shlib versions
#
#      LADI/jack-1.121.4     : 0.1.28 (jack-major=1, api-revision=28)
#      LADI/jack-2.23.0      : 0.2.23 (jack-major=2, jack-minor=23)
#      LADI/PipeWire-0.3.376 : 0.3.376 (jack-major=3, pipewire-libversion-minor=376)
# jackaudio/jack1-0.121      : 0.0.28 (JACK_API_CURRENT=0:JACK_API_REVISION=28:JACK_API_AGE=0)
# jackaudio/jack2-1.9.22     : 0.1.0
#  PipeWire/PipeWire-0.3.77  : 0.3.377 # PipeWire switched to 0.3.PWVER in pipewire-0.3.77
#  PipeWire/PipeWire-0.3.76  : 0.376.0
#
# Keep major at 0, as the shlib major is part of standard ld.so loading magic
JACK_API_VERSION = '0.' + str(JACK_VERSION_MAJOR) + '.' + str(JACK_API_REVISION)

# these variables are mandatory ('/' are converted automatically)
top = '.'
out = 'build'

def options(opt):
    opt.load('compiler_c')
    opt.load('waf_autooptions')

    opt.set_auto_options_define('HAVE_%s')
    opt.set_auto_options_style('yesno_and_hack')

    opt.add_option('--libdir', type='string', help='Library directory [Default: <prefix>/lib64]')
    opt.add_option('--pkgconfigdir', type='string', help='pkg-config file directory [Default: <libdir>/pkgconfig]')

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

    opt.add_option(
        '--enable-pkg-config-dbus-service-dir',
        action='store_true',
        default=False,
        help='force D-Bus service install dir to be one returned by pkg-config',
    )

    # this must be called before the configure phase
    opt.apply_auto_options_hack()

def display_feature(conf, msg, build):
    if build:
        conf.msg(msg, 'yes', color='GREEN')
    else:
        conf.msg(msg, 'no', color='YELLOW')

def configure(conf):
    conf.load('compiler_c')
    conf.load('waf_autooptions')

    conf.env['JACK_API_VERSION'] = JACK_API_VERSION

    flags = WafToolchainFlags(conf)

    if Options.options.libdir:
        conf.env['LIBDIR'] = Options.options.libdir
    else:
        conf.env['LIBDIR'] = conf.env['PREFIX'] + '/lib64'

    if Options.options.pkgconfigdir:
        conf.env['PKGCONFDIR'] = Options.options.pkgconfigdir
    else:
        conf.env['PKGCONFDIR'] = conf.env['LIBDIR'] + '/pkgconfig'

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

    if not conf.check_cfg(package='dbus-1 >= 1.0.0', args='--cflags --libs', mandatory=False):
        print(
            Logs.colors.RED + 'ERROR !! jackdbus will not be built because libdbus-dev is missing' + Logs.colors.NORMAL
        )
        return

    dbus_dir = conf.check_cfg(package='dbus-1', args='--variable=session_bus_services_dir')
    if not dbus_dir:
        print(
            Logs.colors.RED + 'ERROR !! jackdbus will not be built because service dir is unknown' + Logs.colors.NORMAL
        )
        return

    dbus_dir = dbus_dir.strip()
    conf.env['DBUS_SERVICES_DIR_REAL'] = dbus_dir

    if Options.options.enable_pkg_config_dbus_service_dir:
        conf.env['DBUS_SERVICES_DIR'] = dbus_dir
    else:
        conf.env['DBUS_SERVICES_DIR'] = os.path.normpath(conf.env['PREFIX'] + '/share/dbus-1/services')

    conf.check_cfg(package='expat', args='--cflags --libs')
    conf.env['LIB_M'] = ['m']

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

    conf.define('JACK_THREAD_STACK_TOUCH', 500000)
    conf.define('jack_protocol_version', 24)
    conf.define('JACK_SHM_TYPE', 'System V')
    conf.define('USE_POSIX_SHM', 0)
    conf.define('DEFAULT_TMP_DIR', '/dev/shm')
    conf.define('JACK_SEMAPHORE_KEY', 0x282929)
    conf.define('JACK_DEFAULT_DRIVER', 'dummy')
    conf.define('JACK_VERSION', VERSION)
    conf.write_config_header('config.h', remove=False)
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

    conf.msg('D-Bus service install directory', conf.env['DBUS_SERVICES_DIR'], color='CYAN')

    if conf.env['DBUS_SERVICES_DIR'] != conf.env['DBUS_SERVICES_DIR_REAL']:
        print()
        print(Logs.colors.RED + 'WARNING: D-Bus session services directory as reported by pkg-config is')
        print(Logs.colors.RED + 'WARNING:', end=' ')
        print(Logs.colors.CYAN + conf.env['DBUS_SERVICES_DIR_REAL'])
        print(Logs.colors.RED + 'WARNING: but service file will be installed in')
        print(Logs.colors.RED + 'WARNING:', end=' ')
        print(Logs.colors.CYAN + conf.env['DBUS_SERVICES_DIR'])
        print(
            Logs.colors.RED + 'WARNING: You may need to adjust your D-Bus configuration after installing jackdbus'
            )
        print('WARNING: You can override dbus service install directory')
        print('WARNING: with --enable-pkg-config-dbus-service-dir option to this script')
        print(Logs.colors.NORMAL)
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

    includes = ['.', './jack', './config', '..']

    clientlib = bld(features=['c', 'cshlib'])
    clientlib.defines = 'HAVE_CONFIG_H'
    clientlib.includes = includes
    clientlib.target = 'jack'
    clientlib.vnum = bld.env['JACK_API_VERSION']
    clientlib.install_path = '${LIBDIR}'
    clientlib.source = [
        "libjack/client.c",
        "libjack/intclient.c",
        "libjack/messagebuffer.c",
        "libjack/pool.c",
        "libjack/port.c",
        "libjack/midiport.c",
        "libjack/ringbuffer.c",
        "libjack/shm.c",
        "libjack/thread.c",
        "libjack/time.c",
        "libjack/timestamps.c",
        "libjack/transclient.c",
        "libjack/unlock.c",
    ]

    # process jack.pc.in -> jack.pc
    bld(
        features='subst_pc',
        source='jack.pc.in',
        target='jack.pc',
        install_path='${PKGCONFDIR}',
        JACK_VERSION=VERSION,
        INCLUDEDIR=os.path.normpath(bld.env['PREFIX'] + '/include'),
        CLIENTLIB=clientlib.target,
    )

    serverlib = bld(features=['c', 'cshlib'])
    serverlib.defines = 'HAVE_CONFIG_H'
    serverlib.includes = includes
    serverlib.target = 'jackserver'
    serverlib.vnum = bld.env['JACK_API_VERSION']
    serverlib.install_path = '${LIBDIR}'
    serverlib.source = [
        'server/engine.c',
        'server/clientengine.c',
        'server/transengine.c',
        'server/controlapi.c',
        'libjack/systemtest.c',
        'libjack/sanitycheck.c',
        'libjack/client.c',
        'libjack/driver.c',
        'libjack/intclient.c',
        'libjack/messagebuffer.c',
        'libjack/pool.c',
        'libjack/port.c',
        'libjack/midiport.c',
        'libjack/ringbuffer.c',
        'libjack/shm.c',
        'libjack/thread.c',
        'libjack/time.c',
        'libjack/transclient.c',
        'libjack/unlock.c',
    ]

    # process jackserver.pc.in -> jackserver.pc
    bld(
        features='subst_pc',
        source='jackserver.pc.in',
        target='jackserver.pc',
        install_path='${PKGCONFDIR}',
        JACK_VERSION=VERSION,
        INCLUDEDIR=os.path.normpath(bld.env['PREFIX'] + '/include'),
        SERVERLIB=serverlib.target,
    )

    obj = bld(features=['c', 'cprogram'])
    obj.defines = ['HAVE_CONFIG_H']
    obj.use = ['DBUS-1', 'EXPAT', 'M', 'jackserver']
    obj.includes = includes
    obj.source = [
        'server/engine.c',
        'server/clientengine.c',
        'server/transengine.c',
        'server/jackdbus.c',
        'server/jackctl.c',
        'server/jackcontroller.c',
        'server/jackcontroller_iface_introspectable.c',
        'server/jackcontroller_iface_control.c',
        'server/jackcontroller_iface_configure.c',
        'server/jackcontroller_iface_patchbay.c',
        'server/jackcontroller_iface_transport.c',
        'server/jackcontroller_xml_expat.c',
        'server/jackcontroller_xml.c',
        'server/jackcontroller_xml_write_raw.c',
        ]
    obj.target = 'jackdbus'

    # process org.jackaudio.service.in -> org.jackaudio.service
    obj = bld(
        features='subst',
        source='server/org.jackaudio.service.in',
        target='org.jackaudio.service',
        install_path='${DBUS_SERVICES_DIR}/',
        BINDIR=bld.env['PREFIX'] + '/bin')

    #bld.install_as('${PREFIX}/bin/' + "jack_control", 'jack_control/jack_control.py', chmod=Utils.O755)

    driver = bld(
        features=['c', 'cshlib'],
        defines=['HAVE_CONFIG_H'],
        includes=includes,
	use = ['serverlib'],
        target='dummy',
        install_path='${JACK_DRIVER_DIR}/')
    driver.env['cshlib_PATTERN'] = '%s.so'
    driver.source = ['drivers/dummy/dummy_driver.c']

    driver = bld(
        features=['c', 'cshlib'],
        defines=['HAVE_CONFIG_H'],
        includes=includes,
	use = ['ALSA', 'serverlib'],
        target='alsa',
        install_path='${JACK_DRIVER_DIR}/')
    driver.env['cshlib_PATTERN'] = '%s.so'
    driver.source = [
        'drivers/alsa/alsa_driver.c',
        'drivers/alsa/generic_hw.c',
        'drivers/alsa/memops.c',
        'drivers/alsa/hammerfall.c',
        'drivers/alsa/hdsp.c',
        'drivers/alsa/ice1712.c',
        'drivers/alsa/usx2y.c',
        'drivers/alsa-midi/alsa_rawmidi.c',
        'drivers/alsa-midi/alsa_seqmidi.c',
    ]

    driver = bld(
        features=['c', 'cshlib'],
        defines=['HAVE_CONFIG_H'],
        includes=includes,
	use = ['ALSA', 'serverlib'],
        target='alsa_midi',
        install_path='${JACK_DRIVER_DIR}/')
    driver.env['cshlib_PATTERN'] = '%s.so'
    driver.source = [
        'drivers/alsa-midi/alsa_rawmidi.c',
        'drivers/alsa-midi/alsa_seqmidi.c',
        'drivers/alsa-midi/alsa_midi_driver.c',
    ]
