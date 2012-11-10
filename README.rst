Overview
========

This is gstmpg123, a GStreamer plugin for MP3 decoding, using the mpg123 library. Currently, it is written for
GStreamer 0.10.36 (the 0.10 branch) and 1.0.0 (the 1.0 branch).

`mpg123 <http://mpg123.de/>`_ is a free mp3 decoder, released under the LGPL. It consists of a C library and
a command-line client.


Dependencies
============

- GStreamer 0.10.36 or a new version in the 0.10 branch
  or
  GStreamer 1.0.0 or a new version in the 1.0 branch
- mpg123 1.13.0


Build instructions
==================

Run::

  ./waf -h

To get a list of options. --prefix, --plugin-install-path-0-10, --plugin-install-path-1-0 are particularly interesting for local GStreamer
installations and for cross compilings.

GStreamer 1.0
-------------

This plugin makes use of the `waf build system <http://code.google.com/p/waf/>`_. For building, call::

  ./waf configure build

Then, to install the plugin, call::

  ./waf install

(build_1_0 for building and install_1_0 also work).

GStreamer 0.10
--------------

Similar to the 0.10 build, call::

  ./waf configure build_0_10

Then, to install the plugin, call::

  ./waf install_0_10

.. note:: This plugin has been included in the gst-plugins-bad package since version 1.0.0. Most Linux distributions
   have started to support GStreamer 1.0 and offer it in their package repositories. If GStreamer 1.0 packages are
   available to you, it is recommended to install the gst-plugins-bad package use its prebuilt mpg123 plugin instead.

