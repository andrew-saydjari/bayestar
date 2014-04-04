#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
#  plotmap.py
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

from scipy.ndimage.filters import gaussian_filter

import matplotlib as mplib
#mplib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator, AutoMinorLocator
from matplotlib.colors import LogNorm
from mpl_toolkits.axes_grid1 import ImageGrid

import argparse, os, sys, time

import healpy as hp
import h5py

import multiprocessing
import Queue

import hputils, maptools


def plot_EBV(ax, img, bounds, **kwargs):
	# Configure plotting options
	if 'norm' not in kwargs:
		if 'vmin' not in kwargs:
			kwargs['vmin'] = np.min(img[np.isfinite(img)])
		if 'vmax' not in kwargs:
			kwargs['vmax'] = np.max(img[np.isfinite(img)])
	if 'aspect' not in kwargs:
		kwargs['aspect'] = 'auto'
	if 'interpolation' not in kwargs:
		kwargs['interpolation'] = 'nearest'
	if 'origin' in kwargs:
		print "Ignoring option 'origin'."
	if 'extent' in kwargs:
		print "Ignoring option 'extent'."
	kwargs['origin'] = 'lower'
	kwargs['extent'] = bounds
	kwargs['cmap'] = 'binary'
	
	# Plot image in B&W
	img_res = ax.imshow(img.T, **kwargs)
	
	return img_res


def plotter_worker(img_q, lock,
                   n_rasterizers,
                   figsize, dpi,
                   EBV_max, outfname):
	
	n_finished = 0
	
	# Plot images
	while True:
		n, mu, img = img_q.get()
		
		# Count number of rasterizer workers that have finished
		# processing their queue
		if n == 'FINISHED':
			n_finished += 1
			
			if n_finished >= n_rasterizers:
				return
			else:
				continue
		
		# Plot this image
		print 'Plotting mu = %.2f (image %d) ...' % (mu, n+1)
		
		fig = plt.figure(figsize=figsize, dpi=dpi)
		ax = fig.add_subplot(1,1,1)
		
		img = plot_EBV(ax, img, bounds, vmin=0., vmax=EBV_max)
		
		# Colorbar
		fig.subplots_adjust(bottom=0.12, left=0.12, right=0.89, top=0.88)
		cax = fig.add_axes([0.9, 0.12, 0.03, 0.76])
		cb = fig.colorbar(img, cax=cax)
		
		# Labels, ticks, etc.
		ax.set_xlabel(r'$\ell$', fontsize=16)
		ax.set_ylabel(r'$b$', fontsize=16)
		
		ax.xaxis.set_major_locator(MaxNLocator(nbins=5))
		ax.xaxis.set_minor_locator(AutoMinorLocator())
		ax.yaxis.set_major_locator(MaxNLocator(nbins=5))
		ax.yaxis.set_minor_locator(AutoMinorLocator())
		
		# Title
		d = 10.**(mu/5. - 2.)
		ax.set_title(r'$\mu = %.2f \ \ \ d = %.2f \, \mathrm{kpc}$' % (mu, d), fontsize=16)
		
		# Save figure
		full_fname = '%s.%s.%s.%.5d.png' % (outfname, model, method, n)
		
		lock.acquire()
		fig.savefig(full_fname, dpi=dpi)
		lock.release()
		
		plt.close(fig)
		del img


def rasterizer_worker(dist_q, img_q,
                      los_coll,
                      figsize, dpi, size,
                      model, method, mask,
                      proj, l_cent, b_cent, bounds,
                      delta_mu):
	# Reseed random number generator
	t = time.time()
	t_after_dec = int(1.e9*(t - np.floor(t)))
	seed = np.bitwise_xor([t_after_dec], [os.getpid()])
	
	np.random.seed(seed=seed)
	
	# Generate images
	while True:
		try:
			n, mu = dist_q.get_nowait()
			
			# Rasterize E(B-V)
			img, bounds, xy_bounds = los_coll.rasterize(mu, size,
			                                                fit=model,
			                                                method=method,
			                                                mask_sigma=mask,
			                                                delta_mu=delta_mu,
			                                                proj=proj,
			                                                l_cent=l_cent,
			                                                b_cent=b_cent)
			
			# Put image on queue
			img_q.put((n, mu, img, bounds, xy_bounds))
			
		except Queue.Empty:
			img_q.put('FINISHED')
			
			print 'Rasterizer finished.'
			
			return


class PixelPlotter:
	def __init__(self, data, model='piecewise'):
		self.data = data
		self.model = model
	
	def __call__(self, map_idx):
		self.plot_pixel(map_idx)
	
	def plot_pixel(self, map_idx):
		if map_idx == -1:
			return
		
		# Load and stretch stacked stellar pdfs
		star_stack = self.data.star_stack[0][map_idx].astype('f8')
		
		#dE = self.los_coll.star_E_range[1] - self.los_coll.star_E_range[0]
		#dDM = self.los_coll.star_DM_range[1] - self.los_coll.star_DM_range[0]
		#sigma = [0.015/dE, 0.15/dDM]
		#star_stack = gaussian_filter(star_stack, sigma, mode='mirror')
		
		star_stack /= np.max(star_stack)
		norm1 = 1. / np.power(np.max(star_stack, axis=0), 0.8)
		norm2 = 1. / np.power(np.sum(star_stack, axis=0), 0.8)
		norm = np.sqrt(norm1 * norm2)
		norm[np.isinf(norm)] = 0.
		star_stack = np.einsum('ij,j->ij', star_stack, norm)
		
		# Determine maximum E(B-V)
		w_y = np.mean(star_stack, axis=1)
		y_max = np.max(np.where(w_y > 1.e-2)[0])
		EBV_stack_max = y_max * (5. / star_stack.shape[0])
		
		# Load piecewise-linear profiles
		mu = self.data.get_los_DM_range()
		EBV_all = self.data.los_EBV[0][map_idx, :, :]
		
		nside = self.data.nside[0][map_idx]
		pix_idx = self.data.pix_idx[0][map_idx]
		l, b = hputils.pix2lb_scalar(nside, pix_idx, nest=True, use_negative_l=True)
		
		EBV_los_max = 1.5 * np.percentile(EBV_all[:, -1], 95.)
		EBV_max = min([EBV_los_max, EBV_stack_max])
		
		# Load ln(p), if available
		lnp = None
		lnp_txt = None
		
		if self.data.los_lnp != []:
			lnp = self.data.los_lnp[0][map_idx, :] / self.data.n_stars[0][map_idx]
			GR = self.data.los_GR[0][map_idx, :]
			
			lnp_min, lnp_max = np.percentile(lnp[1:], [10., 90.])
			GR_max = np.max(GR)
			
			lnp_txt =  '$\ln \, p_{\mathrm{best}} = %.2f$\n' % lnp[0]
			lnp_txt += '$\ln \, p_{90\%%} = %.2f$\n' % lnp_max
			lnp_txt += '$\ln \, p_{10\%%} = %.2f$\n' % lnp_min
			lnp_txt += '$\mathrm{GR}_{\mathrm{max}} = %.3f$' % GR_max
			
			lnp = (lnp - lnp_min) / (lnp_max - lnp_min)
			lnp[lnp > 1.] = 1.
			lnp[lnp < 0.] = 0.
		else:
			lnp = [0. for EBV in EBV_all]
		
		# Set up figure
		fig = plt.figure(figsize=(8,5), dpi=150)
		ax = fig.add_subplot(1,1,1)
		fig.subplots_adjust(left=0.12, bottom=0.12, top=0.85)
		
		# Plot stacked stellar pdfs
		bounds = self.data.DM_EBV_lim
		ax.imshow(np.sqrt(star_stack), extent=bounds, origin='lower',
		          aspect='auto', cmap='Blues', interpolation='nearest')
		
		# Plot samples
		alpha = 1. / np.power(EBV_all.shape[0], 0.55)
		
		for i,EBV in enumerate(EBV_all[1:]):
			c = (1.-lnp[i+1], 0., lnp[i+1])
			ax.plot(mu, EBV, c=c, alpha=alpha)
		
		# Plot best fit
		ax.plot(mu, EBV_all[0, :], 'g', lw=2, alpha=0.5)
		
		ax.set_xlim(mu[0], mu[-1])
		ax.set_ylim(0., EBV_max)
		
		# Add labels
		ax.set_xlabel(r'$\mu$', fontsize=16)
		ax.set_ylabel(r'$\mathrm{E} \left( B - V \right)$', fontsize=16)
		
		title_txt = '$\mathrm{nside} = %d, \ \mathrm{i} = %d$\n' % (nside, pix_idx)
		title_txt += '$\ell = %.2f, \ b = %.2f$' % (l, b)
		
		ax.set_title(title_txt, fontsize=16, multialignment='center')
		
		if lnp_txt != None:
			ylim = ax.get_ylim()
			y_txt = ylim[0] + 0.95 * (ylim[1] - ylim[0])
			x_txt = mu[0] + 0.05 * (mu[-1] - mu[0])
			ax.text(x_txt, y_txt, lnp_txt, fontsize=16,
			                               multialignment='left',
			                               va='top')
		
		plt.show()


def rasterizer_plotter_worker(dist_q, lock,
                              mapper,
                              figsize, dpi, size,
                              model, method, mask,
                              proj, l_cent, b_cent, bounds,
                              l_lines, b_lines,
                              delta_mu, EBV_max,
                              outfname, show=False):
	# Reseed random number generator
	t = time.time()
	t_after_dec = int(1.e9*(t - np.floor(t)))
	seed = np.bitwise_xor([t_after_dec], [os.getpid()])
	
	np.random.seed(seed=seed)
	
	# Set up rasterizer
	rasterizer = mapper.gen_rasterizer(size, proj=proj,
	                                         l_cent=l_cent,
	                                         b_cent=b_cent)
	bounds = rasterizer.get_lb_bounds()
	
	# Set up grid lines and labels
	l_labels, b_labels = None, None
	x_guides, y_guides = None, None
	x_guides_l0, y_guides_l0 = None, None
	x_guides_b0, y_guides_b0 = None, None
	
	if (l_lines != None) and (b_lines != None):
		# Determine label positions
		l_labels, b_labels = rasterizer.label_locs(l_lines, b_lines, shift_frac=0.04)
		
		# Determine grid lines to plot
		l_lines = np.array(l_lines)
		b_lines = np.array(b_lines)
		
		idx = (np.abs(l_lines) < 1.e-5)
		l_lines_0 = l_lines[idx]
		l_lines = l_lines[~idx]
		
		idx = (np.abs(b_lines) < 1.e-5)
		b_lines_0 = b_lines[idx]
		b_lines = b_lines[~idx]
		
		x_guides, y_guides = rasterizer.latlon_lines(l_lines, b_lines,
		                                             l_spacing=0.25, b_spacing=0.25)
		
		if l_lines_0.size != 0:
			x_guides_l0, y_guides_l0 = rasterizer.latlon_lines(l_lines_0, 0.,
			                                                   mode='meridians',
			                                                   b_spacing=0.15)
		
		if b_lines_0.size != 0:
			x_guides_b0, y_guides_b0 = rasterizer.latlon_lines(0., b_lines_0,
			                                                   mode='parallels',
			                                                   l_spacing=0.15)
	
	first_img = True
	pix_identifier = []
	
	if show:
		pix_plotter = PixelPlotter(mapper)
	
	# Generate images
	while True:
		try:
			n, mu = dist_q.get_nowait()
			
			# Rasterize E(B-V)
			tmp, tmp, pix_val = mapper.gen_EBV_map(mu, fit=model,
			                                           method=method,
			                                           mask_sigma=mask,
			                                           delta_mu=delta_mu,
			                                           reduce_nside=False)
			
			img = rasterizer(pix_val)
			
			# Plot this image
			print 'Plotting mu = %.2f (image %d) ...' % (mu, n+1)
			
			fig = plt.figure(figsize=figsize, dpi=dpi)
			ax = fig.add_subplot(1,1,1, axisbg=(0.6, 0.8, 0.95, 0.95))
			
			if method == 'sigma':
				img = plot_EBV(ax, img, bounds, norm=LogNorm(vmin=0.001, vmax=EBV_max))
			else:
				img = plot_EBV(ax, img, bounds, vmin=0., vmax=EBV_max)
			
			# Colorbar
			fig.subplots_adjust(bottom=0.05, left=0.05,
			                    right=0.89, top=0.88)
			cax = fig.add_axes([0.9, 0.05, 0.03, 0.83])
			cb = fig.colorbar(img, cax=cax)
			
			clabel = r'$\mathrm{E} \left( B - V \right)$'
			if delta_mu != None:
				clabel = r'$\mathrm{d} \mathrm{E} \left( B - V \right) / \mathrm{d} \mu$'
			
			cb.ax.set_ylabel(clabel, fontsize=24, rotation=270)
			cb.ax.tick_params(labelsize=20)
			
			# Labels, ticks, etc.
			ax.set_xticks([])
			ax.set_yticks([])
			
			#ax.set_xlabel(r'$\ell$', fontsize=16)
			#ax.set_ylabel(r'$b$', fontsize=16)
			
			#ax.xaxis.set_major_locator(MaxNLocator(nbins=5))
			#ax.xaxis.set_minor_locator(AutoMinorLocator())
			#ax.yaxis.set_major_locator(MaxNLocator(nbins=5))
			#ax.yaxis.set_minor_locator(AutoMinorLocator())
			
			# Title
			d = 10.**(mu/5. - 2.)
			ax.set_title(r'$\mu = %.2f \ \ \ d = %.2f \, \mathrm{kpc}$' % (mu, d),
			             fontsize=24)
			
			# Lines of constant l and b
			xlim = ax.get_xlim()
			ylim = ax.get_ylim()
			
			if x_guides != None:
				ax.scatter(x_guides, y_guides, s=1., c='b', edgecolor='b', alpha=0.10)
			
			if x_guides_l0 != None:
				ax.scatter(x_guides_l0, y_guides_l0, s=3., c='g', edgecolor='g', alpha=0.25)
			
			if x_guides_b0 != None:
				ax.scatter(x_guides_b0, y_guides_b0, s=3., c='g', edgecolor='g', alpha=0.25)
			
			ax.set_xlim(xlim)
			ax.set_ylim(ylim)
			
			# Label Galactic coordinates
			if l_lines != None:
				if bounds != None:
					if (bounds[2] > -80.) | (bounds[3] < 80.):
						#print bounds
						
						for l, (x_0, y_0), (x_1, y_1) in l_labels:
							ax.text(x_0, y_0, r'$%d^{\circ}$' % l, fontsize=20,
							                               ha='center',
							                               va='center')
							ax.text(x_1, y_1, r'$%d^{\circ}$' % l, fontsize=20,
							                               ha='center',
							                               va='center')
			
			if b_lines != None:
				for b, (x_0, y_0), (x_1, y_1) in b_labels:
					ax.text(x_0, y_0, r'$%d^{\circ}$' % b, fontsize=20,
					                               ha='center',
					                               va='center')
					ax.text(x_1, y_1, r'$%d^{\circ}$' % b, fontsize=20,
					                               ha='center',
					                               va='center')
				
				# Expand axes limits to fit labels
				expand = 0.075
				xlim = ax.get_xlim()
				w = xlim[1] - xlim[0]
				xlim = [xlim[0] - expand * w, xlim[1] + expand * w]
				
				ylim = ax.get_ylim()
				h = ylim[1] - ylim[0]
				ylim = [ylim[0] - expand * h, ylim[1] + expand * h]
				
				ax.set_xlim(xlim)
				ax.set_ylim(ylim)
			
			# Save figure
			full_fname = '%s.%s.%s.%.5d.png' % (outfname, model, method, n)
			
			if first_img:
				lock.acquire()
				fig.savefig(full_fname, dpi=dpi)
				lock.release()
				
				first_img = False
			else:
				fig.savefig(full_fname, dpi=dpi)
			
			if show:
				# Add pixel identifier to allow user to find info on
				# individual HEALPix pixels
				pix_identifier.append(hputils.PixelIdentifier(ax, rasterizer, lb_bounds=True))
				pix_identifier[-1].attach_obj(pix_plotter)
			else:
				plt.close(fig)
				del img
			
		except Queue.Empty:
			print 'Rasterizer finished.'
			
			if show:
				print 'Showing ...'
				plt.show()
			
			print 'Plots closed.'
			
			return


def main():
	parser = argparse.ArgumentParser(prog='plotmap.py',
	                                 description='Generate a map of E(B-V) from bayestar output.',
	                                 add_help=True)
	parser.add_argument('input', type=str, nargs='+', help='Bayestar output files.')
	parser.add_argument('--output', '-o', type=str, help='Output filename for plot.')
	parser.add_argument('--show', '-sh', action='store_true', help='Show plot.')
	parser.add_argument('--dists', '-d', type=float, nargs=3,
	                                     default=(4., 19., 21),
	                                     help='DM min, DM max, # of distance slices.')
	parser.add_argument('--dist-step', '-ds', type=str, default='log',
	                                     choices=('log', 'linear'),
	                                     help='Step logarithmically in distance (linearly in\n'
	                                          'distance modulus) or linearly in distance.')
	parser.add_argument('--delta-mu', '-dmu', type=float, default=None,
	                                     help='Difference in DM used to estimate rate of\n'
	                                          'reddening (default: None, i.e. calculate cumulative reddening).')
	parser.add_argument('--figsize', '-fs', type=int, nargs=2, default=(8, 4),
	                                     help='Figure size (in inches).')
	parser.add_argument('--dpi', '-dpi', type=float, default=200,
	                                     help='Dots per inch for figure.')
	parser.add_argument('--projection', '-proj', type=str, default='Cartesian',
	                                     choices=('Cartesian', 'Mollweide', 'Hammer', 'Eckert IV'),
	                                     help='Map projection to use.')
	parser.add_argument('--center-lb', '-cent', type=float, nargs=2, default=(0., 0.),
	                                     help='Center map on (l, b).')
	parser.add_argument('--bounds', '-b', type=float, nargs=4, default=None,
	                                     help='Bounds of pixels to plot (l_min, l_max, b_min, b_max).')
	parser.add_argument('--model', '-m', type=str, default='piecewise',
	                                     choices=('piecewise', 'cloud'),
	                                     help='Line-of-sight extinction model to use.')
	parser.add_argument('--mask', '-msk', type=float, default=None,
	                                     help=r'Hide parts of map where sigma_{E(B-V)} is greater than given value.')
	parser.add_argument('--method', '-mtd', type=str, default='median',
	                                     choices=('median', 'mean', 'best', 'sample', 'sigma' , '5th', '95th'),
	                                     help='Measure of E(B-V) to plot.')
	parser.add_argument('--processes', '-proc', type=int, default=1,
	                                     help='# of processes to spawn.')
	parser.add_argument('--max-samples', '-samp', type=int, default=None,
	                                     help='Maximum # of MCMC samples to load per pixel (to limit memory usage).')
	parser.add_argument('--l-lines', '-ls', type=float, nargs='+', default=None,
	                                     help='Galactic longitudes at which to draw lines.')
	parser.add_argument('--b-lines', '-bs', type=float, nargs='+', default=None,
	                                     help='Galactic latitudes at which to draw lines.')
	
	if 'python' in sys.argv[0]:
		offset = 2
	else:
		offset = 1
	args = parser.parse_args(sys.argv[offset:])
	
	
	# Parse arguments
	outfname = args.output
	if outfname != None:
		if outfname.endswith('.png'):
			outfname = outfname[:-4]
	
	method = args.method
	if method == '5th':
		method = 5.
	elif method == '95th':
		method = 95.
	
	proj = None
	if args.projection == 'Cartesian':
		proj = hputils.Cartesian_projection()
	elif args.projection == 'Mollweide':
		proj = hputils.Mollweide_projection()
	elif args.projection == 'Hammer':
		proj = hputils.Hammer_projection()
	elif args.projection == 'Eckert IV':
		proj = hputils.EckertIV_projection()
	else:
		raise ValueError("Unrecognized projection: '%s'" % args.proj)
	
	l_cent, b_cent = args.center_lb
	
	size = (int(args.figsize[0] * 0.8 * args.dpi),
	        int(args.figsize[1] * 0.8 * args.dpi))
	
	mu_plot = None
	if args.dist_step == 'log':
		mu_plot = np.linspace(args.dists[0], args.dists[1], args.dists[2])
	else:
		d_0 = 10.**(args.dists[0] / 5. + 1.)
		d_1 = 10.**(args.dists[1] / 5. + 1.)
		d_plot = np.linspace(d_0, d_1, args.dists[2])
		mu_plot = 5. * (np.log10(d_plot) - 1.)
	
	
	# Load in line-of-sight data
	fnames = args.input
	
	mapper = maptools.LOSMapper(fnames, bounds=args.bounds,
	                                    max_samples=args.max_samples,
	                                    processes=args.processes,
	                                    load_stacked_pdfs=args.show)
	
	# Get upper limit on E(B-V)
	method_tmp = method
	
	if method == 'sample':
		method_tmp = 'median'
	
	EBV_max = -np.inf
	
	if args.delta_mu == None:
		mu_eval = None
		
		if method == 'sigma':
			mu_eval = np.array(mapper.data.get_los_DM_range())
			idx = (mu_eval >= args.dists[0]) & (mu_eval <= args.dists[1])
			mu_eval = mu_eval[idx]
			
		else:
			mu_eval = [mu_plot[-1]]
		
		for mu in mu_eval:
			print 'Determining max E(B-V) from mu = %.2f ...' % mu
			
			nside_tmp, pix_idx_tmp, EBV = mapper.gen_EBV_map(mu,
			                                                 fit=args.model,
			                                                 method=method_tmp,
			                                                 mask_sigma=args.mask,
			                                                 delta_mu=args.delta_mu)
			idx = np.isfinite(EBV)
			EBV_max_tmp = np.percentile(EBV[idx], 95.)
			
			if EBV_max_tmp > EBV_max:
				EBV_max = EBV_max_tmp
		
	else:
		EBV_max = mapper.est_dEBV_pctile(95., delta_mu=args.delta_mu,
		                                      fit=args.model)
	
	mask = args.mask
	
	if method == 'sample':
		mask = None
	
	print 'EBV_max = %.3f' % EBV_max
	
	
	# Matplotlib settings
	#mplib.rc('text', usetex=False) # TODO: Set to True once LaTeX is fixed on CentOS 6
	mplib.rc('xtick.major', size=6)
	mplib.rc('xtick.minor', size=2)
	mplib.rc('ytick.major', size=6)
	mplib.rc('ytick.minor', size=2)
	mplib.rc('xtick', direction='out')
	mplib.rc('ytick', direction='out')
	mplib.rc('axes', grid=False)
	
	
	# Plot at each distance
	pix_identifiers = []
	nside_max = mapper.get_nside_levels()[-1]
	
	# Set up queue for rasterizer workers to pull from
	dist_q = multiprocessing.Queue()
	
	for n,mu in enumerate(mu_plot):
		dist_q.put((n, mu))
	
	# Set up results queue for rasterizer workers
	img_q = multiprocessing.Queue()
	lock = multiprocessing.Lock()
	
	# Spawn worker processes to plot images
	n_rasterizers = args.processes
	procs = []
	
	for i in xrange(n_rasterizers):
		p = multiprocessing.Process(target=rasterizer_plotter_worker,
		                            args=(dist_q, lock,
		                                  mapper,
		                                  args.figsize, args.dpi, size,
		                                  args.model, method, mask,
		                                  proj, l_cent, b_cent, args.bounds,
		                                  args.l_lines, args.b_lines,
		                                  args.delta_mu, EBV_max,
		                                  outfname, args.show)
		                           )
		procs.append(p)
	
	for p in procs:
		p.start()
	
	for p in procs:
		p.join()
	
	print 'Done.'
	
	#if args.show:
	#	plt.show()
	
	
	return 0

if __name__ == '__main__':
	main()

