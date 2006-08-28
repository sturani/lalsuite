# $Id$
#
# Copyright (C) 2006  Kipp C. Cannon
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#
# =============================================================================
#
#                                   Preamble
#
# =============================================================================
#

"""
Library of utility code for LIGO Light Weight XML applications.
"""

import gzip
import os
import urllib
import signal
import stat
import sys

from glue.ligolw import ligolw

__author__ = "Kipp Cannon <kipp@gravity.phys.uwm.edu>"
__date__ = "$Date$"[7:-2]
__version__ = "$Revision$"[11:-2]

__all__ = []


#
# =============================================================================
#
#                                 Input/Output
#
# =============================================================================
#

class IOTrappedSignal(Exception):
	"""
	Raised by I/O functions upon completion if they trapped a signal
	during the operation
	"""
	def __init__(self, signum):
		self.signum = signum

	def __str__(self):
		return "trapped signal %d" % self.signum


ContentHandler = ligolw.LIGOLWContentHandler


def measure_file_sizes(filenames, reverse = False):
	"""
	From a list of file names, return a list of (size, name) tuples
	sorted in ascending order by size (or descending order if reverse
	is set to True).
	"""
	l = [(os.stat(name)[stat.ST_SIZE], name) for name in filenames if name]
	l.sort()
	if reverse:
		l.reverse()
	return l


def sort_files_by_size(filenames, verbose = False, reverse = False):
	"""
	Return a new list of the file names sorted in order of smallest
	file to largest file (or largest to smallest if reverse is set to
	True).
	"""
	if verbose:
		if reverse:
			print >>sys.stderr, "sorting files from largest to smallest..."
		else:
			print >>sys.stderr, "sorting files from smallest to largest..."
	return [pair[1] for pair in measure_file_sizes(filenames, reverse = reverse)]


def load_filename(filename, verbose = False, gz = False):
	"""
	Parse the contents of the file identified by filename, and return
	the contents as a LIGO Light Weight document tree.  Helpful
	verbosity messages are printed to stderr if verbose is True, and
	the file is gzip decompressed while reading if gz is set to True.
	If filename is None, then stdin is parsed.

	Example:

	>>> from glue.ligolw import utils
	>>> xmldoc = utils.load_filename(name, verbose = True, gz = (name or "stdin")[-3:] == ".gz")
	"""
	if verbose:
		print >>sys.stderr, "reading %s ..." % (filename or "stdin")
	xmldoc = ligolw.Document()
	if filename:
		fileobj = file(filename)
	else:
		fileobj = sys.stdin
	if gz:
		fileobj = gzip.GzipFile(mode = "rb", fileobj = fileobj)
	ligolw.make_parser(ContentHandler(xmldoc)).parse(fileobj)
	return xmldoc


def load_url(url, verbose = False, gz = False):
	"""
	This function has the same behaviour as load_filename() but accepts
	a URL instead of a filename.  Any source from which Python's urllib
	library can read data is acceptable.  stdin is parsed if the URL is
	None.

	Example:

	>>> from glue.ligolw import utils
	>>> xmldoc = utils.load_url("file://localhost/tmp/data.xml")
	"""
	if verbose:
		print >>sys.stderr, "reading %s ..." % (url or "stdin")
	xmldoc = ligolw.Document()
	if url:
		fileobj = urllib.urlopen(url)
	else:
		fileobj = sys.stdin
	if gz:
		fileobj = gzip.GzipFile(mode = "rb", fileobj = fileobj)
	ligolw.make_parser(ContentHandler(xmldoc)).parse(fileobj)
	return xmldoc


def write_filename(xmldoc, filename, verbose = False, gz = False):
	"""
	Writes the LIGO Light Weight document tree rooted at xmldoc to the
	file name filename.  Friendly verbosity messages are printed while
	doing so if verbose is True.  The output data is gzip compressed on
	the fly if gz is True.
	
	This function traps SIGTERM during the write process, and it does
	this by temporarily installing its own signal handler in place of
	the current handler.  This is done to prevent Condor eviction
	during the write process.  If a SIGTERM is trapped, then when the
	write process has successfully concluded, the last thing this
	function does is raise IOTrappedSignal.  This is the only condition
	in which this function will raise that exception, so calling code
	that wishes its own SIGTERM handler to be executed can arrange for
	that to happen by trapping the IOTrappedSignal exception, and then
	manually running its own handler.

	Example:

	>>> from glue.ligolw import utils
	>>> utils.write_filename(xmldoc, "data.xml")
	"""
	# initialize SIGTERM trap
	global __llwapp_write_filename_got_sigterm
	__llwapp_write_filename_got_sigterm = False
	def newsigterm(signum, frame):
		global __llwapp_write_filename_got_sigterm
		__llwapp_write_filename_got_sigterm = True
	oldsigterm = signal.getsignal(signal.SIGTERM)
	signal.signal(signal.SIGTERM, newsigterm)

	# write the document
	if verbose:
		print >>sys.stderr, "writing %s ..." % (filename or "stdout")
	if filename:
		fileobj = file(filename, "w")
	else:
		fileobj = sys.stdout
	if gz:
		fileobj = gzip.GzipFile(mode = "wb", fileobj = fileobj)
	xmldoc.write(fileobj)

	# restore original SIGTERM handler, and report the signal if it was
	# received
	signal.signal(signal.SIGTERM, oldsigterm)
	if __llwapp_write_filename_got_sigterm:
		raise IOTrappedSignal(signal.SIGTERM)
