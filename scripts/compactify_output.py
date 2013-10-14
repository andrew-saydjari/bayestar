#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
#  compactify_output.py
#  
#  Copyright 2013 Greg Green <greg@greg-UX31A>
#  
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#  
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#  
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
#  MA 02110-1301, USA.
#  
#  

import argparse, sys

import maptools


def main():
	parser = argparse.ArgumentParser(prog='compactify_output.py',
	                                 description='Store line-of-sight output to one file.',
	                                 add_help=True)
	parser.add_argument('input', type=str, nargs='+', help='Bayestar output files.')
	parser.add_argument('--output', '-o', type=str, help='Filename for unified output.')
	parser.add_argument('--processes', '-proc', type=int, default=1,
	                                     help='# of processes to spawn.')
	if 'python' in sys.argv[0]:
		offset = 2
	else:
		offset = 1
	args = parser.parse_args(sys.argv[offset:])
	
	# Load in line-of-sight data
	print 'Loading Bayestar output files ...'
	fnames = args.input
	los_coll = maptools.los_collection(fnames, bounds=args.bounds,
	                                           processes=args.processes)
	
	# Save to unified output file
	print 'Saving to unified output file ...'
	los_coll.save_unified(args.output)
	
	print 'Done.'
	
	return 0

if __name__ == '__main__':
	main()

