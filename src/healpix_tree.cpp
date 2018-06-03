/*
 * healpix_tree.cpp
 * 
 * Traverse a tree structure representing a nested HEALPix map,
 * stored in an HDF5 file by nested groups.
 * 
 * This file is part of bayestar.
 * Copyright 2018 Gregory Green
 * 
 * Bayestar is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 */

#include "healpix_tree.h"


void healpix_loc2digits(
    uint32_t nside,
    uint32_t pix_idx,
    std::vector<uint8_t>& digits)
{
    // Converts a healpix (nside, index) specification
    // to a set of digits representing the location of
    // the pixel in the nested scheme.
    //
    // For example, the digits
    //   (10, 0, 1, 3, 1)
    // corresponds to selecting the 10th (of 12)
    // top-level pixels, then the 0th (of 4) nested
    // pixel, then the 1st (of 4) nested pixel, etc.
    // The above digits also correspond to nside = 16,
    // and
    //   pix_idx = 1 + 4*3 + 4^2*1 + 4^3*0 + 4^4*10

    // Take log_2(nside) + 1
    uint32_t n_levels = 1;
    while(nside >>= 1) {
        n_levels++;
    }
    
    // Read off the digits, from last to first
    digits.resize(n_levels);
    uint32_t d;
    
    for(int i=0; i<n_levels-1; i++) {
        d = pix_idx % 4;
        digits[n_levels-i-1] = d;
        pix_idx = (pix_idx - d) / 4;
    }
    
    d = pix_idx % 12;
    digits[0] = d;
}


std::unique_ptr<H5::DataSet> healtree_get_dataset(
    H5::H5File& file,
    uint32_t nside,
    uint32_t pix_idx)
{
    // Returns the dataset containing the requested
    // pixel, described by (nside, pix_idx). The
    // file is assumed to contain nested groups,
    // representing a nested tree structure that
    // mirrors a HEALPix map (a "HEALTree").
    //
    // For example, the pixel described by the digits
    // (9, 1, 0, 3, 3, 2, 0) might be contained in
    // the dataset "/9/1/0/3/3".

    // Convert location to digits
    std::vector<uint8_t> digits;
    healpix_loc2digits(nside, pix_idx, digits);
    
    // Find deepest group level present in file
    std::stringstream g;
    //for(; depth<digits.size()-1; depth++) {
    for(uint8_t d : digits) {
        g << "/" << d;
        if(!H5Utils::group_exists(g.str(), file)) {
            break;
        }
    }
    
    // Load dataset
    return H5Utils::openDataSet(file, g.str());
}
