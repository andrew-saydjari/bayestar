#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
#  compareSEGUE.py
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
import scipy
import scipy.stats
import scipy.special
import h5py
import time

import matplotlib.pyplot as plt
import matplotlib as mplib
from matplotlib.ticker import FormatStrFormatter, AutoMinorLocator, MaxNLocator

import hdf5io

def get2DProbSurfs(fname):
	f = h5py.File(fname, 'r')
	
	# Hack to get the file to read properly
	try:
		f.items()
	except:
		pass
	
	# Load in probability surfaces from each pixel
	surfs = []
	pixIdx = []
	minEBV, maxEBV = None, None
	for name,item in f.iteritems():
		if 'pixel' in name:
			dset = str(name + '/stellar pdfs')
			idx = int(name.split()[1])
			stack = hdf5io.TProbSurf(f, dset)
			#tmp = np.sum(stack.p[:,:,:], axis=1)
			#tmp = np.einsum('ij,i->ij', tmp, 1./np.sum(tmp, axis=1))
			surfs.append(stack.p[:,:,:])
			pixIdx.append(idx)
			minEBV = f[dset].attrs['min'][0]
			maxEBV = f[dset].attrs['max'][0]
			break
	
	f.close()
	
	return surfs, minEBV, maxEBV, pixIdx
	
def get1DProbSurfs(fname):
	f = h5py.File(fname, 'r')
	
	# Hack to get the file to read properly
	try:
		f.items()
	except:
		pass
	
	# Load in probability surfaces from each pixel
	surfs = []
	EBVsamples = []
	Mrsamples = []
	pixIdx = []
	good = []
	lnZ = []
	minEBV, maxEBV = None, None
	for name,item in f.iteritems():
		if 'pixel' in name:
			dset = str(name + '/stellar pdfs')
			idx = int(name.split()[1])
			#stack = hdf5io.TProbSurf(f, dset)
			#tmp = np.sum(stack.p[:,:,:], axis=1)
			#tmp = np.einsum('ij,i->ij', tmp, 1./np.sum(tmp, axis=1))
			#surfs.append(tmp)
			pixIdx.append(idx)
			minEBV = f[dset].attrs['min'][0]
			maxEBV = f[dset].attrs['max'][0]
			
			n_stars, n_bins, tmp = f[dset].shape
			
			#minEBV = -3.
			#maxEBV = 5.
			
			dset = str(name + '/stellar chains')
			EBVsamples.append(f[dset][:,1:,1])
			lnZ.append(f[dset].attrs['ln(Z)'][:])
			conv = f[dset].attrs['converged'][:]
			mask = conv & (lnZ[-1] > np.max(lnZ[-1]) - 20.)
			good.append(mask.astype(np.bool))
			
			dset = str(name + '/stellar chains')
			Mrsamples.append(f[dset][:,1:,3])
			
			# Surfaces from samples
			stack = np.empty((n_stars, n_bins), dtype='f8')
			
			for k in xrange(n_stars):
				idx = (Mrsamples[-1][k] < 6.)
				stack[k, :], edges = np.histogram(EBVsamples[-1][k, idx],
				                                  bins=n_bins,
				                                  range=(minEBV, maxEBV))
			
			tmp = np.einsum('ij,i->ij', stack, 1./np.sum(stack, axis=1))
			surfs.append(tmp)
	
	f.close()
	
	return surfs, EBVsamples, Mrsamples, good, lnZ, minEBV, maxEBV, pixIdx

def getSEGUE(fname):
	f = h5py.File(fname, 'r')
	
	# Hack to get the file to read properly
	try:
		f.items()
	except:
		pass
	
	SEGUE = f['SEGUE']
	
	# Load in properties from each pixel
	props = []
	pixIdx = []
	for name,item in SEGUE.iteritems():
		if 'pixel' in name:
			idx = int(name.split()[1])
			dset = str(name)
			prop = SEGUE[dset][:]
			props.append(prop)
			pixIdx.append(idx)
	
	return props, pixIdx

def getPhotometry(fname):
	f = h5py.File(fname, 'r')
	
	# Hack to get the file to read properly
	try:
		f.items()
	except:
		pass
	
	phot = f['photometry']
	
	# Load in photometry from each pixel
	mags = []
	errs = []
	EBV_SFD = []
	pixIdx = []
	for name,item in phot.iteritems():
		if 'pixel' in name:
			idx = int(name.split()[1])
			dset = str(name)
			mags.append(phot[dset]['mag'][:])
			errs.append(phot[dset]['err'][:])
			EBV_SFD.append(phot[dset]['EBV'][:])
			pixIdx.append(idx)
	
	return mags, errs, EBV_SFD, pixIdx

def calc_color_stats(mags, cov):
	'''
	Transform from magnitudes to colors.
	
	Inputs:
	    mags.shape = (# of samples, # of bands)
	    cov.shape = (# of samples, # of bands, # of bands)
	
	Outputs:
	    colors.shape = (# of samples, # of bands - 1)
	    cov_colors = (# of samples, # of bands - 1, # of bands - 1)
	'''
	dtype = mags.dtype
	n_samples, n_bands = mags.shape
	
	colors = -np.diff(mags, axis=1)
	cov_colors = cov[:,1:,1:] + cov[:,:-1,:-1] - cov[:,1:,:-1] - cov[:,:-1,1:]
	
	return colors, cov_colors

def calc_mu_sigma(cov, mu, A):
	#print cov.shape
	#print mu.shape
	#print A.shape
	inv_cov = np.array([np.linalg.inv(c) for c in cov])
	inv_cov_A = np.einsum('ijk,k->ij', inv_cov, A)
	#print inv_cov_A.shape
	denom = np.einsum('j,ij->i', A, inv_cov_A)
	num = np.einsum('ij,ij->i', mu, inv_cov_A)
	
	sigma = np.sqrt(1./denom)
	mu_scalar = num / denom
	
	return mu_scalar, sigma

def getSegueEBV(props, bands=5):
	A_coeff = np.array([4.239, 3.303, 2.285, 1.698, 1.263])[:bands]
	A_diff = -np.diff(A_coeff)
	
	EBVs = []
	sigmaEBVs = []
	for prop in props:
		n_stars, n_bands = prop['ssppmag'].shape
		cov_SSPP = np.reshape(prop['ssppmagcovar'][:], (n_stars, n_bands, n_bands))
		if n_bands > bands:
			n_bands = bands
			cov_SSPP = cov_SSPP[:,:bands,:bands]
		
		mags_SSPP = prop['ssppmag'][:,:bands]
		
		errs_SDSS = prop['ubermagerr'][:,:bands] + 0.02
		mags_SDSS = prop['ubermag'][:,:bands]
		
		colors_SSPP, cov_colors_SSPP = calc_color_stats(mags_SSPP, cov_SSPP)
		
		cov_SDSS = np.zeros((n_stars, n_bands, n_bands), dtype='f8')
		for k in xrange(n_bands):
			cov_SDSS[:,k,k] = errs_SDSS[:,k] * errs_SDSS[:,k]
		colors_SDSS, cov_colors_SDSS = calc_color_stats(mags_SDSS, cov_SDSS)
		
		cov_tot = cov_colors_SSPP + cov_colors_SDSS
		E = colors_SDSS - colors_SSPP
		
		EBV, sigma_EBV = calc_mu_sigma(cov_tot, E, A_diff)
		
		'''
		E = np.diff(prop['ubermag'] - prop['ssppmag'], axis=1)
		#print E.shape
		s1 = prop['ubermagerr'][:,:-1]
		s2 = prop['ubermagerr'][:,1:]
		s3 = prop['ssppmagerr'][:,:-1]
		s4 = prop['ssppmagerr'][:,1:]
		#s1 = np.sqrt(s1*s1+0.02*0.02)
		sigmaE = np.sqrt(s1*s1 + s2*s2 + s3*s3 + s4*s4 + 0.02*0.02*(bands-1.))
		EBV = E / ADiff
		sigmaEBV = np.abs(sigmaE / E) * EBV
		
		num = np.sum(EBV * sigmaEBV * sigmaEBV, axis=1)
		den1 = np.sum(sigmaEBV * sigmaEBV, axis=1)
		den2 = np.sum(1. / (sigmaEBV * sigmaEBV), axis=1)
		
		EBV = num / den1
		sigmaEBV = np.sqrt(1. / den2)
		'''
		
		EBVs.append(EBV)
		sigmaEBVs.append(sigma_EBV)
	
	return EBVs, sigmaEBVs

def percentile(surf, EBV, minEBV, maxEBV):
	nCells = surf.shape[1]
	nStars = surf.shape[0]
	
	DeltaEBV = (maxEBV - minEBV) / nCells
	cellNo = np.floor((EBV - minEBV) / DeltaEBV).astype('i4')
	maskRemove = (cellNo >= nCells) | (cellNo < 0)
	cellNo[maskRemove] = 0
	
	starNo = np.arange(nStars, dtype='i4')
	
	p_threshold = surf[starNo, cellNo]
	#print p_threshold
	
	p_threshold.shape = (nStars, 1)
	p_threshold = np.repeat(p_threshold, nCells, axis=1)
	
	surfZeroed = surf - p_threshold
	idx = surfZeroed < 0
	surfZeroed[idx] = 0.
	pctiles = 1. - np.sum(surfZeroed, axis=1)
	
	pctiles[maskRemove] = np.nan
	
	return pctiles


def multiply1DSurfs(surfs, EBV, sigmaEBV, minEBV, maxEBV):
	nStars, nCells = surfs.shape
	
	DeltaEBV = (maxEBV - minEBV) / nCells
	muCell = (EBV - minEBV) / DeltaEBV
	sigmaCell = sigmaEBV / DeltaEBV
	
	dist = np.linspace(0.5, nCells - 0.5, nCells)
	dist.shape = (1, nCells)
	dist = np.repeat(dist, nStars, axis=0)
	muCell.shape = (nStars, 1)
	muCell = np.repeat(muCell, nCells, axis=1)
	dist -= muCell
	sigmaCell.shape = (nStars, 1)
	sigmaCell = np.repeat(sigmaCell, nCells, axis=1)
	dist /= sigmaCell
	
	pEBV = np.exp(-0.5 * dist * dist)
	pEBV = np.einsum('ij,i->ij', pEBV, 1./np.sum(pEBV, axis=1))
	
	return np.sum(pEBV * surfs, axis=1)
	
	'''
	projProb = np.sum(pEBV, axis=0)
	maxIdx = np.max( np.where(projProb > 1.e-5*np.max(projProb))[0] )
	plotMaxEBV = minEBV + (maxEBV - minEBV) * (float(maxIdx) / float(nCells))
	plotMaxEBV = max(plotMaxEBV, 1.2 * np.max(EBV))
	
	# Set matplotlib style attributes
	mplib.rc('text', usetex=True)
	mplib.rc('xtick.major', size=6)
	mplib.rc('xtick.minor', size=4)
	mplib.rc('ytick.major', size=6)
	mplib.rc('ytick.minor', size=4)
	mplib.rc('xtick', direction='out')
	mplib.rc('ytick', direction='out')
	mplib.rc('axes', grid=False)
	
	fig = plt.figure(figsize=(4,3), dpi=200)
	ax = fig.add_subplot(1,1,1)
	
	#pEBV /= np.max(pEBV)
	#surfs /= np.max(surfs)
	
	#img = np.ones((nCells, nStars, 3), dtype='f8')
	#img[:,:,0] -= pEBV.T
	#img[:,:,1] -= surfs.T
	
	img = pEBV * surfs
	
	bounds = [0, nStars, minEBV, maxEBV]
	ax.imshow(img.T, extent=bounds, origin='lower', aspect='auto',
	                                            interpolation='nearest')
	
	n = np.linspace(0.5, nStars-0.5, nStars)
	ax.errorbar(n, EBV, yerr=sigmaEBV, fmt=None, ecolor='g', capsize=2,
	                                                          alpha=0.5)
	
	ax.set_xlim(bounds[0:2])
	ax.set_ylim(bounds[2], plotMaxEBV)
	
	ax.set_xlabel(r'$\mathrm{Index \ of \ Star}$', fontsize=16)
	ax.set_ylabel(r'$\mathrm{E} \left( B - V \right)$', fontsize=16)
	fig.subplots_adjust(left=0.18, bottom=0.18)
	'''


def pval1DSurfs(surfs, EBV, sigmaEBV, minEBV, maxEBV):
	nStars, nCells = surfs.shape
	
	DeltaEBV = (maxEBV - minEBV) / nCells
	
	y = np.linspace(0.5, nCells - 0.5, nCells)
	y.shape = (1, nCells)
	y = np.repeat(y, nStars, axis=0)
	yCell = np.sum(y*surfs, axis=1) / np.sum(surfs, axis=1)
	y2Cell = np.sum(y*y*surfs, axis=1) / np.sum(surfs, axis=1)
	
	mu = yCell * DeltaEBV
	sigma2 = (y2Cell - yCell*yCell) * (DeltaEBV*DeltaEBV)
	
	sigma2 = sigmaEBV*sigmaEBV + sigma2
	#print EBV, mu
	nSigma = np.abs(EBV - mu) / np.sqrt(sigma2)
	
	return 1. - scipy.special.erf(nSigma)


def binom_confidence(nbins, ntrials, confidence):
	q = 0.5 * (1. - confidence)
	qprime = (1. - q)**(1./nbins)
	
	rv = scipy.stats.binom(ntrials, 1./float(nbins))
	P = rv.cdf(np.arange(ntrials+1))
	
	lower = np.where((1. - P) >= qprime)[0][-1]
	upper = np.where(P < qprime)[0][-1] + 1
	
	return lower, upper

def plotPercentiles(pctiles):
	# Set matplotlib style attributes
	mplib.rc('text', usetex=True)
	mplib.rc('xtick.major', size=6)
	mplib.rc('xtick.minor', size=4)
	mplib.rc('ytick.major', size=6)
	mplib.rc('ytick.minor', size=4)
	mplib.rc('xtick', direction='out')
	mplib.rc('ytick', direction='out')
	mplib.rc('axes', grid=False)
	
	fig = plt.figure(figsize=(4,3), dpi=200)
	ax = fig.add_subplot(1,1,1)
	
	ax.hist(pctiles, alpha=0.6)
	
	lower, upper = binom_confidence(10, pctiles.shape[0], 0.95)
	ax.fill_between([0., 1.], [lower, lower], [upper, upper], facecolor='g', alpha=0.2)
	
	lower, upper = binom_confidence(10, pctiles.shape[0], 0.50)
	ax.fill_between([0., 1.], [lower, lower], [upper, upper], facecolor='g', alpha=0.2)
	
	ax.set_xlim(0., 1.)
	ax.set_ylim(0., 1.5*upper)
	
	ax.set_xlabel(r'$\% \mathrm{ile}$', fontsize=16)
	ax.set_ylabel(r'$\mathrm{\# \ of \ stars}$', fontsize=16)
	fig.subplots_adjust(left=0.20, bottom=0.18)
	
	return fig

def plot1DSurfs(surfs, good, EBV, sigmaEBV, minEBV, maxEBV):
	surfs = surfs[good]
	EBV = EBV[good]
	sigmaEBV = sigmaEBV[good]
	
	nStars, nCells = surfs.shape
	
	projProb = np.sum(surfs, axis=0)
	maxIdx = np.max( np.where(projProb > 1.e-5*np.max(projProb))[0] )
	plotMaxEBV = minEBV + (maxEBV - minEBV) * (float(maxIdx) / float(nCells))
	plotMaxEBV = max(plotMaxEBV, 1.2 * np.max(EBV))
	
	# Set matplotlib style attributes
	mplib.rc('text', usetex=True)
	mplib.rc('xtick.major', size=6)
	mplib.rc('xtick.minor', size=4)
	mplib.rc('ytick.major', size=6)
	mplib.rc('ytick.minor', size=4)
	mplib.rc('xtick', direction='out')
	mplib.rc('ytick', direction='out')
	mplib.rc('axes', grid=False)
	
	fig = plt.figure(figsize=(4,3), dpi=200)
	ax = fig.add_subplot(1,1,1)
	
	bounds = [0, nStars, minEBV, maxEBV]
	ax.imshow(surfs.T, extent=bounds, origin='lower', aspect='auto',
	                                cmap='hot', interpolation='nearest')
	
	n = np.linspace(0.5, nStars-0.5, nStars)
	ax.errorbar(n, EBV, yerr=sigmaEBV, fmt=None, ecolor='g', capsize=2,
	                                                          alpha=0.5)
	
	ax.set_xlim(bounds[0:2])
	ax.set_ylim(bounds[2], plotMaxEBV)
	
	ax.set_xlabel(r'$\mathrm{Index \ of \ Star}$', fontsize=16)
	ax.set_ylabel(r'$\mathrm{E} \left( B - V \right)$', fontsize=16)
	fig.subplots_adjust(left=0.18, bottom=0.18)

def plot2DSurfs(surfs2D, surfs1D, EBV, sigmaEBV, minEBV, maxEBV):
	nStars, nCells = surfs1D.shape
	
	projProb = np.sum(surfs1D, axis=0)
	maxIdx = np.max( np.where(projProb > 1.e-5*np.max(projProb))[0] )
	plotMaxEBV = minEBV + (maxEBV - minEBV) * (float(maxIdx) / float(nCells))
	plotMaxEBV = max(plotMaxEBV, 1.2 * np.max(EBV))
	
	# Set matplotlib style attributes
	mplib.rc('text', usetex=True)
	mplib.rc('xtick.major', size=6)
	mplib.rc('xtick.minor', size=4)
	mplib.rc('ytick.major', size=6)
	mplib.rc('ytick.minor', size=4)
	mplib.rc('xtick', direction='out')
	mplib.rc('ytick', direction='out')
	mplib.rc('axes', grid=False)
	
	fig = plt.figure(figsize=(4,3), dpi=200)
	ax = fig.add_subplot(1,2,1)
	
	bounds = [0, nStars, minEBV, maxEBV]
	ax.imshow(surfs1D.T, extent=bounds, origin='lower', aspect='auto',
	                                cmap='hot', interpolation='nearest')
	
	n = np.linspace(0.5, nStars-0.5, nStars)
	ax.errorbar(n, EBV, yerr=sigmaEBV, fmt=None, ecolor='g', capsize=2,
	                                                          alpha=0.5)
	
	ax.set_xlim(bounds[0:2])
	ax.set_ylim(bounds[2], plotMaxEBV)
	ax.set_xlabel(r'$\mathrm{Index \ of \ Star}$', fontsize=16)
	ax.set_ylabel(r'$\mathrm{E} \left( B - V \right)$', fontsize=16)
	
	ax = fig.add_subplot(1,2,2)
	bounds = [5., 20., minEBV, maxEBV]
	ax.imshow(surfs2D[2].T, extent=bounds, origin='lower', aspect='auto',
	                                cmap='hot', interpolation='nearest')
	
	ax.set_ylim(minEBV, plotMaxEBV)
	ax.set_xlabel(r'$\mu$', fontsize=16)
	ax.set_ylabel(r'$A_{r}$', fontsize=16)
	
	fig.subplots_adjust(left=0.18, bottom=0.18)

def plotScatter(surfs, EBV_samples, EBV, sigmaEBV, Mr_samples,
                minEBV, maxEBV, EBV_SFD, method='max', norm=False, filt_giant=False):
	nStars, nCells = surfs.shape 
	DeltaEBV = (maxEBV - minEBV) / nCells
	
	nStars = EBV_samples.shape[0]
	
	mu, yCell, err = None, None, None
	title = None
	
	if method == 'max':
		title = r'$\mathrm{Maximum \ Probability \ Reddening}$'
		#mu = EBV_samples[:, 0]
		
		yCell = np.argmax(surfs, axis=1).astype('f8')
		yCell += np.random.random(size=(surfs.shape[0]))
		mu = minEBV + yCell * DeltaEBV
		
		samples_err = np.std(EBV_samples[:, 1:], axis=1)
		#err = np.sqrt(samples_err*samples_err + sigmaEBV*sigmaEBV)
		err = samples_err
		
	elif method == 'mean':
		title = r'$\mathrm{Mean \ Reddening}$'
		
		'''
		y = np.linspace(0.5, nCells - 0.5, nCells)
		y.shape = (1, nCells)
		y = np.repeat(y, nStars, axis=0)
		yCell = np.sum(y*surfs, axis=1) / np.sum(surfs, axis=1)
		y2Cell = np.sum(y*y*surfs, axis=1) / np.sum(surfs, axis=1)
		sigma2 = (y2Cell - yCell*yCell) * (DeltaEBV*DeltaEBV)
		mu = minEBV + yCell * DeltaEBV
		'''
		
		mu = np.mean(EBV_samples[:, 1:], axis=1)
		
		samples_err = np.std(EBV_samples, axis=1)
		#err = np.sqrt(samples_err*samples_err + sigmaEBV*sigmaEBV)
		err = samples_err
		
	elif method == 'resample':
		title = r'$\mathrm{Reddening \ Samples}$'
		
		EBVNew = []
		EBV_samples_tmp = EBV_samples[:, 1:]
		Mr_samples_tmp = Mr_samples[:, 1:]
		
		for i in xrange(EBV_samples_tmp.shape[1]):
			# Draw a set of of samples from SEGUE
			nDev = np.random.normal(size=nStars)
			EBVNew.append(EBV + nDev * sigmaEBV)
		
		mu = np.reshape(EBV_samples_tmp.T, (EBV_samples_tmp.size))
		EBV = np.hstack(EBVNew)
		
		EBV_SFD = np.reshape(EBV_SFD, (1, EBV_SFD.size))
		EBV_SFD = np.repeat(EBV_SFD, EBV_samples_tmp.shape[1], axis=0)
		EBV_SFD = np.hstack(EBV_SFD)
		
		SEGUE_err = np.reshape(sigmaEBV, (1, sigmaEBV.size))
		SEGUE_err = np.repeat(SEGUE_err, EBV_samples_tmp.shape[1], axis=0)
		SEGUE_err = np.hstack(SEGUE_err)
		
		
		samples_err = None
		if filt_giant:
			samples_ma = np.ma.masked_array(EBV_samples_tmp, Mr_samples_tmp <= 6.)
			samples_err = np.std(EBV_samples_tmp, axis=1)
		else:
			samples_err = np.std(EBV_samples_tmp, axis=1)
		
		samples_err = np.reshape(samples_err, (1, samples_err.size))
		samples_err = np.repeat(samples_err, EBV_samples_tmp.shape[1], axis=0)
		samples_err = np.hstack(samples_err)
		
		#err = np.sqrt(SEGUE_err*SEGUE_err + samples_err*samples_err)
		err = samples_err
		
		if filt_giant:
			Mr = np.reshape(Mr_samples_tmp.T, (EBV_samples_tmp.size))
			dwarf = (Mr > 6.)
			print 'Filtering out %.2f%% as dwarfs.' % (100. * float(np.sum(dwarf)) / Mr.size)
			mu = mu[~dwarf]
			EBV = EBV[~dwarf]
			EBV_SFD = EBV_SFD[~dwarf]
			err = err[~dwarf]
	
	# Set matplotlib style attributes
	mplib.rc('text', usetex=True)
	mplib.rc('xtick.major', size=6)
	mplib.rc('xtick.minor', size=4)
	mplib.rc('ytick.major', size=6)
	mplib.rc('ytick.minor', size=4)
	mplib.rc('xtick', direction='out')
	mplib.rc('ytick', direction='out')
	mplib.rc('axes', grid=False)
	
	fig1 = plt.figure(figsize=(5,4), dpi=200)
	ax = fig1.add_subplot(1,1,1)
	
	EBVavg = EBV[:] #0.5 * (EBV + mu)
	print np.mean(EBVavg), np.std(EBVavg)
	EBVdiff = mu - EBV
	xlim = np.percentile(EBVavg, [1., 99.9])
	ylim = [-3., 3.]
	print xlim
	idx = ((EBVavg >= xlim[0]) & (EBVavg <= xlim[1]) & 
	       (EBVdiff >= ylim[0]) & (EBVdiff <= ylim[1]))
	print np.sum(idx)
	correlation_plot(ax, EBVavg[idx], EBVdiff[idx], nbins=(25,80))
	
	ax.set_xlim(xlim)
	ax.set_ylim([-0.6, 0.6])
	ax.xaxis.set_minor_locator(AutoMinorLocator())
	ax.yaxis.set_minor_locator(AutoMinorLocator())
	#ax.set_xlabel(r'$\frac{1}{2} \left[ \mathrm{E} \left( B \! - \! V \right)_{\mathrm{Bayes}} + \mathrm{E} \left( B \! - \! V \right)_{\mathrm{SEGUE}} \right]$', fontsize=14)
	ax.set_xlabel(r'$\mathrm{E} \left( B \! - \! V \right)_{\mathrm{SEGUE}}$', fontsize=14)
	ax.set_ylabel(r'$\mathrm{E} \left( B \! - \! V \right)_{\mathrm{Bayes}} - \mathrm{E} \left( B \! - \! V \right)_{\mathrm{SEGUE}}$', fontsize=14)
	fig1.subplots_adjust(left=0.20, bottom=0.20)
	
	
	fig2 = plt.figure(figsize=(5,4), dpi=200)
	ax = fig2.add_subplot(1,1,1)
	
	xlim = [-0.8, 1.6]
	ylim = [-1.0, 1.5]
	#xlim = [np.percentile(EBV, 0.5), np.percentile(EBV, 99.5)]
	#width = xlim[1] - xlim[0]
	#xlim[1] += 1. * width
	#xlim[0] -= 0.25 * width
	#yMin = min([0., np.percentile(mu, 0.5)])
	#ylim = [yMin, 2.*np.percentile(mu, 99.5)]
	#ylim = [-0.5, 2.*np.percentile(mu, 99.)]
	
	idx = ((EBV >= xlim[0]) & (EBV <= xlim[1]) &
	       (mu >= ylim[0]) & (mu <= ylim[1]))
	density_scatter(ax, EBV[idx], mu[idx], nbins=(100,100))
	#ax.scatter(EBV, mu, s=1., alpha=0.3)
	print np.max(EBV)
	max_x = max(np.abs(xlim + ylim))
	x = [-max_x, max_x]
	ax.plot(x, x, 'b-', alpha=0.5)
	
	ax.set_xlim(xlim)
	ax.set_ylim(ylim)
	
	ax.set_xlabel(r'$\mathrm{E} \left( B - V \right)_{\mathrm{SEGUE}}$', fontsize=16)
	ax.set_ylabel(r'$\mathrm{E} \left( B - V \right)_{\mathrm{Bayes}}$', fontsize=16)
	
	fig2.subplots_adjust(left=0.20, bottom=0.20)
	
	
	fig3 = plt.figure(figsize=(5,4), dpi=200)
	ax = fig3.add_subplot(1,1,1)
	fig3.subplots_adjust(left=0.20, right=0.75, bottom=0.20, top=0.90)
	ax_hist = fig3.add_axes([0.75, 0.20, 0.10, 0.70])
	
	EBVavg = EBV_SFD[:]
	EBVdiff = mu - EBV
	xlim = [0., np.percentile(EBVavg, 99.9)]
	ylim = [-3., 3.]
	idx = ((EBVavg >= xlim[0]) & (EBVavg <= xlim[1]) & 
	       (EBVdiff >= ylim[0]) & (EBVdiff <= ylim[1]))
	correlation_plot(ax, EBVavg[idx], EBVdiff[idx], nbins=(25,80), ax_hist=ax_hist)
	
	ax.set_xlim(xlim)
	ax.set_ylim([-0.6, 0.6])
	ax_hist.set_ylim(ax.get_ylim())
	ax.xaxis.set_minor_locator(AutoMinorLocator())
	ax.yaxis.set_minor_locator(AutoMinorLocator())
	ax.set_xlabel(r'$\mathrm{E} \left( B \! - \! V \right)_{\mathrm{SFD}}$', fontsize=14)
	ax.set_ylabel(r'$\mathrm{E} \left( B \! - \! V \right)_{\mathrm{Bayes}} - \mathrm{E} \left( B \! - \! V \right)_{\mathrm{SEGUE}}$', fontsize=14)
	#ax.set_title(title, fontsize=14)
	#fig3.subplots_adjust(left=0.20, bottom=0.20)
	
	
	
	fig5 = plt.figure(figsize=(5,4), dpi=200)
	ax = fig5.add_subplot(1,1,1)
	fig5.subplots_adjust(left=0.20, right=0.75, bottom=0.20, top=0.90)
	ax_hist = fig5.add_axes([0.75, 0.20, 0.10, 0.70])
	
	EBVavg = EBV_SFD[:]
	EBVdiff = (mu - EBV) / err
	xlim = [0., np.percentile(EBVavg, 99.9)]
	ylim = [-3., 3.]
	idx = ((EBVavg >= xlim[0]) & (EBVavg <= xlim[1]) & 
	       (EBVdiff >= ylim[0]) & (EBVdiff <= ylim[1]))
	correlation_plot(ax, EBVavg[idx], EBVdiff[idx], nbins=(25,80), ax_hist=ax_hist)
	
	ax.set_xlim(xlim)
	#ax.set_ylim([-0.6, 0.6])
	ax_hist.set_ylim(ax.get_ylim())
	ax.xaxis.set_minor_locator(AutoMinorLocator())
	ax.yaxis.set_minor_locator(AutoMinorLocator())
	ax.set_xlabel(r'$\mathrm{E} \left( B \! - \! V \right)_{\mathrm{SFD}}$', fontsize=14)
	ax.set_ylabel(r'$\chi_{\mathrm{Bayes} - \mathrm{SEGUE}}$', fontsize=14)
	#ax.set_title(title, fontsize=14)
	
	
	fig4 = plt.figure(figsize=(5,4), dpi=200)
	ax = fig4.add_subplot(1,1,1)
	
	EBVdiff = mu - EBV
	xlim = [-1., 1.]
	idx = (EBVdiff >= xlim[0]) & (EBVdiff <= xlim[1])
	ax.hist(EBVdiff[idx], bins=50, normed=True, alpha=0.6)
	
	mean = np.mean(EBVdiff[idx])
	sigma = np.std(EBVdiff[idx])
	#norm = 1. / np.sqrt(2. * np.pi) / sigma
	
	print mean, sigma
	
	# Clip at 2.5 sigma
	n_clip = 2.5
	idx = (EBVdiff > mean - n_clip * sigma) & (EBVdiff < mean + n_clip * sigma)
	mean = np.mean(EBVdiff[idx])
	sigma = np.std(EBVdiff[idx])
	norm = 1. / np.sqrt(2. * np.pi) / sigma
	
	print ''
	print 'method = %s:' % method
	print 'Delta E(B-V) = %.3f +- %.3f' % (mean, sigma)
	
	x = np.linspace(xlim[0], xlim[1], 1000)
	gauss_fit = norm * np.exp(-(x-mean)*(x-mean)/(2.*sigma*sigma))
	ax.plot(x, gauss_fit, 'g-', lw=2, alpha=0.6)
	
	ylim = ax.get_ylim()
	txt = '$\mu = %.2f$\n$\sigma = %.2f$' % (mean, sigma)
	print 0.95*xlim[1], 0.95*ylim[1]
	ax.text(0.95*xlim[1], 0.95*ylim[1], txt,
	        va='top', ha='right', fontsize=12)
	
	ax.set_xlim(xlim)
	ax.set_ylim(ylim)
	ax.xaxis.set_minor_locator(AutoMinorLocator())
	ax.yaxis.set_minor_locator(AutoMinorLocator())
	ax.set_xlabel(r'$\mathrm{E} \left( B \! - \! V \right)_{\mathrm{Bayes}} - \mathrm{E} \left( B \! - \! V \right)_{\mathrm{SEGUE}}$', fontsize=14)
	ax.set_ylabel(r'$\mathrm{Frequency}$', fontsize=14)
	fig4.subplots_adjust(left=0.20, bottom=0.20)
	
	
	return fig1, fig2, fig3, fig4, fig5


def correlation_plot(ax, x, y, nbins=(25,20), ax_hist=None):
	width = (1. + 1.e-5) * (np.max(x) - np.min(x)) / nbins[0]
	diffMax = np.percentile(np.abs(y), 99.9)
	#diffMax = np.percentile(np.abs(y-x), 99.)
	height = 2. * diffMax / nbins[1]
	
	density = np.zeros(nbins, dtype='f8')
	thresholds = np.zeros((nbins[0], 3), dtype='f8')
	
	#print ''
	for n in xrange(nbins[0]):
		#print '%.3f to %.3f' % (n*width, (n+1)*width)
		idx = (x >= np.min(x) + n * width) & (x < np.min(x) + (n+1) * width)
		if np.sum(idx) != 0:
			diff = y[idx]
			density[n,:] = np.histogram(diff, bins=nbins[1], density=True,
			                                      range=[-diffMax,diffMax])[0]
			diff.sort()
			for i,q in enumerate([15.87, 50., 84.13]):
				k = (len(diff) - 1) * q / 100.
				kFloor = int(k)
				#print kFloor, len(diff)
				#a = k - kFloor
				#pctile = a * diff[kFloor] + (1. - a) * diff[kFloor+1]
				thresholds[n,i] = diff[kFloor] #pctile
			#thresholds[n,:] = np.percentile(diff, [15.87, 51., 84.13])
	
	extent = (np.min(x), np.max(x), -diffMax, diffMax)
	ax.imshow(-density.T, extent=extent, origin='lower', aspect='auto',
	                               cmap='gray', interpolation='nearest')
	
	ax.plot([np.min(x)-0.5*width, np.max(x)+0.5*width], [0., 0.], 'c-', alpha=0.3)
	ax.set_xlim([np.min(x), np.max(x)])
	
	# Histogram
	if ax_hist != None:
		y_pctiles = np.percentile(y, [15.87, 84.13])
		
		ax_hist.axhline(y=y_pctiles[0], c='b', ls='-', alpha=0.25)
		ax_hist.axhline(y=y_pctiles[1], c='b', ls='-', alpha=0.25)
		
		#ny = np.ceil(nbins[1]/2.)
		#bins = np.linspace(-ny*height, ny*height, 2*int(ny)+1)
		
		n_bins = nbins[1]
		bins = np.linspace(-diffMax, diffMax, nbins+1)
		
		while bins[1] - bins[0] > 0.04:
			n_bins *= 2
			bins = np.linspace(-diffMax, diffMax, n_bins+1)
		
		idx1 = np.argmin(np.abs(bins - y_pctiles[0]))
		idx2 = np.argmin(np.abs(bins - y_pctiles[1]))
		
		ax_hist.hist(y, bins=bins[:idx1+1], orientation='horizontal',
		             color='k', alpha=0.35)
		ax_hist.hist(y, bins=bins[idx1:idx2+1], orientation='horizontal',
		             color='k', alpha=0.75)
		ax_hist.hist(y, bins=bins[idx2:], orientation='horizontal',
		             color='k', alpha=0.35)
		
		ax_hist.yaxis.tick_right()
		ax_hist.yaxis.set_major_formatter(FormatStrFormatter('$%.2f$'))
		
		ax_hist.set_ylim(ax.get_ylim())
		ax_hist.set_xticks([])
		ax_hist.set_yticks(y_pctiles)
	
	# Envelope
	EBVRange = np.linspace(np.min(x)-0.5*width, np.max(x)+0.5*width, nbins[0]+2)
	for i in xrange(3):
		y_envelope = np.hstack([[thresholds[0,i]], thresholds[:,i], [thresholds[-1,i]]])
		ax.step(EBVRange, y_envelope, where='mid', c='b', alpha=0.5)


def density_scatter(ax, x, y, nbins=(50,50), binsize=None,
                    threshold=5, c='b', s=1, cmap='jet'):
	'''
	Draw a combination density map / scatterplot to the given axes.
	
	Adapted from answer to stackoverflow question #10439961
	'''
	
	# Make histogram of data
	bounds = [[np.min(x)-1.e-10, np.max(x)+1.e-10],
	          [np.min(y)-1.e-10, np.max(y)+1.e-10]]
	if binsize != None:
		nbins = []
		if len(binsize) != 2:
			raise Exception('binsize must have size 2. Size is %d.' % len(binsize))
		for i in range(2):
			nbins.append((bounds[i][1] - bounds[i][0])/float(binsize[i]))
	h, loc_x, loc_y = scipy.histogram2d(x, y, range=bounds, bins=nbins)
	pos_x, pos_y = np.digitize(x, loc_x), np.digitize(y, loc_y)
	
	# Mask histogram points below threshold
	idx = (h[pos_x - 1, pos_y - 1] < threshold)
	h[h < threshold] = np.nan
	
	# Density plot
	img = ax.imshow(np.log(h.T), origin='lower', cmap=cmap,
	                             extent=np.array(bounds).flatten(),
	                             interpolation='nearest', aspect='auto')
	
	# Scatterplot
	ax.scatter(x[idx], y[idx], c=c, s=s, edgecolors='none')
	
	return img

def lnZ_plot(lnZ, EBV_samples):
	fig = plt.figure(figsize=(7,5), dpi=150)
	ax = fig.add_subplot(1,1,1)
	
	idx = np.isfinite(lnZ) & (lnZ > -1000.)
	
	lnZ_tmp = lnZ[idx]
	lnZ_tmp.shape = (lnZ_tmp.size, 1)
	lnZ_tmp = np.repeat(lnZ_tmp, EBV_samples.shape[1], axis=1)
	lnZ_tmp = lnZ_tmp.flatten()
	
	EBV_samples_flat = EBV_samples[idx].flatten()
	density_scatter(ax, EBV_samples_flat, lnZ_tmp)
	
	return fig
	

def tests():
	inFName = '../input/SEGUEsmall2.00005.h5'
	outFName = '../output/SEGUEsmall2.00005.h5'
	
	print 'Loading probability surfaces...'
	surfs, EBVsamples, good, minEBV, maxEBV, pixIdx1 = get1DProbSurfs(outFName)
	print 'Loading SEGUE properties...'
	props, pixIdx2 = getSEGUE(inFName)
	print 'Calculating E(B-V)...'
	SegueEBVs, SegueSigmaEBVs = getSegueEBV(props)
	
	print '# of pixels: %d' % (len(surfs))
	
	#print 'Loading 2D probability surfaces...'
	#surfs2D, minEBV, maxEBV, pixIdx3 = get2DProbSurfs(outFName)
	#plot2DSurfs(surfs2D[0], surfs[0], SegueEBVs[0], SegueSigmaEBVs[0], minEBV, maxEBV)
	#plt.show()
	
	print 'Calculating percentiles...'
	pctiles = []
	overlaps = []
	pvals = []
	for surf, SegueEBV, sigmaEBV, mask in zip(surfs, SegueEBVs, SegueSigmaEBVs, good):
		surf = surf[mask]
		SegueEBV = SegueEBV[mask]
		sigmaEBV = sigmaEBV[mask]
		
		#np.random.shuffle(SegueEBV)
		overlaps.append(multiply1DSurfs(surf, SegueEBV, sigmaEBV, minEBV, maxEBV))
		pvals.append(pval1DSurfs(surf, SegueEBV, sigmaEBV, minEBV, maxEBV))
		
		for i in range(1):
			norm = 0.#np.random.normal(size=len(SegueEBV))
			EBV = SegueEBV + sigmaEBV * norm
			pctiles.append(percentile(surf, EBV, minEBV, maxEBV))
	
	pctiles = np.hstack(pctiles)
	overlaps = np.hstack(overlaps)
	idx = ~np.isnan(pctiles)
	pctiles = pctiles[idx]
	idx = ~np.isnan(overlaps)
	overlaps = overlaps[idx]
	pvals = np.hstack(pvals)
	
	print 'Plotting percentiles...'
	plotPercentiles(pctiles)
	plotPercentiles(overlaps)
	plotPercentiles(pvals)
	
	print 'Plotting 1D surfaces...'
	for i in xrange(300,310):
		#multiply1DSurfs(surfs[i], SegueEBVs[i], SegueSigmaEBVs[i], minEBV, maxEBV)
		plot1DSurfs(surfs[i], good[i], SegueEBVs[i], SegueSigmaEBVs[i],
		                                                 minEBV, maxEBV)
	
	plt.show()

def main():
	directory = '/n/wise/ggreen/bayestar'
	inFNames = ['%s/input/SEGUEcovar.%.5d.h5' % (directory, i) for i in xrange(10)]
	outFNames = ['%s/output/SEGUEpaper2.%.5d.h5' % (directory, i) for i in xrange(10)]
	bands = 5
	max_SEGUE_sigma_EBV = 0.15
	
	surfs, EBV_samples, Mr_samples, SegueEBVs, SegueSigmaEBVs, EBV_SFD = [], [], [], [], [], []
	lnZ = []
	minEBV, maxEBV = None, None
	
	for inFName, outFName in zip(inFNames, outFNames):
		print inFName
		print outFName
		print 'Loading probability surfaces...'
		surfs_tmp, EBV_samples_tmp, Mr_samples_tmp, good_tmp, lnZ_tmp, minEBV, maxEBV, pixIdx1 = get1DProbSurfs(outFName)
		print 'Loading SEGUE properties...'
		props, pixIdx2 = getSEGUE(inFName)
		print 'Loading PS1 photometry...'
		mags, errs, EBV_SFD_tmp, pixIdx3 = getPhotometry(inFName)
		print 'Calculating E(B-V)...'
		SegueEBVs_tmp, SegueSigmaEBVs_tmp = getSegueEBV(props, bands=bands)
		
		print 'Combining pixels...'
		surfs_tmp = np.vstack(surfs_tmp)
		EBV_samples_tmp = np.vstack(EBV_samples_tmp)
		Mr_samples_tmp = np.vstack(Mr_samples_tmp)
		good_tmp = np.hstack(good_tmp)
		SegueEBVs_tmp = np.hstack(SegueEBVs_tmp)
		SegueSigmaEBVs_tmp = np.hstack(SegueSigmaEBVs_tmp)
		lnZ_tmp = np.hstack(lnZ_tmp)
		mags = np.vstack(mags)
		EBV_SFD_tmp = np.hstack(EBV_SFD_tmp)
		
		nanMask = np.isfinite(SegueEBVs_tmp)
		nanSigmaMask = np.isfinite(SegueSigmaEBVs_tmp)
		brightMask = np.all(mags > 14., axis=1)
		sigma_lowpass_mask = (SegueSigmaEBVs_tmp < max_SEGUE_sigma_EBV)
		print 'Filtered out %d stars based on NaN E(B-V).' % (nanMask.size - np.sum(nanMask))
		print 'Filtered out %d stars based on NaN sigma E(B-V).' % (nanSigmaMask.size - np.sum(nanSigmaMask))
		print 'Filtered out %d stars based on high sigma E(B-V).' % (sigma_lowpass_mask.size - np.sum(sigma_lowpass_mask))
		print 'Filtered out %d stars based on brightness.' % (brightMask.size - np.sum(brightMask))
		good_tmp &= nanMask & nanSigmaMask & sigma_lowpass_mask & brightMask
		
		surfs.append(surfs_tmp[good_tmp])
		EBV_samples.append(EBV_samples_tmp[good_tmp])
		Mr_samples.append(Mr_samples_tmp[good_tmp])
		SegueEBVs.append(SegueEBVs_tmp[good_tmp])
		SegueSigmaEBVs.append(SegueSigmaEBVs_tmp[good_tmp])
		lnZ.append(lnZ_tmp[good_tmp])
		EBV_SFD.append(EBV_SFD_tmp[good_tmp])
	
	print 'Combining stars from different files...'
	surfs = np.vstack(surfs)
	EBV_samples = np.vstack(EBV_samples)
	Mr_samples = np.vstack(Mr_samples)
	SegueEBVs = np.hstack(SegueEBVs)
	SegueSigmaEBVs = np.hstack(SegueSigmaEBVs)
	lnZ = np.hstack(lnZ)
	EBV_SFD = np.hstack(EBV_SFD)
	
	print '# of stars: %d' % (lnZ.size)
	print 'SEGUE:'
	print '  E(B-V) = %.3f +- %.3f' % (np.mean(SegueEBVs), np.std(SegueEBVs))
	print '           (%.3f, %.3f, %.3f)' % (np.percentile(SegueEBVs, 5.),
	                                         np.percentile(SegueEBVs, 50.),
	                                         np.percentile(SegueEBVs, 95.))
	
	print 'Plotting ln(Z) vs. E(B-V)...'
	fig6 = lnZ_plot(lnZ, EBV_samples)
	
	print 'Making scatterplots...'
	fig11, fig12, fig13, fig14, fig15 = plotScatter(surfs, EBV_samples, SegueEBVs, SegueSigmaEBVs,
	                                                Mr_samples, minEBV, maxEBV, EBV_SFD, method='max')
	print '  E(B-V) = %.3f +- %.3f' % (np.mean(SegueEBVs), np.std(SegueEBVs))
	fig21, fig22, fig23, fig24, fig25 = plotScatter(surfs, EBV_samples, SegueEBVs, SegueSigmaEBVs,
	                                                Mr_samples, minEBV, maxEBV, EBV_SFD, method='mean')
	fig31, fig32, fig33, fig34, fig35 = plotScatter(surfs, EBV_samples, SegueEBVs, SegueSigmaEBVs,
	                                                Mr_samples, minEBV, maxEBV, EBV_SFD, method='resample',
	                                                filt_giant=False)
	fig41, fig42, fig43, fig44, fig45 = plotScatter(surfs, EBV_samples, SegueEBVs, SegueSigmaEBVs,
	                                                Mr_samples, minEBV, maxEBV, EBV_SFD, method='resample',
	                                                filt_giant=True)
	
	'''
	print 'Calculating p-values...'
	pvals = pval1DSurfs(surfs, SegueEBVs, SegueSigmaEBVs, minEBV, maxEBV)
	idx = np.arange(len(SegueEBVs))
	np.random.shuffle(idx)
	SegueEBVs = SegueEBVs[idx]
	SegueSigmaEBVs = SegueSigmaEBVs[idx]
	pvals_shuffled = pval1DSurfs(surfs, SegueEBVs, SegueSigmaEBVs, minEBV, maxEBV)
	
	print 'Plotting percentiles...'
	fig4 = plotPercentiles(pvals)
	fig5 = plotPercentiles(pvals_shuffled)
	'''
	
	print 'Saving plots...'
	name = 'SEGUEpaper2'
	fig11.savefig('plots/SEGUE/%s-corr-maxprob.png' % name, dpi=300)
	fig12.savefig('plots/SEGUE/%s-scatter-maxprob.png' % name, dpi=300)
	fig13.savefig('plots/SEGUE/%s-SFDcorr-maxprob.png' % name, dpi=300)
	fig14.savefig('plots/SEGUE/%s-hist-maxprob.png' % name, dpi=300)
	fig15.savefig('plots/SEGUE/%s-chi-maxprob.png' % name, dpi=300)
	fig21.savefig('plots/SEGUE/%s-corr-mean.png' % name, dpi=300)
	fig22.savefig('plots/SEGUE/%s-scatter-mean.png' % name, dpi=300)
	fig23.savefig('plots/SEGUE/%s-SFDcorr-mean.png' % name, dpi=300)
	fig24.savefig('plots/SEGUE/%s-hist-mean.png' % name, dpi=300)
	fig25.savefig('plots/SEGUE/%s-chi-mean.png' % name, dpi=300)
	fig31.savefig('plots/SEGUE/%s-corr-resample.png' % name, dpi=300)
	fig32.savefig('plots/SEGUE/%s-scatter-resample.png' % name, dpi=300)
	fig33.savefig('plots/SEGUE/%s-SFDcorr-resample.png' % name, dpi=300)
	fig34.savefig('plots/SEGUE/%s-hist-resample.png' % name, dpi=300)
	fig35.savefig('plots/SEGUE/%s-chi-resample.png' % name, dpi=300)
	fig41.savefig('plots/SEGUE/%s-corr-resample-nodwarf.png' % name, dpi=300)
	fig42.savefig('plots/SEGUE/%s-scatter-resample-nodwarf.png' % name, dpi=300)
	fig43.savefig('plots/SEGUE/%s-SFDcorr-resample-nodwarf.png' % name, dpi=300)
	fig44.savefig('plots/SEGUE/%s-hist-resample-nodwarf.png' % name, dpi=300)
	fig45.savefig('plots/SEGUE/%s-chi-resample-nodwarf.png' % name, dpi=300)
	#fig4.savefig('plots/SEGUE/%s-pvals.png' % name, dpi=300)
	#fig5.savefig('plots/SEGUE/%s-pvals-shuffled.png' % name, dpi=300)
	fig6.savefig('plots/SEGUE/%s-lnZ-vs-EBV.png' % name, dpi=300)
	
	#plt.show()
	
	return 0

if __name__ == '__main__':
	main()

