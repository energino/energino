#!/usr/bin/env python

import sys
from distutils.core import setup

if sys.version < '2.7':
	raise ValueError("Sorry Python versions older than 2.7 are not supported")

setup(name="pachubino",
	version="0.1",
	description="PyEnergino",
	author="Roberto Riggio",
	author_email="roberto.riggio@create-net.org",
	url="https://github.com/rriggio/energino",
	long_description="Energino distributed energy monitoring toolkit",
	data_files = [('etc/', ['pachubino.conf'])],
	entry_points={"console_scripts": ["energino=energino.energino:main", "pachubino=energino.pachubino:main"]},
	packages=['energino'],
	license = "Python",
	platforms="any"
)

