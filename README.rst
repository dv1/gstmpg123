Overview
========

This is gstmpg123, a GStreamer plugin for MP3 decoding, using the mpg123 library. Currently, it is written for
GStreamer 0.10.36 (the 0.10 branch) and 0.11.92 (the 1.0 branch).

`mpg123 <http://mpg123.de/>`_ is a free mp3 decoder, released under the LGPL. It consists of a C library and
a command-line client.


Dependencies
============

- GStreamer 0.10.36 or a new version in the 0.10 branch
  or
  GStreamer 0.11.92 or a new version in the 0.11/1.0 branch
- mpg123 1.13.0


Build instructions
==================

Run::

  ./waf -h

To get a list of options. --prefix, --plugin-install-path, --plugin-install-path-1-0 are particularly interesting for local GStreamer
installations and for cross compilings.

GStreamer 0.10
--------------

This plugin makes use of the `waf build system <http://code.google.com/p/waf/>`_. For building, call::

  ./waf configure build

Then, to install the plugin, call::

  ./waf install

(build_0_10 for building and install_0_10 also work).

GStreamer 1.0
-------------

Similar to the 0.10 build, call::

  ./waf configure build_1_0

Then, to install the plugin, call::

  ./waf install_1_0

.. note:: This plugin has been included in the gst-plugins-bad package in GStreamer 1.0.
   Currently, it can only be found in the `freedesktop git repository <cgit.freedesktop.org/gstreamer/gst-plugins-bad/>`_ , but may get included
   in the GStreamer 1.0 release. Using the version from gst-plugin-bad is preferred, since it is already integrated in GStreamer.

