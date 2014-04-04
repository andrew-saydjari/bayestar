#!/usr/bin/env python2.7
# -*- coding: utf-8 -*-
#
#       plotpdf.py
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

import sys, argparse
from os.path import abspath, expanduser

import numpy as np
import scipy.optimize as opt
from scipy.ndimage.filters import gaussian_filter

import matplotlib as mplib
import matplotlib.pyplot as plt
from mpl_toolkits.axes_grid1 import ImageGrid
from matplotlib.ticker import MaxNLocator, AutoMinorLocator

import hdf5io


def los_integral(surfs, DEBV, EBV_lim=(0., 5.), subsampling=1):
	n_stars, n_DM, n_EBV = surfs.shape
	n_regions = DEBV.size - 1
	
	assert (n_DM % n_regions == 0)
	
	n_pix_per_bin = n_DM / n_regions
	n_samples = subsampling * n_pix_per_bin
	
	EBV_per_pix = (EBV_lim[1] - EBV_lim[0]) / float(n_EBV)
	
	EBV = np.hstack([np.cumsum(DEBV), [0.]]) - EBV_lim[0]
	
	x = np.arange(subsampling * n_DM).astype('f4') / n_pix_per_bin
	x_floor = x.astype('i4')
	a = (x - x_floor.astype('f4'))
	
	y = ((1. - a) * EBV[x] + a * EBV[x+1]) * EBV_per_pix
	y_floor = y.astype('i4')
	a = (y - y_floor.astype('f4'))
	
	p = ( (1. - a) * surfs[:, np.arange(n_DM), y_floor]
	           + a * surfs[:, np.arange(n_DM), y_floor+1] )
	
	return np.sum(p, axis=1)


def los2ax(ax, fname, group, DM_lim, *args, **kwargs):
	chain = hdf5io.TChain(fname, '%s/los' % group)
	
	mu = np.linspace(DM_lim[0], DM_lim[1], chain.get_nDim())
	if 'alpha' not in kwargs:
		kwargs['alpha'] = 1. / np.power(chain.get_nSamples(), 0.55)
	
	# Plot all paths
	EBV_all = np.cumsum(np.exp(chain.get_samples(0)), axis=1)
	lnp = chain.get_lnp()[0, 1:]
	#lnp = np.exp(lnp - np.max(lnp))
	lnp_min, lnp_max = np.percentile(lnp, [10., 90.])
	lnp = (lnp - lnp_min) / (lnp_max - lnp_min)
	lnp[lnp > 1.] = 1.
	lnp[lnp < 0.] = 0.
	
	for i,EBV in enumerate(EBV_all[1:]):
		c = (1.-lnp[i], 0., lnp[i])
		kwargs['c'] = c
		ax.plot(mu, EBV, *args, **kwargs)
	
	kwargs['c'] = 'g'
	kwargs['lw'] = 1.5
	kwargs['alpha'] = 0.5
	ax.plot(mu, EBV_all[0], *args, **kwargs)
	
	# Plot mean path
	#y = np.mean(EBV_all, axis=0)
	#y_err = np.std(EBV_all, axis=0)
	#ax.errorbar(mu, y, yerr=y_err, c='g', ecolor=(0., 1., 0., 0.5), alpha=0.3)
	
	# Plot best path
	#i = np.argsort(chain.get_lnp(0))[::-1]
	#alpha = 1.
	#for ii in i[:3]:
	#	ax.plot(mu, EBV_all[ii], 'r-', alpha=alpha)
	#	alpha *= 0.5
	#alpha = 1.
	#for ii in i[-10:]:
	#	ax.plot(mu, EBV_all[ii], 'k-', alpha=alpha)
	#	alpha *= 0.85
	
	ax.set_xlim(DM_lim[0], DM_lim[1]) 

def clouds2ax(ax, fname, group, DM_lim, *args, **kwargs):
	chain = hdf5io.TChain(fname, '%s/clouds' % group)
	mu_range = np.linspace(DM_lim[0], DM_lim[1], chain.get_nDim())
	if 'alpha' not in kwargs:
		kwargs['alpha'] = 1. / np.power(chain.get_nSamples(), 0.55)
	
	# Plot all paths
	N_clouds = chain.get_nDim() / 2
	N_paths = chain.get_nSamples()
	mu_tmp = np.cumsum(chain.get_samples(0)[:,:N_clouds], axis=1)
	EBV_tmp = np.cumsum(np.exp(chain.get_samples(0)[:,N_clouds:]), axis=1)
	
	mu_all = np.zeros((N_paths, 2*(N_clouds+1)), dtype='f8')
	EBV_all = np.zeros((N_paths, 2*(N_clouds+1)), dtype='f8')
	mu_all[:,0] = mu_range[0]
	mu_all[:,1:-1:2] = mu_tmp
	mu_all[:,2:-1:2] = mu_tmp
	mu_all[:,-1] = mu_range[-1]
	EBV_all[:,2:-1:2] = EBV_tmp
	EBV_all[:,3::2] = EBV_tmp
	#EBV_all[:,-1] = EBV_tmp[:,-1]
	
	lnp = chain.get_lnp()[0, 1:]
	lnp_min, lnp_max = np.percentile(lnp, [10., 90.])
	lnp = (lnp - lnp_min) / (lnp_max - lnp_min)
	lnp[lnp > 1.] = 1.
	lnp[lnp < 0.] = 0.
	
	for i,(mu,EBV) in enumerate(zip(mu_all[1:], EBV_all[1:])):
		c = (1.-lnp[i], 0., lnp[i])
		kwargs['c'] = c
		ax.plot(mu, EBV, *args, **kwargs)
	
	kwargs['c'] = 'g'
	kwargs['alpha'] = 0.5
	ax.plot(mu_all[0], EBV_all[0], *args, **kwargs)
	
	# Plot mean path
	#y = np.mean(EBV_all, axis=0)
	#y_err = np.std(EBV_all, axis=0)
	#ax.errorbar(mu, y, yerr=y_err, c='g', ecolor=(0., 1., 0., 0.5), alpha=0.3)
	
	# Plot best path
	#i = np.argsort(chain.get_lnp(0))[::-1]
	#alpha = 1.
	#for ii in i[:3]:
	#	ax.plot(mu, EBV_all[ii], 'r-', alpha=alpha)
	#	alpha *= 0.5
	#alpha = 1.
	#for ii in i[-10:]:
	#	ax.plot(mu, EBV_all[ii], 'k-', alpha=alpha)
	#	alpha *= 0.85
	
	ax.set_xlim(DM_lim[0], DM_lim[1]) 

def find_contour_levels(pdf, pctiles):
	norm = np.sum(pdf)
	pctile_diff = lambda pixval, target: np.sum(pdf[pdf > pixval]) / norm - target
	
	levels = []
	
	for P in pctiles:
		l = opt.brentq(pctile_diff, np.min(pdf), np.max(pdf),
		               args=P/100., xtol=1.e-5, maxiter=25)
		levels.append(l)
	
	return np.array(levels)


def main():
	# Parse commandline arguments
	parser = argparse.ArgumentParser(prog='plotpdf',
	              description='Plots posterior distributions produced by galstar',
	              add_help=True)
	parser.add_argument('input', type=str, help='Bayestar output file.')
	parser.add_argument('loc', type=int, nargs=2, help='HEALPix nside and index.')
	parser.add_argument('-o', '--output', type=str, help='Filename for plot.')
	parser.add_argument('-s', '--show', action='store_true', help='Show plot.')
	parser.add_argument('-pdfs', '--show-pdfs', action='store_true',
	                            help='Show stellar pdfs in background.')
	parser.add_argument('-los', '--show-los', action='store_true',
	                            help='Show piecewise-linear l.o.s. reddening.')
	parser.add_argument('-cl', '--show-clouds', action='store_true',
	                            help='Show cloud model of reddening.')
	parser.add_argument('-ind', '--show-individual', action='store_true',
	                            help='Show individual stellar pdfs above main plot.')
	parser.add_argument('-ovplt', '--overplot-clouds', type=float, nargs='+', default=None,
	                            help='Overplot clouds at specified distance/depth pairs.')
	#parser.add_argument('--testfn', nargs='+', type=str, default=None,
	#                    help='ASCII file with true stellar parameters (same as used for galstar input).')
	#parser.add_argument('-cnv', '--converged', action='store_true',
	#                    help='Show only converged stars.')
	parser.add_argument('-fig', '--figsize', type=float, nargs=2, default=(7, 5.),
	                          help='Figure width and height in inches.')
	parser.add_argument('-y', '--EBV-max', type=float, default=None,
	                          help='Upper limit of E(B-V) for the y-axis.')
	if 'python' in sys.argv[0]:
		offset = 2
	else:
		offset = 1
	args = parser.parse_args(sys.argv[offset:])
	
	if (args.output == None) and not args.show:
		print 'Either --output or --show must be given.'
		return 0
	
	fname = abspath(expanduser(args.input))
	group = 'pixel %d-%d' % (args.loc[0], args.loc[1])
	
	# Load in pdfs
	x_min, x_max = [4., 0.], [19., 5.]
	pdf_stack = None
	pdf_indiv = None
	EBV_max = args.EBV_max
	
	if args.show_pdfs:
		dset = '%s/stellar chains' % group
		chain = hdf5io.TChain(fname, dset)
		
		conv = chain.get_convergence()[:]
		
		lnZ = chain.get_lnZ()[:]
		lnZ_max = np.percentile(lnZ[np.isfinite(lnZ)], 98.)
		lnZ_idx = (lnZ > lnZ_max - 10.)
		#print np.sum(lnZ_idx), np.sum(~lnZ_idx)
		
		print 'ln(Z_98) = %.2f' % (lnZ_max)
		
		for Delta_lnZ in [2.5, 5., 10., 15., 100.]:
			tmp = np.sum(lnZ < lnZ_max - Delta_lnZ) / float(lnZ.size)
			print '%.2f %% fail D = %.1f cut' % (100.*tmp, Delta_lnZ)
		
		try:
			dset = '%s/stellar pdfs' % group
			pdf = hdf5io.TProbSurf(fname, dset)
			x_min, x_max = pdf.x_min, pdf.x_max
			
			idx = conv & lnZ_idx
			
			pdf_indiv = pdf.get_p()[idx]
			pdf_stack = np.sum(pdf_indiv, axis=0)
			
			if args.show_individual:
				idx = np.arange(pdf_indiv.shape[0])
				np.random.shuffle(idx)
				pdf_indiv = pdf_indiv[idx[:4]]
			
		except:
			print 'Using chains to create image of stacked pdfs...'
			
			star_samples = chain.get_samples()[:, :, 0:2]
			
			idx = conv & lnZ_idx
			
			star_samples = star_samples[idx]
			
			n_stars_tmp, n_star_samples, n_star_dim = star_samples.shape
			star_samples.shape = (n_stars_tmp * n_star_samples, n_star_dim)
			
			res = (501, 121)
			
			E_range = np.linspace(x_min[1], x_max[1], res[0]*2+1)
			DM_range = np.linspace(x_min[0], x_max[0], res[1]*2+1)
			
			pdf_stack, tmp1, tmp2 = np.histogram2d(star_samples[:,0],
			                                       star_samples[:,1],
			                                       bins=[E_range, DM_range])
			
			pdf_stack = gaussian_filter(pdf_stack.astype('f8'),
			                            sigma=(6, 3), mode='reflect')
			pdf_stack = pdf_stack.reshape([res[0], 2, res[1], 2]).mean(3).mean(1)
			pdf_stack *= 100. / np.max(pdf_stack)
			pdf_stack = pdf_stack.T
			
			if args.show_individual:
				star_samples.shape = (n_stars_tmp, n_star_samples, n_star_dim)
				idx = np.arange(n_stars_tmp)
				np.random.shuffle(idx)
				
				pdf_indiv = []
				
				for k in idx[:4]:
					pdf_tmp, tmp1, tmp2 = np.histogram2d(star_samples[k,:,0],
					                                     star_samples[k,:,1],
					                                     bins=[E_range, DM_range])
					pdf_tmp = gaussian_filter(pdf_tmp.astype('f8'),
					                          sigma=(8,4), mode='reflect')
					pdf_tmp = pdf_tmp.reshape([res[0], 2, res[1], 2]).mean(3).mean(1)
					pdf_tmp *= 100. / np.max(pdf_tmp)
					pdf_tmp = pdf_tmp.T
					
					pdf_indiv.append(pdf_tmp)
				
				pdf_indiv = np.array(pdf_indiv)
		
		# Normalize peak to unity at each distance
		pdf_stack /= np.max(pdf_stack)
		norm = 1. / np.power(np.max(pdf_stack, axis=1), 0.75)
		norm[np.isinf(norm)] = 0.
		pdf_stack = np.einsum('ij,i->ij', pdf_stack, norm)
		
		# Determine maximum E(B-V)
		if EBV_max == None:
			w_y = np.mean(pdf_stack, axis=0)
			y_max = np.max(np.where(w_y > 1.e-2)[0])
			EBV_max = y_max * (5. / pdf_stack.shape[1])
		
		# Save individual stellar pdfs to show
		#if args.show_individual:
		#	idx = np.arange(pdf_stack.shape[0])
		#	np.random.shuffle(idx)
		#	pdf_indiv = pdf_stack[idx[:4]]
	
	# Set matplotlib style attributes
	mplib.rc('text', usetex=True)
	mplib.rc('xtick.major', size=5)
	mplib.rc('xtick.minor', size=2)
	mplib.rc('ytick.major', size=5)
	mplib.rc('ytick.minor', size=2)
	mplib.rc('xtick', direction='out')
	mplib.rc('ytick', direction='out')
	mplib.rc('axes', grid=False)
	
	# Set up figure
	fig = plt.figure(figsize=args.figsize, dpi=150)
	
	ax = fig.add_subplot(1,1,1)
	
	ax.set_xlabel(r'$\mu$', fontsize=16)
	ax.set_ylabel(r'$\mathrm{E} \left( B - V \right)$', fontsize=16)
	ax.xaxis.set_major_locator(MaxNLocator(nbins=5))
	ax.xaxis.set_minor_locator(AutoMinorLocator())
	ax.yaxis.set_major_locator(MaxNLocator(nbins=4))
	ax.yaxis.set_minor_locator(AutoMinorLocator())
	fig.subplots_adjust(bottom=0.12, left=0.12, right=0.98)
	
	bounds = [x_min[0], x_max[0], x_min[1], x_max[1]]
	if args.show_pdfs:
		ax.imshow(np.sqrt(pdf_stack.T), extent=bounds, origin='lower',
		             aspect='auto', cmap='Blues', interpolation='nearest')
	
	# Mini-plots of individual stellar pdfs
	ax_indiv = []
	if args.show_individual:
		fig.subplots_adjust(top=0.75)
		
		x_sep = 0.02
		w = (0.98 - 0.12 - 3. * x_sep) / 4.
		
		for i in xrange(4):
			x_0 = 0.12 + i * (w + x_sep)
			rect = [x_0, 0.78, w, 0.20]
			ax_tmp = fig.add_axes(rect)
			
			if args.show_pdfs:
				ax_tmp.imshow(np.sqrt(pdf_indiv[i].T), extent=bounds, origin='lower',
				              aspect='auto', cmap='Blues', interpolation='nearest')
				
				#levels = find_contour_levels(pdf_indiv[i], [50., 95.])
				
				#X = np.linspace(bounds[0], bounds[1], pdf_indiv[i].shape[0])
				#Y = np.linspace(bounds[2], bounds[3], pdf_indiv[i].shape[1])
				
				#ax_tmp.contour(X.flatten(), Y.flatten(), pdf_indiv[i].T, levels)
			
			ax_tmp.set_xticks([])
			ax_tmp.set_yticks([])
			
			ax_indiv.append(ax_tmp)
	
	# Plot l.o.s. extinction to figure
	DM_lim = [x_min[0], x_max[0]]
	
	if args.show_los:
		try:
			los2ax(ax, fname, group, DM_lim, c='k', alpha=0.05, lw=1.5)
			for sub_ax in ax_indiv:
				los2ax(sub_ax, fname, group, DM_lim, c='k', alpha=0.015)
		except:
			pass
	
	if args.show_clouds:
		try:
			clouds2ax(ax, fname, group, DM_lim, c='k', alpha=0.08, lw=1.5)
			for sub_ax in ax_indiv:
				clouds2ax(sub_ax, fname, group, DM_lim, c='k')
		except:
			pass
	
	if EBV_max != None:
		ax.set_ylim(x_min[1], EBV_max)
		
		for sub_ax in ax_indiv:
			sub_ax.set_ylim(x_min[1], EBV_max)
	
	# Overplot manually-specified clouds
	if args.overplot_clouds != None:
		mu_cloud = np.array(args.overplot_clouds[::2])
		EBV_cloud = np.cumsum(np.array(args.overplot_clouds[1::2]))
		
		#mu_plot = np.linspace(DM_lim[0], DM_lim[1], chain.get_nDim())
		
		# Plot all paths
		N_clouds = len(args.overplot_clouds) / 2
		#mu_tmp = np.cumsum(chain.get_samples(0)[:,:N_clouds], axis=1)
		#EBV_tmp = np.cumsum(np.exp(chain.get_samples(0)[:,N_clouds:]), axis=1)
		
		mu_all = np.zeros(2*(N_clouds+1), dtype='f8')
		EBV_all = np.zeros(2*(N_clouds+1), dtype='f8')
		mu_all[0] = DM_lim[0]
		mu_all[1:-1:2] = mu_cloud
		mu_all[2:-1:2] = mu_cloud
		mu_all[-1] = DM_lim[1]
		EBV_all[2:-1:2] = EBV_cloud
		EBV_all[3::2] = EBV_cloud
		
		ax.plot(mu_all, EBV_all, 'k--', lw=2, alpha=0.5)
	
	
	# Save/show plot
	if args.output != None:
		fig.savefig(args.output, dpi=300)
	if args.show:
		plt.show()
	
	return 0

if __name__ == '__main__':
	main()

