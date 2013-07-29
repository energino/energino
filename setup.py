#!/usr/bin/env python

import sys
from distutils.core import setup

if sys.version < '2.7':
	raise ValueError("Sorry Python versions older than 2.7 are not supported")

setup(name="energino",
	version="0.1",
	description="PyEnergino",
	author="Roberto Riggio",
	author_email="roberto.riggio@create-net.org",
	url="https://github.com/rriggio/energino",
	long_description="Energino distributed energy monitoring toolkit",
	data_files = [('etc/', ['xively.conf'])],
	entry_points={"console_scripts": ["energino=energino.energino:main", "xively_client=energino.xively_client:main"]},
	packages=['energino'],
	license = "Python",
	platforms="any"
)

