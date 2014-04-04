#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
#       query_lsd_multiple.py
#       
#       Copyright 2012 Greg <greg@greg-G53JW>
#       
#       This program is free software; you can redistribute it and/or modify
#       it under the terms of the GNU General Public License as published by
#       the Free Software Foundation; either version 2 of the License, or
#       (at your option) any later version.
#       
#       This program is distributed in the hope that it will be useful,
#       but WITHOUT ANY WARRANTY; without even the implied warranty of
#       MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#       GNU General Public License for more details.
#       
#       You should have received a copy of the GNU General Public License
#       along with this program; if not, write to the Free Software
#       Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
#       MA 02110-1301, USA.
#       
#       

import os, sys, argparse
from os.path import abspath

import healpy as hp
import numpy as np
#import pyfits
import h5py

import lsd

import iterators

import matplotlib.pyplot as plt


def mapper(qresult, target_tp, target_radius):
	obj = lsd.colgroup.fromiter(qresult, blocks=True)
	
	if (obj != None) and (len(obj) > 0):
		# Find nearest target center to each star
		theta = np.pi/180. * (90. - obj['b'])
		phi = np.pi/180. * obj['l']
		tp_star = np.array([theta, phi]).T
		d = great_circle_dist(tp_star, target_tp) / target_radius
		min_idx = np.argmin(d, axis=1)
		
		# Group together stars belonging to the same target
		for target_idx, block_idx in iterators.index_by_key(min_idx):
			yield (target_idx, (d[block_idx,target_idx], obj[block_idx]))


def reducer(keyvalue, max_per_pixel):
	key, val = keyvalue
	
	# Reorganize distances and object records pairs
	d = []
	obj = []
	
	for dd,oo in val:
		d.append(dd)
		obj.append(oo)
	
	obj = lsd.colgroup.fromiter(obj, blocks=True)
	d = lsd.colgroup.fromiter(d, blocks=True)
	
	# Find stars with bad detections
	mask_zero_mag = (obj['mean'] == 0.)
	mask_zero_err = (obj['err'] == 0.)
	mask_nan_mag = np.isnan(obj['mean'])
	mask_nan_err = np.isnan(obj['err'])
	
	# Set errors for nondetections to some large number
	obj['mean'][mask_nan_mag] = 0.
	obj['err'][mask_zero_err] = 1.e10
	obj['err'][mask_nan_err] = 1.e10
	obj['err'][mask_zero_mag] = 1.e10
	
	#obj['EBV'][:] = d[:]
	
	# Combine and apply the masks
	mask_detect = np.sum(obj['mean'], axis=1).astype(np.bool)
	mask_informative = (np.sum(obj['err'] > 1.e10, axis=1) < 3).astype(np.bool)
	mask_keep = np.logical_and(mask_detect, mask_informative)
	obj = obj[mask_keep]
	
	# Limit number of stars
	if max_per_pixel != None:
		if len(obj) > max_per_pixel:
			d = d[mask_keep]
			idx = np.argsort(d)
			obj = obj[idx[:max_per_pixel]]
	
	yield (key, obj)


def start_file(base_fname, index):
	fout = open('%s_%d.in' % (base_fname, index), 'wb')
	f.write(np.array([0], dtype=np.uint32).tostring())
	return f


def to_file(f, target_idx, target_name, gal_lb, EBV, data):
	close_file = False
	if type(f) == str:
		f = h5py.File(fname, 'a')
		close_file = True
	
	ds_name = '/photometry/pixel 512-%d' % target_idx
	ds = f.create_dataset(ds_name, data.shape, data.dtype, chunks=True,
	                      compression='gzip', compression_opts=9)
	ds[:] = data[:]
	
	N_stars = data.shape[0]
	gal_lb = np.array(gal_lb, dtype='f8')
	pix_idx = target_idx
	nside = 512
	nest = True
	
	att_f8 = np.array([EBV], dtype='f8')
	att_u8 = np.array([pix_idx, target_idx], dtype='u8')
	att_u4 = np.array([nside, N_stars], dtype='u4')
	att_u1 = np.array([nest], dtype='u1')
	
	ds.attrs['healpix_index'] = att_u8[0]
	ds.attrs['nested'] = att_u1[0]
	ds.attrs['nside'] = att_u4[0]
	ds.attrs['l'] = gal_lb[0]
	ds.attrs['b'] = gal_lb[1]
	ds.attrs['EBV'] = att_f8[0]
	ds.attrs['target_index'] = att_u8[1]
	ds.attrs['target_name'] = target_name
	
	if close_file:
		f.close()
	
	return gal_lb

def great_circle_dist(tp0, tp1):
	'''
	Returns the great-circle distance bewteen two sets of coordinates,
	tp0 and tp1.
	
	Inputs:
	    tp0  (N, 2) numpy array. Each element is (theta, phi) in rad.
	    tp1  (M, 2) numpy array. Each element is (theta, phi) in rad.
	
	Output:
	    dist  (N, M) numpy array. dist[n,m] = dist(tp0[n], tp1[m]).
	'''
	
	N = tp0.shape[0]
	M = tp1.shape[0]
	out = np.empty((N,M), dtype=tp0.dtype)
	
	dist = lambda p0, t0, p1, t1: np.arccos(np.cos(t0)*np.cos(t1)
	                              + np.sin(t0)*np.sin(t1)*np.cos(p0-p1))
	
	if N <= M:
		for n in xrange(N):
			out[n,:] = dist(tp0[n,1], tp0[n,0], tp1[:,1], tp1[:,0])
	else:
		for m in xrange(M):
			out[:,m] = dist(tp0[:,1], tp0[:,0], tp1[m,1], tp1[m,0])
	
	return out

def get_bounds(infname):
	bounds = []
	name, l, b, radius = [], [], [], []
	
	f = open(infname, 'r')
	
	for line in f:
		line_stripped = line.lstrip().rstrip()
		if len(line_stripped) == 0:
			continue
		elif line_stripped[0] == '#':
			continue
		
		tmp = line_stripped.split()
		name.append(tmp[0])
		l.append(float(tmp[1]))
		b.append(float(tmp[2]))
		radius.append(float(tmp[3]))
		bounds.append( lsd.bounds.beam(l[-1], b[-1], radius[-1], coordsys='gal') )
	
	return bounds, name, np.array(l), np.array(b), np.array(radius)

def main():
	parser = argparse.ArgumentParser(
	           prog='query_lsd_multiple.py',
	           description='Generate bayestar input files from PS1 photometry, given list of targets.',
	           add_help=True)
	parser.add_argument('targets', type=str, help='Input target list.\n'
	                                              'Each line should be of the form "name l b radius (deg)".')
	parser.add_argument('out', type=str, help='Output filename.')
	parser.add_argument('-min', '--min-stars', type=int, default=1,
	                    help='Minimum # of stars in pixel (default: 1).')
	parser.add_argument('-max', '--max-stars', type=int, default=50000,
	                    help='Maximum # of stars in file')
	parser.add_argument('-ppix', '--max-per-pix', type=int, default=None,
	                    help='Take at most N nearest stars to center of pixel.')
	parser.add_argument('-sdss', '--sdss', action='store_true',
	                    help='Only select objects identified in the SDSS catalog as stars.')
	parser.add_argument('-ext', '--maxAr', type=float, default=None,
	                    help='Maximum allowed A_r.')
	parser.add_argument('--n-bands', type=int, default=4,
	                    help='Min. # of passbands with detection.')
	parser.add_argument('--n-det', type=int, default=4,
	                    help='Min. # of detections.')
	if 'python' in sys.argv[0]:
		offset = 2
	else:
		offset = 1
	values = parser.parse_args(sys.argv[offset:])
	
	nPointlike = values.n_bands - 1
	if nPointlike == 0:
		nPointlike = 1
	
	# Determine the query bounds
	query_bounds, target_name, l, b, target_radius = get_bounds(values.targets)
	query_bounds = lsd.bounds.make_canonical(query_bounds)
	
	# Convert target positions to useful forms
	target_lb = np.empty((len(l), 2), dtype='f8')
	target_lb[:,0] = l
	target_lb[:,1] = b
	target_tp = np.empty((len(l), 2), dtype='f8')
	target_tp[:,0] = np.pi/180. * (90. - b)
	target_tp[:,1] = np.pi/180. * l
	
	# Set up the query
	db = lsd.DB(os.environ['LSD_DB'])
	query = None
	if values.sdss:
		if values.maxAr == None:
			query = ("select obj_id, equgal(ra, dec) as (l, b), "
                                 "mean, err, mean_ap, nmag_ok from sdss, "
                                 "ucal_magsqx_noref(matchedto=sdss,nmax=1,dmax=5) "
                                 "where (numpy.sum(nmag_ok > 0, axis=1) >= %d) & "
                                 "(nmag_ok[:,0] > 0) & "
			         "(numpy.sum(nmag_ok, axis=1) >= %d) & "
                                 "(numpy.sum(mean - mean_ap < 0.1, axis=1) >= %d) & "
                                 "(type == 6)"
			         % (values.n_bands, values.n_det, nPointlike))
		else:
			query = ("select obj_id, equgal(ra, dec) as (l, b), "
			         "mean, err, mean_ap, nmag_ok from sdss, "
			         "ucal_magsqx_noref(matchedto=sdss,nmax=1,dmax=5) "
			         "where (numpy.sum(nmag_ok > 0, axis=1) >= 4) & "
			         "(nmag_ok[:,0] > 0) & "
			         "(numpy.sum(mean - mean_ap < 0.1, axis=1) >= 2) & "
			         "(type == 6) & (rExt <= %.4f)" % values.maxAr)
	else:
		query = ("select obj_id, equgal(ra, dec) as (l, b), mean, err, "
		         "mean_ap, nmag_ok, maglimit, SFD.EBV(l, b) as EBV "
		         "from ucal_magsqx_noref "
		         "where (numpy.sum(nmag_ok > 0, axis=1) >= %d) "
		         "& (nmag_ok[:,0] > 0) "
		         "& (numpy.sum(nmag_ok, axis=1) >= %d) "
		         "& (numpy.sum(mean - mean_ap < 0.1, axis=1) >= %d)"
		         % (values.n_bands, values.n_det, nPointlike))
	
	query = db.query(query)
	
	# Initialize stats on pixels, # of stars, etc.
	l_min = np.inf
	l_max = -np.inf
	b_min = np.inf
	b_max = -np.inf
	
	N_stars = 0
	N_pix = 0
	N_min = np.inf
	N_max = -np.inf
	
	fnameBase = abspath(values.out)
	fnameSuffix = 'h5'
	if fnameBase.endswith('.h5'):
		fnameBase = fnameBase[:-3]
	elif fnameBase.endswith('.hdf5'):
		fnameBase = fnameBase[:-5]
		fnameSuffix = 'hdf5'
	f = None
	nFiles = 0
	nInFile = 0
	
	# Write each pixel to the same file
	for (t_idx, obj) in query.execute([(mapper, target_tp, target_radius),
	                                   (reducer, values.max_per_pix)],
	                                  bounds=query_bounds):
		if len(obj) < values.min_stars:
			continue
		
		# Prepare output for pixel
		outarr = np.empty(len(obj), dtype=[('obj_id','u8'),
		                                   ('l','f8'), ('b','f8'), 
		                                   ('mag','f4',5), ('err','f4',5),
		                                   ('maglimit','f4',5),
		                                   ('nDet','u4',5),
		                                   ('EBV','f4')])
		outarr['obj_id'][:] = obj['obj_id'][:]
		outarr['l'][:] = obj['l'][:]
		outarr['b'][:] = obj['b'][:]
		outarr['mag'][:] = obj['mean'][:]
		outarr['err'][:] = obj['err'][:]
		outarr['maglimit'][:] = obj['maglimit'][:]
		outarr['nDet'][:] = obj['nmag_ok'][:]
		outarr['EBV'][:] = obj['EBV'][:]
		EBV = np.percentile(obj['EBV'][:], 95.)
		
		# Open output file
		if f == None:
			fname = '%s.%.5d.%s' % (fnameBase, nFiles, fnameSuffix)
			f = h5py.File(fname, 'w')
			nInFile = 0
			nFiles += 1
		
		# Write to file
		gal_lb = to_file(f, t_idx, target_name[t_idx],
		                    target_lb[t_idx], EBV, outarr)
		
		# Update stats
		N_pix += 1
		stars_in_pix = len(obj)
		N_stars += stars_in_pix
		nInFile += stars_in_pix
		
		if gal_lb[0] < l_min:
			l_min = gal_lb[0]
		if gal_lb[0] > l_max:
			l_max = gal_lb[0]
		if gal_lb[1] < b_min:
			b_min = gal_lb[1]
		if gal_lb[1] > b_max:
			b_max = gal_lb[1]
		
		if stars_in_pix < N_min:
			N_min = stars_in_pix
		if stars_in_pix > N_max:
			N_max = stars_in_pix
		
		# Close file if size exceeds max_stars
		if nInFile >= values.max_stars:
			f.close()
			f = None
	
	if f != None:
		f.close()
	
	if N_pix != 0:
		print '# of stars: %d.' % N_stars
		print '# of targets: %d.' % N_pix
		print 'Stars per target:'
		print '    min: %d' % N_min
		print '    mean: %d' % (N_stars / N_pix)
		print '    max: %d' % N_max
		print '# of files: %d.' % nFiles
		
		if np.sum(N_pix) != 0:
			print ''
			print 'Bounds of included pixel centers:'
			print '\t(l_min, l_max) = (%.3f, %.3f)' % (l_min, l_max)
			print '\t(b_min, b_max) = (%.3f, %.3f)' % (b_min, b_max)
	else:
		print 'No pixels in specified bounds with sufficient # of stars.'
	
	return 0

if __name__ == '__main__':
	main()

