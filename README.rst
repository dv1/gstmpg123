Overview
========

This is gstmpg123, a GStreamer plugin for MP3 decoding, using the mpg123 library. Currently, it is written for GStreamer 0.10.36;
a port to the 0.11/1.0 branch is underway.

`mpg123 <http://mpg123.de/>`_ is a free mp3 decoder, released under the LGPL. It consists of a C library and a command-line client.


Dependencies
============

- GStreamer 0.10.36 or a new version in the 0.10 branch
- mpg123 1.12.1


Build instructions
==================

This plugin makes use of the `waf build system <http://code.google.com/p/waf/>`_. For building, call::

  ./waf configure build

Then, to install the plugin, call::

  ./waf install

Run::

  ./waf -h

To get a list of options. --prefix and --plugin-install-path are particularly interesting for local GStreamer installations and for cross compilings.
