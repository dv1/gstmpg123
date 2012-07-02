#!/usr/bin/env python

from waflib.Build import BuildContext, CleanContext, InstallContext, UninstallContext

top = '../..'
out = 'build'

import Options


def check_compiler_flag(conf, flag, lang):
	return conf.check(fragment = 'int main() { float f = 4.0; char c = f; return c - 4; }\n', execute = 0, define_ret = 0, msg = 'Checking for compiler switch %s' % flag, cxxflags = conf.env[lang + 'FLAGS'] + [flag], okmsg = 'yes', errmsg = 'no')  # the code inside fragment deliberately does an unsafe implicit cast float->char to trigger a compiler warning; sometimes, gcc does not tell about an unsupported parameter *unless* the code being compiled causes a warning



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
			env.append_value(flags_pattern, [flag_candidate])
		elif flag_alternative:
			if check_compiler_flag(conf, flag_alternative, compiler):
				env.append_value(flags_pattern, [flag_alternative])


def options(opt):
	opt.add_option('--debug', action='store_true', default=False, help='enable debug build')
	opt.tool_options('compiler_cc')


def configure(conf):
	conf.check_tool('compiler_cc')

	# test for GStreamer libraries
	conf.check_cfg(package='gstreamer-0.10', uselib_store='GSTREAMER', args='--cflags --libs', mandatory=1)
	conf.check_cfg(package='gstreamer-base-0.10', uselib_store='GSTREAMER_BASE', args='--cflags --libs', mandatory=1)
	conf.check_cfg(package='gstreamer-audio-0.10', uselib_store='GSTREAMER_AUDIO', args='--cflags --libs', mandatory=1)

	# test for mpg123
	conf.check_cfg(package='libmpg123', uselib_store='MPG123', args='--cflags --libs', mandatory=1)
	sampleformats_to_test_for = [ \
		{ "label" :  "8 bit unsigned", "name" : "MPG123_ENC_UNSIGNED_8"  }, \
		{ "label" :  "8 bit signed",   "name" : "MPG123_ENC_SIGNED_8"    }, \
		{ "label" : "16 bit unsigned", "name" : "MPG123_ENC_UNSIGNED_16" }, \
		{ "label" : "16 bit signed",   "name" : "MPG123_ENC_SIGNED_16"   }, \
		{ "label" : "24 bit unsigned", "name" : "MPG123_ENC_UNSIGNED_24" }, \
		{ "label" : "24 bit signed",   "name" : "MPG123_ENC_SIGNED_24"   }, \
		{ "label" : "32 bit unsigned", "name" : "MPG123_ENC_UNSIGNED_32" }, \
		{ "label" : "32 bit signed",   "name" : "MPG123_ENC_SIGNED_32"   }, \
		{ "label" : "32 bit float",    "name" : "MPG123_ENC_FLOAT_32"    }, \
		{ "label" : "64 bit float",    "name" : "MPG123_ENC_FLOAT_64"    }, \
		{ "label" :  "8 bit A-law",    "name" : "MPG123_ENC_ALAW_8"      }, \
		{ "label" :  "8 bit mu-law",   "name" : "MPG123_ENC_ULAW_8"      } \
	]
	for sampleformat in sampleformats_to_test_for:
		conf.check_cc( \
			fragment = '#include <mpg123.h>\nint main() { return %s; }\n' % sampleformat["name"], \
			use = 'MPG123', \
			execute = 0, \
			define_name = sampleformat["name"] + "_SUPPORTED", \
			define_ret = True, \
			msg = 'Checking for %s sample support in mpg123' % sampleformat["label"], \
			okmsg = 'yes', \
			errmsg = 'no', \
			mandatory = 0 \
		)

	# check and add compiler flags
	compiler_flags = ['-Wextra', '-Wall', '-std=c99', '-pedantic']
	if conf.options.debug:
		compiler_flags += ['-O0', '-g3', '-ggdb']
	else:
		compiler_flags += ['-O2', '-s', '-fomit-frame-pointer', '-pipe']

	add_compiler_flags(conf, conf.env, compiler_flags, 'C', 'CC', 'COMMON')

	conf.write_config_header('config.h')


def build(bld):
	bld(
		features = ['c', 'cshlib'],
		includes = '.',
		uselib = 'GSTREAMER GSTREAMER_BASE GSTREAMER_AUDIO MPG123 COMMON',
		target = 'gstmpg123',
		source = 'gstmpg123.c'
	)

