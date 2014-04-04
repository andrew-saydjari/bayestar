#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
#  hputils.py
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

import numpy as np
import healpy as hp


def lb2pix(nside, l, b, nest=True):
	'''
	Convert (l, b) to pixel index.
	'''
	
	theta = np.pi/180. * (90. - b)
	phi = np.pi/180. * l
	
	return hp.pixelfunc.ang2pix(nside, theta, phi, nest=nest)


def pix2lb(nside, ipix, nest=True, use_negative_l=False):
	'''
	Convert pixel index to (l, b).
	'''
	
	theta, phi = hp.pixelfunc.pix2ang(nside, ipix, nest=nest)
	
	l = 180./np.pi * phi
	b = 90. - 180./np.pi * theta
	
	idx = (l > 180.)
	l[idx] = l[idx] - 360.
	
	return l, b

def pix2lb_scalar(nside, ipix, nest=True, use_negative_l=False):
	'''
	Convert pixel index to (l, b).
	
	Takes scalar input (no arrays).
	'''
	
	theta, phi = hp.pixelfunc.pix2ang(nside, ipix, nest=nest)
	
	l = 180./np.pi * phi
	b = 90. - 180./np.pi * theta
	
	if l > 180.:
		l -= 360.
	
	return l, b

def wrap_longitude(lon, delta_lon, degrees=True):
	'''
	Shift longitudes by delta_lon, and wrap
	back to range [0, 360].
	
	If degrees=False, then radians are assumed.
	'''
	
	lon_shifted = lon + delta_lon
	
	if degrees:
		return np.mod(lon_shifted, 360.)
	else:
		return np.mod(lon_shifted, 2. * np.pi)


def lb_in_bounds(l, b, bounds):
	'''
	Determine whether the given (l, b) coordinates
	are within the provided bounds.
	
	The bounds are in the format:
	
	    l_0, l_1, b_0, b_1
	
	l and b can be either floats or numpy arrays. In the
	first case, a boolean will be returned, and in the
	second, a numpy boolean array will be returned.
	'''
	
	l_0 = np.mod(bounds[0], 360.)
	l_1 = np.mod(bounds[1] - l_0, 360.)
	
	l_p = np.mod(l - l_0, 360.)
	
	return (l_p >= 0.) & (l_p <= l_1) & (b >= bounds[2]) & (b <= bounds[3])


def shift_lon_lat(lon, lat, delta_lon, delta_lat,
                  degrees=True, clip=False):
	'''
	Shift latitudes and longitudes, but do not
	move them off edges map, and do not wrap
	longitude.
	'''
	
	lon_shifted = lon + delta_lon
	lat_shifted = lat + delta_lat
	
	if clip:
		idx = (lon_shifted > 360.)
		lon_shifted[idx] = 360.
		
		idx = (lon_shifted < 0.)
		lon_shifted[idx] = 0.
		
		idx = (lat_shifted > 90.)
		lat_shifted[idx] = 90.
		
		idx = (lat_shifted < -90.)
		lat_shifted[idx] = -90.
	
	return lon_shifted, lat_shifted


class Mollweide_projection:
	'''
	The Mollweide projection of the sphere onto a flat plane.
	
	Pseudocylindrical, equal-area.
	'''
	
	def __init__(self, lam_0=180.):
		'''
		lam_0 is the central longitude of the map.
		'''
		
		self.lam_0 = np.pi/180. * lam_0
	
	def proj(self, phi, lam, iterations=15):
		'''
		Mollweide projection.
		
		phi = latitude
		lam = longitude
		'''
		
		theta = self.Mollweide_theta(phi, iterations)
		
		x = 2. * np.sqrt(2.) * (lam - self.lam_0) * np.cos(theta) / np.pi
		y = np.sqrt(2.) * np.sin(theta)
		
		#x = 180. * (lam - self.lam_0) * np.cos(theta) / np.pi
		#y = 90. * np.sin(theta)
		
		return x, y
	
	def inv(self, x, y):
		'''
		Inverse Mollweide projection.
		
		Returns (phi, lam), given (x, y).
		
		phi = latitude
		lam = longitude
		
		x and y can be floats or numpy float arrays.
		'''
		
		theta = np.arcsin(y / np.sqrt(2.))
		
		phi = np.arcsin((2. * theta + np.sin(2. * theta)) / np.pi)
		lam = self.lam_0 + np.pi * x / (2. * np.sqrt(2.) * np.cos(theta))
		
		#theta = np.arcsin(y / 90.)
		
		#phi = np.arcsin((2. * theta + np.sin(2. * theta)) / np.pi)
		
		#lam = self.lam_0 + np.pi * x / (180. * np.cos(theta))
		
		out_of_bounds = (lam < 0.) | (lam > 2.*np.pi) | (phi < -np.pi) | (phi > np.pi)
		
		return phi, lam, out_of_bounds
	
	def Mollweide_theta(self, phi, iterations):
		theta = np.arcsin(2. * phi / np.pi)
		sin_phi = np.sin(phi)
		
		for i in xrange(iterations):
			theta -= 0.5 * (2. * theta + np.sin(2. * theta) - np.pi * sin_phi) / (1. + np.cos(2. * theta))
		
		idx = np.isnan(theta)
		theta[idx] = np.sign(sin_phi) * 0.5 * np.pi
		
		return theta


class EckertIV_projection:
	'''
	The Eckert IV projection of the sphere onto a flat plane.
	
	Pseudocylindrical, equal-area.
	'''
	
	def __init__(self, lam_0=180.):
		'''
		lam_0 is the central longitude of the map.
		'''
		
		self.lam_0 = np.pi/180. * lam_0
		
		self.x_scale = 180. / 2.65300085635
		self.y_scale = 90. / 1.32649973731
		
		self.a = np.sqrt(np.pi * (4. + np.pi))
		self.b = np.sqrt(np.pi / (4. + np.pi))
		self.c = 2. + np.pi / 2.
		
		#self.a = 2. / np.sqrt(np.pi * (4. + np.pi))
		#self.b = 2. * np.sqrt(np.pi / (4. + np.pi))
		#self.d = np.sqrt((4. + np.pi) / np.pi)
		#self.e = np.sqrt(np.pi * (4. + np.pi))
	
	def proj(self, phi, lam, iterations=10):
		'''
		Eckert IV projection.
		
		phi = latitude
		lam = longitude
		'''
		
		theta = self.EckertIV_theta(phi, iterations)
		
		x = self.x_scale * 2. / self.a * (lam - self.lam_0) * (1. + np.cos(theta))
		y = self.y_scale * 2. * self.b * np.sin(theta)
		
		return x, y
	
	def inv(self, x, y):
		'''
		Inverse Eckert projection.
		
		Returns (phi, lam), given (x, y).
		
		phi = latitude
		lam = longitude
		
		x and y can be floats or numpy float arrays.
		'''
		
		theta = np.arcsin((y / self.y_scale) / 2. / self.b)
		
		phi = np.arcsin((theta + 0.5 * np.sin(2. * theta) + 2. * np.sin(theta)) / self.c)
		
		lam = self.lam_0 + self.a / 2. * (x / self.x_scale) / (1. + np.cos(theta))
		
		out_of_bounds = (lam < 0.) | (lam > 2.*np.pi) | (phi < -np.pi) | (phi > np.pi)
		
		return phi, lam, out_of_bounds
	
	def EckertIV_theta(self, phi, iterations):
		theta = phi / 2.
		sin_phi = np.sin(phi)
		
		for i in xrange(iterations):
			theta -= (theta + 0.5 * np.sin(2. * theta) + 2. * np.sin(theta) - self.c * sin_phi) / (2. * np.cos(theta) * (1. + np.cos(theta)))
		
		return theta


class Hammer_projection:
	'''
	The Hammer projection of the sphere onto a flat plane.
	
	Equal-area. Similar to the Mollweide projection, but with curved
	parallels to reduce distortion at the outer limbs.
	'''
	
	def __init__(self, lam_0=180.):
		'''
		lam_0 is the central longitude of the map.
		'''
		
		self.lam_0 = np.pi/180. * lam_0
	
	def proj(self, phi, lam):
		'''
		Hammer projection.
		
		phi = latitude
		lam = longitude
		'''
		
		denom = np.sqrt(1. + np.cos(phi) * np.cos((lam - self.lam_0)/2.))
		
		x = 2. * np.sqrt(2.) * np.cos(phi) * np.sin((lam - self.lam_0)/2.) / denom
		y = np.sqrt(2.) * np.sin(phi) / denom
		
		return x, y
	
	def inv(self, x, y):
		'''
		Inverse Hammer projection.
		
		Returns (phi, lam, out_of_bounds), given (x, y).
		
		phi = latitude
		lam = longitude
		
		out_of_bounds = True if pixel is not in standard range (ellipse
		                that typically bounds the Hammer projection)
		
		x and y can be floats or numpy float arrays.
		'''
		
		z = np.sqrt(1. - np.power(x/4., 2.) - np.power(y/2., 2.))
		
		lam = self.lam_0 + 2. * np.arctan(z * x / (2. * (2. * np.power(z, 2.) - 1.)))
		
		phi = np.arcsin(z * y)
		
		out_of_bounds = (0.25 * x*x + y*y) > 2.
		
		return phi, lam, out_of_bounds


class Cartesian_projection:
	'''
	The Cartesian projection of the sphere onto a flat plane.
	'''
	
	def __init__(self, lam_0=180.):
		self.lam_0 = np.pi / 180. * lam_0
	
	def proj(self, phi, lam):
		x = 180./np.pi * (lam - self.lam_0)
		y = 180./np.pi * phi
		
		return x, y
	
	def inv(self, x, y):
		lam = self.lam_0 + np.pi/180. * x
		phi = np.pi/180. * y
		
		out_of_bounds = (lam < 0.) | (lam > 2.*np.pi) | (phi < -np.pi) | (phi > np.pi)
		
		return phi, lam, out_of_bounds


def Euler_rotation_vec(x, y, z, alpha, beta, gamma, inverse=False):
	if inverse:
		alpha *= -1.
		beta *= -1.
		gamma *= -.1
	
	X = np.array([[1., 0.,             0.           ],
	              [0., np.cos(gamma), -np.sin(gamma)],
	              [0., np.sin(gamma),  np.cos(gamma)]])
	
	Y = np.array([[ np.cos(beta), 0., np.sin(beta)],
	              [ 0.,           1., 0.          ],
	              [-np.sin(beta), 0., np.cos(beta)]])
	
	Z = np.array([[np.cos(alpha), -np.sin(alpha), 0.],
	              [np.sin(alpha),  np.cos(alpha), 0.],
	              [0.,             0.,            1.]])
	
	shape = x.shape
	
	vec = np.empty((x.size, 3), dtype='f8')
	vec[:,0] = x.flatten()
	vec[:,1] = y.flatten()
	vec[:,2] = z.flatten()
	
	if inverse:
		vec = np.einsum('ij,jk,kl,ml->mi', Z, Y, X, vec)
	else:
		vec = np.einsum('ij,jk,kl,ml->mi', X, Y, Z, vec)
	
	x = np.reshape(vec[:,0], shape)
	y = np.reshape(vec[:,1], shape)
	z = np.reshape(vec[:,2], shape)
	
	return x, y, z


def ang2vec(theta, phi):
	cos_theta = np.cos(theta)
	
	x = np.cos(phi) * cos_theta
	y = np.sin(phi) * cos_theta
	z = np.sin(theta)
	
	return x, y, z


def vec2ang(x, y, z):
	r = np.sqrt(x*x + y*y + z*z)
	
	phi = np.arctan2(y, x)
	theta = np.arcsin(z/r)
	
	return theta, phi


def Euler_rotation_ang(theta, phi, alpha, beta, gamma,
                       degrees=False, inverse=False):
	x, y, z = None, None, None
	
	if degrees:
		x, y, z = ang2vec(np.pi/180. * theta, np.pi/180. * phi)
		
		alpha *= np.pi/180.
		beta *= np.pi/180.
		gamma *= np.pi/180.
		
	else:
		x, y, z = ang2vec(theta, phi)
	
	x, y, z = Euler_rotation_vec(x, y, z, alpha, beta, gamma,
	                                      inverse=inverse)
	
	t, p = vec2ang(x, y, z)
	
	if degrees:
		t *= 180. / np.pi
		p *= 180. / np.pi
	
	return t, p


def rasterize_map_old(pix_idx, pix_val,
                      nside, size,
                      nest=True, clip=True,
                      proj=Cartesian_projection(),
                      l_cent=0., b_cent=0.):
	'''
	Rasterize a healpix map.
	'''
	
	pix_scale = 180./np.pi * hp.nside2resol(nside)
	
	# Determine pixel centers
	l_0, b_0 = pix2lb(nside, pix_idx, nest=nest, use_negative_l=True)
	
	# Rotate coordinate system to center (l_0, b_0)
	if (l_cent != 0.) | (b_cent != 0.):
		b_0, l_0 = Euler_rotation_ang(b_0, l_0, -l_cent, b_cent, 0.,
		                                        degrees=True)
		#l_0 = np.mod(l_0, 360.)
	
	lam_0 = 180. - l_0
	
	# Determine display-space bounds
	shift = [(0., 0.), (1., 0.), (0., 1.), (-1., 0.), (0., -1.)]
	x_min, x_max, y_min, y_max = [], [], [], []
	
	#for (s_x, s_y) in shift:
	for s_x in np.linspace(-pix_scale, pix_scale, 3):
		for s_y in np.linspace(-pix_scale, pix_scale, 3):
			lam, b = shift_lon_lat(lam_0, b_0, 0.75*s_x, 0.75*s_y, clip=True)
			
			x_0, y_0 = proj.proj(np.pi/180. * b, np.pi/180. * lam)
			
			x_min.append(np.min(x_0))
			x_max.append(np.max(x_0))
			y_min.append(np.min(y_0))
			y_max.append(np.max(y_0))
	
	x_min = np.min(x_min)
	x_max = np.max(x_max)
	y_min = np.min(y_min)
	y_max = np.max(y_max)
	
	# Make grid of display-space pixels
	x_size, y_size = size
	
	x, y = np.mgrid[0:x_size, 0:y_size].astype(np.float32) + 0.5
	x = x_min + (x_max - x_min) * x / float(x_size)
	y = y_min + (y_max - y_min) * y / float(y_size)
	
	# Convert display-space pixels to (l, b)
	b, lam, mask = proj.inv(x, y)
	l = 180. - 180./np.pi * lam
	b *= 180./np.pi
	
	# Rotate back to original (l, b)-space
	if (l_cent != 0.) | (b_cent != 0.):
		b, l = Euler_rotation_ang(b, l, -l_cent, b_cent, 0.,
		                                degrees=True, inverse=True)
	
	# Determine bounds in (l, b)-space
	#if clip:
	#l_min, l_max = np.min(l[~mask]), np.max(l[~mask])
	#b_min, b_max = np.min(b[~mask]), np.max(b[~mask])
	#else:
	#	l_min, l_max = np.min(l), np.max(l)
	#	b_min, b_max = np.min(b), np.max(b)
	
	# Convert (l, b) to healpix indices
	disp_idx = lb2pix(nside, l, b, nest=nest)
	
	# Generate full map
	n_pix = hp.pixelfunc.nside2npix(nside)
	pix_idx_full = np.arange(n_pix)
	pix_val_full = np.empty(n_pix, dtype='f8')
	pix_val_full[:] = np.nan
	pix_val_full[pix_idx] = pix_val[:]
	
	# Grab pixel values
	img = None
	good_idx = None
	
	if len(pix_val.shape) == 1:
		img = pix_val_full[disp_idx]
		
		if clip:
			img[mask] = np.nan
		
		good_idx = np.isfinite(img)
		
		img.shape = (x_size, y_size)
		
	elif len(pix_val.shape) == 2:
		img = pix_val[:,disp_idx]
		
		if clip:
			img[:,mask] = np.nan
		
		good_idx = np.any(np.isfinite(img), axis=0)
		
		img.shape = (img.shape[0], x_size, y_size)
		
	else:
		raise Exception('pix_val must be either 1- or 2-dimensional.')
	
	l_min, l_max = np.min(l[good_idx]), np.max(l[good_idx])
	b_min, b_max = np.min(b[good_idx]), np.max(b[good_idx])
	
	bounds = (l_max, l_min, b_min, b_max)
	
	return img, bounds


def latlon_lines(ls, bs,
                 l_spacing=1., b_spacing=1.,
                 proj=Cartesian_projection(),
                 l_cent=0., b_cent=0.,
                 bounds=None, xy_bounds=None,
                 mode='both'):
	'''
	Return the x- and y- positions of points along a grid of parallels
	and meridians.
	'''
	
	if mode not in ['both', 'parallels', 'meridians']:
		raise ValueError("Unrecognized mode: '%s'\n"
		                 "Must be 'both', 'parallels' or 'meridians'" % mode)
	
	# Construct a set of points along the meridians and parallels
	l = []
	b = []
	
	if mode in ['both', 'parallels']:
		l_row = np.arange(-180., 180.+l_spacing/2., l_spacing)
		b_row = np.ones(l_row.size)
		
		for b_val in bs:
			b.append(b_val * b_row)
			l.append(l_row)
	
	if mode in ['both', 'meridians']:
		b_row = np.arange(-90., 90.+b_spacing/2., b_spacing)
		l_row = np.ones(b_row.size)
		
		for l_val in ls:
			l.append(l_val * l_row)
			b.append(b_row)
	
	l = np.hstack(l)
	b = np.hstack(b)
	
	# Rotate coordinate system to center (l_0, b_0)
	if (l_cent != 0.) | (b_cent != 0.):
		b, l = Euler_rotation_ang(b, l, -l_cent, b_cent, 0.,
		                                        degrees=True)
	
	lam = 180. - l
	
	# Project to (x, y)
	x, y = proj.proj(np.pi/180. * b, np.pi/180. * lam)
	
	# Scale (x, y) to display bounds
	if (bounds != None) and (xy_bounds != None):
		x_scale = (bounds[1] - bounds[0]) / (xy_bounds[1] - xy_bounds[0])
		y_scale = (bounds[3] - bounds[2]) / (xy_bounds[3] - xy_bounds[2])
		
		x = bounds[0] + (x - xy_bounds[0]) * x_scale
		y = bounds[2] + (y - xy_bounds[2]) * y_scale
	
	return x, y


def rasterize_map(pix_idx, pix_val,
                  nside, size,
                  nest=True, clip=True,
                  proj=Cartesian_projection(),
                  l_cent=0., b_cent=0.,
                  l_lines=None, b_lines=None,
                  l_spacing=1., b_spacing=1.):
	'''
	Rasterize a healpix map.
	'''
	
	pix_scale = 180./np.pi * hp.nside2resol(nside)
	
	# Determine pixel centers
	l_0, b_0 = pix2lb(nside, pix_idx, nest=nest, use_negative_l=True)
	
	# Rotate coordinate system to center (l_0, b_0)
	if (l_cent != 0.) | (b_cent != 0.):
		b_0, l_0 = Euler_rotation_ang(b_0, l_0, -l_cent, b_cent, 0.,
		                                        degrees=True)
		#l_0 = np.mod(l_0, 360.)
	
	lam_0 = 180. - l_0
	
	# Determine display-space bounds
	shift = [(0., 0.), (1., 0.), (0., 1.), (-1., 0.), (0., -1.)]
	x_min, x_max, y_min, y_max = [], [], [], []
	
	#for (s_x, s_y) in shift:
	for s_x in np.linspace(-pix_scale, pix_scale, 3):
		for s_y in np.linspace(-pix_scale, pix_scale, 3):
			lam, b = shift_lon_lat(lam_0, b_0, 0.75*s_x, 0.75*s_y, clip=True)
			
			x_0, y_0 = proj.proj(np.pi/180. * b, np.pi/180. * lam)
			
			x_min.append(np.min(x_0))
			x_max.append(np.max(x_0))
			y_min.append(np.min(y_0))
			y_max.append(np.max(y_0))
	
	x_min = np.min(x_min)
	x_max = np.max(x_max)
	y_min = np.min(y_min)
	y_max = np.max(y_max)
	
	# Make grid of display-space pixels
	x_size, y_size = size
	
	x, y = np.mgrid[0:x_size, 0:y_size].astype(np.float32) + 0.5
	x = x_min + (x_max - x_min) * x / float(x_size)
	y = y_min + (y_max - y_min) * y / float(y_size)
	
	# Convert display-space pixels to (l, b)
	b, lam, mask = proj.inv(x, y)
	l = 180. - 180./np.pi * lam
	b *= 180./np.pi
	
	# Rotate back to original (l, b)-space
	if (l_cent != 0.) | (b_cent != 0.):
		b, l = Euler_rotation_ang(b, l, -l_cent, b_cent, 0.,
		                                degrees=True, inverse=True)
	
	# Determine bounds in (l, b)-space
	#if clip:
	#l_min, l_max = np.min(l[~mask]), np.max(l[~mask])
	#b_min, b_max = np.min(b[~mask]), np.max(b[~mask])
	#else:
	#	l_min, l_max = np.min(l), np.max(l)
	#	b_min, b_max = np.min(b), np.max(b)
	
	# Convert (l, b) to healpix indices
	disp_idx = lb2pix(nside, l, b, nest=nest)
	
	# Generate full map
	n_pix = hp.pixelfunc.nside2npix(nside)
	pix_idx_full = np.arange(n_pix)
	pix_val_full = np.empty(n_pix, dtype='f8')
	pix_val_full[:] = np.nan
	pix_val_full[pix_idx] = pix_val[:]
	
	# Grab pixel values
	img = None
	good_idx = None
	
	if len(pix_val.shape) == 1:
		img = pix_val_full[disp_idx]
		
		if clip:
			img[mask] = np.nan
		
		good_idx = np.isfinite(img)
		
		img.shape = (x_size, y_size)
		
	elif len(pix_val.shape) == 2:
		img = pix_val[:,disp_idx]
		
		if clip:
			img[:,mask] = np.nan
		
		good_idx = np.any(np.isfinite(img), axis=0)
		
		img.shape = (img.shape[0], x_size, y_size)
		
	else:
		raise Exception('pix_val must be either 1- or 2-dimensional.')
	
	l_min, l_max = np.min(l[good_idx]), np.max(l[good_idx])
	b_min, b_max = np.min(b[good_idx]), np.max(b[good_idx])
	
	bounds = (l_max, l_min, b_min, b_max)
	xy_bounds = (x_min, x_max, y_min, y_max)
	
	if (l_lines != None) and (b_lines != None):
		x, y = latlon_lines(l_lines, b_lines,
		                    l_spacing=l_spacing, b_spacing=b_spacing,
		                    proj=proj,
		                    l_cent=l_cent, b_cent=b_cent,
		                    bounds=bounds, xy_bounds=xy_bounds)
		
		return img, bounds, x, y
	else:
		return img, bounds, xy_bounds


class MapRasterizer:
	'''
	A class that rasterizes a multi-resolution HEALPix map with a given
	set of (nside, pixel index) pairs. Pre-computes mapping between
	display-space pixels and HEALPix pixels, so that maps with different
	pixel intensities can be rasterized quickly.
	'''
	
	def __init__(self, nside, pix_idx, img_shape,
	                   nest=True, clip=True,
	                   proj=Cartesian_projection(),
	                   l_cent=0., b_cent=0.):
		'''
		Pre-computes mapping between (nside, pix_idx) pairs and
		display-space pixels.
		'''
		
		self.img_shape = img_shape
		self.clip = clip
		self.proj = proj
		self.l_cent = l_cent
		self.b_cent = b_cent
		
		
		#
		# Determine display-space bounds
		#
		
		shift = [(0., 0.), (1., 0.), (0., 1.), (-1., 0.), (0., -1.)]
		x_min, x_max, y_min, y_max = [], [], [], []
		
		nside_unique = np.unique(nside)
		
		for n in nside_unique:
			pix_scale = 180./np.pi * hp.nside2resol(n)
				
			# Determine pixel centers
			idx = (nside == n)
			l_0, b_0 = pix2lb(n, pix_idx[idx], nest=nest, use_negative_l=True)
			
			# Rotate coordinate system to center (l_0, b_0)
			if (l_cent != 0.) | (b_cent != 0.):
				b_0, l_0 = Euler_rotation_ang(b_0, l_0, -l_cent, b_cent, 0.,
				                                        degrees=True)
			
			lam_0 = 180. - l_0
			
			# Compute bounds for given pixel shift
			for s_x in np.linspace(-pix_scale, pix_scale, 3):
				for s_y in np.linspace(-pix_scale, pix_scale, 3):
					lam, b = shift_lon_lat(lam_0, b_0, 0.75*s_x, 0.75*s_y, clip=True)
					
					x_0, y_0 = proj.proj(np.pi/180. * b, np.pi/180. * lam)
					
					x_min.append(np.min(x_0))
					x_max.append(np.max(x_0))
					y_min.append(np.min(y_0))
					y_max.append(np.max(y_0))
					
					del x_0
					del y_0
					del lam
					del b
		
		x_min = np.min(x_min)
		x_max = np.max(x_max)
		y_min = np.min(y_min)
		y_max = np.max(y_max)
		
		
		#
		# Make grid of display-space pixels
		#
		
		x_size, y_size = img_shape
		
		x, y = np.mgrid[0:x_size, 0:y_size].astype(np.float32) + 0.5
		x = ( x_min + (x_max - x_min) * x / float(x_size) ).flatten()
		y = ( y_min + (y_max - y_min) * y / float(y_size) ).flatten()
		
		# Convert display-space pixels to (l, b)
		b, lam, self.clip_mask = proj.inv(x, y)
		l = 180. - 180./np.pi * lam
		b *= 180./np.pi
		del lam
		
		# Rotate back to original (l, b)-space
		if (l_cent != 0.) | (b_cent != 0.):
			b, l = Euler_rotation_ang(b, l, -l_cent, b_cent, 0.,
			                                degrees=True, inverse=True)
		
		
		#
		# Determine mapping from image index to map index
		#
		
		#image_idx = np.arange(l.size, dtype='i8')
		input_idx = np.arange(pix_idx.size, dtype='i8')
		self.map_idx_full = np.empty(l.size, dtype='i8')
		self.map_idx_full[:] = -1
		
		for n in nside_unique:
			idx = (nside == n)
			
			# Determine healpix index of each remaining image pixel
			disp_idx = lb2pix(n, l, b, nest=nest)
			
			# Generate full map
			n_pix = hp.pixelfunc.nside2npix(n)
			healpix_2_map = np.empty(n_pix, dtype='i8')
			healpix_2_map[:] = -1
			healpix_2_map[pix_idx[idx]] = input_idx[idx]
			
			# Update map indices
			map_idx_tmp = healpix_2_map[disp_idx]
			mask = ~(map_idx_tmp == -1)
			
			self.map_idx_full[mask] = map_idx_tmp[mask]
			
			del healpix_2_map
			del map_idx_tmp
		
		self.good_idx = ~(self.map_idx_full == -1)
		self.map_idx = self.map_idx_full[self.good_idx]
		
		l_min, l_max = np.min(l[self.good_idx]), np.max(l[self.good_idx])
		b_min, b_max = np.min(b[self.good_idx]), np.max(b[self.good_idx])
		
		self.lb_bounds = (l_max, l_min, b_min, b_max)
		self.xy_bounds = (x_min, x_max, y_min, y_max)
		
		self.x_scale = (self.lb_bounds[1] - self.lb_bounds[0]) / (self.xy_bounds[1] - self.xy_bounds[0])
		self.y_scale = (self.lb_bounds[3] - self.lb_bounds[2]) / (self.xy_bounds[3] - self.xy_bounds[2])
	
	def rasterize(self, pix_val):
		'''
		Rasterize the given HEALpix pixel intensity values.
		
		pix_val is assumed to correspond exactly to the nside and
		pix_idx arrays, in terms of the pixels it represents.
		'''
		
		# Grab pixel values
		img = np.empty(self.img_shape[0] * self.img_shape[1], dtype='f8')
		img[:] = np.nan
		
		img[self.good_idx] = pix_val[self.map_idx]
		
		if self.clip:
			img[self.clip_mask] = np.nan
		
		img.shape = (self.img_shape[0], self.img_shape[1])
		
		return img
	
	def __call__(self, pix_val):
		return self.rasterize(pix_val)
	
	def xy2idx(self, x, y, lb_bounds=True):
		'''
		Return the map index of the raster position (x, y).
		
		Returns -1 if (x, y) is off the map.
		
		If lb_bounds is True, then assume that lb_bounds was used to set
		extent of image (in imshow).
		
		This is useful for letting the user click on the image of the
		map to retrieve information on a given pixel.
		'''
		
		bounds = self.xy_bounds
		
		if lb_bounds:
			bounds = self.lb_bounds
		
		dx = (bounds[1] - bounds[0]) / float(self.img_shape[0])
		dy = (bounds[3] - bounds[2]) / float(self.img_shape[1])
		
		x_idx = int(np.floor((x - bounds[0]) / dx))
		y_idx = int(np.floor((y - bounds[2]) / dy))
		
		if (    (x_idx < 0) or (x_idx >= self.img_shape[0])
		     or (y_idx < 0) or (y_idx >= self.img_shape[1])):
			return -1
		
		idx = x_idx * self.img_shape[1] + y_idx
		
		if self.clip:
			if self.clip_mask[idx]:
				return -1
		
		return self.map_idx_full[idx]
	
	def latlon_lines(self, l_lines, b_lines,
	                       l_spacing=1., b_spacing=1.,
	                       clip=True, mode='both'):
		'''
		Project lines of constant Galactic longitude and latitude to
		display space (x, y).
		
		Inputs:
		    l_lines    Galactic longitudes at which to place lines.
		    b_lines    Galactic latitudes at which to place lines.
		    l_spacing  Longitude spacing between dots in lines.
		    b_spacing  Latitude spacing between dots in lines.
		    clip       If True, clip lines to display-space bounds of
		               the map.
		
		Outputs:
		    x          x-coordinates of dots that comprise lines.
		    y          y-coordinates of dots that comprise lines.
		'''
		
		x, y = latlon_lines(l_lines, b_lines,
		                    l_spacing=l_spacing, b_spacing=b_spacing,
		                    proj=self.proj,
		                    l_cent=self.l_cent, b_cent=self.b_cent,
		                    bounds=self.lb_bounds, xy_bounds=self.xy_bounds,
		                    mode=mode)
		
		if clip:
			x_idx = (x >= self.lb_bounds[1]) & (x <= self.lb_bounds[0])
			y_idx = (y >= self.lb_bounds[2]) & (y <= self.lb_bounds[3])
			idx = x_idx & y_idx
			
			return x[idx], y[idx]
		
		return x, y
	
	def label_locs(self, l_locs, b_locs, shift_frac=0.05):
		'''
		Find the locations in display-space (x, y) at which the given
		l and b labels should be placed.
		
		Inputs:
		    l_locs      List or array containing Galactic longitudes of labels
		    b_locs      List or array containing Galactic latitudes of labels
		    shift_frac  Fraction of width/heigh of plot to shift labels
		                away from the edges of the map by.
		
		Outputs:
		    l_labels  [l, (x_0, y_0), (x_1, y_1)] for each l in l_locs,
		                  where (x, y) are the positions of the labels.
		                  There are two label positions given for each
		                  longitude (in general, one on the top of the
		                  plot and one on the bottom).
		    
		    b_labels  [b, (x_0, y_0), (x_1, y_1)] for each l in b_locs.
		'''
		
		l_labels = []
		
		std_dist = shift_frac * np.sqrt(
		                                  abs(self.lb_bounds[1] - self.lb_bounds[0])
		                                * abs(self.lb_bounds[3] - self.lb_bounds[2])
		                               )
		
		for l in l_locs:
			l_arr = np.array([l])
			b_arr = np.array([0.])
			x, y = self.latlon_lines(l_arr, b_arr, clip=True, mode='meridians')
			
			if x.size != 0:
				dx = np.diff(np.hstack([x[-1], x]))
				dy = np.diff(np.hstack([y[-1], y]))
				
				ds = np.sqrt(dx*dx + dy*dy)
				cut_idx = np.argmax(ds)
				
				# Shift label positions off edge of map
				dx_0, dy_0 = None, None
				try:
					dx_0 = -dx[cut_idx+1]
					dy_0 = -dy[cut_idx+1]
				except:
					dx_0 = -dx[0]
					dy_0 = -dy[0]
				ds_0 = np.sqrt(dx_0*dx_0 + dy_0*dy_0)
				
				dx_0 *= std_dist / ds_0
				dy_0 *= std_dist / ds_0
				
				dx_1 = dx[cut_idx-1]
				dy_1 = dy[cut_idx-1]
				ds_1 = np.sqrt(dx_1*dx_1 + dy_1*dy_1)
				
				dx_1 *= std_dist / ds_1
				dy_1 *= std_dist / ds_1
				
				x_0, y_0 = x[cut_idx] + dx_0, y[cut_idx] + dy_0
				x_1, y_1 = x[cut_idx-1] + dx_1, y[cut_idx-1] + dy_1
				
				l_labels.append([l, (x_0, y_0),
				                    (x_1, y_1)])
		
		b_labels = []
		
		for b in b_locs:
			b_arr = np.array([b])
			l_arr = np.array([0.])
			x, y = self.latlon_lines(l_arr, b_arr, clip=True, mode='parallels')
			
			if x.size != 0:
				dx = np.diff(np.hstack([x[-1], x]))
				dy = np.diff(np.hstack([y[-1], y]))
				
				ds = np.sqrt(dx*dx + dy*dy)
				cut_idx = np.argmax(ds)
				
				# Shift label positions off edge of map
				dx_0, dy_0 = None, None
				try:
					dx_0 = -dx[cut_idx+1]
					dy_0 = -dy[cut_idx+1]
				except:
					dx_0 = -dx[0]
					dy_0 = -dy[0]
				ds_0 = np.sqrt(dx_0*dx_0 + dy_0*dy_0)
				
				dx_0 *= std_dist / ds_0
				dy_0 *= std_dist / ds_0
				
				dx_1 = dx[cut_idx-1]
				dy_1 = dy[cut_idx-1]
				ds_1 = np.sqrt(dx_1*dx_1 + dy_1*dy_1)
				
				dx_1 *= std_dist / ds_1
				dy_1 *= std_dist / ds_1
				
				x_0, y_0 = x[cut_idx] + dx_0, y[cut_idx] + dy_0
				x_1, y_1 = x[cut_idx-1] + dx_1, y[cut_idx-1] + dy_1
				
				b_labels.append([b, (x_0, y_0),
				                    (x_1, y_1)])
		
		return l_labels, b_labels
	
	def _scale_disp_coords(self, x, y):
		x_sc = self.lb_bounds[0] + (x - self.xy_bounds[0]) * self.x_scale
		y_sc = self.lb_bounds[2] + (y - self.xy_bounds[2]) * self.y_scale
		
		return x_sc, y_sc
	
	def _unscale_lb_coords(self, x, y):
		l_sc = (x - self.lb_bounds[0]) / self.x_scale + self.xy_bounds[0]
		b_sc = (y - self.lb_bounds[2]) / self.y_scale + self.xy_bounds[2]
		
		return l_sc, b_sc
	
	def get_lb_bounds(self):
		'''
		Return the bounds of the map in Galactic coordinates:
		    (l_min, l_max, b_min, b_max)
		'''
		
		return self.lb_bounds
	
	def get_xy_bounds(self):
		'''
		Return the display-space bounds of the map:
		    (x_min, x_max, y_min, y_max)
		'''
		
		return self.xy_bounds


class PixelIdentifier:
	def __init__(self, ax, rasterizer, lb_bounds=False):
		self.ax = ax
		self.cid = ax.figure.canvas.mpl_connect('button_press_event', self)
		
		self.rasterizer = rasterizer
		self.lb_bounds = lb_bounds
		
		self.objs = []
	
	def __call__(self, event):
		if event.inaxes != self.ax:
			return
		
		# Determine map index of the raster coordinates
		x, y = event.xdata, event.ydata
		
		map_idx = self.rasterizer.xy2idx(x, y, lb_bounds=self.lb_bounds)
		
		print '(%.2f, %.2f) -> %d' % (x, y, map_idx)
		
		# Pass map index to attached objects
		for obj in self.objs:
			obj(map_idx)
	
	def attach_obj(self, obj):
		self.objs.append(obj)


def test_Mollweide():
	proj = Mollweide_projection()
	
	phi = np.pi * (np.random.random(10) - 0.5)
	lam = 2. * np.pi * (np.random.random(10) - 0.5)
	
	x, y = proj.proj(phi, lam)
	phi_1, lam_1 = proj.inv(x, y)
	
	print 'lat  lon  x    y'
	
	for i in xrange(len(phi)):
		print '%.2f %.2f %.2f %.2f' % (phi[i]*180./np.pi, lam[i]*180./np.pi, x[i], y[i])
	
	print ''
	print "phi  phi'  lam  lam'"
	for i in xrange(len(phi)):
		print '%.2f %.2f %.2f %.2f' % (phi[i], phi_1[i], lam[i], lam_1[i])


def test_EckertIV():
	proj = EckertIV_projection()
	
	phi = np.pi * (np.random.random(10) - 0.5)
	lam = 2. * np.pi * (np.random.random(10) - 0.5)
	
	x, y = proj.proj(phi, lam)
	phi_1, lam_1 = proj.inv(x, y)
	
	iterations = 10
	theta = proj.EckertIV_theta(phi, iterations)
	lhs = theta + np.sin(theta) * np.cos(theta) + 2. * np.sin(theta)
	rhs = (2. + np.pi / 2.) * np.sin(phi)
	
	print 'lat  lon  x    y'
	
	for i in xrange(len(phi)):
		print '%.2f %.2f %.2f %.2f' % (phi[i]*180./np.pi, lam[i]*180./np.pi, x[i], y[i])
	
	print ''
	print "phi  phi'  lam  lam'"
	for i in xrange(len(phi)):
		print '%.2f %.2f %.2f %.2f' % (phi[i], phi_1[i], lam[i], lam_1[i])
	
	
	print ''
	print 'theta  lhs   rhs'
	for t,l,r in zip(theta, lhs, rhs):
		print '%.3f %.3f %.3f' % (t, l, r)
	
	# Find corners
	phi = np.array([0., 0., np.pi/2., -np.pi/2.])
	lam = np.array([0., 2. * np.pi, 0., 0.])
	x, y = proj.proj(phi, lam)
	
	print ''
	print 'x   y'
	for xx,yy in zip(x, y):
		print xx, yy


def test_Cartesian():
	proj = Cartesian_projection()
	
	phi = np.pi * (np.random.random(10) - 0.5)
	lam = 2. * np.pi * (np.random.random(10) - 0.5)
	
	x, y = proj.proj(phi, lam)
	phi_1, lam_1 = proj.inv(x, y)
	
	print 'lat  lon  x    y'
	
	for i in xrange(len(phi)):
		print '%.2f %.2f %.2f %.2f' % (phi[i]*180./np.pi, lam[i]*180./np.pi, x[i], y[i])
	
	print ''
	print "phi  phi'  lam  lam'"
	for i in xrange(len(phi)):
		print '%.2f %.2f %.2f %.2f' % (phi[i], phi_1[i], lam[i], lam_1[i])


def test_proj():
	import matplotlib.pyplot as plt
	
	nside = 512
	nest = True
	clip = True
	size = (2000, 1000)
	proj = Hammer_projection()
	l_cent = 90.
	b_cent = 10.
	
	n_pix = hp.pixelfunc.nside2npix(nside)
	pix_idx = np.arange(n_pix)#[10000:11000]#[4*n_pix/12:5*n_pix/12]
	l, b = pix2lb(nside, pix_idx, nest=nest)
	pix_val = pix_idx[:]
	
	# Plot map
	
	fig = plt.figure()
	ax = fig.add_subplot(1,1,1, axisbg=(0.6, 0.8, 0.95, 0.95))
	
	# Generate grid lines
	ls = np.linspace(-180., 180., 13)
	bs = np.linspace(-90., 90., 7)[1:-1]
	
	# Rasterize map
	print 'Constructing rasterizer ...'
	nside_arr = nside * np.ones(pix_idx.size, dtype='i4')
	rasterizer = MapRasterizer(nside_arr, pix_idx, size,
	                           proj=proj, l_cent=l_cent, b_cent=b_cent,
	                           nest=nest)
	
	print 'Rasterizing map ...'
	img = rasterizer(pix_val)
	bounds = rasterizer.get_lb_bounds()
	x, y = rasterizer.latlon_lines(l_lines=ls, b_lines=bs,
	                               l_spacing=2., b_spacing=2.)
	
	'''
	img, bounds, x, y = rasterize_map(pix_idx, pix_val,
	                                  nside, size,
	                                  nest=nest, clip=clip,
	                                  proj=proj,
	                                  l_cent=l_cent, b_cent=b_cent,
	                                  l_lines=ls, b_lines=bs,
	                                  l_spacing=2., b_spacing=2.)
	'''
	
	cimg = ax.imshow(img.T, extent=bounds,
	                 origin='lower', interpolation='nearest',
	                 aspect='auto')
	
	# Color bar
	fig.subplots_adjust(left=0.10, right=0.90, bottom=0.20, top=0.90)
	cax = fig.add_axes([0.10, 0.10, 0.80, 0.05])
	fig.colorbar(cimg, cax=cax, orientation='horizontal')
	
	#x, y = latlon_lines(ls, bs,
	#                    proj=proj,
	#                    l_cent=l_cent, b_cent=b_cent,
	#                    bounds=bounds, xy_bounds=xy_bounds)
	
	
	# Grid lines
	xlim = ax.get_xlim()
	ylim = ax.get_ylim()
	
	ax.scatter(x, y, c='k', s=1, alpha=0.25)
	
	# Latitude/Longitude labels
	l_labels, b_labels = rasterizer.label_locs(ls, bs, shift_frac=0.055)
	
	for b, (x_0, y_0), (x_1, y_1) in b_labels:
		#print b, x_0, y_0
		
		ax.text(x_0, y_0, r'$%d^{\circ}$' % b, fontsize=12,
		                               ha='center',
		                               va='center')
		ax.text(x_1, y_1, r'$%d^{\circ}$' % b, fontsize=12,
		                               ha='center',
		                               va='center')
	
	# Add pixel identifier
	pix_identifier = PixelIdentifier(ax, rasterizer, lb_bounds=True)
	
	plt.show()


def test_rot():
	p = np.array([[0., 45., 90., 135., 180.],
	              [225., 270., 315., 360., 45.]])
	t = 180. * (np.random.random(size=p.shape) - 0.5)
	
	print t
	
	#t *= np.pi / 180.
	#p *= np.pi / 180.
	
	t, p = Euler_rotation_ang(t, p, 90., 45., 0., degrees=True)
	t, p = Euler_rotation_ang(t, p, 90., 45., 0., degrees=True, inverse=True)
	
	#t *= 180. / np.pi
	#p *= 180. / np.pi
	
	print t
	print p


def main():
	#test_Cartesian()
	#test_EckertIV()
	#test_Mollweide()
	test_proj()
	#test_rot()


if __name__ == '__main__':
	main()
