#!/usr/bin/env python

import sys
from distutils.core import setup

if sys.version < '2.6':
	raise ValueError("Sorry Python versions older than 2.6 are not supported")

setup(name="pachubino",
	version="0.1",
	description="PyEnergino",
	author="Roberto Riggio",
	author_email="roberto.riggio@create-net.org",
	url="http://gforge.create-net.org/",
	py_modules=['energino','daemonino','pachubino','pisolino', 'dispatcher'],
	long_description="Pachubino distributed energy monitoring toolkit",
	data_files = [('etc/', ['pachubino.conf'])],
	license = "Python",
	platforms="any")

