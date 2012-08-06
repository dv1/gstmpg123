#!/usr/bin/env python

from waflib.Build import BuildContext, CleanContext, InstallContext, UninstallContext

top = '.'
out = 'build'


def check_compiler_flag(conf, flag, lang):
	return conf.check(fragment = 'int main() { float f = 4.0; char c = f; return c - 4; }\n', execute = 0, define_ret = 0, msg = 'Checking for compiler switch %s' % flag, cxxflags = conf.env[lang + 'FLAGS'] + [flag], okmsg = 'yes', errmsg = 'no')  # the code inside fragment deliberately does an unsafe implicit cast float->char to trigger a compiler warning; sometimes, gcc does not tell about an unsupported parameter *unless* the code being compiled causes a warning


def check_compiler_flags_2(conf, cflags, ldflags, msg):
	return conf.check(fragment = 'int main() { float f = 4.0; char c = f; return c - 4; }\n', execute = 0, define_ret = 0, msg = msg, cxxflags = cflags, ldflags = ldflags, okmsg = 'yes', errmsg = 'no')  # the code inside fragment deliberately does an unsafe implicit cast float->char to trigger a compiler warning; sometimes, gcc does not tell about an unsupported parameter *unless* the code being compiled causes a warning


def add_compiler_flags(conf, env, flags, lang, compiler, uselib = ''):
	for flag in flags:
		if type(flag) == type(()):
			flag_candidate = flag[0]
			flag_alternative = flag[1]
		else:
			flag_candidate = flag
			flag_alternative = None

		if uselib:
			flags_pattern = lang + 'FLAGS_' + uselib
		else:
			flags_pattern = lang + 'FLAGS'

		if check_compiler_flag(conf, flag_candidate, compiler):
			env.prepend_value(flags_pattern, [flag_candidate])
		elif flag_alternative:
			if check_compiler_flag(conf, flag_alternative, compiler):
				env.prepend_value(flags_pattern, [flag_alternative])



def options(opt):
	opt.add_option('--enable-debug', action='store_true', default=False, help='enable debug build [default: %default]')
	opt.add_option('--with-package-name', action='store', default="gstmpg123 plug-in source release", help='specify package name to use in plugin [default: %default]')
	opt.add_option('--with-package-origin', action='store', default="Unknown package origin", help='specify package origin URL to use in plugin [default: %default]')
	opt.add_option('--disable-gstreamer-0-10', action='store_true', default=False, help='disables build for GStreamer 0.10 [default: enabled]')
	opt.add_option('--enable-gstreamer-1-0', action='store_true', default=False, help='enables build for GStreamer 1.0 [default: disabled]')
	opt.add_option('--plugin-install-path', action='store', default="${PREFIX}/lib/gstreamer-0.10", help='where to install the plugin for GStreamer 0.10 [default: %default]')
	opt.add_option('--plugin-install-path-1-0', action='store', default="${PREFIX}/lib/gstreamer-1.0", help='where to install the plugin for GStreamer 1.0 [default: %default]')
	opt.load('compiler_cc')



def configure(conf):
	import os


	conf.load('compiler_cc')


	# check and add compiler flags
	if conf.env['CFLAGS'] and conf.env['LINKFLAGS']:
		check_compiler_flags_2(conf, conf.env['CFLAGS'], conf.env['LINKFLAGS'], "Testing compiler flags %s and linker flags %s" % (' '.join(conf.env['CFLAGS']), ' '.join(conf.env['LINKFLAGS'])))
	elif conf.env['CFLAGS']:
		check_compiler_flags_2(conf, conf.env['CFLAGS'], '', "Testing compiler flags %s" % ' '.join(conf.env['CFLAGS']))
	elif conf.env['LINKFLAGS']:
		check_compiler_flags_2(conf, '', conf.env['LINKFLAGS'], "Testing linker flags %s" % ' '.join(conf.env['LINKFLAGS']))

	compiler_flags = ['-Wextra', '-Wall', '-std=c99', '-pedantic']
	if conf.options.enable_debug:
		compiler_flags += ['-O0', '-g3', '-ggdb']
	else:
		compiler_flags += ['-O2', '-s']

	add_compiler_flags(conf, conf.env, compiler_flags, 'C', 'CC')


	# test for mpg123
	conf.check_cfg(package='libmpg123 >= 1.13.0', uselib_store='MPG123', args='--cflags --libs', mandatory=1)


	# test for GStreamer libraries

	gst_0_10 = not conf.options.disable_gstreamer_0_10
	gst_1_0  = conf.options.enable_gstreamer_1_0

	conf.env['GSTREAMER_0_10_ENABLED'] = gst_0_10
	conf.env['GSTREAMER_1_0_ENABLED'] = gst_1_0

	from waflib import Logs

	original_env = conf.env
	if gst_0_10:
		conf.setenv('0_10', env=original_env.derive())
		conf.check_cfg(package='gstreamer-0.10 >= 0.10.36', uselib_store='GSTREAMER', args='--cflags --libs', mandatory=1)
		conf.check_cfg(package='gstreamer-base-0.10 >= 0.10.36', uselib_store='GSTREAMER_BASE', args='--cflags --libs', mandatory=1)
		conf.check_cfg(package='gstreamer-audio-0.10 >= 0.10.36', uselib_store='GSTREAMER_AUDIO', args='--cflags --libs', mandatory=1)
		conf.env['PLUGIN_INSTALL_PATH'] = os.path.abspath(os.path.expanduser(conf.options.plugin_install_path))
		conf.define('GST_PACKAGE_NAME', conf.options.with_package_name)
		conf.define('GST_PACKAGE_ORIGIN', conf.options.with_package_origin)
		conf.define('PACKAGE', "gstmpg123")
		conf.define('VERSION', "0.10.1")
		conf.write_config_header('0_10/config.h')
		Logs.info("GStreamer 0.10 support enabled. To build, type ./waf or ./waf build_0_10 ; to install, type ./waf install or ./waf install_0_10")
		conf.env['SOURCES'] = ['src/gstmpg123-0_10.c']
	if gst_1_0:
		conf.setenv('1_0', env=original_env.derive())
		conf.check_cfg(package='gstreamer-1.0 >= 0.11.92', uselib_store='GSTREAMER', args='--cflags --libs', mandatory=1)
		conf.check_cfg(package='gstreamer-base-1.0 >= 0.11.92', uselib_store='GSTREAMER_BASE', args='--cflags --libs', mandatory=1)
		conf.check_cfg(package='gstreamer-audio-1.0 >= 0.11.92', uselib_store='GSTREAMER_AUDIO', args='--cflags --libs', mandatory=1)
		conf.env['PLUGIN_INSTALL_PATH'] = os.path.abspath(os.path.expanduser(conf.options.plugin_install_path_1_0))
		conf.define('GST_PACKAGE_NAME', conf.options.with_package_name)
		conf.define('GST_PACKAGE_ORIGIN', conf.options.with_package_origin)
		conf.define('PACKAGE', "gstmpg123")
		conf.define('VERSION', "1.0.1")
		conf.write_config_header('1_0/config.h')
		Logs.info("GStreamer 1.0 support enabled. To build, type ./waf build_1_0 ; to install, type ./waf install_1_0")
		conf.env['SOURCES'] = ['src/gstmpg123-1_0.c']



def build(bld):
	if bld.variant == "0_10":
		if not bld.env['GSTREAMER_0_10_ENABLED']:
			bld.fatal('Support for GStreamer 0.10 is disabled')
	elif bld.variant == "1_0":
		if not bld.env['GSTREAMER_1_0_ENABLED']:
			bld.fatal('Support for GStreamer 1.0 is disabled')
	else:
		bld.fatal('Invalid build command "' + bld.variant + '"')

	bld(
		features = ['c', 'cshlib'],
		includes = ['.', 'src'],
		uselib = 'GSTREAMER GSTREAMER_BASE GSTREAMER_AUDIO MPG123 COMMON',
		target = 'gstmpg123',
		source = bld.env['SOURCES'],
		install_path = bld.env['PLUGIN_INSTALL_PATH']
	)



def init(ctx):
	from waflib.Build import BuildContext, CleanContext, InstallContext, UninstallContext

	for x in '0_10 1_0'.split():
		for y in (BuildContext, CleanContext, InstallContext, UninstallContext):
			name = y.__name__.replace('Context','').lower()
			class tmp(y):
				cmd = name + '_' + x
				variant = x

	def buildall(ctx):
		import waflib.Options
		for x in ['build_0_10', 'build_1_0']:
			waflib.Options.commands.insert(0, x)

	for y in (BuildContext, CleanContext, InstallContext, UninstallContext):
		class tmp(y):
			variant = '0_10'


