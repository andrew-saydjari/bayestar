/*
 * los_sampler.cpp
 *
 * Samples from posterior distribution of line-of-sight extinction
 * model, given a set of stellar posterior densities in DM, E(B-V).
 *
 * This file is part of bayestar.
 * Copyright 2012 Gregory Green
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

#include "los_sampler.h"


/*
 *  Test l.o.s. fits
 */

void test_extinction_profiles(TLOSMCMCParams &params) {
    bool exit = false;

    while(!exit) {
        std::string response;
        std::string yn;

        std::cout << std::endl << "Cloud ('c') or Piecewise-linear ('p') model ('-' to exit)? ";
        std::cin >> response;

        if(response == "c") {
            double dist, depth;
            std::cout << "Cloud distance (DM): ";
            std::cin >> dist;
            std::cout << "Cloud depth (mags): ";
            std::cin >> depth;

            double x[2];
            x[0] = dist;
            x[1] = log(depth);

            double lnp = lnp_los_extinction_clouds(&(x[0]), 2, params);

            std::cout << "ln(p) = " << lnp << std::endl;

            std::cout << "Show more information (y/n)? ";
            std::cin >> yn;

            if(yn == "y") {
                // Compute line integrals through probability surfaces
                double *line_int = params.get_line_int(0);
                los_integral_clouds(*(params.img_stack), params.subpixel.data(), line_int, &(x[0]), &(x[1]), 1);


                std::cout << "  #   ln(p)  p_0/Z" << std::endl;

                double lnp_soft;
                double ln_L = 0.;

                for(size_t i=0; i<params.img_stack->N_images; i++) {
                    if(line_int[i] > params.p0_over_Z[i]) {
                        lnp_soft = log(line_int[i]) + log(1. + params.p0_over_Z[i] / line_int[i]);
                    } else {
                        lnp_soft = params.ln_p0_over_Z[i] + log(1. + line_int[i] * params.inv_p0_over_Z[i]);
                    }

                    ln_L += lnp_soft;

                    std::cout << "  " << i << ": " << log(line_int[i]) << "  " << params.ln_p0_over_Z[i] << "  " << lnp_soft << std::endl;
                }

                std::cout << std::endl;
                std::cout << "ln(L) = " << ln_L << std::endl;
                std::cout << "ln(prior) = " << lnp - ln_L << std::endl;
            }

        } else if(response == "p") {
            std::cout << "Not yet implemented.";

        } else if(response == "-") {
            exit = true;

        } else {
            std::cout << "Invalid option: '" << response << "'" << std::endl;
        }
    }
}



/*
 *  Discrete cloud model
 */

void sample_los_extinction_clouds(const std::string& out_fname, const std::string& group_name,
                                  TMCMCOptions &options, TLOSMCMCParams &params,
                                  unsigned int N_clouds, int verbosity) {
    timespec t_start, t_write, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    /*double x[] = {8., 4., -0.693, -1.61};
    gsl_rng *r;
    seed_gsl_rng(&r);
    //gen_rand_los_extinction_clouds(&(x[0]), 4, r, params);
    double lnp_tmp = lnp_los_extinction_clouds(&(x[0]), 4, params);
    std::cout << lnp_tmp << std::endl;
    gsl_rng_free(r);*/

    if(verbosity >= 2) {
        std::cout << "subpixel: " << std::endl;
        for(size_t i=0; i<params.subpixel.size(); i++) {
            std::cout << " " << params.subpixel[i];
        }
        std::cout << std::endl;
    }

    TNullLogger logger;

    unsigned int max_attempts = 2;
    unsigned int N_steps = options.steps;
    unsigned int N_samplers = options.samplers;
    unsigned int N_runs = options.N_runs;
    unsigned int ndim = 2 * N_clouds;

    std::vector<double> GR_transf;
    TLOSCloudTransform transf(ndim);
    double GR_threshold = 1.25;

    TAffineSampler<TLOSMCMCParams, TNullLogger>::pdf_t f_pdf = &lnp_los_extinction_clouds;
    TAffineSampler<TLOSMCMCParams, TNullLogger>::rand_state_t f_rand_state = &gen_rand_los_extinction_clouds;

    if(verbosity >= 1) {
        std::cout << std::endl;
        std::cout << "Discrete cloud l.o.s. model" << std::endl;
        std::cout << "====================================" << std::endl;
    }

    //std::cerr << "# Setting up sampler" << std::endl;
    TParallelAffineSampler<TLOSMCMCParams, TNullLogger> sampler(f_pdf, f_rand_state, ndim, N_samplers*ndim, params, logger, N_runs);
    sampler.set_sigma_min(1.e-5);
    sampler.set_scale(2.);
    sampler.set_replacement_bandwidth(0.35);

    // Burn-in
    if(verbosity >= 1) {
        std::cout << "# Burn-in ..." << std::endl;
    }
    sampler.step(int(N_steps*25./100.), false, 0., 0.);
    sampler.step(int(N_steps*20./100.), false, 0., options.p_replacement);
    sampler.step(int(N_steps*20./100.), false, 0., 0.85, 0.);
    sampler.step(int(N_steps*20./100.), false, 0., options.p_replacement);
    sampler.tune_stretch(5, 0.40);
    sampler.step(int(N_steps*20./100.), false, 0., 0.85);
    if(verbosity >= 2) { sampler.print_stats(); }
    sampler.clear();

    // Main sampling phase
    if(verbosity >= 1) {
        std::cout << "# Main run ..." << std::endl;
    }
    bool converged = false;
    size_t attempt;
    for(attempt = 0; (attempt < max_attempts) && (!converged); attempt++) {
        if(verbosity >= 2) {
            std::cout << std::endl;
            std::cout << "scale: (";
            std::cout << std::setprecision(2);
            for(int k=0; k<sampler.get_N_samplers(); k++) {
                std::cout << sampler.get_sampler(k)->get_scale() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
            }
        }
        sampler.tune_stretch(8, 0.40);
        if(verbosity >= 2) {
            std::cout << ") -> (";
            for(int k=0; k<sampler.get_N_samplers(); k++) {
                std::cout << sampler.get_sampler(k)->get_scale() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
            }
            std::cout << ")" << std::endl;
        }

        sampler.step((1<<attempt)*N_steps, true, 0., options.p_replacement);

        sampler.calc_GR_transformed(GR_transf, &transf);

        if(verbosity >= 2) {
            std::cout << std::endl << "Transformed G-R Diagnostic:";
            for(unsigned int k=0; k<ndim; k++) {
                std::cout << "  " << std::setprecision(3) << GR_transf[k];
            }
            std::cout << std::endl << std::endl;
        }

        converged = true;
        for(size_t i=0; i<ndim; i++) {
            if(GR_transf[i] > GR_threshold) {
                converged = false;
                if(attempt != max_attempts-1) {
                    if(verbosity >= 2) {
                        sampler.print_stats();
                    }

                    if(verbosity >= 1) {
                        std::cerr << "# Extending run ..." << std::endl;
                    }

                    sampler.step(int(N_steps*1./5.), false, 0., 1.);
                    sampler.clear();
                    //logger.clear();
                }
                break;
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t_write);

    //std::stringstream group_name;
    //group_name << "/pixel " << healpix_index;
    //group_name << "/los clouds";
    //chain.save(out_fname, group_name.str(), 0, "Delta mu, Delta E(B-V)", 3, 100, converged);

    std::stringstream group_name_full;
    group_name_full << "/" << group_name;
    TChain chain = sampler.get_chain();

    TChainWriteBuffer writeBuffer(ndim, 100, 1);
    writeBuffer.add(chain, converged, std::numeric_limits<double>::quiet_NaN(), GR_transf.data());
    writeBuffer.write(out_fname, group_name_full.str(), "clouds");

    clock_gettime(CLOCK_MONOTONIC, &t_end);

    if(verbosity >= 2) { sampler.print_stats(); }

    if(verbosity >= 1) {
        std::cout << std::endl;

        if(!converged) {
            std::cout << "# Failed to converge." << std::endl;
        }

        std::cout << "# Number of steps: " << (1<<(attempt-1))*N_steps << std::endl;
        std::cout << "# Time elapsed: " << std::setprecision(2) << (t_end.tv_sec - t_start.tv_sec) + 1.e-9*(t_end.tv_nsec - t_start.tv_nsec) << " s" << std::endl;
        std::cout << "# Sample time: " << std::setprecision(2) << (t_write.tv_sec - t_start.tv_sec) + 1.e-9*(t_write.tv_nsec - t_start.tv_nsec) << " s" << std::endl;
        std::cout << "# Write time: " << std::setprecision(2) << (t_end.tv_sec - t_write.tv_sec) + 1.e-9*(t_end.tv_nsec - t_write.tv_nsec) << " s" << std::endl << std::endl;
    }
}

void los_integral_clouds(
        TImgStack &img_stack,
        const double *const subpixel,
        double *const ret,
        const double *const Delta_mu,
        const double *const logDelta_EBV,
        unsigned int N_clouds)
{
    int x = 0;
    int x_next = ceil((Delta_mu[0] - img_stack.rect->min[1]) / img_stack.rect->dx[1]);

    floating_t y_0 = -img_stack.rect->min[0] / img_stack.rect->dx[0];
    floating_t y = 0.;
    int y_max = img_stack.rect->N_bins[0];
    floating_t y_ceil, y_floor, dy, y_scaled;
    int y_ceil_int, y_floor_int;

    for(size_t i=0; i<img_stack.N_images; i++) { ret[i] = 0.; }

    for(int i=0; i<N_clouds+1; i++) {
        if(i == N_clouds) {
            x_next = img_stack.rect->N_bins[1];
        } else if(i != 0) {
            x_next += ceil(Delta_mu[i] / img_stack.rect->dx[1]);
        }

        if(x_next > img_stack.rect->N_bins[1]) {
            x_next = img_stack.rect->N_bins[1];
        } else if(x_next < 0) {
            x_next = 0;
        }

        if(i != 0) {
            y += exp(logDelta_EBV[i-1]) / img_stack.rect->dx[0];
        }

        int x_start = x;
        for(int k=0; k<img_stack.N_images; k++) {
            y_scaled = y_0 + y*subpixel[k];
            y_floor = floor(y_scaled);
            y_ceil = y_floor + 1.;
            y_floor_int = (int)y_floor;
            y_ceil_int = (int)y_ceil;

            //if(y_ceil_int >= y_max) { std::cout << "!! y_ceil_int >= y_max !!" << std::endl; break; }
            //if(y_floor_int < 0) { std::cout << "!! y_floor_int < 0 !!" << std::endl; break; }

            for(x = x_start; x<x_next; x++) {
                ret[k] += (y_ceil - y_scaled) * img_stack.img[k]->at<floating_t>(y_floor_int, x)
                          + (y_scaled - y_floor) * img_stack.img[k]->at<floating_t>(y_ceil_int, x);
            }
        }
    }
}

double lnp_los_extinction_clouds(const double* x, unsigned int N, TLOSMCMCParams& params) {
    int thread_num = omp_get_thread_num();

    const size_t N_clouds = N / 2;
    const double *Delta_mu = x;
    const double *logDelta_EBV = x + N_clouds;

    double lnp = 0.;

    // Delta_mu must be positive
    double mu_tot = 0.;
    for(size_t i=0; i<N_clouds; i++) {
        if(Delta_mu[i] <= 0.) { return neg_inf_replacement; }
        mu_tot += Delta_mu[i];
    }

    // Don't consider clouds outside of the domain under consideration
    if(Delta_mu[0] < params.img_stack->rect->min[1]) { return neg_inf_replacement; }
    //if(mu_tot >= params.img_stack->rect->max[1]) { return neg_inf_replacement; }
    int mu_tot_idx = ceil((mu_tot * params.subpixel_max - params.img_stack->rect->min[1]) / params.img_stack->rect->dx[1]);
    if(mu_tot_idx + 1 >= params.img_stack->rect->N_bins[1]) { return neg_inf_replacement; }

    const double bias = -5.;
    const double sigma = 5.;

    double EBV_tot = 0.;
    double tmp;
    for(size_t i=0; i<N_clouds; i++) {
        tmp = exp(logDelta_EBV[i]);
        EBV_tot += tmp;

        // Prior to prevent EBV from straying high
        lnp -= 0.5 * tmp * tmp / (2. * 2.);

        // Wide Gaussian prior on Delta_EBV to prevent fit from straying drastically
        lnp -= (logDelta_EBV[i] - bias) * (logDelta_EBV[i] - bias) / (2. * sigma * sigma);
    }

    // Extinction must not exceed maximum value
    //if(EBV_tot * params.subpixel_max >= params.img_stack->rect->max[0]) { return neg_inf_replacement; }
    double EBV_tot_idx = ceil((EBV_tot * params.subpixel_max - params.img_stack->rect->min[0]) / params.img_stack->rect->dx[0]);
    if(EBV_tot_idx + 1 >= params.img_stack->rect->N_bins[0]) { return neg_inf_replacement; }

    // Prior on total extinction
    if((params.EBV_max > 0.) && (EBV_tot > params.EBV_max)) {
        lnp -= (EBV_tot - params.EBV_max) * (EBV_tot - params.EBV_max) / (2. * 0.20 * 0.20 * params.EBV_max * params.EBV_max);
    }

    // Repulsive force to keep clouds from collapsing into one
    for(size_t i=1; i<N_clouds; i++) {
        lnp -= 1. / Delta_mu[i];
    }

    // Compute line integrals through probability surfaces
    double *line_int = params.get_line_int(thread_num);
    los_integral_clouds(*(params.img_stack), params.subpixel.data(), line_int, Delta_mu, logDelta_EBV, N_clouds);

    // Soften and multiply line integrals
    double lnp_indiv;
    for(size_t i=0; i<params.img_stack->N_images; i++) {
        /*if(line_int[i] < 1.e5*params.p0) {
            line_int[i] += params.p0 * exp(-line_int[i]/params.p0);
        }
        lnp += log(line_int[i]);*/
        if(line_int[i] > params.p0_over_Z[i]) {
            lnp_indiv = log(line_int[i]) + log(1. + params.p0_over_Z[i] / line_int[i]);
        } else {
            lnp_indiv = params.ln_p0_over_Z[i] + log(1. + line_int[i] * params.inv_p0_over_Z[i]);
        }

        lnp += lnp_indiv;
    }

    return lnp;
}

void gen_rand_los_extinction_clouds(double *const x, unsigned int N, gsl_rng *r, TLOSMCMCParams &params) {
    double mu_floor = params.img_stack->rect->min[1];
    double mu_ceil = params.img_stack->rect->max[1];
    double EBV_ceil = params.img_stack->rect->max[0] / params.subpixel_max;
    unsigned int N_clouds = N / 2;

    double logEBV_mean = log(1.5 * params.EBV_guess_max / params.subpixel_max / (double)N_clouds);
    double mu_mean = (mu_ceil - mu_floor) / N_clouds;
    double EBV_sum = 0.;
    double mu_sum = mu_floor;

    double *Delta_mu = x;
    double *logDelta_EBV = x + N_clouds;

    double log_mu_mean = log(0.5 * mu_mean);
    for(size_t i=0; i<N_clouds; i++) {
        logDelta_EBV[i] = logEBV_mean + gsl_ran_gaussian_ziggurat(r, 1.5);
        EBV_sum += exp(logDelta_EBV[i]);

        Delta_mu[i] = exp(log_mu_mean + gsl_ran_gaussian_ziggurat(r, 1.5));
        mu_sum += Delta_mu[i];
    }
    Delta_mu[0] += mu_floor;

    // Ensure that reddening is not more than allowed
    if(EBV_sum >= 0.95 * EBV_ceil) {
        double factor = log(0.95 * EBV_ceil / EBV_sum);
        for(size_t i=0; i<N_clouds; i++) {
            logDelta_EBV[i] += factor;
        }
    }

    // Ensure that distance to farthest cloud is not more than allowed
    if(mu_sum >= 0.95 * mu_ceil) {
        double factor = 0.95 * mu_ceil / mu_sum;
        for(size_t i=0; i<N_clouds; i++) {
            Delta_mu[i] *= factor;
        }
    }
}



/*
 *  Piecewise-linear line-of-sight model
 */

void sample_los_extinction(const std::string& out_fname, const std::string& group_name,
                           TMCMCOptions &options, TLOSMCMCParams &params,
                           int verbosity) {
    timespec t_start, t_write, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    if(verbosity >= 1) {
        //std::cout << std::endl;
        std::cout << "Piecewise-linear l.o.s. model" << std::endl;
        std::cout << "====================================" << std::endl;
    }

    if(verbosity >= 2) {
        std::cout << "guess of EBV max = " << params.EBV_guess_max << std::endl;
    }

    if(verbosity >= 1) {
        std::cout << "# Generating Guess ..." << std::endl;
    }

    //std::vector<double> guess_time;

    /*for(int i=0; i<50; i++) {
        timespec t_0, t_1;
        double t_tmp;

        clock_gettime(CLOCK_MONOTONIC, &t_0);

        guess_EBV_profile(options, params);

        clock_gettime(CLOCK_MONOTONIC, &t_1);

        t_tmp = (t_1.tv_sec - t_0.tv_sec) + 1.e-9 * (t_1.tv_nsec - t_0.tv_nsec);
        //guess_time.push_back(t_tmp);

        std::cerr << "Guess " << i << ": " << t_tmp << " s" << std::endl;
    }*/

    guess_EBV_profile(options, params, verbosity);


    //monotonic_guess(img_stack, N_regions, params.EBV_prof_guess, options);
    if(verbosity >= 2) {
        for(size_t i=0; i<params.EBV_prof_guess.size(); i++) {
            std::cout << "\t" << params.EBV_prof_guess[i] << std::endl;
        }
        std::cout << std::endl;
    }

    TNullLogger logger;

    unsigned int max_attempts = 2;
    unsigned int N_steps = options.steps;
    unsigned int N_samplers = options.samplers;
    unsigned int N_runs = options.N_runs;
    unsigned int ndim = params.N_regions + 1;

    double max_conv_mu = 15.;
    double DM_max = params.img_stack->rect->max[1];
    double DM_min = params.img_stack->rect->min[1];
    double Delta_DM = (DM_max - DM_min) / (double)(params.N_regions);
    unsigned int max_conv_idx = ceil((max_conv_mu - DM_min) / Delta_DM);
    //std::cout << "max_conv_idx = " << max_conv_idx << std::endl;

    std::vector<double> GR_transf;
    TLOSTransform transf(ndim);
    double GR_threshold = 1.25;

    TAffineSampler<TLOSMCMCParams, TNullLogger>::pdf_t f_pdf = &lnp_los_extinction;
    TAffineSampler<TLOSMCMCParams, TNullLogger>::rand_state_t f_rand_state = &gen_rand_los_extinction_from_guess;
    TAffineSampler<TLOSMCMCParams, TNullLogger>::reversible_step_t switch_step = &switch_adjacent_log_Delta_EBVs;
    TAffineSampler<TLOSMCMCParams, TNullLogger>::reversible_step_t mix_step = &mix_log_Delta_EBVs;
    TAffineSampler<TLOSMCMCParams, TNullLogger>::reversible_step_t move_one_step = &step_one_Delta_EBV;

    TParallelAffineSampler<TLOSMCMCParams, TNullLogger> sampler(f_pdf, f_rand_state, ndim, N_samplers*ndim, params, logger, N_runs);

    // Burn-in
    if(verbosity >= 1) { std::cout << "# Burn-in ..." << std::endl; }

    // Round 1 (5/20)
    unsigned int base_N_steps = ceil((double)N_steps * 1./20.);

    sampler.set_sigma_min(1.e-5);
    sampler.set_scale(1.1);
    sampler.set_replacement_bandwidth(0.25);
    sampler.set_MH_bandwidth(0.15);

    sampler.tune_MH(8, 0.25);
    sampler.step_MH(base_N_steps, false);

    sampler.tune_MH(8, 0.25);
    sampler.step_MH(base_N_steps, false);

    if(verbosity >= 2) {
        std::cout << "scale: (";
        for(int k=0; k<sampler.get_N_samplers(); k++) {
            std::cout << sampler.get_sampler(k)->get_scale() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
        }
    }
    sampler.tune_stretch(5, 0.30);
    if(verbosity >= 2) {
        std::cout << ") -> (";
        for(int k=0; k<sampler.get_N_samplers(); k++) {
            std::cout << sampler.get_sampler(k)->get_scale() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
        }
        std::cout << ")" << std::endl;
    }

    sampler.step(2*base_N_steps, false, 0., options.p_replacement);
    sampler.step(base_N_steps, false, 0., 1., true, true);

    if(verbosity >= 2) {
        std::cout << "Round 1 diagnostics:" << std::endl;
        sampler.print_diagnostics();
        std::cout << std::endl;
    }

    // Round 2 (5/20)

    sampler.set_replacement_accept_bias(1.e-2);

    if(verbosity >= 2) {
        std::cout << "scale: (";
        for(int k=0; k<sampler.get_N_samplers(); k++) {
            std::cout << sampler.get_sampler(k)->get_scale() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
        }
    }
    sampler.tune_stretch(8, 0.30);
    if(verbosity >= 2) {
        std::cout << ") -> (";
        for(int k=0; k<sampler.get_N_samplers(); k++) {
            std::cout << sampler.get_sampler(k)->get_scale() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
        }
        std::cout << ")" << std::endl;
    }

    sampler.step(int(N_steps*2./20.), false, 0., options.p_replacement);

    sampler.step_custom_reversible(base_N_steps, switch_step, false);
    sampler.step_custom_reversible(base_N_steps, mix_step, false);
    sampler.step_custom_reversible(base_N_steps, move_one_step, false);

    //sampler.step(2*base_N_steps, false, 0., options.p_replacement);
    sampler.step(base_N_steps, false, 0., 1., true, true);

    if(verbosity >= 2) {
        std::cout << "Round 2 diagnostics:" << std::endl;
        sampler.print_diagnostics();
        std::cout << std::endl;
    }

    // Round 3 (5/20)

    if(verbosity >= 2) {
        std::cout << "scale: (";
        for(int k=0; k<sampler.get_N_samplers(); k++) {
            std::cout << sampler.get_sampler(k)->get_scale() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
        }
    }
    sampler.tune_stretch(8, 0.30);
    if(verbosity >= 2) {
        std::cout << ") -> (";
        for(int k=0; k<sampler.get_N_samplers(); k++) {
            std::cout << sampler.get_sampler(k)->get_scale() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
        }
        std::cout << ")" << std::endl;
    }

    //sampler.step_MH(int(N_steps*1./20.), false);
    sampler.step(2*base_N_steps, false, 0., options.p_replacement);

    sampler.step_custom_reversible(base_N_steps, switch_step, false);
    sampler.step_custom_reversible(base_N_steps, mix_step, false);
    sampler.step_custom_reversible(base_N_steps, move_one_step, false);

    if(verbosity >= 2) {
        std::cout << "Round 3 diagnostics:" << std::endl;
        sampler.print_diagnostics();
        std::cout << std::endl;
    }

    // Round 4 (5/20)
    sampler.set_replacement_accept_bias(0.);

    //sampler.tune_MH(8, 0.25);
    if(verbosity >= 2) {
        std::cout << "scale: (";
        for(int k=0; k<sampler.get_N_samplers(); k++) {
            std::cout << sampler.get_sampler(k)->get_scale() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
        }
    }
    sampler.tune_stretch(8, 0.30);
    if(verbosity >= 2) {
        std::cout << ") -> (";
        for(int k=0; k<sampler.get_N_samplers(); k++) {
            std::cout << sampler.get_sampler(k)->get_scale() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
        }
        std::cout << ")" << std::endl;
    }

    //sampler.step_MH(int(N_steps*2./15.), false);
    sampler.step(2*base_N_steps, false, 0., options.p_replacement);

    sampler.step_custom_reversible(2*base_N_steps, switch_step, false);
    //sampler.step_custom_reversible(base_N_steps, mix_step, false);
    sampler.step_custom_reversible(base_N_steps, move_one_step, false);

    if(verbosity >= 2) {
        std::cout << "Round 4 diagnostics:" << std::endl;
        sampler.print_diagnostics();
        std::cout << std::endl;
    }

    sampler.clear();

    // Main sampling phase (15/15)
    if(verbosity >= 1) { std::cout << "# Main run ..." << std::endl; }
    bool converged = false;
    size_t attempt;
    for(attempt = 0; (attempt < max_attempts) && (!converged); attempt++) {
        /*if(verbosity >= 2) {
            std::cout << std::endl;
            std::cout << "M-H bandwidth: (";
            std::cout << std::setprecision(3);
            for(int k=0; k<sampler.get_N_samplers(); k++) {
                std::cout << sampler.get_sampler(k)->get_MH_bandwidth() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
            }
        }
        sampler.tune_MH(10, 0.25);
        if(verbosity >= 2) {
            std::cout << ") -> (";
            for(int k=0; k<sampler.get_N_samplers(); k++) {
                std::cout << sampler.get_sampler(k)->get_MH_bandwidth() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
            }
            std::cout << ")" << std::endl;
        }*/

        if(verbosity >= 2) {
            std::cout << "scale: (";
            for(int k=0; k<sampler.get_N_samplers(); k++) {
                std::cout << sampler.get_sampler(k)->get_scale() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
            }
        }
        //sampler.tune_stretch(8, 0.30);
        if(verbosity >= 2) {
            std::cout << ") -> (";
            for(int k=0; k<sampler.get_N_samplers(); k++) {
                std::cout << sampler.get_sampler(k)->get_scale() << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
            }
            std::cout << ")" << std::endl;
        }

        base_N_steps = ceil((double)((1<<attempt)*N_steps)*1./15.);

        // Round 1 (5/15)
        sampler.step(2*base_N_steps, true, 0., options.p_replacement);
        sampler.step_custom_reversible(2*base_N_steps, switch_step, true);
        //sampler.step_custom_reversible(base_N_steps, mix_step, true);
        sampler.step_custom_reversible(base_N_steps, move_one_step, true);
        //sampler.step_MH((1<<attempt)*N_steps*1./12., true);

        // Round 2 (5/15)
        sampler.step(2*base_N_steps, true, 0., options.p_replacement);
        sampler.step_custom_reversible(2*base_N_steps, switch_step, true);
        //sampler.step_custom_reversible(base_N_steps, mix_step, true);
        sampler.step_custom_reversible(base_N_steps, move_one_step, true);
        //sampler.step_MH((1<<attempt)*N_steps*1./12., true);

        // Round 3 (5/15)
        sampler.step(2*base_N_steps, true, 0., options.p_replacement);
        sampler.step_custom_reversible(2*base_N_steps, switch_step, true);
        //sampler.step_custom_reversible(base_N_steps, mix_step, true);
        sampler.step_custom_reversible(base_N_steps, move_one_step, true);
        //sampler.step_MH((1<<attempt)*N_steps*1./12., true);

        sampler.calc_GR_transformed(GR_transf, &transf);

        if(verbosity >= 2) {
            std::cout << std::endl << "Transformed G-R Diagnostic:";
            for(unsigned int k=0; k<ndim; k++) {
                std::cout << "  " << std::setprecision(3) << GR_transf[k];
            }
            std::cout << std::endl << std::endl;
        }

        converged = true;
        for(size_t i=0; i<max_conv_idx; i++) {
            if(GR_transf[i] > GR_threshold) {
                converged = false;
                if(attempt != max_attempts-1) {
                    if(verbosity >= 2) {
                        sampler.print_stats();
                    }

                    if(verbosity >= 1) {
                        std::cout << "# Extending run ..." << std::endl;
                    }

                    sampler.step(3*base_N_steps, false, 0., 1.);
                    sampler.step_custom_reversible(base_N_steps, switch_step, true);

                    sampler.clear();
                    //logger.clear();
                }
                break;
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t_write);

    std::stringstream group_name_full;
    group_name_full << "/" << group_name;
    TChain chain = sampler.get_chain();

    TChainWriteBuffer writeBuffer(ndim, 500, 1);
    writeBuffer.add(chain, converged, std::numeric_limits<double>::quiet_NaN(), GR_transf.data());
    writeBuffer.write(out_fname, group_name_full.str(), "los");

    std::stringstream los_group_name;
    los_group_name << group_name_full.str() << "/los";
    H5Utils::add_watermark<double>(out_fname, los_group_name.str(), "DM_min", params.img_stack->rect->min[1]);
    H5Utils::add_watermark<double>(out_fname, los_group_name.str(), "DM_max", params.img_stack->rect->max[1]);

    clock_gettime(CLOCK_MONOTONIC, &t_end);

    /*
    std::vector<double> best_dbl;
    std::vector<float> best;
    double * line_int_best = new double[params.img_stack->N_images];

    std::cout << "get_best" << std::endl;
    chain.get_best(best_dbl);

    std::cout << "exp" << std::endl;
    double tmp_tot = 0;
    for(int i=0; i<best_dbl.size(); i++) {
        best.push_back( exp(best_dbl[i]) );
        tmp_tot += best[i];
        std::cout << best[i] << "  " << tmp_tot << std::endl;
    }

    std::cout << "los_integral" << std::endl;
    los_integral(*(params.img_stack), params.subpixel.data(), line_int_best, best.data(), ndim-1);

    double lnp_soft;
    double ln_L = 0.;

    std::cout << std::endl;
    std::cout << "Line integrals:" << std::endl;
    for(size_t i=0; i<params.img_stack->N_images; i++) {
        if(line_int_best[i] > params.p0_over_Z[i]) {
            lnp_soft = log(line_int_best[i]) + log(1. + params.p0_over_Z[i] / line_int_best[i]);
        } else {
            lnp_soft = params.ln_p0_over_Z[i] + log(1. + line_int_best[i] * params.inv_p0_over_Z[i]);
        }

        ln_L += lnp_soft;

        std::cout << "  " << i << ": " << log(line_int_best[i]) << "  " << params.ln_p0_over_Z[i] << "  " << lnp_soft << std::endl;
    }

    std::cout << std::endl;
    std::cout << "ln(L) = " << ln_L << std::endl;
    std::cout << std::endl;

    delete[] line_int_best;
    */

    if(verbosity >= 2) { sampler.print_stats(); }

    if(verbosity >= 1) {
        std::cout << std::endl;

        if(!converged) {
            std::cout << "# Failed to converge." << std::endl;
        }

        std::cout << "# Number of steps: " << (1<<(attempt-1))*N_steps << std::endl;
        std::cout << "# Time elapsed: " << std::setprecision(2) << (t_end.tv_sec - t_start.tv_sec) + 1.e-9*(t_end.tv_nsec - t_start.tv_nsec) << " s" << std::endl;
        std::cout << "# Sample time: " << std::setprecision(2) << (t_write.tv_sec - t_start.tv_sec) + 1.e-9*(t_write.tv_nsec - t_start.tv_nsec) << " s" << std::endl;
        std::cout << "# Write time: " << std::setprecision(2) << (t_end.tv_sec - t_write.tv_sec) + 1.e-9*(t_end.tv_nsec - t_write.tv_nsec) << " s" << std::endl << std::endl;
    }
}

void los_integral(TImgStack &img_stack, const double *const subpixel, double *const ret,
                                        const float *const Delta_EBV, unsigned int N_regions) {
    assert(img_stack.rect->N_bins[1] % N_regions == 0);

    const int subsampling = 1;
    const int N_pix_per_bin = img_stack.rect->N_bins[1] / N_regions;
    const float N_samples = subsampling * N_pix_per_bin;
    const int y_max = img_stack.rect->N_bins[0];

    int x;

    float Delta_y_0 = Delta_EBV[0] / img_stack.rect->dx[0];
    const float y_0 = -img_stack.rect->min[0] / img_stack.rect->dx[0];
    float y, dy;

    // Integer arithmetic is the poor man's fixed-point math
    typedef uint32_t fixed_point_t;
    const int base_2_prec = 18; // unsigned Q14.18 format

    const fixed_point_t prec_factor_int = (1 << base_2_prec);
    const float prec_factor = (float)prec_factor_int;

    fixed_point_t y_int, dy_int;
    fixed_point_t y_ceil, y_floor;
    fixed_point_t diff;

    // Pre-computed multiplicative factors
    float dy_mult_factor = 1. / N_samples / img_stack.rect->dx[0];
    float ret_mult_factor = 1. / (float)subsampling / prec_factor;

    float tmp_ret, tmp_subpixel;
    cv::Mat *img;

    // For each image
    for(int k=0; k<img_stack.N_images; k++) {
        tmp_ret = 0.;
        img = img_stack.img[k];
        tmp_subpixel = subpixel[k];

        x = 0;
        y = y_0 + tmp_subpixel * Delta_y_0;
        y_int = (fixed_point_t)(prec_factor * y);

        for(int i=1; i<N_regions+1; i++) {
            // Determine y increment in region (slope)
            dy = tmp_subpixel * Delta_EBV[i] * dy_mult_factor;
            dy_int = (fixed_point_t)(prec_factor * dy);

            // For each DM pixel
            for(int j=0; j<N_pix_per_bin; j++, x++, y_int+=dy_int) {

                // Manual loop unrolling. It's ugly, but it works!

                // 0
                y_floor = (y_int >> base_2_prec);
                diff = y_int - (y_floor << base_2_prec);

                tmp_ret += (prec_factor_int - diff) * img->at<floating_t>(y_floor, x)
                        + diff * img->at<floating_t>(y_floor+1, x);

                /*
                // 1
                y_int += dy_int;
                y_floor = (y_int >> base_2_prec);
                diff = y_int - (y_floor << base_2_prec);

                tmp_ret += diff * img->at<floating_t>(y_floor, x)
                        + (prec_factor_int - diff) * img->at<floating_t>(y_floor+1, x);

                // 2
                y_int += dy_int;
                y_floor = (y_int >> base_2_prec);
                diff = y_int - (y_floor << base_2_prec);

                tmp_ret += diff * img->at<floating_t>(y_floor, x)
                        + (prec_factor_int - diff) * img->at<floating_t>(y_floor+1, x);
                */
            }
        }

        ret[k] = tmp_ret * ret_mult_factor;
    }
}

double lnp_los_extinction(const double *const logEBV, unsigned int N, TLOSMCMCParams& params) {
    double lnp = 0.;

    double EBV_tot = 0.;
    double EBV_tmp;
    double diff_scaled;

    int thread_num = omp_get_thread_num();

    // Calculate Delta E(B-V) from log(Delta E(B-V))
    float *Delta_EBV = params.get_Delta_EBV(thread_num);

    for(int i=0; i<N; i++) {
        Delta_EBV[i] = exp(logEBV[i]);
    }

    if(params.log_Delta_EBV_prior != NULL) {
        //const double sigma = 2.5;

        for(size_t i=0; i<N; i++) {
            EBV_tot += Delta_EBV[i];

            // Prior that reddening traces stellar disk
            diff_scaled = (logEBV[i] - params.log_Delta_EBV_prior[i]) / params.sigma_log_Delta_EBV[i];
            lnp -= 0.5 * diff_scaled * diff_scaled;
            lnp += log(1. + erf(params.alpha_skew * diff_scaled * INV_SQRT2));
        }
    } else {
        const double bias = -4.;
        const double sigma = 2.;

        for(size_t i=0; i<N; i++) {
            EBV_tot += Delta_EBV[i];

            // Wide Gaussian prior on logEBV to prevent fit from straying drastically
            lnp -= (logEBV[i] - bias) * (logEBV[i] - bias) / (2. * sigma * sigma);
        }
    }

    // Extinction must not exceed maximum value
    //if(EBV_tot * params.subpixel_max >= params.img_stack->rect->max[0]) { return neg_inf_replacement; }
    double EBV_tot_idx = ceil((EBV_tot * params.subpixel_max - params.img_stack->rect->min[0]) / params.img_stack->rect->dx[0]);
    if(EBV_tot_idx + 1 >= params.img_stack->rect->N_bins[0]) { return neg_inf_replacement; }

    // Prior on total extinction
    if((params.EBV_max > 0.) && (EBV_tot > params.EBV_max)) {
        lnp -= (EBV_tot - params.EBV_max) * (EBV_tot - params.EBV_max) / (2. * 0.20 * 0.20 * params.EBV_max * params.EBV_max);
    }

    // Compute line integrals through probability surfaces
    double *line_int = params.get_line_int(thread_num);
    los_integral(*(params.img_stack), params.subpixel.data(), line_int, Delta_EBV, N-1);

    // Soften and multiply line integrals
    double lnp_indiv;
    for(size_t i=0; i<params.img_stack->N_images; i++) {
        //if(line_int[i] < 1.e5*params.p0) {
        //  line_int[i] += params.p0 * exp(-line_int[i]/params.p0);
        //}
        if(line_int[i] > params.p0_over_Z[i]) {
            lnp_indiv = log(line_int[i]) + log(1. + params.p0_over_Z[i] / line_int[i]);
        } else {
            lnp_indiv = params.ln_p0_over_Z[i] + log(1. + line_int[i] * params.inv_p0_over_Z[i]);
        }

        lnp += lnp_indiv;

        /*#pragma omp critical (cout)
        {
        std::cerr << i << "(" << params.ln_p0_over_Z[i] <<"): " << log(line_int[i]) << " --> " << lnp_indiv << std::endl;
        }*/
    }

    return lnp;
}

void gen_rand_los_extinction(double *const logEBV, unsigned int N, gsl_rng *r, TLOSMCMCParams &params) {
    double EBV_ceil = params.img_stack->rect->max[0] / params.subpixel_max;
    double mu = 1.5 * params.EBV_guess_max / params.subpixel_max / (double)N;
    double EBV_sum = 0.;

    if((params.log_Delta_EBV_prior != NULL) && (gsl_rng_uniform(r) < 0.8)) {
        for(size_t i=0; i<N; i++) {
            logEBV[i] = params.log_Delta_EBV_prior[i] + gsl_ran_gaussian_ziggurat(r, params.sigma_log_Delta_EBV[i]);
            EBV_sum += exp(logEBV[i]);
        }
    } else {
        double log_scaling = gsl_ran_gaussian_ziggurat(r, 0.25);
        for(size_t i=0; i<N; i++) {
            logEBV[i] = log(mu * gsl_rng_uniform(r)) + log_scaling; //mu + gsl_ran_gaussian_ziggurat(r, 2.5);
            EBV_sum += exp(logEBV[i]);
        }
        /*#pragma omp critical (cout)
        {
        std::cerr << EBV_sum << ": ";
        for(size_t i=0; i<N; i++) {
            std::cerr << logEBV[i] << " ";
        }
        std::cerr << std::endl;
        }*/
    }

    // Add in cloud to bring total reddening up to guess value (with some scatter)
    if(gsl_rng_uniform(r) < 0.25) {
        const double sigma_tmp = 0.5;
        double EBV_target_tmp = params.EBV_guess_max * exp(gsl_ran_gaussian_ziggurat(r, sigma_tmp) - 0.5*sigma_tmp*sigma_tmp - 0.5);
        if(EBV_sum < EBV_target_tmp) {
            size_t k = gsl_rng_uniform_int(r, N);
            logEBV[k] = log(exp(logEBV[k]) + EBV_target_tmp - EBV_sum);
            /*#pragma omp critical (cout)
            {
            std::cerr << EBV_sum << ": " << EBV_target_tmp << ": ";
            for(size_t j=0; j<N; j++) {
                std::cerr << logEBV[j] << " ";
            }
            std::cerr << std::endl;
            }*/
            EBV_sum = EBV_target_tmp;
        }
    }

    // Ensure that reddening is not more than allowed
    if(EBV_sum >= 0.95 * EBV_ceil) {
        double factor = log(0.95 * EBV_ceil / EBV_sum);
        for(size_t i=0; i<N; i++) {
            logEBV[i] += factor;
        }
    }
}

// Guess upper limit for E(B-V) based on stacked probability surfaces
double guess_EBV_max(TImgStack &img_stack) {
    cv::Mat stack, col_avg;

    // Stack images
    img_stack.stack(stack);

    // Sum across each EBV
    cv::reduce(stack, col_avg, 1, cv::REDUCE_AVG);
    //float max_sum = *std::max_element(col_avg.begin<floating_t>(), col_avg.end<floating_t>());
    //std::cerr << "max_sum = " << max_sum << std::endl;

    double tot_weight = 0; //std::accumulate(col_avg.begin<floating_t>(), col_avg.end<floating_t>(), 0);

    for(int i = 0; i < col_avg.rows; i++) {
        tot_weight += col_avg.at<floating_t>(i, 0);
        //std::cerr << "col_avg.at<floating_t>(" << i << ", 0) = " << col_avg.at<floating_t>(i, 0) << std::endl;
    }

    //std::cerr << "tot_weight = " << tot_weight << std::endl;

    double partial_sum_weight = 0.;

    for(int i = 0; i < col_avg.rows; i++) {
        partial_sum_weight += col_avg.at<floating_t>(i, 0);
        if(partial_sum_weight > 0.90 * tot_weight) {
            // Return E(B-V) corresponding to bin index
            return (double)i * img_stack.rect->dx[0] + img_stack.rect->min[0];
            //std::cerr << "Passed 90% of weight at " << i << std::endl;
        }
    }

    return (col_avg.rows - 1) * img_stack.rect->dx[0] + img_stack.rect->min[0];

    //int max = 1;
    //for(int i = col_avg.rows - 1; i > 0; i--) {
    //    std::cerr << "  " << i << " : " << col_avg.at<floating_t>(i, 0) << std::endl;
    //  if(col_avg.at<floating_t>(i, 0) > 0.01 * max_sum) {
    //      max = i;
    //      break;
    //  }
    //}

    // Convert bin index to E(B-V)
    //return max * img_stack.rect->dx[0] + img_stack.rect->min[0];
}

void guess_EBV_profile(TMCMCOptions &options, TLOSMCMCParams &params, int verbosity) {
    TNullLogger logger;

    unsigned int N_steps = options.steps / 8;
    unsigned int N_samplers = options.samplers;
    //if(N_samplers < 10) { N_samplers = 10; }
    unsigned int N_runs = options.N_runs;
    unsigned int ndim = params.N_regions + 1;

    if(N_steps < 50) { N_steps = 50; }
    if(N_steps < 2*ndim) { N_steps = 2*ndim; }

    unsigned int base_N_steps = int(ceil(N_steps/10.));

    TAffineSampler<TLOSMCMCParams, TNullLogger>::pdf_t f_pdf = &lnp_los_extinction;
    TAffineSampler<TLOSMCMCParams, TNullLogger>::rand_state_t f_rand_state = &gen_rand_los_extinction;
    TAffineSampler<TLOSMCMCParams, TNullLogger>::reversible_step_t switch_step = &switch_adjacent_log_Delta_EBVs;
    TAffineSampler<TLOSMCMCParams, TNullLogger>::reversible_step_t mix_step = &mix_log_Delta_EBVs;
    TAffineSampler<TLOSMCMCParams, TNullLogger>::reversible_step_t move_one_step = &step_one_Delta_EBV;

    TParallelAffineSampler<TLOSMCMCParams, TNullLogger> sampler(f_pdf, f_rand_state, ndim, N_samplers*ndim, params, logger, N_runs);
    sampler.set_sigma_min(0.001);
    sampler.set_scale(1.05);
    sampler.set_replacement_bandwidth(0.25);

    sampler.step_MH(2*base_N_steps, true);
    //sampler.step(int(N_steps*10./100.), true, 0., 0.);
    sampler.step_custom_reversible(base_N_steps, switch_step, true);

    //sampler.step(int(N_steps*10./100), true, 0., 1., true);
    sampler.step_MH(base_N_steps, true);
    sampler.step_custom_reversible(base_N_steps, switch_step, true);
    sampler.step_custom_reversible(base_N_steps, move_one_step, true);
    sampler.step(base_N_steps, false, 0., 1., true, true);

    //sampler.step(int(N_steps*10./100.), true, 0., 0.5, true);
    //sampler.step(int(N_steps*10./100), true, 0., 1., true);
    /*std::cout << "scale: (";
    for(int k=0; k<sampler.get_N_samplers(); k++) {
        std::cout << sampler.get_MH_bandwidth(k) << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
    }*/
    //sampler.tune_MH(8, 0.30);
    /*std::cout << ") -> (";
    for(int k=0; k<sampler.get_N_samplers(); k++) {
        std::cout << sampler.get_MH_bandwidth(k) << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
    }
    std::cout << ")" << std::endl;*/
    sampler.step_MH(base_N_steps, true);
    sampler.step_custom_reversible(base_N_steps, switch_step, true);
    sampler.step_custom_reversible(base_N_steps, move_one_step, true);
    sampler.step(base_N_steps, false, 0., 1., true, true);

    /*std::cout << "scale: (";
    for(int k=0; k<sampler.get_N_samplers(); k++) {
        std::cout << sampler.get_MH_bandwidth(k) << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
    }*/
    //sampler.tune_MH(8, 0.30);
    /*std::cout << ") -> (";
    for(int k=0; k<sampler.get_N_samplers(); k++) {
        std::cout << sampler.get_MH_bandwidth(k) << ((k == sampler.get_N_samplers() - 1) ? "" : ", ");
    }
    std::cout << ")" << std::endl;*/
    sampler.step_MH(base_N_steps, true);

    if(verbosity >= 2) {
        sampler.print_diagnostics();
        std::cout << std::endl;
    }

    //std::cout << std::endl;

    //if(verbosity >= 2) {
    //  sampler.print_stats();
    //  std::cout << std::endl << std::endl;
    //}

    sampler.get_chain().get_best(params.EBV_prof_guess);
}


struct TEBVGuessParams {
    std::vector<double> EBV;
    std::vector<double> sigma_EBV;
    std::vector<double> sum_weight;
    double EBV_max, EBV_ceil;

    TEBVGuessParams(std::vector<double>& _EBV, std::vector<double>& _sigma_EBV, std::vector<double>& _sum_weight, double _EBV_ceil)
        : EBV(_EBV.size()), sigma_EBV(_sigma_EBV.size()), sum_weight(_sum_weight.size())
    {
        assert(_EBV.size() == _sigma_EBV.size());
        assert(_sum_weight.size() == _sigma_EBV.size());
        std::copy(_EBV.begin(), _EBV.end(), EBV.begin());
        std::copy(_sigma_EBV.begin(), _sigma_EBV.end(), sigma_EBV.begin());
        std::copy(_sum_weight.begin(), _sum_weight.end(), sum_weight.begin());
        EBV_max = -1.;
        for(unsigned int i=0; i<EBV.size(); i++) {
            if(EBV[i] > EBV_max) { EBV_max = EBV[i]; }
        }
        EBV_ceil = _EBV_ceil;
    }
};

double lnp_monotonic_guess(const double* Delta_EBV, unsigned int N, TEBVGuessParams& params) {
    double lnp = 0;

    double EBV = 0.;
    double tmp;
    for(unsigned int i=0; i<N; i++) {
        if(Delta_EBV[i] < 0.) { return neg_inf_replacement; }
        EBV += Delta_EBV[i];
        if(params.sum_weight[i] > 1.e-10) {
            tmp = (EBV - params.EBV[i]) / params.sigma_EBV[i];
            lnp -= 0.5 * tmp * tmp; //params.sum_weight[i] * tmp * tmp;
        }
    }

    return lnp;
}

void gen_rand_monotonic(double *const Delta_EBV, unsigned int N, gsl_rng *r, TEBVGuessParams &params) {
    double EBV_sum = 0.;
    double mu = 2. * params.EBV_max / (double)N;
    for(size_t i=0; i<N; i++) {
        Delta_EBV[i] = mu * gsl_rng_uniform(r);
        EBV_sum += Delta_EBV[i];
    }

    // Ensure that reddening is not more than allowed
    if(EBV_sum >= 0.95 * params.EBV_ceil) {
        double factor = EBV_sum / (0.95 * params.EBV_ceil);
        for(size_t i=0; i<N; i++) { Delta_EBV[i] *= factor; }
    }
}

void monotonic_guess(TImgStack &img_stack, unsigned int N_regions, std::vector<double>& Delta_EBV, TMCMCOptions& options) {
    std::cout << "stacking images" << std::endl;
    // Stack images
    cv::Mat stack;
    img_stack.stack(stack);

    std::cout << "calculating weighted mean at each distance" << std::endl;
    // Weighted mean of each distance
    double * dist_y_sum = new double[stack.cols];
    double * dist_y2_sum = new double[stack.cols];
    double * dist_sum = new double[stack.cols];
    for(int k = 0; k < stack.cols; k++) {
        dist_y_sum[k] = 0.;
        dist_y2_sum[k] = 0.;
        dist_sum[k] = 0.;
    }
    double y = 0.5;
    for(int j = 0; j < stack.rows; j++, y += 1.) {
        for(int k = 0; k < stack.cols; k++) {
            dist_y_sum[k] += y * stack.at<floating_t>(j,k);
            dist_y2_sum[k] += y*y * stack.at<floating_t>(j,k);
            dist_sum[k] += stack.at<floating_t>(j,k);
        }
    }

    for(int k = 0; k < stack.cols; k++) {
        std::cout << k << "\t" << dist_y_sum[k]/dist_sum[k] << "\t" << sqrt(dist_y2_sum[k]/dist_sum[k]) << "\t" << dist_sum[k] << std::endl;
    }

    std::cout << "calculating weighted mean about each anchor" << std::endl;
    // Weighted mean in region of each anchor point
    std::vector<double> y_sum(N_regions+1, 0.);
    std::vector<double> y2_sum(N_regions+1, 0.);
    std::vector<double> w_sum(N_regions+1, 0.);
    int kStart = 0;
    int kEnd;
    double width = (double)(stack.cols) / (double)(N_regions);
    for(int n = 0; n < N_regions+1; n++) {
        std::cout << "n = " << n << std::endl;
        if(n == N_regions) {
            kEnd = stack.cols;
        } else {
            kEnd = ceil(((double)n + 0.5) * width);
        }
        for(int k = kStart; k < kEnd; k++) {
            y_sum[n] += dist_y_sum[k];
            y2_sum[n] += dist_y2_sum[k];
            w_sum[n] += dist_sum[k];
        }
        kStart = kEnd + 1;
    }

    delete[] dist_sum;
    delete[] dist_y_sum;
    delete[] dist_y2_sum;

    std::cout << "Covert to EBV and sigma_EBV" << std::endl;
    // Create non-monotonic guess
    Delta_EBV.resize(N_regions+1);
    std::vector<double> sigma_EBV(N_regions+1, 0.);
    for(int i=0; i<N_regions+1; i++) { Delta_EBV[i] = 0; }
    for(int n = 0; n < N_regions+1; n++) {
        Delta_EBV[n] = img_stack.rect->min[0] + img_stack.rect->dx[1] * y_sum[n] / w_sum[n];
        sigma_EBV[n] = img_stack.rect->dx[0] * sqrt( (y2_sum[n] - (y_sum[n] * y_sum[n] / w_sum[n])) / w_sum[n] );
        std::cout << n << "\t" << Delta_EBV[n] << "\t+-" << sigma_EBV[n] << std::endl;
    }

    // Fit monotonic guess
    unsigned int N_steps = 100;
    unsigned int N_samplers = 2 * N_regions;
    unsigned int N_runs = options.N_runs;
    unsigned int ndim = N_regions + 1;

    std::cout << "Setting up params" << std::endl;
    TEBVGuessParams params(Delta_EBV, sigma_EBV, w_sum, img_stack.rect->max[0]);
    TNullLogger logger;

    TAffineSampler<TEBVGuessParams, TNullLogger>::pdf_t f_pdf = &lnp_monotonic_guess;
    TAffineSampler<TEBVGuessParams, TNullLogger>::rand_state_t f_rand_state = &gen_rand_monotonic;

    std::cout << "Setting up sampler" << std::endl;
    TParallelAffineSampler<TEBVGuessParams, TNullLogger> sampler(f_pdf, f_rand_state, ndim, N_samplers*ndim, params, logger, N_runs);
    sampler.set_scale(1.1);
    sampler.set_replacement_bandwidth(0.75);

    std::cout << "Stepping" << std::endl;
    sampler.step(int(N_steps*40./100.), true, 0., 0.5);
    sampler.step(int(N_steps*10./100), true, 0., 1., true);
    sampler.step(int(N_steps*40./100.), true, 0., 0.5);
    sampler.step(int(N_steps*10./100), true, 0., 1., true);

    sampler.print_stats();

    std::cout << "Getting best value" << std::endl;
    Delta_EBV.clear();
    sampler.get_chain().get_best(Delta_EBV);

    std::cout << "Monotonic guess" << std::endl;
    double EBV_sum = 0.;
    for(size_t i=0; i<Delta_EBV.size(); i++) {
        EBV_sum += Delta_EBV[i];
        std::cout << EBV_sum << std::endl;
        Delta_EBV[i] = log(Delta_EBV[i]);
    }
    std::cout << std::endl;
}



void gen_rand_los_extinction_from_guess(double *const logEBV, unsigned int N, gsl_rng *r, TLOSMCMCParams &params) {
    assert(params.EBV_prof_guess.size() == N);
    double EBV_ceil = params.img_stack->rect->max[0];
    double EBV_sum = 0.;
    double guess_sum = 0.;
    double factor;

    //if(params.sigma_log_Delta_EBV != NULL) {
    //  for(size_t i=0; i<N; i++) {
    //      logEBV[i] = params.EBV_prof_guess[i] + gsl_ran_gaussian_ziggurat(r, 1.);//1.0 * params.sigma_log_Delta_EBV[i]);
    //      EBV_sum += logEBV[i];
    //  }
    //} else {
    //for(size_t i=0; i<N; i++) {
    //  logEBV[i] = params.EBV_prof_guess[i] + gsl_ran_gaussian_ziggurat(r, 1.);
    //  EBV_sum += logEBV[i];
    //}
    //}

    const double sigma = 0.05;

    if(params.guess_cov == NULL) {
        for(size_t i=0; i<N; i++) {
            logEBV[i] = params.EBV_prof_guess[i] + gsl_ran_gaussian_ziggurat(r, sigma);
            EBV_sum += exp(logEBV[i]);
        }
    } else {
        // Redistribute reddening among distance bins
        draw_from_cov(logEBV, params.guess_sqrt_cov, N, r);

        /*#pragma omp critical (cout)
        {
        for(int i=0; i<N; i++) {
            std::cout << std::setw(6) << std::setprecision(2) << logEBV[i] << " ";
        }
        std::cout << std::endl;
        }*/

        for(size_t i=0; i<N; i++) {
            logEBV[i] *= sigma;
            logEBV[i] += params.EBV_prof_guess[i];
            EBV_sum += exp(logEBV[i]);
            guess_sum += exp(params.EBV_prof_guess[i]);
        }

        // Change in reddening at infinity
        //double norm = exp(gsl_ran_gaussian_ziggurat(r, 0.05));
        //factor = log(norm * guess_sum / EBV_sum);
        //for(size_t i=0; i<N; i++) { logEBV[i] += factor; }
    }

    // Switch adjacent reddenings
    /*int n_switches = sl_rng_uniform_int(r, 2);
    size_t k;
    double tmp_log_EBV;
    //int max_dist = std::min((int)(N-1)/2, 5);
    for(int i=0; i<n_switches; i++) {
        int dist = 1; //gsl_rng_uniform_int(r, max_dist+1);
        k = gsl_rng_uniform_int(r, N-dist);
        tmp_log_EBV = logEBV[k];
        logEBV[k] = logEBV[k+dist];
        logEBV[k+dist] = tmp_log_EBV;
    }*/

    // Ensure that reddening is not more than allowed
    if(EBV_sum >= 0.95 * EBV_ceil) {
        factor = log(0.95 * EBV_ceil / EBV_sum);
        for(size_t i=0; i<N; i++) {
            logEBV[i] += factor;
        }
    }
}


// Custom reversible step for piecewise-linear model.
// Switch two log(Delta E(B-V)) values.
double switch_log_Delta_EBVs(double *const _X, double *const _Y, unsigned int _N, gsl_rng* r, TLOSMCMCParams& _params) {
    for(int i=0; i<_N; i++) { _Y[i] = _X[i]; }

    // Choose two Deltas to switch
    int j = gsl_rng_uniform_int(r, _N);
    int k = gsl_rng_uniform_int(r, _N-1);
    if(k >= j) { k++; }

    _Y[j] = _X[k];
    _Y[k] = _X[j];

    // log[Q(Y -> X) / Q(X -> Y)]
    return 0.;
}

// Custom reversible step for piecewise-linear model.
// Switch two log(Delta E(B-V)) values.
double switch_adjacent_log_Delta_EBVs(double *const _X, double *const _Y, unsigned int _N, gsl_rng* r, TLOSMCMCParams& _params) {
    for(int i=0; i<_N; i++) { _Y[i] = _X[i]; }

    // Choose which Deltas to switch
    int j = gsl_rng_uniform_int(r, _N-1);

    _Y[j] = _X[j+1];
    _Y[j+1] = _X[j];

    // log[Q(Y -> X) / Q(X -> Y)]
    return 0.;
}


double mix_log_Delta_EBVs(double *const _X, double *const _Y, unsigned int _N, gsl_rng* r, TLOSMCMCParams& _params) {
    for(int i=0; i<_N; i++) { _Y[i] = _X[i]; }

    // Choose two Deltas to mix
    int j = gsl_rng_uniform_int(r, _N-1);
    int k;
    if(gsl_rng_uniform(r) < 0.5) {
        k = j;
        j += 1;
    } else {
        k = j+1;
    }
    //int k = gsl_rng_uniform_int(r, _N-1);
    //if(k >= j) { k++; }
    double pct = gsl_rng_uniform(r);

    _Y[j] = log(1. - pct) + _X[j];
    _Y[k] = log(exp(_Y[k]) + pct * exp(_X[j]));

    // log[Q(Y -> X) / Q(X -> Y)]
    return 2. * _X[j] + _X[k] - 2. * _Y[j] - _Y[k];
}


double step_one_Delta_EBV(double *const _X, double *const _Y, unsigned int _N, gsl_rng* r, TLOSMCMCParams& _params) {
    for(int i=0; i<_N; i++) { _Y[i] = _X[i]; }

    // Choose Delta to step in
    int j = _N - 1 - gsl_rng_uniform_int(r, _N/2);

    _Y[j] += gsl_ran_gaussian_ziggurat(r, 0.5);

    // log[Q(Y -> X) / Q(X -> Y)]
    return 0.;
}




/****************************************************************************************************************************
 *
 * TLOSMCMCParams
 *
 ****************************************************************************************************************************/

TLOSMCMCParams::TLOSMCMCParams(
        TImgStack* _img_stack, const std::vector<double>& _lnZ, double _p0,
        unsigned int _N_runs, unsigned int _N_threads, unsigned int _N_regions,
        double _EBV_max)
    : img_stack(_img_stack), subpixel(_img_stack->N_images, 1.),
      N_runs(_N_runs), N_threads(_N_threads), N_regions(_N_regions),
      line_int(NULL), Delta_EBV_prior(NULL),
      log_Delta_EBV_prior(NULL), sigma_log_Delta_EBV(NULL),
      guess_cov(NULL), guess_sqrt_cov(NULL)
{
    line_int = new double[_img_stack->N_images * N_threads];
    Delta_EBV = new float[(N_regions+1) * N_threads];

    //std::cout << "Allocated line_int[" << _img_stack->N_images * N_threads << "] (" << _img_stack->N_images << " images, " << N_threads << " threads)" << std::endl;
    p0 = _p0;
    lnp0 = log(p0);

    p0_over_Z.reserve(_lnZ.size());
    inv_p0_over_Z.reserve(_lnZ.size());
    ln_p0_over_Z.reserve(_lnZ.size());

    for(std::vector<double>::const_iterator it=_lnZ.begin(); it != _lnZ.end(); ++it) {
        ln_p0_over_Z.push_back(lnp0 - *it);
        p0_over_Z.push_back(exp(lnp0 - *it));
        inv_p0_over_Z.push_back(exp(*it - lnp0));
    }

    EBV_max = _EBV_max;
    EBV_guess_max = guess_EBV_max(*img_stack);
    subpixel_max = 1.;
    subpixel_min = 1.;
    alpha_skew = 0.;
}

TLOSMCMCParams::~TLOSMCMCParams() {
    if(line_int != NULL) { delete[] line_int; }
    if(Delta_EBV != NULL) { delete[] Delta_EBV; }
    if(Delta_EBV_prior != NULL) { delete[] Delta_EBV_prior; }
    if(log_Delta_EBV_prior != NULL) { delete[] log_Delta_EBV_prior; }
    if(sigma_log_Delta_EBV != NULL) { delete[] sigma_log_Delta_EBV; }
    if(guess_cov != NULL) { gsl_matrix_free(guess_cov); }
    if(guess_sqrt_cov != NULL) { gsl_matrix_free(guess_sqrt_cov); }
}

void TLOSMCMCParams::set_p0(double _p0) {
    p0 = _p0;
    lnp0 = log(p0);
}

void TLOSMCMCParams::set_subpixel_mask(TStellarData& data) {
    assert(data.star.size() == img_stack->N_images);
    subpixel.clear();
    subpixel_max = 0.;
    subpixel_min = inf_replacement;
    double EBV;
    for(size_t i=0; i<data.star.size(); i++) {
        EBV = data.star[i].EBV;
        if(EBV > subpixel_max) { subpixel_max = EBV; }
        if(EBV < subpixel_min) { subpixel_min = EBV; }
        subpixel.push_back(EBV);
    }
}

void TLOSMCMCParams::set_subpixel_mask(std::vector<double>& new_mask) {
    assert(new_mask.size() == img_stack->N_images);
    subpixel.clear();
    subpixel_max = 0.;
    subpixel_min = inf_replacement;
    for(size_t i=0; i<new_mask.size(); i++) {
        if(new_mask[i] > subpixel_max) { subpixel_max = new_mask[i]; }
        if(new_mask[i] < subpixel_min) { subpixel_min = new_mask[i]; }
        subpixel.push_back(new_mask[i]);
    }
}

// Calculate the mean and std. dev. of log(delta_EBV)
void TLOSMCMCParams::calc_Delta_EBV_prior(TGalacticLOSModel& gal_los_model,
                                          double log_Delta_EBV_floor,
                                          double log_Delta_EBV_ceil,
                                          double EBV_tot,
                                          double sigma,
                                          int verbosity) {
    double mu_0 = img_stack->rect->min[1];
    double mu_1 = img_stack->rect->max[1];
    assert(mu_1 > mu_0);

    int subsampling = 100;
    double Delta_mu = (mu_1 - mu_0) / (double)(N_regions * subsampling);

    // Allocate space for information on priors
    if(Delta_EBV_prior != NULL) { delete[] Delta_EBV_prior; }
    Delta_EBV_prior = new double[N_regions+1];

    if(log_Delta_EBV_prior != NULL) { delete[] log_Delta_EBV_prior; }
    log_Delta_EBV_prior = new double[N_regions+1];

    if(sigma_log_Delta_EBV != NULL) { delete[] sigma_log_Delta_EBV; }
    sigma_log_Delta_EBV = new double[N_regions+1];

    /*for(double x = 0.; x < 20.5; x += 1.) {
        std::cout << "rho(DM = " << x << ") = " << std::setprecision(5) << gal_los_model.dA_dmu(x) / exp10(x/5.) << std::endl;
    }*/

    // Normalization information
    // double sigma = 0.5;
    double dEBV_ds = 0.2;       // mag kpc^{-1}

    // Determine normalization
    double ds_dmu = 10. * log(10.) / 5. * exp10(-10./5.);
    double dEBV_ds_local = gal_los_model.dA_dmu(-10.) / ds_dmu * exp(0.5 * sigma * sigma);
    double norm = 0.001 * dEBV_ds / dEBV_ds_local;
    double log_norm = log(norm);

    // Integrate Delta E(B-V) from close distance to mu_0
    int n_close = 1; // As n_close -> infty, inner distance approaches 0
    double mu = mu_0 - n_close * Delta_mu * (double)subsampling;
    Delta_EBV_prior[0] = 0.;
    for(int k=0; k<n_close*subsampling; k++, mu += Delta_mu) {
        Delta_EBV_prior[0] += gal_los_model.dA_dmu(mu);
    }
    Delta_EBV_prior[0] *= Delta_mu;

    // Integrate Delta E(B-V) in each region
    for(int i=1; i<N_regions+1; i++) {
        Delta_EBV_prior[i] = 0.;

        for(int k=0; k<subsampling; k++, mu += Delta_mu) {
            Delta_EBV_prior[i] += gal_los_model.dA_dmu(mu);
        }

        Delta_EBV_prior[i] *= Delta_mu;
    }

    // Determine std. dev. of reddening in each distance bin
    double * log_Delta_EBV_bias = new double[N_regions+1];

    for(int i=0; i<N_regions+1; i++) {
        sigma_log_Delta_EBV[i] = sigma;
        log_Delta_EBV_bias[i] = 0.;

        log_Delta_EBV_prior[i] = log(Delta_EBV_prior[i]) + log_Delta_EBV_bias[i];
    }

    // Normalize Delta E(B-V)
    if(verbosity >= 2) {
        std::cout << "Delta_EBV_prior:" << std::endl;
    }

    double EBV_sum = 0.;
    mu = mu_0;

    for(int i=0; i<N_regions+1; i++) {
        log_Delta_EBV_prior[i] += log_norm;

        // Floor on log(Delta EBV) prior
        if(log_Delta_EBV_prior[i] < log_Delta_EBV_floor) {
            log_Delta_EBV_prior[i] = log_Delta_EBV_floor;
        } else if(log_Delta_EBV_prior[i] > log_Delta_EBV_ceil) {
            log_Delta_EBV_prior[i] = log_Delta_EBV_ceil;
        }

        Delta_EBV_prior[i] = exp(log_Delta_EBV_prior[i]);

        EBV_sum += Delta_EBV_prior[i] * exp(0.5 * sigma_log_Delta_EBV[i] * sigma_log_Delta_EBV[i]);

        if(verbosity >= 2) {
            std::cout << std::setprecision(5)
                      << exp10(mu / 5. - 2.)
                  << "\t" << mu
                      << "\t" << log_Delta_EBV_prior[i]
                      << " +- " << sigma_log_Delta_EBV[i]
                      << " -> " << Delta_EBV_prior[i] * exp(0.5 * sigma_log_Delta_EBV[i] * sigma_log_Delta_EBV[i])
                      << std::endl;
        }

        mu += (mu_1 - mu_0) / (double)N_regions;
    }

    if(verbosity >= 2) {
        std::cout << "Total E(B-V) = " << EBV_sum << std::endl;
        std::cout << std::endl;
    }

    // Convert means and errors for skew normal distribution
    // alpha_skew = 1.;
    double delta_skew = alpha_skew / (1. + alpha_skew*alpha_skew);

    if(verbosity >= 2) {
        std::cout << "Skewed mean/variance:" << std::endl;
    }

    for(int i=0; i<N_regions+1; i++) {
        sigma_log_Delta_EBV[i] /= sqrt(1. - 2. * delta_skew*delta_skew / PI);
        log_Delta_EBV_prior[i] -= delta_skew * sigma_log_Delta_EBV[i] * SQRT2 / PI;

        if(verbosity >= 2) {
            std::cout << std::setprecision(6)
                      << "\t" << log_Delta_EBV_prior[i]
                      << " +- " << sigma_log_Delta_EBV[i] << std::endl;
        }
    }

    if(verbosity >= 2) {
        std::cout << std::endl;
    }

    delete[] log_Delta_EBV_bias;
}


void TLOSMCMCParams::gen_guess_covariance(double scale_length) {
    if(guess_cov != NULL) { gsl_matrix_free(guess_cov); }
    if(guess_sqrt_cov != NULL) { gsl_matrix_free(guess_sqrt_cov); }

    guess_cov = gsl_matrix_alloc(N_regions+1, N_regions+1);
    guess_sqrt_cov = gsl_matrix_alloc(N_regions+1, N_regions+1);

    // Generate guess covariance matrix
    double val;

    for(int k=0; k<N_regions+1; k++) {
        gsl_matrix_set(guess_cov, k, k, 1.);
    }

    for(int offset=1; offset<N_regions+1; offset++) {
        val = -exp(-(double)(offset*offset) / (2. * scale_length * scale_length));

        for(int k=0; k<N_regions+1-offset; k++) {
            gsl_matrix_set(guess_cov, k+offset, k, val);
            gsl_matrix_set(guess_cov, k, k+offset, val);
        }
    }

    // Find square root of covariance matrix (A A^T = B)
    sqrt_matrix(guess_cov, guess_sqrt_cov);

    /*std::cout << std::endl;
    std::cout << "Guess covariance:" << std::endl;
    for(int i=0; i<N_regions+1; i++) {
        for(int j=0; j<N_regions+1; j++) {
            std::cout << std::setprecision(2) << gsl_matrix_get(guess_cov, i, j) << "  ";
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;*/
}



double* TLOSMCMCParams::get_line_int(unsigned int thread_num) {
    assert(thread_num < N_threads);
    return line_int + img_stack->N_images * thread_num;
}

float* TLOSMCMCParams::get_Delta_EBV(unsigned int thread_num) {
    assert(thread_num < N_threads);
    return Delta_EBV + (N_regions+1) * thread_num;
}


/****************************************************************************************************************************
 *
 * TDiscreteLosMcmcParams
 *
 ****************************************************************************************************************************/

TDiscreteLosMcmcParams::TDiscreteLosMcmcParams(
            std::unique_ptr<TImgStack> _img_stack,
            std::unique_ptr<TNeighborPixels> _neighbor_pixels,
            unsigned int _N_runs,
            unsigned int _N_threads,
            int verbosity)
        : img_stack(std::move(_img_stack)),
          neighbor_pixels(std::move(_neighbor_pixels)),
          N_runs(_N_runs), N_threads(_N_threads)
{
    // Initialize random number generator
    std::random_device rd;
    std::vector<long unsigned int> seeds = {
        rd(), 
        static_cast<long unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count())
    };
    std::seed_seq seq(seeds.begin(), seeds.end());
    std::mt19937 r(seq);

    // Number of distance, reddening bins
    n_dists = img_stack->rect->N_bins[1];
    n_E = img_stack->rect->N_bins[0];

    // Line integrals through p(dm, E) of each star
    line_int = new double[img_stack->N_images * N_threads];

    E_pix_idx = new int16_t[n_dists * N_threads];

    y_zero_idx = -img_stack->rect->min[0] / img_stack->rect->dx[0];

    // Priors
    mu_log_dE = -10.;
    set_sigma_log_dE(0.75);
    //sigma_log_dE = 0.75; // TODO: Make this configurable
    mu_log_dy = mu_log_dE - log(img_stack->rect->dx[0]);
    //inv_sigma_log_dy = 1. / sigma_log_dE;
    inv_sigma_dy_neg = 1. / 0.1;

    priors_subsampling = 1; //10;
    
    log_P_dy = std::make_shared<cv::Mat>();

    if(verbosity >= 2) {
        std::cerr << "n_dists = " << n_dists << std::endl;
        std::cerr << "n_E = " << n_E << std::endl;
        std::cerr << "y_zero_idx = " << y_zero_idx << std::endl;
        std::cerr << "mu_log_dy = " << mu_log_dy << std::endl;
        std::cerr << "inv_sigma_log_dy = " << inv_sigma_log_dy
                  << std::endl;
    }
}


TDiscreteLosMcmcParams::~TDiscreteLosMcmcParams() {
    if(line_int != NULL) { delete[] line_int; }
    if(E_pix_idx != NULL) { delete[] E_pix_idx; }
}


void TDiscreteLosMcmcParams::set_sigma_log_dE(const double s) {
    sigma_log_dE = s;
    inv_sigma_log_dy = 1. / s;
}


void randomize_neighbors(
        TNeighborPixels& neighbor_pixels,
        std::vector<uint16_t>& neighbor_sample,
        std::mt19937& r)
{
    // Choose random neighbor samples
    std::uniform_int_distribution<int> u(0, neighbor_pixels.get_n_samples());
    neighbor_sample.clear();
    for(int i=0; i<neighbor_pixels.get_n_pix(); i++) {
        neighbor_sample.push_back(u(r));
    }
}


void TDiscreteLosMcmcParams::set_central_delta(int16_t* y_idx) {
    //std::cerr << "y_idx:";
    for(int i=0; i<n_dists; i++) {
        //std::cerr << " " << y_idx[i];
        neighbor_pixels->set_delta((double)(y_idx[i]), 0, 0, i);
    }
    //std::cerr << std::endl;
    neighbor_pixels->apply_priors_indiv(
        mu_log_dE_0,
        sigma_log_dE_0,
        img_stack->rect->dx[0],
        0, 0);
}


double neighbor_gibbs_step(
        int pix,
        TNeighborPixels& neighbor_pixels,
        std::vector<uint16_t>& neighbor_sample,
        std::vector<double>& log_p_sample_ws,
        std::vector<double>& p_sample_ws,
        std::mt19937& r,
        double beta)
{
    // Takes a Gibbs step in one of the neighboring pixels, choosing
    // a sample at random, weighted by the Gaussian process prior.
    
    double mu, ivar, dx;

    int n_samples = neighbor_pixels.get_n_samples();
    int n_dists = neighbor_pixels.get_n_dists();

    log_p_sample_ws.resize(n_samples);

    // Determine chi^2 of each sample
    for(int sample=0; sample<n_samples; sample++) {
        log_p_sample_ws[sample] = 0.;
        
        for(int dist=1; dist<n_dists-1; dist++) {
            // Calculate mean, sigma of pixel
            mu = neighbor_pixels.calc_mean(pix, dist, neighbor_sample);
            ivar = neighbor_pixels.get_inv_var(pix, dist);

            // Add to chi^2
            dx = neighbor_pixels.get_delta(pix, sample, dist) - mu;
            log_p_sample_ws[sample] += ivar * dx*dx;

            //if(pix == 1) {
            //    std::cerr << "d = " << dist << std::endl
            //              << "     delta =";
            //    for(int k=0; k<neighbor_pixels->get_n_pix(); k++) {
            //        int s = neighbor_sample[k];
            //        std::cerr  << " " << neighbor_pixels->get_delta(k, s, dist);
            //    }
            //    std::cerr << std::endl
            //              << "         x = " << mu << " +- " << 1./std::sqrt(ivar)
            //                                 << std::endl
            //              << "  delta[1] = " << neighbor_pixels->get_delta(1, sample, dist)
            //                                 << std::endl
            //              << "        dx = " << dx
            //                                 << std::endl
            //              << "     chi^2 = " << ivar * dx*dx
            //                                 << std::endl;
            //}
        }
        
        log_p_sample_ws[sample] *= -0.5;

        // Prior term
        log_p_sample_ws[sample] -= neighbor_pixels.get_prior(pix, sample);
    }
    
    // Turn chi^2 into probability
    //std::cerr << "p_sample.size() = " << p_sample.size() << std::endl;
    double log_p_max = *std::max_element(log_p_sample_ws.begin(), log_p_sample_ws.end());
    //std::cerr << std::endl
    //          << "p_min = " << p_min << std::endl;
    for(int sample=0; sample<n_samples; sample++) {
        //std::cerr << p_sample[sample] << " ";
        p_sample_ws[sample] = std::exp(beta * (log_p_sample_ws[sample] - log_p_max));
    }
    //std::cerr << std::endl << std::endl;

    // Choose a sample at random, weighted by the probabilities
    std::discrete_distribution<> d(p_sample_ws.begin(), p_sample_ws.end());
    int idx = d(r);
    neighbor_sample[pix] = idx;

    //if(pix == 1) {
    //    std::vector<double> p = d.probabilities();
    //    std::sort(p.begin(), p.end());

    //    std::cerr << "Probabilities:";
    //    for(int i=p.size()-1; (i>0) && (i>p.size()-10); i--) {
    //        std::cerr << " " << p.at(i);
    //    }
    //    std::cerr << std::endl;

    //    double P_tot = 0;
    //    int n_eff = 0;
    //    for(auto pp = p.rbegin(); (pp != p.rend()) && (P_tot < 0.99); ++pp, ++n_eff) {
    //        P_tot += *pp;
    //    }
    //    std::cerr << "n_eff(" << pix << ") = " << n_eff << std::endl;
    //}
    //std::cerr << std::endl;
    
    return log_p_sample_ws[idx];
}


std::unique_ptr<std::discrete_distribution<int>>
neighbor_gibbs_step_shifted_factory(
        const int pix,
        TNeighborPixels& neighbor_pixels,
        const std::vector<uint16_t>& neighbor_sample,
        std::vector<double>& log_p_sample_ws,
        std::vector<double>& p_sample_ws,
        const double beta,
        const double shift_weight)
{
    // Returns a pointer containing a discrete_distribution,
    // which allows one to take Gibbs steps in one of the
    // neighboring pixels. The new samples are
    // chosen at random, weighted by the Gaussian process prior.
    
    double mu, ivar, dx;

    int n_samples = neighbor_pixels.get_n_samples();
    int n_dists = neighbor_pixels.get_n_dists();

    log_p_sample_ws.resize(n_samples);

    // Determine chi^2 of each sample
    for(int sample=0; sample<n_samples; sample++) {
        log_p_sample_ws[sample] = 0.;
        
        for(int dist=0; dist<n_dists; dist++) {
            // Calculate mean, sigma of pixel
            mu = neighbor_pixels.calc_mean_shifted(
                pix,
                dist,
                neighbor_sample,
                shift_weight
            );
            ivar = neighbor_pixels.get_inv_var(pix, dist);

            // Add to chi^2
            dx = neighbor_pixels.get_delta(pix, sample, dist) - mu;
            log_p_sample_ws[sample] += ivar * dx*dx;
        }
        
        log_p_sample_ws[sample] *= -0.5;

        // Prior term
        log_p_sample_ws[sample] -= neighbor_pixels.get_prior(pix, sample);
    }
    
    // Turn chi^2 into probability
    double log_p_max = *std::max_element(
        log_p_sample_ws.begin(),
        log_p_sample_ws.end()
    );
    for(int sample=0; sample<n_samples; sample++) {
        p_sample_ws[sample] = std::exp(
            beta
            * (log_p_sample_ws[sample] - log_p_max)
        );
    }

    // Return a unique pointer containing a discrete_distribution object,
    // which can generate random samples for this pixel.
    return std::make_unique<std::discrete_distribution<int>>(
        p_sample_ws.begin(),
        p_sample_ws.end()
    );
}


struct NeighborGibbsCacheData {
    // Data required in cache to speed up Gibbs steps in
    // neighboring pixels.
    std::vector<uint16_t> samples;
    std::vector<double> mu;
};


std::unique_ptr<NeighborGibbsCacheData>
neighbor_gibbs_step_shifted_cache_data(
        const int pix,
        TNeighborPixels& neighbor_pixels,
        const std::vector<uint16_t>& neighbor_sample,
        std::vector<double>& log_p_sample_ws,
        std::vector<double>& p_sample_ws,
        std::vector<double>& mu_ws,
        const double beta,
        const double shift_weight,
        const double lnp_cutoff)
{
    // Gather cache data allowing one to execute a Gibbs step
    // in one of the neighboring pixels, choosing
    // a sample at random, weighted by the Gaussian process prior.
    
    double mu, ivar, dx;

    const int n_samples = neighbor_pixels.get_n_samples();
    const int n_dists = neighbor_pixels.get_n_dists();

    log_p_sample_ws.resize(n_samples);
    mu_ws.resize(n_samples*n_dists);
    
    // Determine chi^2 of each sample
    uint32_t k = 0;
    for(int sample=0; sample<n_samples; sample++) {
        log_p_sample_ws[sample] = 0.;
        
        for(int dist=0; dist<n_dists; dist++, k++) {
            // Calculate mean, sigma of pixel
            mu_ws[k] = neighbor_pixels.calc_mean_shifted(
                pix,
                dist,
                neighbor_sample,
                shift_weight,
                1
            );
            ivar = neighbor_pixels.get_inv_var(pix, dist);

            // Add to chi^2
            dx = neighbor_pixels.get_delta(pix, sample, dist) - mu;
            log_p_sample_ws[sample] += ivar * dx*dx;
        }
        
        log_p_sample_ws[sample] *= -0.5;

        // Prior term
        log_p_sample_ws[sample] -= neighbor_pixels.get_prior(pix, sample);
    }
    
    // Turn chi^2 into probability
    double log_p_max = *std::max_element(
        log_p_sample_ws.begin(),
        log_p_sample_ws.end()
    );
    
    std::unique_ptr<NeighborGibbsCacheData> cache_data
        = std::make_unique<NeighborGibbsCacheData>();
    
    cache_data->samples.reserve(n_samples);
    for(int sample=0; sample<n_samples; sample++) {
        double lnp_sample = beta
                            * (log_p_sample_ws[sample] - log_p_max);
        if(lnp_sample > lnp_cutoff) {
            cache_data->samples.push_back(sample);
        }
    }
    
    const int n_samples_cached = cache_data->samples.size();
    cache_data->mu.reserve(n_dists*n_samples_cached);
    for(auto s : cache_data->samples) {
        for(int k=s*n_dists; k<(s+1)*n_dists; k++) {
            cache_data->mu.push_back(mu_ws[k]);
        }
    }

    return std::move(cache_data);
}


//typedef LRUCache::CachedFunction<
//            std::vector<uint16_t>,
//            std::shared_ptr<NeighborGibbsCacheData>,
//            LRUCache::VectorHasher<uint16_t>
//        > NeighborGibbsCache;


int neighbor_gibbs_step_shifted_cached(
        const int pix,
        NeighborGibbsCacheData& cache_data,
        TNeighborPixels& neighbor_pixels,
        const std::vector<uint16_t>& neighbor_sample,
        std::vector<double>& log_p_sample_ws,
        std::vector<double>& p_sample_ws,
        std::vector<double>& mu_ws,
        const double beta,
        const double shift_weight,
        const double lnp_cutoff,
        std::mt19937& r)
{
    // Calculate ln(p) for each sample
    const int n_dists = neighbor_pixels.get_n_dists();
    const int n_samples = cache_data.samples.size();
    
    double mu, dx, ivar;
    double inv_cov_0;
    double inv_cov_m1;
    double inv_cov_p1;
    
    int j = 0;
    
    for(int i=0; i<n_samples; i++) {
        uint16_t sample = cache_data.samples[i];
        
        log_p_sample_ws[i] = 0.;
        
        // At each distance, update mean to take into
        // account central pixel.
        // The first and last distances are treated as special cases.

        // First distance
        mu = cache_data.mu[j];
        
        inv_cov_0 = neighbor_pixels.get_inv_cov(0, pix, 0);
        inv_cov_p1 = neighbor_pixels.get_inv_cov(1, pix, 0);
        
        mu += inv_cov_0 * neighbor_pixels.get_delta(0, sample, 0)
              + shift_weight * (
                    inv_cov_p1
                    * neighbor_pixels.get_delta(0, sample, 1)
                );
        ivar = neighbor_pixels.get_inv_var(pix, 0);

        // Add to chi^2
        dx = neighbor_pixels.get_delta(pix, sample, 0) - mu;
        log_p_sample_ws[sample] += ivar * dx*dx;
        
        j++;
        
        // Middle distances
        for(int dist=1; dist<n_dists-1; dist++, j++) {
            mu = cache_data.mu[j];
            
            inv_cov_0 = neighbor_pixels.get_inv_cov(dist, pix, 0);
            inv_cov_m1 = neighbor_pixels.get_inv_cov(dist-1, pix, 0);
            inv_cov_p1 = neighbor_pixels.get_inv_cov(dist+1, pix, 0);
            
            mu += inv_cov_0 * neighbor_pixels.get_delta(0, sample, dist)
                  + shift_weight * (
                        inv_cov_m1
                        * neighbor_pixels.get_delta(0, sample, dist-1)
                      + inv_cov_p1
                        * neighbor_pixels.get_delta(0, sample, dist+1)
                    );
            ivar = neighbor_pixels.get_inv_var(pix, dist);

            // Add to chi^2
            dx = neighbor_pixels.get_delta(pix, sample, dist) - mu;
            log_p_sample_ws[i] += ivar * dx*dx;
        }
        
        // Last distance
        mu = cache_data.mu[j];
        
        inv_cov_0 = neighbor_pixels.get_inv_cov(n_dists-1, pix, 0);
        inv_cov_m1 = neighbor_pixels.get_inv_cov(n_dists-2, pix, 0);
        
        mu += inv_cov_0 * neighbor_pixels.get_delta(0, sample, n_dists-1)
              + shift_weight * (
                    inv_cov_m1
                    * neighbor_pixels.get_delta(0, sample, n_dists-2)
                );
        ivar = neighbor_pixels.get_inv_var(pix, 0);

        // Add to chi^2
        dx = neighbor_pixels.get_delta(pix, sample, n_dists-1) - mu;
        log_p_sample_ws[i] += ivar * dx*dx;
        
        j++;
        
        log_p_sample_ws[i] *= -0.5 * beta;
        
        // Prior term
        log_p_sample_ws[i] -= neighbor_pixels.get_prior(pix, sample);
    }
    
    // Turn chi^2 into probability
    double log_p_max = *std::max_element(
        log_p_sample_ws.begin(),
        log_p_sample_ws.begin()+n_samples
    );
    
    for(int i=0; i<n_samples; i++) {
        double lnp_sample = log_p_sample_ws[i] - log_p_max;
        if(lnp_sample < -8.) {
            p_sample_ws[i] = 0.;
        } else {
            p_sample_ws[i] = std::exp(lnp_sample);
        }
    }

    std::discrete_distribution<int> dd(
        p_sample_ws.begin(),
        p_sample_ws.begin()+n_samples
    );
    
    return dd(r);
}


double neighbor_gibbs_step_shifted(
        const int pix,
        TNeighborPixels& neighbor_pixels,
        std::vector<uint16_t>& neighbor_sample,
        std::vector<double>& log_p_sample_ws,
        std::vector<double>& p_sample_ws,
        std::mt19937& r,
        const double beta,
        const double shift_weight)
{
    // Takes a Gibbs step in one of the neighboring pixels, choosing
    // a sample at random, weighted by the Gaussian process prior.
    
    double mu, ivar, dy, y;

    int n_samples = neighbor_pixels.get_n_samples();
    int n_dists = neighbor_pixels.get_n_dists();

    log_p_sample_ws.resize(n_samples);
    
    const int track_pix = 1;

    for(int sample=0; sample<n_samples; sample++) {
        log_p_sample_ws[sample] = 0.;
    }
        
    for(int dist=0; dist<n_dists; dist++) {
        // Calculate mean, sigma of pixel
        mu = neighbor_pixels.calc_mean_shifted(
            pix,
            dist,
            neighbor_sample,
            shift_weight
        );
        ivar = neighbor_pixels.get_inv_var(pix, dist);
        
        //double mu_0 = neighbor_pixels.calc_mean_shifted(
        //    pix,
        //    dist,
        //    neighbor_sample,
        //    0.
        //);
        //
        //if(beta <= 0.975) {
        //    std::cerr << "(pix, dist) = (" << pix << ", " << dist << "): "
        //              << "(mu, mu') = (" << mu_0 << ", " << mu << ")"
        //              << std::endl;
        //}

        // Determine chi^2 of each sample at this distance
        for(int sample=0; sample<n_samples; sample++) {
            y = neighbor_pixels.get_delta(pix, sample, dist);
            dy = y - mu;
            log_p_sample_ws[sample] += ivar * dy*dy;
        }
    }
        
    for(int sample=0; sample<n_samples; sample++) {
        log_p_sample_ws[sample] *= -0.5;
        log_p_sample_ws[sample] -= neighbor_pixels.get_sum_log_dy(pix, sample);
        log_p_sample_ws[sample] *= beta;

        //if((pix == track_pix) && (beta > 0.9999)) {
        //    double p0 = log_p_sample_ws[sample];
        //    double p1 = neighbor_pixels.get_prior(pix, sample);
        //    std::cerr << "log(prior_" << sample << "): "
        //              << p0
        //              << " "
        //              << p1
        //              << " : "
        //              << p0-p1
        //              << std::endl;
        //}

        // Prior and likelihood terms
        log_p_sample_ws[sample] -=
            neighbor_pixels.get_prior(pix, sample) + 
            (1.-beta) * neighbor_pixels.get_likelihood(pix, sample);
        
        //if((pix == track_pix) && ((sample == 82) || (sample == 83))) {
        //    std::cerr << std::endl << std::endl;
        //}
    }
    
    // Turn chi^2 into probability
    //std::cerr << "p_sample.size() = " << p_sample.size() << std::endl;
    double log_p_max = *std::max_element(
        log_p_sample_ws.begin(),
        log_p_sample_ws.end()
    );
    //std::cerr << std::endl
    //          << "p_min = " << p_min << std::endl;
    for(int sample=0; sample<n_samples; sample++) {
        double lnp_sample = log_p_sample_ws[sample] - log_p_max;
        if(lnp_sample < -8.) {
            p_sample_ws[sample] = 0.;
        } else {
            p_sample_ws[sample] = std::exp(lnp_sample);
        }
    }
    //std::cerr << std::endl << std::endl;
    
    // Choose a sample at random, weighted by the probabilities
    int idx_old = neighbor_sample[pix];
    std::discrete_distribution<> d(p_sample_ws.begin(), p_sample_ws.end());
    int idx = d(r);
    neighbor_sample[pix] = idx;
    
    // Calculate entropy of distribution
    //if((pix == track_pix) && (beta >= 0.9999)) {
    //if(beta >= 0.9999) {
    //    double plnp = 0.;
    //    for(auto p : d.probabilities()) {
    //        if(p > 1.e-10) {
    //            plnp += p * std::log(p);
    //        }
    //    }
    //    double n_eff = std::exp(-1. * plnp);
    //    std::cerr << "n_eff_" << pix << "(b=" << beta << ")" << " = " << n_eff << std::endl;
    //}

    //if(pix == 2) {
    //    std::cerr << "beta = " << beta << std::endl;
    //    std::vector<double> p = d.probabilities();

    //    int p_max_idx = std::distance(
    //        p.begin(),
    //        std::max_element(p.begin(), p.end())
    //    );

    //    std::sort(p.begin(), p.end());

    //    std::cerr << "Probabilities:";
    //    for(int i=p.size()-1; (i>0) && (i>p.size()-10); i--) {
    //        std::cerr << " " << p.at(i);
    //    }
    //    std::cerr << std::endl;

    //    std::cerr << "Index of max. prob.: " << p_max_idx << std::endl;

    //    double P_tot = 0;
    //    int n_eff = 0;
    //    for(auto pp = p.rbegin(); (pp != p.rend()) && (P_tot < 0.99); ++pp, ++n_eff) {
    //        P_tot += *pp;
    //    }
    //    std::cerr << "n_eff(" << pix << ") = " << n_eff << std::endl;
    //}
    //std::cerr << std::endl;
    
    return log_p_sample_ws[idx] - log_p_sample_ws[idx_old];
}



double neighbor_gibbs_step_shifted_2(
        int pix,
        TNeighborPixels& neighbor_pixels,
        std::vector<uint16_t>& neighbor_sample,
        double log_w_shift,
        std::vector<double>& log_p_sample_ws,
        std::vector<double>& p_sample_ws,
        std::mt19937& r,
        double beta)
{
    // Takes a Gibbs step in one of the neighboring pixels, choosing
    // a sample at random, weighted by the Gaussian process prior.
    
    double mu, ivar, dx;

    int n_samples = neighbor_pixels.get_n_samples();
    int n_dists = neighbor_pixels.get_n_dists();

    log_p_sample_ws.resize(n_samples*3);

    // Determine chi^2 of each sample
    for(int sample=0; sample<n_samples; sample++) {
        log_p_sample_ws[sample] = 0.;
        log_p_sample_ws[sample+n_samples] = 0.;     // -1 distance bin
        log_p_sample_ws[sample+2*n_samples] = 0.;   // +1 distance bin

        for(int dist=1; dist<n_dists-1; dist++) {
            // Calculate mean, sigma of pixel
            mu = neighbor_pixels.calc_mean(pix, dist, neighbor_sample);
            ivar = neighbor_pixels.get_inv_var(pix, dist);

            // Add to chi^2
            dx = neighbor_pixels.get_delta(pix, sample, dist) - mu;
            log_p_sample_ws[sample] += ivar * dx*dx;
            
            //if((pix == 2) && (sample == 55)) {
            //    std::cerr << "dist = " << dist << std::endl;
            //    std::cerr << "log_p_sample_ws[sample]             += " << ivar*dx*dx << std::endl;
            //}

            dx = neighbor_pixels.get_delta(pix, sample, dist-1) - mu;
            log_p_sample_ws[sample+n_samples] += ivar * dx*dx;

            //if((pix == 2) && (sample == 55)) {
            //    std::cerr << "log_p_sample_ws[sample+n_samples]   += " << ivar*dx*dx << std::endl;
            //}

            dx = neighbor_pixels.get_delta(pix, sample, dist+1) - mu;
            log_p_sample_ws[sample+2*n_samples] += ivar * dx*dx;
            
            //if((pix == 2) && (sample == 55)) {
            //    std::cerr << "log_p_sample_ws[sample+2*n_samples] += " << ivar*dx*dx << std::endl;
            //    std::cerr << std::endl;
            //}
        }

        // First distance bin
        mu = neighbor_pixels.calc_mean(pix, 0, neighbor_sample);
        ivar = neighbor_pixels.get_inv_var(pix, 0);

        dx = neighbor_pixels.get_delta(pix, sample, 0) - mu;
        log_p_sample_ws[sample] += ivar * dx*dx;
        
        dx = neighbor_pixels.get_delta(pix, sample, 1) - mu;
        log_p_sample_ws[sample+2*n_samples] += ivar * dx*dx;

        // Last distance bin
        mu = neighbor_pixels.calc_mean(pix, n_dists-1, neighbor_sample);
        ivar = neighbor_pixels.get_inv_var(pix, 0);

        dx = neighbor_pixels.get_delta(pix, sample, n_dists-1) - mu;
        log_p_sample_ws[sample] += ivar * dx*dx;
        
        dx = neighbor_pixels.get_delta(pix, sample, n_dists-2) - mu;
        log_p_sample_ws[sample+n_samples] += ivar * dx*dx;

        // Multiply chi^2 by -0.5
        log_p_sample_ws[sample] *= -0.5;
        log_p_sample_ws[sample+n_samples] *= -0.5;
        log_p_sample_ws[sample+2*n_samples] *= -0.5;

        // Subtract log(prior)
        log_p_sample_ws[sample] -= neighbor_pixels.get_prior(pix, sample);
        log_p_sample_ws[sample+n_samples] -= neighbor_pixels.get_prior(pix, sample);
        log_p_sample_ws[sample+2*n_samples] -= neighbor_pixels.get_prior(pix, sample);
    }
    
    // Turn chi^2 into probability
    double log_p_max = *std::max_element(log_p_sample_ws.begin(), log_p_sample_ws.end());
    //double log_p_max_sample, log_p_sample_tmp, p_sample_tmp;
    double xi, zeta;

    double log_a = 0.;          // log(weight) for no shift
    double log_b = log_w_shift; // log(weight) for shift of +-1 distance bin

    for(int sample=0; sample<n_samples; sample++) {
        //p_sample_ws[sample] = std::exp(beta * (log_p_sample_ws[sample] - log_p_max));
    
        //log_p_max_sample = std::max(log_p_sample_ws[sample]+log_a, log_p_sample_ws[sample+n_samples]+log_b);
        //log_p_max_sample = std::max(log_p_max_sample, log_p_sample_ws[sample+2*n_samples]+log_b);
        //
        //log_p_sample_tmp = beta * (log_p_max_sample-log_p_max);
        //p_sample_ws[sample] = std::exp(log_p_sample_tmp);

        //p_sample_tmp = std::exp(log_p_sample_ws[sample] + log_a - log_p_max_sample);
        //p_sample_tmp += std::exp(log_p_sample_ws[sample+n_samples] + log_b - log_p_max_sample);
        //p_sample_tmp += std::exp(log_p_sample_ws[sample+2*n_samples] + log_b - log_p_max_sample);
        //
        //if((pix == 2) && (sample == 55)) {
        //    std::cerr << std::endl;
        //    std::cerr << "beta = " << beta << std::endl;
        //    std::cerr << "log_p_sample_ws[sample] = " << log_p_sample_ws[sample] << std::endl;
        //    std::cerr << "log_p_sample_ws[sample+n_samples] = " << log_p_sample_ws[sample+n_samples] << std::endl;
        //    std::cerr << "log_p_sample_ws[sample+2*n_samples] = " << log_p_sample_ws[sample+2*n_samples] << std::endl;
        //    std::cerr << "log_p_max_sample = " << log_p_max_sample << std::endl;
        //    std::cerr << "log_p_sample_tmp = " << log_p_sample_tmp << std::endl;
        //    std::cerr << "p_sample_tmp = " << p_sample_tmp << std::endl;
        //    std::cerr << std::endl;
        //}

        //p_sample_ws[sample] *= std::pow(p_sample_tmp, beta);
        //log_p_sample_ws[sample] = log_p_sample_tmp + beta * std::log(p_sample_tmp);
        
        //
        log_p_sample_ws[sample] += log_a - log_p_max;
        log_p_sample_ws[sample+n_samples] += log_b - log_p_max;
        log_p_sample_ws[sample+2*n_samples] += log_b - log_p_max;
        
        // The maximum log(p_sample) + log(weight) - log(p_max) for the three shifts
        xi = std::max(log_p_sample_ws[sample], log_p_sample_ws[sample+n_samples]);
        xi = std::max(xi, log_p_sample_ws[sample+2*n_samples]);
    
        // Should be of order unity, but > 1.
        zeta =   std::exp(log_p_sample_ws[sample] - xi)
               + std::exp(log_p_sample_ws[sample+n_samples] - xi)
               + std::exp(log_p_sample_ws[sample+2*n_samples] - xi);

        log_p_sample_ws[sample] = beta * (xi + std::log(zeta));
        p_sample_ws[sample] = log_p_sample_ws[sample];
    }
    //std::cerr << std::endl << std::endl;

    // Choose a sample at random, weighted by the probabilities
    int idx_old = neighbor_sample[pix];

    std::discrete_distribution<> d(p_sample_ws.begin(), p_sample_ws.end());
    int idx = d(r);
    neighbor_sample[pix] = idx;

    //if(pix == 1) {
    //    std::vector<double> p = d.probabilities();
    //    std::sort(p.begin(), p.end());

    //    std::cerr << "Probabilities (beta = " << beta << "):";
    //    for(int i=p.size()-1; (i>0) && (i>p.size()-10); i--) {
    //        std::cerr << " " << p.at(i);
    //    }
    //    std::cerr << std::endl;

    //    double P_tot = 0;
    //    int n_eff = 0;
    //    for(auto pp = p.rbegin(); (pp != p.rend()) && (P_tot < 0.99); ++pp, ++n_eff) {
    //        P_tot += *pp;
    //    }
    //    std::cerr << "n_eff(" << pix << ") = " << n_eff << std::endl;
    //}
    //std::cerr << std::endl;
    
    return (log_p_sample_ws[idx] - log_p_sample_ws[idx_old]) / beta;
}


void TDiscreteLosMcmcParams::initialize_priors(
        TGalacticLOSModel& gal_los_model,
        double log_Delta_EBV_floor,
        double log_Delta_EBV_ceil,
        double sigma_log_Delta_EBV,
        int verbosity)
{
    // Calculate log-normal prior on reddening increase at each distance
    std::cerr << "Initializing discrete l.o.s. priors ..." << std::endl;
    std::vector<double> lnZ_dummy;
    TLOSMCMCParams los_params(
        img_stack.get(),
        lnZ_dummy,
        0., 11, 1,
        n_dists
    );
    los_params.alpha_skew = 0.;
    set_sigma_log_dE(sigma_log_Delta_EBV);
    los_params.calc_Delta_EBV_prior(
        gal_los_model,
        log_Delta_EBV_floor,
        log_Delta_EBV_ceil,
        0.,
        sigma_log_dE,
        verbosity
    );

    mu_log_dE_0.reserve(n_dists);
    sigma_log_dE_0.reserve(n_dists);
    for(int i=0; i<n_dists; i++) {
        mu_log_dE_0.push_back(los_params.log_Delta_EBV_prior[i]);
        sigma_log_dE_0.push_back(los_params.sigma_log_Delta_EBV[i]);
    }
    
    if(neighbor_pixels) {
        p_sample.resize(neighbor_pixels->get_n_samples());
        log_p_sample.resize(neighbor_pixels->get_n_samples());

        // Scale reddening values in neighbor pixels
        neighbor_pixels->apply_priors(
            mu_log_dE_0,
            sigma_log_dE_0,
            img_stack->rect->dx[0]);
    }

    if(!neighbor_pixels) {
        std::vector<uint16_t> neighbors_sample_tmp;
        update_priors_image(
            neighbors_sample_tmp,
            0.,
            priors_subsampling,
            -1.,
            verbosity
        );
    }

    std::cerr << "Done initializing discrete l.o.s. priors ..." << std::endl;
}


void TDiscreteLosMcmcParams::update_priors_image(
        std::vector<uint16_t>& neighbor_sample,
        double alpha_skew,
        int subsampling,
        const double shift_weight,
        int verbosity)
{
    update_priors_image(
        *log_P_dy,
        neighbor_sample,
        alpha_skew,
        subsampling,
        shift_weight,
        verbosity
    );
}


void TDiscreteLosMcmcParams::update_priors_image(
        cv::Mat& img,
        std::vector<uint16_t>& neighbor_sample,
        double alpha_skew,
        int subsampling,
        const double shift_weight,
        int verbosity)
{
    // Evaluate the probability mass for each (reddening jump, distance)
    img = cv::Mat::zeros(n_E, n_dists, CV_FLOATING_TYPE);

    double dE0, dE, P_dist;
    
    // Cache value of log(dE) for each dE
    std::vector<double> log_dE_cache;
    log_dE_cache.reserve(n_E * subsampling);
    for(int y=0; y<n_E; y++) {
        dE0 = y * img_stack->rect->dx[0];
        for(int k=0; k<subsampling; k++) {
            dE = dE0 + (double)k / (double)subsampling * img_stack->rect->dx[0];
            log_dE_cache.push_back(std::log(dE));
        }
    }
    
    for(int x=0; x<n_dists; x++) {
        // Calculate <log(dE)> and sigma_{log(dE)} at this distance
        double mu, inv_var;
        if(neighbor_pixels) {
            inv_var = neighbor_pixels->get_inv_var(0, x);

            if(shift_weight > 0.) {
                mu = neighbor_pixels->calc_mean_shifted(
                    0, x, neighbor_sample, shift_weight);
            } else {
                mu = neighbor_pixels->calc_mean(0, x, neighbor_sample);
            }
            //if(shift_weight < std::exp(-1.9)) {
            //    std::cerr << "x = " << x << std::endl
            //              << "  * mu = " << mu << std::endl
            //              << "  * sigma = " << 1./std::sqrt(inv_var) << std::endl;
            //}
        } else {
            mu = 0.;
            inv_var = 1.;
        }
        double sigma_0 = sigma_log_dE_0.at(x);
        mu *= sigma_0;
        mu += mu_log_dE_0.at(x);
        inv_var /= sigma_0*sigma_0;
        
        // Calculate the probability of each reddening jump at this distance
        P_dist = 0.;

        double log_scale_tmp = std::log(img_stack->rect->dx[0]);
        
        // Handle dy = 0 as a special case
        if(mu < log_scale_tmp) {
            // 1.5 is a fudge factor
            img.at<floating_t>(0, x) = subsampling / img_stack->rect->dx[0] * 1.5;
            P_dist += img.at<floating_t>(0, x);
        } else {
            double delta = log_scale_tmp - mu;
            img.at<floating_t>(0, x) = subsampling * std::exp(-0.5*delta*delta*inv_var)
                                       / img_stack->rect->dx[0] * 1.5;
            P_dist += img.at<floating_t>(0, x);
        }
        
        // Handle dy > 0
        for(int y=1; y<n_E; y++) {
            dE0 = y * img_stack->rect->dx[0];
            for(int k=0; k<subsampling; k++) {
                dE = dE0 + (double)k / (double)subsampling * img_stack->rect->dx[0];
                double log_dE = log_dE_cache[y*subsampling+k];
                if(std::isfinite(log_dE)) {//dE > 0) {
                    double delta = log_dE - mu;
                    double P_tmp = std::exp(-0.5 * delta*delta * inv_var);
                    // TODO: Remove alpha_skew parameter & option
                    //P_tmp *= 1. + erf(alpha_skew * score * INV_SQRT2);
                    P_tmp /= dE;
                    img.at<floating_t>(y, x) += P_tmp;
                }
            }
            // P_dy->at<floating_t>(y, x) /= subsampling;
            P_dist += img.at<floating_t>(y, x);
        }

        //if(mu < log_scale_tmp) {
        //    std::cerr << "mu_{" << x << "} = " << mu << " < " << log_scale_tmp << std::endl;
        //    for(int y=0; y<10; y++) {
        //        std::cerr << "P_{" << x << ", " << y << "} = " << img.at<floating_t>(y, x) << std::endl;
        //    }
        //}
        
        // Normalize total probability at this distance to unity
        for(int y=0; y<n_E; y++) {
            if(P_dist > 0) {
                img.at<floating_t>(y, x) /= P_dist;
            }
            img.at<floating_t>(y, x) = std::log(img.at<floating_t>(y, x));
            if(img.at<floating_t>(y, x) < -100.) {
                img.at<floating_t>(y, x) = -100. - 0.01 * y*y;
            }
        }
        
        if(img.at<floating_t>(0, x) <= -99.999) {
            img.at<floating_t>(0, x) = 0.;
        }
        
        //if(mu < log_scale_tmp) {
        //    for(int y=0; y<10; y++) {
        //        std::cerr << "ln P'_{" << x << ", " << y << "} = " << img.at<floating_t>(y, x) << std::endl;
        //    }
        //}
    }

    //if(verbosity >= 2) {
    //    std::cerr << std::endl
    //              << "log P(dy):" << std::endl
    //              << "==========" << std::endl;
    //    for(int x=0; x<n_dists; x+=5) {
    //        for(int y=0; y<10; y++) {
    //            std::cerr << img.at<floating_t>(y, x) << "  ";
    //        }
    //        std::cerr << "... ";
    //        for(int y=15; y<=30; y+=5) {
    //            std::cerr << img.at<floating_t>(y, x) << "  ";
    //        }
    //        std::cerr << std::endl;
    //    }
    //    std::cerr << std::endl;
    //}

    //std::cerr << "Done initializing discrete l.o.s. priors." << std::endl;
}


double* TDiscreteLosMcmcParams::get_line_int(unsigned int thread_num) {
    assert(thread_num < N_threads);
    return line_int + img_stack->N_images * thread_num;
}


int16_t* TDiscreteLosMcmcParams::get_E_pix_idx(unsigned int thread_num) {
    assert(thread_num < N_threads);
    return E_pix_idx + n_dists * thread_num;
}


// Calculates the line integrals for a model in which each distance bin has a
// (possibly) different, constant reddening.
//
// Reddening is discretized, so that it is described by an integer, called the
// y-value or -index. The distance is called the x-value or -index.
//
// Inputs:
//   y_idx : Pointer to array containing the y-value in each distance bin.
//   line_int_ret : Pointer to array that will store the line integrals.
//
void TDiscreteLosMcmcParams::los_integral_discrete(
        const int16_t *const y_idx,
        double *const line_int_ret)
{
    // For each image
    for(int k = 0; k < img_stack->N_images; k++) {
        line_int_ret[k] = 0.;

        // For each distance
        for(int j = 0; j < n_dists; j++) {
            line_int_ret[k] += (double)img_stack->img[k]->at<floating_t>(y_idx[j], j);
        }

        // line_int_ret[k] *= img_stack->rect->dx[1];   // Multiply by dDM
    }
    // line_int_ret[0] = 1.; // TODO: remove this line.
}


// Calculates the change to the line integrals for a step that changes the
// value of E in one distance bin.
//
// Inputs:
//   x_idx : Index of distance bin to change.
//   y_idx_old : Old y-value of the given distance bin.
//   y_idx_new : New y-value of the given distance bin.
//   delta_line_int_ret : Pointer to array that will store updated line integrals.
//
void TDiscreteLosMcmcParams::los_integral_diff_step(
        const int16_t x_idx,
        const int16_t y_idx_old,
        const int16_t y_idx_new,
        double *const delta_line_int_ret)
{
    // For each image
    for(int k=0; k < img_stack->N_images; k++) {
        delta_line_int_ret[k] = (double)img_stack->img[k]->at<floating_t>(y_idx_new, x_idx)
                              - (double)img_stack->img[k]->at<floating_t>(y_idx_old, x_idx);
        // delta_line_int_ret[k] *= img_stack->rect->dx[1]; // Multiply by dDM
    }
}


floating_t TDiscreteLosMcmcParams::log_dy_prior(
    const int16_t x_idx,
    const int16_t dy,
    const cv::Mat& lnP_dy)
{
    // if(dy > 0) {
    //     float dxi = inv_sigma_log_dy * (log((float)dy) - mu_log_dy);
    //     return -0.5 * dxi*dxi;
    // } else if(dy == 0) {
    //     float dxi = inv_sigma_log_dy * mu_log_dy;
    //     return -0.5 * dxi*dxi;
    // } else {
    //     float dxi = inv_sigma_dy_neg * (float)dy;
    //     return -0.5 * dxi*dxi;
    // }
    if((dy < 0) || (dy >= lnP_dy.cols)) {
        return -std::numeric_limits<floating_t>::infinity();
    } else {
        return lnP_dy.at<floating_t>(dy, x_idx);
    }
}


floating_t TDiscreteLosMcmcParams::log_dy_prior(
    const int16_t x_idx,
    const int16_t dy)
{
    return log_dy_prior(x_idx, dy, *log_P_dy);
}


floating_t TDiscreteLosMcmcParams::log_prior(
    const int16_t *const y_idx,
    const cv::Mat& lnP_dy)
{
    floating_t dy = y_idx[0] - (int)y_zero_idx;
    floating_t log_p = log_dy_prior(0, dy, lnP_dy);

    for(int x=1; x<n_dists; x++) {
        dy = y_idx[x] - y_idx[x-1];
        log_p += log_dy_prior(x, dy, lnP_dy);
    }

    return log_p;
}


floating_t TDiscreteLosMcmcParams::log_prior(const int16_t *const y_idx) {
    return log_prior(y_idx, *log_P_dy);
}


floating_t TDiscreteLosMcmcParams::log_prior_diff_step(
        const int16_t x_idx,
        const int16_t *const y_idx_los_old,
        const int16_t y_idx_new,
        const cv::Mat& lnP_dy)
{
    // Left side
    int16_t dy_old = y_idx_los_old[x_idx];
    int16_t dy_new = y_idx_new;

    if(x_idx != 0) {
        dy_old -= y_idx_los_old[x_idx-1];
        dy_new -= y_idx_los_old[x_idx-1];
    } else {
        dy_old -= (int)y_zero_idx;
        dy_new -= (int)y_zero_idx;
    }

    floating_t dlog_prior = log_dy_prior(x_idx, dy_new, lnP_dy)
                          - log_dy_prior(x_idx, dy_old, lnP_dy);

    // Right side
    if(x_idx != n_dists-1) {
        dy_old = y_idx_los_old[x_idx+1] - y_idx_los_old[x_idx];
        dy_new = y_idx_los_old[x_idx+1] - y_idx_new;

        dlog_prior += log_dy_prior(x_idx+1, dy_new, lnP_dy)
                    - log_dy_prior(x_idx+1, dy_old, lnP_dy);
    }

    return dlog_prior;
}


floating_t TDiscreteLosMcmcParams::log_prior_diff_step(
        const int16_t x_idx,
        const int16_t *const y_idx_los_old,
        const int16_t y_idx_new)
{
    return log_prior_diff_step(x_idx, y_idx_los_old, y_idx_new, *log_P_dy);
}


floating_t TDiscreteLosMcmcParams::log_prior_diff_swap(
        const int16_t x0_idx,
        const int16_t *const y_idx_los_old,
        const cv::Mat& lnP_dy)
{
    floating_t y_left = y_idx_los_old[x0_idx-1];
    floating_t y0 = y_idx_los_old[x0_idx];
    floating_t y_right = y_idx_los_old[x0_idx+1];
    floating_t dy_left = y0 - y_left;
    floating_t dy_right = y_right - y0;

    floating_t dlog_prior = log_dy_prior(x0_idx, dy_right, lnP_dy)
                     + log_dy_prior(x0_idx+1, dy_left, lnP_dy)
                     - log_dy_prior(x0_idx, dy_left, lnP_dy)
                     - log_dy_prior(x0_idx+1, dy_right, lnP_dy);
    // return 0.;
    return dlog_prior;
}


floating_t TDiscreteLosMcmcParams::log_prior_diff_swap(
        const int16_t x0_idx,
        const int16_t *const y_idx_los_old)
{
    return log_prior_diff_swap(x0_idx, y_idx_los_old, *log_P_dy);
}

// Calculates the change to the line integrals for a step that swaps the
// values of E in two neighboring distance bins.
//
// Inputs:
//   x0_idx : Index of left distance bin. Will be swapped with bin to the right.
//   y_idx  : y-values in all the distance bins.
//   delta_line_int_ret : Pointer to array that will store updated line integrals.
//
void TDiscreteLosMcmcParams::los_integral_diff_swap(
        const int16_t x0_idx,
        const int16_t *const y_idx,
        double *const delta_line_int_ret)
{
    int16_t dy = y_idx[x0_idx+1] - y_idx[x0_idx];
    int16_t y_old = y_idx[x0_idx];
    int16_t y_new = y_idx[x0_idx-1] + dy;

    // For each image
    for(int k = 0; k < img_stack->N_images; k++) {
        delta_line_int_ret[k] = (double)img_stack->img[k]->at<floating_t>(y_new, x0_idx)
                              - (double)img_stack->img[k]->at<floating_t>(y_old, x0_idx);
        // delta_line_int_ret[k] *= img_stack->rect->dx[1]; // Multiply by dDM
    }
}


bool TDiscreteLosMcmcParams::shift_r_step_valid(
        const int16_t x_idx,
        const int16_t dy,
        const int16_t *const y_idx_old) {
    // Determine whether shift causes y to go above maximum or below zero
    for(int j=x_idx; j<n_dists; j++) {
        if((y_idx_old[j]+dy < 0) || (y_idx_old[j]+dy >= n_E)) {
            return false;
        }
    }
    return true;
}


bool TDiscreteLosMcmcParams::shift_l_step_valid(
        const int16_t x_idx,
        const int16_t dy,
        const int16_t *const y_idx_old) {
    // Determine whether shift causes y to go above maximum or below zero
    for(int j=0; j<=x_idx; j++) {
        if((y_idx_old[j]+dy < 0) || (y_idx_old[j]+dy >= n_E)) {
            return false;
        }
    }
    return true;
}


void TDiscreteLosMcmcParams::los_integral_diff_shift_r(
        const int16_t x_idx,
        const int16_t dy,
        const int16_t *const y_idx_old,
        double *const delta_line_int_ret) {
    // Determine whether shift causes y to go above maximum or below zero
    // for(int j=x_idx; j<n_dists; j++) {
    //  if((y_idx_old[j]+dy < 0) || (y_idx_old[j]+dy >= n_E)) {
    //      // Set line integrals to -infinity
    //      for(int k=0; k < img_stack->N_images; k++) {
    //          delta_line_int_ret[k] = -std::numeric_limits<floating_t>::infinity();
    //      }
    //      // Bail out before calculating line integrals, which will try
    //      // to access out-of-bounds memory.
    //      return;
    //  }
    // }

    // Determine difference in line integral

    // For each image
    for(int k=0; k < img_stack->N_images; k++) {
        delta_line_int_ret[k] = 0;

        // For each distance
        for(int j=x_idx; j<n_dists; j++) {
            delta_line_int_ret[k] +=
                  (double)img_stack->img[k]->at<floating_t>(y_idx_old[j]+dy, j)
                - (double)img_stack->img[k]->at<floating_t>(y_idx_old[j], j);
        }
    }
}

void TDiscreteLosMcmcParams::los_integral_diff_shift_l(
        const int16_t x_idx,
        const int16_t dy,
        const int16_t *const y_idx_old,
        double *const delta_line_int_ret) {
    // Determine difference in line integral
    // For each image
    for(int k=0; k < img_stack->N_images; k++) {
        delta_line_int_ret[k] = 0;

        // For each distance
        for(int j=0; j<=x_idx; j++) {
            delta_line_int_ret[k] +=
                  (double)img_stack->img[k]->at<floating_t>(y_idx_old[j]+dy, j)
                - (double)img_stack->img[k]->at<floating_t>(y_idx_old[j], j);
        }
    }
}

void TDiscreteLosMcmcParams::los_integral_diff_shift_compare_operations(
        const int16_t x_idx,
        const int16_t dy,
        const int16_t *const y_idx_old,
        unsigned int& n_eval_diff,
        unsigned int& n_eval_cumulative) {
    // Determine number of jumps in old and new line-of-sight reddening profile
    n_eval_diff = 2 * (n_dists - x_idx);
    n_eval_cumulative = 4;

    for(int j=x_idx+1; j<n_dists-1; j++) {
        if(y_idx_old[j] != y_idx_old[j-1]) {
            n_eval_cumulative += 2;
        }
    }
}


floating_t TDiscreteLosMcmcParams::log_prior_diff_shift_l(
        const int16_t x_idx,
        const int16_t dy,
        const int16_t *const y_idx_los_old,
        const cv::Mat& lnP_dy)
{
    // Prior changes at x-index of shift and at x=0
    int16_t dy_old = y_idx_los_old[x_idx+1] - y_idx_los_old[x_idx];

    floating_t dlog_prior = log_dy_prior(x_idx+1, dy_old-dy, lnP_dy)
                          - log_dy_prior(x_idx+1, dy_old, lnP_dy)
                          + log_dy_prior(0, y_idx_los_old[0]+dy, lnP_dy)
                          - log_dy_prior(0, y_idx_los_old[0], lnP_dy);

    return dlog_prior;
}


floating_t TDiscreteLosMcmcParams::log_prior_diff_shift_l(
        const int16_t x_idx,
        const int16_t dy,
        const int16_t *const y_idx_los_old)
{
    return log_prior_diff_shift_l(x_idx, dy, y_idx_los_old, *log_P_dy);
}


floating_t TDiscreteLosMcmcParams::log_prior_diff_shift_r(
        const int16_t x_idx,
        const int16_t dy,
        const int16_t *const y_idx_los_old,
        const cv::Mat& lnP_dy)
{
    // Prior only changes at x-index of shift
    int16_t dy_old = y_idx_los_old[x_idx];

    if(x_idx != 0) {
        dy_old -= y_idx_los_old[x_idx-1];
    } else {
        dy_old -= (int)y_zero_idx;
    }

    floating_t dlog_prior = log_dy_prior(x_idx, dy_old+dy, lnP_dy)
                          - log_dy_prior(x_idx, dy_old, lnP_dy);

    return dlog_prior;
}


floating_t TDiscreteLosMcmcParams::log_prior_diff_shift_r(
        const int16_t x_idx,
        const int16_t dy,
        const int16_t *const y_idx_los_old)
{
    return log_prior_diff_shift_r(x_idx, dy, y_idx_los_old, *log_P_dy);
}


void TDiscreteLosMcmcParams::guess_EBV_profile_discrete(int16_t *const y_idx_ret, gsl_rng *r) {
    double EBV_max_guess = guess_EBV_max(*img_stack) * (0.8 + 0.4 * gsl_rng_uniform(r));

    int n_x = n_dists; //img_stack->rect->N_bins[1];
    int n_y = n_E; //img_stack->rect->N_bins[0];
    double dy = img_stack->rect->dx[0];

    double *y = new double[n_x];
    y[0] = gsl_ran_chisq(r, 1);

    for(int i = 1; i < n_x; i++) {
        y[i] = y[i-1] + gsl_ran_chisq(r, 1);
    }

    double y_scale = (EBV_max_guess / y[n_x - 1]) / dy;

    for(int i = 0; i < n_x; i++) {
        y_idx_ret[i] = (int16_t)ceil(y[i] * y_scale + y_zero_idx);

        if(y_idx_ret[i] >= n_y) {
            y_idx_ret[i] = (int16_t)(n_y - 1);
        }
    }

    delete[] y;
}


void ascii_progressbar(
        int state,
        int max_state,
        int width,
        double t_elapsed,
        std::ostream& out,
        bool terminate=true,
        bool rollback=false)
{
    double pct = (double)state / (double)(max_state-1);
    int n_ticks = pct * width;

    if(rollback) {
        out << ' ' << '\r';
    }

    out << "|";
    for(int i=0; i<n_ticks-1; i++) {
        out << "=";
    }
    if(n_ticks != 0) {
        out << ">";
    }
    for(int i=n_ticks; i<width; i++) {
        out << " ";
    }
    
    out << "| " << round(100. * pct) << " % "
        << "| " << round(t_elapsed) << " s elapsed "
        << "| " << round(t_elapsed * (1./pct - 1.)) << " s remaining";
    if(terminate) {
        out << std::endl;
    }
}

void discrete_los_ascii_art(int n_x, int n_y, int16_t *y_idx,
                            int img_y, int max_y, double dy,
                            double x_min, double x_max,
                            std::ostream& out) {
    // Padding for labels
    int pad_x = 8;
    int pad_y = 4;

    // Empty image
    int row_width = n_x + pad_x + 1;
    int n_rows = img_y + pad_y;
    int n_pix = row_width * n_rows;
    char *ascii_img = new char[n_pix];

    for(int k = 0; k < n_pix; k++) {
        ascii_img[k] = ' ';
    }

    //std::cerr << "# of pixels: " << n_pix << std::endl;

    // Scaling of x- and y-axes
    double x_scale = (x_max - x_min) / (double)n_x;
    double y_scale = (double)(img_y-1) / (double)(max_y-1);

    // Fill in each column
    int row, idx;

    for(int k = 0; k < n_x; k++) {
        if(y_idx[k] < max_y) {
            row = img_y - int((double)(y_idx[k]) * y_scale) - 1;
            idx = row_width * row + k;
            ascii_img[idx] = '*';
        }
        //std::cerr << "(col, row) = (" << k << ", " << row << ") --> " << idx << std::endl;
    }

    // Add in y labels
    for(int k = 0; k < img_y; k++) {
        idx = row_width * k + n_x + 1;
        ascii_img[idx] = '|';
    }

    for(int k=img_y-1; k>=0; k-=5) {
        idx = row_width * k  + n_x + 2;
        ascii_img[idx] = '-';

        const char* fmt = "%4.2f";
        double y_label = (img_y - 1 - k) * dy / y_scale;
        int label_len = std::snprintf(nullptr, 0, fmt, y_label);
        std::vector<char> buf(label_len + 1);
        std::snprintf(&buf[0], buf.size(), fmt, y_label);

        idx += 2;
        for(int j=0; j<4; j++) {
            ascii_img[idx + j] = buf[j];
        }
    }

    // Add in x labels
    for(int k=0; k<n_x+2; k++) {
        row = img_y;
        idx = row_width * row + k;
        ascii_img[idx] = '-';
    }

    for(int k=10; k<n_x; k+=20) {
        idx = row_width * (img_y+1) + k;
        ascii_img[idx] = '|';

        const char* fmt = "%4.1f";
        double x_label = x_min + k * x_scale;
        int label_len = std::snprintf(nullptr, 0, fmt, x_label);
        std::vector<char> buf(label_len + 1);
        std::snprintf(&buf[0], buf.size(), fmt, x_label);

        idx += row_width - 2;
        for(int j=0; j<4; j++) {
            ascii_img[idx + j] = buf[j];
        }
    }

    // Add endlines to rows
    for(int k = 0; k < n_rows; k++) {
        idx = row_width * (k+1) - 1;
        //std::cerr << "Adding endline " << k << ") --> " << idx << std::endl;
        ascii_img[idx] = '\n';
    }

    // Terminate with null char
    //std::cerr << "Adding 0 at end of char array" << std::endl;
    ascii_img[n_pix - 1] = 0;

    // Print and cleanup
    //std::cerr << "Printing char" << std::endl;
    out << ascii_img << "\n";

    //std::cerr << "done with ascii art" << std::endl;

    delete[] ascii_img;

    //std::cerr << "done with ascii art cleanup" << std::endl;
}


const int N_PROPOSAL_TYPES = 6;
const int STEP_PROPOSAL = 0;
const int SWAP_PROPOSAL = 1;
const int SHIFT_L_PROPOSAL = 2;
const int SHIFT_R_PROPOSAL = 3;
const int SHIFT_ABS_L_PROPOSAL = 4;
const int SHIFT_ABS_R_PROPOSAL = 5;


struct DiscreteProposal {
    // DiscreteProposal(gsl_rng* r);
    void roll(gsl_rng* r);
    void set(bool _step, bool _swap, bool _shift,
             bool _left, bool _absolute, int _code);
    bool step, swap, shift, left, absolute;
    int code;
};

void DiscreteProposal::set(bool _step, bool _swap, bool _shift,
                           bool _left, bool _absolute, int _code)
{
    step = _step;
    swap = _swap;
    shift = _shift;
    left = _left;
    absolute = _absolute;
    code = _code;
}

void DiscreteProposal::roll(gsl_rng* r) {
    int p = gsl_rng_uniform_int(r, 12);
    if(p < 4) {
        set(true, false, false, false, false, STEP_PROPOSAL);
    } else if(p < 8) {
        set(false, true, false, false, false, SWAP_PROPOSAL);
    } else if(p == 8) {
        set(false, false, true, true, false, SHIFT_L_PROPOSAL);
    } else if(p == 9) {
        set(false, false, true, false, false, SHIFT_R_PROPOSAL);
    } else if(p == 10) {
        set(false, false, true, true, true, SHIFT_ABS_L_PROPOSAL);
    } else {
        set(false, false, true, false, true, SHIFT_ABS_R_PROPOSAL);
    }
}


// Propose to take a step up or down in one pixel
void discrete_propose_step(gsl_rng* r, int n_x, int& x_idx, int& dy) {
    x_idx = gsl_rng_uniform_int(r, n_x);    // Random distance bin: [0, nx-1]
    dy = 2 * gsl_rng_uniform_int(r, 2) - 1; // Step up or down one unit
}


// Propose to swap differential reddening btw/ two neighboring distance bins
void discrete_propose_swap(gsl_rng* r, int n_x, int& x_idx) {
    // Random distance bin: [1, nx-2]
    x_idx = gsl_rng_uniform_int(r, n_x-2) + 1;
}


// Propose to take a shift step up or down in all pixels beyond
// a certain distance
void discrete_propose_shift(gsl_rng* r, int n_x, int& x_idx, int& dy) {
    x_idx = gsl_rng_uniform_int(r, n_x-1);  // Random distance bin: [0, nx-2]
    dy = 2 * gsl_rng_uniform_int(r, 2) - 1; // Step up or down one unit
}


double gen_exponential_variate(gsl_rng* r, double lambda, double tau) {
    double u = gsl_rng_uniform(r);
    // std::cerr << u << std::endl;
    // std::cerr << " -lambda*tau = " << -lambda * tau << std::endl;
    // std::cerr << " lambda = " << lambda << std::endl;
    return -log(1. - (1. - exp(-lambda * tau)) * u) / lambda;
}


// double p_exponential(double x, double lambda) {
//  return exp(-lambda * x);
// }


void discrete_propose_shift_abs(
    gsl_rng* r,
    int16_t* y_idx,
    int n_x,
    double y_mean,
    double y_max,
    int& x_idx,
    int& dy,
    double& ln_proposal_factor
) {
    x_idx = gsl_rng_uniform_int(r, n_x-1);  // Random distance bin: [0, nx-2]
    double lambda = 1. / y_mean;
    int y = (int)gen_exponential_variate(r, lambda, y_max);
    // if((y < 0) || (y >= 700)) {
    //  std::cerr << "y = " << y << std::endl;
    //  std::exit(1);
    // }
    // if((x_idx < 0) || (x_idx > n_x-2)) {
    //  std::cerr << "x = " << x_idx << std::endl;
    //  std::exit(1);
    // }
    dy = y - y_idx[x_idx];
    ln_proposal_factor = lambda * dy;
}


// Checks whether a proposal lands in a valid region of parameter space
bool discrete_proposal_valid(
        DiscreteProposal& proposal_type,
        int y_idx_new, int n_y,
        TDiscreteLosMcmcParams& params,
        int x_idx, int dy, const int16_t *const y_idx_los_old)
{
    // STEP_PROPOSAL or SWAP_PROPOSAL
    if(!proposal_type.shift) {
        return (y_idx_new >= 0) && (y_idx_new < n_y);
    } else if(proposal_type.left) {
        // SHIFT_L_PROPOSAL or SHIFT_ABS_L_PROPOSAL
        return params.shift_l_step_valid(x_idx, dy, y_idx_los_old);
    } else {
        // SHIFT_R_PROPOSAL or SHIFT_ABS_R_PROPOSAL
        return params.shift_r_step_valid(x_idx, dy, y_idx_los_old);
    }
}


void sample_los_extinction_discrete(
        const std::string& out_fname,
        const std::string& group_name,
        TMCMCOptions& options,
        TDiscreteLosMcmcParams& params,
        const std::vector<uint16_t>& neighbor_sample,
        const TDiscreteLOSSamplingSettings& s,
        int verbosity)
{
    std::cerr << "Beginning to sample discrete l.o.s. model ..." << std::endl;
    
    //
    // Parallel tempering parameters
    //
    // -> Taken from TDiscreteSamplingSettings& s

    //
    // Stellar PDF image parameters
    //
    int n_x = params.img_stack->rect->N_bins[1];    // # of distance pixels
    int n_y = params.img_stack->rect->N_bins[0];    // # of reddening pixels
    int n_stars = params.img_stack->N_images;       // # of stars

    //
    // Derived sampling parameters
    //
    int save_every = s.n_swaps / s.n_save; // Save one sample every # of swaps
    int save_in = save_every; // Counts down until next saved sample
    int n_saved = 0;
    
    int n_swaps_burnin = s.burnin_frac * s.n_swaps;
    int n_swaps = s.n_swaps + n_swaps_burnin;
    int central_steps_per_update = s.central_steps_per_update * n_x;
    
    if(verbosity >= 2) {
        std::cerr << "Total # of central steps: "
                  << n_swaps
                     * s.updates_per_swap
                     * central_steps_per_update
                  << std::endl;
    }
    
    //int n_steps = options.steps * n_x;
    //int n_burnin = 0.25 * n_steps;

    //int n_neighbor_steps = 500; // TODO: Make this adjustable?
    //int neighbor_step_every = n_steps / n_neighbor_steps;
    //int neighbor_step_in = 1;
    //
    //int n_save = 500;
    //int save_every = n_steps / n_save;
    //int save_in = save_every;

    // How often to recalculate exact line integrals
    int recalculate_every = 1000;
    std::vector<int> recalculate_in(s.n_temperatures, recalculate_every);
    
    // GSL random number generator
    gsl_rng *r;
    seed_gsl_rng(&r);
    
    // Temperature ladder
    std::vector<double> beta;
    beta.reserve(s.n_temperatures);
    double b = 1.0;
    for(int t=0; t<s.n_temperatures; t++, b*=s.beta_spacing) {
        beta.push_back(b);
    }
    
    // Shift-weight ladder
    std::vector<double> shift_weight_ladder;
    shift_weight_ladder.reserve(s.n_temperatures);
    double ln_sw_min = s.log_shift_weight_min;
    double ln_sw_max = s.log_shift_weight_max;
    if(s.shift_weight_ladder_logarithmic) {
        double dlnsw = (ln_sw_max - ln_sw_min) / (s.n_temperatures - 1);
        double ln_sw = ln_sw_min;
        for(int t=0; t<s.n_temperatures; t++, ln_sw+=dlnsw) {
            double sw = std::exp(ln_sw);
            shift_weight_ladder.push_back(sw);
            if(verbosity >= 1) {
                std::cerr << "shift_weight_" << t << " = " << sw << std::endl;
            }
        }
    } else {
        double sw_min = std::exp(ln_sw_min);
        double sw_max = std::exp(ln_sw_max);
        double dsw = (sw_max - sw_min) / (s.n_temperatures - 1);
        double sw = sw_min;
        for(int t=0; t<s.n_temperatures; t++, sw+=dsw) {
            shift_weight_ladder.push_back(sw);
            if(verbosity >= 1) {
                std::cerr << "shift_weight_" << t << " = " << sw << std::endl;
            }
        }
    }

    // Temporary variables
    std::vector<double> log_p(s.n_temperatures, 0.);
    std::vector<double> logPr(s.n_temperatures, 0.);
    std::vector<double> logL(s.n_temperatures, 0.);
    double dlog_p;
    //double log_p = 0;
    //double logL = 0;
    //double logPr = 0;
    double ln_proposal_factor = 0;
    
    //
    // Temporary variables for line integrals, etc.
    //
    std::vector<std::unique_ptr<std::vector<double>>> line_int;
    std::vector<double> delta_line_int(n_stars, 0.);
    line_int.reserve(s.n_temperatures);
    for(int t=0; t<s.n_temperatures; t++) {
        line_int.push_back(
            std::make_unique<std::vector<double>>(n_stars, 0.)
        );
    }
    std::vector<double> line_int_test(n_stars, 0.);
    //std::vector<double> line_int_test_old(n_stars, 0.);
    
    //double* line_int = new double[n_stars];
    //double* delta_line_int = new double[n_stars];

    //double* line_int_test = new double[n_stars];
    //double* line_int_test_old = new double[n_stars];

    // Calculate line integrals with correct line-of-sight distribution
    // int16_t* y_idx_true = new int16_t[n_x];
    // double* line_int_true = new double[n_stars];
    //
    // for(int j=0; j<24; j++) {
    //  y_idx_true[j] = 0;
    // }
    // for(int j=24; j<72; j++) {
    //  y_idx_true[j] = 100;
    // }
    // for(int j=72; j<n_x; j++) {
    //  y_idx_true[j] = 150;
    // }
    //
    // params.los_integral_discrete(y_idx_true, line_int_true);
    //
    // std::cerr << "true l.o.s. integral (0) = "
    //           << line_int_true[0] << std::endl;

    //
    // Reddening profile
    //
    std::cerr << "Set up reddening profile" << std::endl;
    std::vector<std::unique_ptr<std::vector<int16_t>>> y_idx;
    std::vector<double> y_idx_dbl(n_x, 0.);
    y_idx.reserve(s.n_temperatures);
    for(int t=0; t<s.n_temperatures; t++) {
        if(t == 0) {
            y_idx.push_back(
                std::make_unique<std::vector<int16_t>>(n_x, 0)
            );
            // Guess reddening profile
            params.guess_EBV_profile_discrete(y_idx.at(0)->data(), r);
        } else {
            y_idx.push_back(
                std::make_unique<std::vector<int16_t>>(
                    y_idx.at(0)->begin(),
                    y_idx.at(0)->end()
                )
            );
        }
    }
    //int16_t* y_idx = new int16_t[n_x];
    //double* y_idx_dbl = new double[n_x];
    //params.guess_EBV_profile_discrete(y_idx, r);

    // Calculate initial line integral for each star
    std::cerr << "Calculate initial line integral for each star" << std::endl;
    params.los_integral_discrete(
        y_idx.at(0)->data(),
        line_int.at(0)->data()
    );
    for(int t=1; t<s.n_temperatures; t++) {
        for(int s=0; s<n_stars; s++) {
            line_int.at(t)->at(s) = line_int.at(0)->at(s);
        }
    }

    // params.los_integral_discrete(y_idx, line_int_test_old);

    // for(int k = 0; k < n_x; k++) {
    //     y_idx_dbl[k] = (double)(y_idx[k]);
    // }

    // Number of steps, samples to save, etc.
    //int n_steps = options.steps * n_x;
    //int n_burnin = 0.25 * n_steps;

    //int n_neighbor_steps = 500; // TODO: Make this adjustable?
    //int neighbor_step_every = n_steps / n_neighbor_steps;
    //int neighbor_step_in = 1;
    //
    //int s.n_save = 500;
    //int save_every = n_steps / n_save;
    //int save_in = save_every;

    // Acceptance statistics
    int64_t n_proposals[] = {0, 0, 0, 0, 0, 0},
            n_proposals_accepted[] = {0, 0, 0, 0, 0, 0},
            n_proposals_valid[] = {0, 0, 0, 0, 0, 0};

    // Chain
    int n_save_buffered = 1.1*s.n_save + 5; // Number to save, + some margin
    
    std::vector<std::unique_ptr<TChain>> chain;
    chain.push_back(std::make_unique<TChain>(n_x, n_save_buffered));
    if(s.save_all_temperatures) {
        for(int t=1; t<s.n_temperatures; t++) {
            chain.push_back(std::make_unique<TChain>(n_x, n_save_buffered));
        }
    }

    // Neighboring pixels
    //unsigned int n_temperatures = 4;
    //double beta_spacing = 0.50; // Spacing of sampling temperatures (0<x<1. 1 = degenerate)
    //unsigned int steps_per_swap = 2;
    //unsigned int n_swaps = 50;
    // For each temperature, which sample to select for each neighboring pix
    //std::vector<std::unique_ptr<std::vector<uint16_t>>> neighbor_sample;
    //std::vector<double> beta; // Temperature of each neighbor sampler
    
    //
    // Information on neighboring pixels
    //

    // Quantities needed for calculation of marginal probabilities.
    // One vector per temperature to save, so shape is: (temperature, sample)
    std::vector<std::vector<double>> logL_chain, logPr_chain;
    std::vector<std::vector<int16_t>> y_idx_chain;
    std::vector<std::vector<uint16_t>> neighbor_sample_chain;
    int n_neighbors = 1;
    int n_neighbor_samples = 1;
    
    // Maximum temperature to save
    int t_save_max = 1;
    if(s.save_all_temperatures) {
        t_save_max = s.n_temperatures;
    }
    
    if(params.neighbor_pixels) {
        n_neighbors = params.neighbor_pixels->get_n_pix();
        n_neighbor_samples = params.neighbor_pixels->get_n_samples();
        
        logL_chain.reserve(t_save_max);
        logPr_chain.reserve(t_save_max);
        y_idx_chain.reserve(t_save_max);
        neighbor_sample_chain.reserve(t_save_max);
        
        for(int t=0; t<t_save_max; t++) {
            logL_chain.emplace_back();
            logL_chain.back().reserve(n_save_buffered);
            
            logPr_chain.emplace_back();
            logPr_chain.back().reserve(n_save_buffered);
            
            y_idx_chain.emplace_back();
            y_idx_chain.back().reserve(n_x*n_save_buffered);
            
            neighbor_sample_chain.emplace_back();
            neighbor_sample_chain.back().reserve(
                (n_neighbors-1)*n_save_buffered
            );
        }
    }
    
    std::vector<int64_t> n_swaps_proposed(s.n_temperatures-1, 0);
    std::vector<int64_t> n_swaps_accepted(s.n_temperatures-1, 0);
    
    //if(params.neighbor_pixels) {
        //double b = 1.0;
        //beta.reserve(n_temperatures);
        //log_p_neighbor.resize(n_temperatures);

        //for(int t=0; t<n_temperatures; t++, b*=beta_spacing) {
        //    neighbor_sample.push_back(std::make_unique<std::vector<uint16_t>>());
        //    neighbor_sample.at(t)->reserve(params.neighbor_pixels->get_n_pix());
        //    randomize_neighbors(
        //        *(params.neighbor_pixels),
        //        *neighbor_sample.at(t),
        //        params.r);
        //    (*(neighbor_sample.at(t)))[0] = 0;

        //    beta.push_back(b);
        //}
        
        //gibbs_order.reserve(params.neighbor_pixels->get_n_pix()-1);
        //for(int i=1; i<params.neighbor_pixels->get_n_pix(); i++) {
        //    gibbs_order.push_back(i);
        //}
    //}
    
    // Pixel indices chosen for neighbors
    std::vector<std::unique_ptr<std::vector<uint16_t>>> neighbor_idx;
    neighbor_idx.reserve(s.n_temperatures);
    // log(prior) of neighboring pixel combination
    std::vector<double> logPr_neighbor(s.n_temperatures, 0.);
    // Sampling order for neighboring pixels. Will be shuffled.
    std::vector<int> neighbor_gibbs_order;
    // Workspaces used during neighbor pixel sampling
    std::vector<double> log_p_sample_ws;
    std::vector<double> p_sample_ws;
    std::vector<double> mu_ws;
    // # of samples stored in neighboring pixels
    
    if(params.neighbor_pixels) {
        for(int t=0; t<s.n_temperatures; t++) {
            neighbor_idx.push_back(
                std::make_unique<std::vector<uint16_t>>()
            );
            neighbor_idx.at(t)->reserve(n_neighbors);
            
            // Initialize different temperatures to same staring position
            if(t == 0) {
                randomize_neighbors(
                    *(params.neighbor_pixels),
                    *(neighbor_idx.at(0)),
                    params.r
                );
                // Central pixel always fixed to sample 0 for simplicity.
                // Sampled line-of-sight profile will be copied in.
                neighbor_idx.at(0)->at(0) = 0;
            } else {
                std::copy(
                    neighbor_idx.at(0)->begin(),
                    neighbor_idx.at(0)->end(),
                    std::back_inserter(*(neighbor_idx.at(t)))
                );
            }
        }
        
        log_p_sample_ws.resize(n_neighbor_samples);
        p_sample_ws.resize(n_neighbor_samples);
        
        neighbor_gibbs_order.reserve(n_neighbors-1);
        for(int n=1; n<n_neighbors; n++) {
            neighbor_gibbs_order.push_back(n);
        }
    
    } else { // No neighboring pixels loaded
        for(int t=0; t<s.n_temperatures; t++) {
            neighbor_idx.push_back(
                std::make_unique<std::vector<uint16_t>>(
                    n_neighbor_samples,
                    0
                )
            );
        }
    }

    // Priors on dE in central pixel
    std::vector<std::unique_ptr<cv::Mat>> lnP_dy;
    lnP_dy.reserve(s.n_temperatures);
    for(int t=0; t<s.n_temperatures; t++) {
        lnP_dy.push_back(
            std::make_unique<cv::Mat>()
        );
        params.update_priors_image(
            *(lnP_dy.at(t)),
            *(neighbor_idx.at(t)),
            0.,
            params.priors_subsampling,
            shift_weight_ladder.at(t),
            verbosity
        );
        logPr.at(t) = params.log_prior(
            y_idx.at(t)->data(),
            *(lnP_dy.at(t))
        );
    }
    
    // Calculate initial prior
    //std::cerr << "Calculate initial prior" << std::endl;
    //logPr.at(0) = params.log_prior(y_idx.at(0)->data());
    //for(int t=1; t<n_temperatures; t++) {
    //    logPr.at(t) = params.log_prior(y_idx.at(0)->data());
    //}
    
    // Random sampler to select temperature (excluding T=1)
    std::uniform_int_distribution<int> r_temperature(1, s.n_temperatures-1);

    //std::uniform_int_distribution<int> r_neighbor(
    //    0,
    //    neighbor_sample.size()/(n_neighbors+1) - 1
    //);

    // std::cerr << std::endl
    //        << "##################################" << std::endl
    //        << "n_x = " << n_x << std::endl
    //        << "n_y = " << n_y << std::endl
    //        << "n_stars = " << n_stars << std::endl
    //        << "n_steps = " << n_steps << std::endl
    //        << "##################################" << std::endl
    //        << std::endl;
    //

    // Hash to speed up neighbor gibbs steps
    #define USE_NEIGHBOR_GIBBS_CACHE 0
    #if USE_NEIGHBOR_GIBBS_CACHE
    int gibbs_cache_capacity = 5000;
    std::vector<
        LRUCache::CachedFunction<
            std::vector<uint16_t>,
            std::shared_ptr<NeighborGibbsCacheData>,
            LRUCache::VectorHasher<uint16_t>
        >
    > gibbs_step_cache;
    
    int gibbs_step_pix;
    int disc_distr_res;
    double lnp_cutoff_cache = -10.;
    
    std::vector<
        std::function<void(std::shared_ptr<NeighborGibbsCacheData>&)>
    > roll_gibbs_idx;
    
    if(params.neighbor_pixels) {
        gibbs_step_cache.reserve(s.n_temperatures);
        roll_gibbs_idx.reserve(s.n_temperatures);
        for(int t=0; t<s.n_temperatures; t++) {
            gibbs_step_cache.push_back(
            LRUCache::CachedFunction<
                std::vector<uint16_t>,
                std::shared_ptr<NeighborGibbsCacheData>,
                LRUCache::VectorHasher<uint16_t>
            >(
                [
                    &gibbs_step_pix,
                    &neighbor_pixels=*(params.neighbor_pixels),
                    &log_p_sample_ws,
                    &p_sample_ws,
                    &mu_ws,
                    bt=beta.at(t),
                    shift_weight_ladder.at(t),
                    lnp_cutoff_cache
                ]
                (const std::vector<uint16_t>& nbor_idx)
                -> std::shared_ptr<NeighborGibbsCacheData>
            {
                //uint16_t s_tmp = nbor_samp[step_pix];
                //nbor_samp[step_pix] = neighbor_pixels.get_n_pix();
                std::unique_ptr<NeighborGibbsCacheData> cache_entry = 
                neighbor_gibbs_step_shifted_cache_data(
                    gibbs_step_pix,
                    neighbor_pixels,
                    nbor_idx,
                    log_p_sample_ws,
                    p_sample_ws,
                    mu_ws,
                    bt,
                    shift_weight_ladder.at(t),
                    lnp_cutoff_cache
                );
                //nbor_samp[step_pix] = s_tmp;
                return std::move(cache_entry);
            },
            gibbs_cache_capacity,
            nullptr)
            );
            
            roll_gibbs_idx.push_back(
                [
                    &gibbs_step_pix,
                    &neighbor_pixels=*(params.neighbor_pixels),
                    &neighbor_idx,
                    &log_p_sample_ws,
                    &p_sample_ws,
                    &mu_ws,
                    bt=beta.at(t),
                    shift_weight_ladder.at(t),
                    lnp_cutoff_cache,
                    &rr=params.r,
                    &disc_distr_res,
                    t
                ]
                (std::shared_ptr<NeighborGibbsCacheData>& cache_entry)
                -> void 
                {
                    disc_distr_res = neighbor_gibbs_step_shifted_cached(
                        gibbs_step_pix,
                        *cache_entry,
                        neighbor_pixels,
                        *(neighbor_idx.at(t)),
                        log_p_sample_ws,
                        p_sample_ws,
                        mu_ws,
                        bt,
                        shift_weight_ladder.at(t),
                        lnp_cutoff_cache,
                        rr
                    );
                }
            );
        }
    }
    #endif

    //int w = 0;

    // Softening parameter
    // TODO: Make p_badstar either a config option or dep. on ln(Z)
    const floating_t p_badstar = s.p_badstar;//1.e-5; //0.0001;
    const floating_t epsilon = p_badstar / (floating_t)n_y;

    double sigma_dy_neg = 1.e-5;
    double sigma_dy_neg_target = 1.e-10;
    double tau_decay = (double)n_swaps / 20.;

    // Proposal settings
    // TODO: Set these more intelligently, or make them configurable?
    // Mean value of y chosen in "absolute shift" proposals
    double y_shift_abs_mean = n_y / 20;
    // Maximum value of y chosen in "absolute shift" proposals
    double y_shift_abs_max = n_y;

    // uint64_t n_eval_diff = 0;
    // uint64_t n_eval_cumulative = 0;
    // uint64_t n_shift_steps = 0;

    DiscreteProposal proposal_type;

    std::uniform_real_distribution<> uniform_dist(0., 1.0);
    
    auto t_start = std::chrono::steady_clock::now();
    
    // Loop over swaps between temperatures
    for(int swap=0; swap<n_swaps; swap++) {
        //std::cerr << "Swap " << swap-n_swaps_burnin
        //          << " of " << n_swaps-n_swaps_burnin
        //          << std::endl;
        
        // Smoothly ramp penalty on negative reddening steps up
        sigma_dy_neg -= (sigma_dy_neg - sigma_dy_neg_target) / tau_decay;
        params.inv_sigma_dy_neg = 1. / sigma_dy_neg;
        
        // Sample within each temperature
        for(int t=0; t<s.n_temperatures; t++) {
            int16_t* y_idx_t = y_idx.at(t)->data();
            double* line_int_t = line_int.at(t)->data();
            double& logPr_t = logPr.at(t);
            double& logL_t = logL.at(t);
            double& log_p_t = log_p.at(t);
            cv::Mat& lnP_dy_t = *(lnP_dy.at(t));
            double b = beta.at(t);
            
            // Loop over update cycles
            for(int u=0; u<s.updates_per_swap; u++) {
                // Update neighbors
                if(params.neighbor_pixels) {
                    // Copy in central pixel's l.o.s. reddening profile
                    params.set_central_delta(y_idx_t);
                    
                    for(int n=0; n<s.neighbor_steps_per_update; n++) {
                        // Randomize Gibbs step order
                        std::shuffle(neighbor_gibbs_order.begin(),
                                     neighbor_gibbs_order.end(),
                                     params.r);
                        
                        // Take a Gibbs step in each neighbor pixel
                        for(auto k : neighbor_gibbs_order) {
                            #if USE_NEIGHBOR_GIBBS_CACHE
                            neighbor_idx[t]->at(k) = n_neighbor_samples;
                            gibbs_step_pix = k;
                            gibbs_step_cache.at(t)(
                                *(neighbor_idx[t]),
                                roll_gibbs_idx.at(t)
                            );
                            neighbor_idx[t]->at(k) = disc_distr_res;
                            #else
                            neighbor_gibbs_step_shifted(
                                k,
                                *(params.neighbor_pixels),
                                *(neighbor_idx[t]),
                                log_p_sample_ws,
                                p_sample_ws,
                                params.r,
                                b,
                                shift_weight_ladder.at(t)
                            );
                            #endif
                        }
                    }
                    
                    // Update priors on central
                    params.update_priors_image(
                        lnP_dy_t,
                        *(neighbor_idx.at(t)),
                        0.,
                        params.priors_subsampling,
                        shift_weight_ladder.at(t),
                        verbosity
                    );
                    logPr_t = params.log_prior(y_idx_t, lnP_dy_t);
                    log_p_t = logPr_t + logL_t;
                }
                
                // Between neighbor updates, update central pixel
                for(int c=0; c<central_steps_per_update; c++) {
                    // Propose a new state
                    int x_idx, dy, y_idx_new, dy1;

                    // Determine what type of proposal to make
                    proposal_type.roll(r);
                    n_proposals[proposal_type.code]++;
                    
                    if(proposal_type.step) {
                        // STEP
                        discrete_propose_step(r, n_x, x_idx, dy);
                        y_idx_new = y_idx_t[x_idx] + dy;
                        //std::cerr << "x_idx = " << x_idx << std::endl;
                        //std::cerr << "dy = " << dy << std::endl;
                        //std::cerr << "y_idx_new = " << y_idx_new << std::endl;
                    } else if(proposal_type.swap) {
                        // SWAP
                        discrete_propose_swap(r, n_x, x_idx);
                        dy1 = y_idx_t[x_idx+1] - y_idx_t[x_idx];
                        y_idx_new = y_idx_t[x_idx-1] + dy1;
                    } else if(proposal_type.absolute) {
                        // SHIFT_ABS_L_PROPOSAL or SHIFT_ABS_R_PROPOSAL
                        discrete_propose_shift_abs(
                            r, y_idx_t, n_x,
                            y_shift_abs_mean, y_shift_abs_max,
                            x_idx, dy, ln_proposal_factor
                        );
                    } else {
                        // SHIFT_L_PROPOSAL or SHIFT_R_PROPOSAL
                        discrete_propose_shift(r, n_x, x_idx, dy);
                    }
                    
                    
                    // Check if the proposal lands in a valid region
                    // of parameter space
                    bool prop_valid = discrete_proposal_valid(
                        proposal_type, y_idx_new, n_y,
                        params, x_idx, dy, y_idx_t
                    );
                    
                    //std::cerr << "prop_valid = " << prop_valid << std::endl;
                    
                    if(!prop_valid) { continue; }
                    
                    n_proposals_valid[proposal_type.code]++;

                    double dlogL = 0;
                    double dlogPr, alpha;

                    // Calculate difference in line integrals and
                    // prior (between the current and proposed states).
                    if(proposal_type.step) {
                        params.los_integral_diff_step(
                            x_idx,
                            y_idx_t[x_idx],
                            y_idx_new,
                            delta_line_int.data()
                        );
                        dlogPr = params.log_prior_diff_step(
                            x_idx,
                            y_idx_t,
                            y_idx_new,
                            lnP_dy_t
                        );
                    } else if(proposal_type.swap) {
                        params.los_integral_diff_swap(
                            x_idx, y_idx_t,
                            delta_line_int.data()
                        );
                        dlogPr = params.log_prior_diff_swap(
                            x_idx,
                            y_idx_t,
                            lnP_dy_t
                        );
                    } else if(proposal_type.left) {
                        dlogPr = params.log_prior_diff_shift_l(
                            x_idx,
                            dy,
                            y_idx_t,
                            lnP_dy_t
                        );
                        // No point in calculating line integrals
                        // if prior -> -infinity.
                        if(dlogPr
                           != -std::numeric_limits<double>::infinity())
                        {
                            params.los_integral_diff_shift_l(
                                x_idx, dy, y_idx_t,
                                delta_line_int.data()
                            );
                        }
                    } else { // SHIFT_R_PROPOSAL or SHIFT_ABS_R_PROPOSAL
                        dlogPr = params.log_prior_diff_shift_r(
                            x_idx,
                            dy,
                            y_idx_t,
                            lnP_dy_t
                        );
                        // No point in calculating line integrals
                        // if prior -> -infinity.
                        if(dlogPr
                           != -std::numeric_limits<double>::infinity())
                        {
                            params.los_integral_diff_shift_r(
                                x_idx, dy, y_idx_t,
                                delta_line_int.data()
                            );
                        }
                    }
                    
                    //std::cerr << "dlogPr = " << dlogPr << std::endl;
                    
                    // Change in likelihood
                    if(dlogPr != -std::numeric_limits<double>::infinity()) {
                        for(int k = 0; k < n_stars; k++) {
                            double zeta = delta_line_int[k]
                                          / (line_int_t[k]+epsilon);
                            if(std::fabs(zeta) < 1.e-2) {
                                // Taylor expansion of ln(1+zeta)
                                // for zeta << 1.
                                dlogL += zeta
                                         - 0.5 * zeta*zeta
                                         + 0.33333333*zeta*zeta*zeta;
                            } else {
                                dlogL += std::log(1.0 + zeta);
                            }
                        }
                        
                        //std::cerr << "dlogL = " << dlogL << std::endl;
                    }

                    // Acceptance probability
                    alpha = dlogL + dlogPr;
                    alpha *= b; // TODO: Multiply before prop factor?

                    if(proposal_type.absolute) {
                        alpha += ln_proposal_factor;
                    }

                    //std::cerr << "alpha = " << alpha << std::endl;
                    
                    // Accept proposal?
                    if((alpha > 0) // > 0 means automatic acceptance
                       || (
                            (alpha > -10.) && // Treat ln(-10) as zero
                            (std::exp(alpha) > gsl_rng_uniform(r))
                          ))
                    {
                        // ACCEPT
                        n_proposals_accepted[proposal_type.code]++;

                        // Update state to proposal
                        if(!proposal_type.shift) {
                            // STEP_PROPOSAL or SWAP_PROPOSAL
                            y_idx_t[x_idx] = y_idx_new;
                        } else if(proposal_type.left) {
                            for(int j=0; j<=x_idx; j++) {
                                y_idx_t[j] += dy;
                            }
                        } else { // SHIFT_R_PROPOSAL or SHIFT_ABS_R_PROPOSAL
                            for(int j=x_idx; j<params.n_dists; j++) {
                                y_idx_t[j] += dy;
                            }
                        }

                        // Update line integrals
                        for(int k = 0; k < n_stars; k++) {
                            line_int_t[k] += delta_line_int[k];
                        }

                        // Calculate line integrals exactly every
                        // certain number of steps
                        if(--recalculate_in.at(t) == 0) {
                            recalculate_in.at(t) = recalculate_every;
                            params.los_integral_discrete(
                                y_idx_t,
                                line_int_t
                            );
                        }

                        // Update prior & likelihood
                        log_p_t += dlogL + dlogPr;
                        logL_t += dlogL;
                        logPr_t += dlogPr;
                        //logPr_t = params.log_prior(y_idx_t);
                        //logPr.at(t) = params.log_prior(y_idx_t);
                        
                        //double logPr_tmp = params.log_prior(y_idx_t);
                        //if(std::abs(logPr_t-logPr_tmp) > 1.e-3) {
                        //
                        //}
                    }
                } // c (central steps)
            } // n (neighbor steps)
        } // t (temperatures)
        
        // Add beta=1 state to chain (must occur *before* possible swap)
        if((swap >= n_swaps_burnin) && (--save_in == 0)) {
            for(int t=0; t<t_save_max; t++) {
                int16_t* y_idx_t = y_idx.at(t)->data();
                
                for(int k=0; k<n_x; k++) {
                    y_idx_dbl[k] = (double)y_idx_t[k];
                }
                chain.at(t)->add_point(
                    y_idx_dbl.data(),
                    logL.at(t),
                    logPr.at(t),
                    1.
                );

                // Save info needed to calculate marginal probabilities
                if(params.neighbor_pixels) {
                    logL_chain.at(t).push_back(logL.at(t));
                    logPr_chain.at(t).push_back(logPr.at(t));
                    neighbor_sample_chain.at(t).insert(
                        neighbor_sample_chain.at(t).end(),
                        neighbor_idx.at(t)->begin()+1,
                        neighbor_idx.at(t)->end()
                    );
                    for(int k=0; k<n_x; k++) {
                        y_idx_chain[t].push_back(y_idx_t[k]);
                    }
                }
            }

            // Reset save counter
            save_in = save_every;
            n_saved++;
        }

        if(verbosity >= 2) {
            int t_report = 0;//s.n_temperatures-1;
            int16_t* y_idx_t = y_idx.at(t_report)->data();
            double* line_int_t = line_int.at(t_report)->data();
            
            discrete_los_ascii_art(
                n_x, n_y, y_idx_t,
                40, 700,
                params.img_stack->rect->dx[0],
                4., 19.,
                std::cerr);
            std::cerr << std::endl;

            params.los_integral_discrete(y_idx_t, line_int_test.data());
            double abs_resid_max = -std::numeric_limits<double>::infinity();
            double rel_resid_max = -std::numeric_limits<double>::infinity();
            for(int k=0; k<n_stars; k++) {
                double abs_resid = line_int_t[k] - line_int_test[k];
                double rel_resid = abs_resid / line_int_test[k];
                abs_resid_max = std::max(abs_resid_max, abs_resid);
                rel_resid_max = std::max(rel_resid_max, rel_resid);
            }
            std::cerr << std::endl
                      << "max. line integral residuals: "
                      << abs_resid_max << " (abs) "
                      << rel_resid_max << " (rel)"
                      << std::endl;

            double log_Pr_tmp = params.log_prior(
                y_idx_t,
                *(lnP_dy.at(t_report))
            );
            std::cerr << "log(prior) : "
                      << log_Pr_tmp << " (actual) "
                      << logPr.at(t_report) << " (running) "
                      << log_Pr_tmp - logPr.at(t_report) << " (difference)"
                      << std::endl;
            
            if(params.neighbor_pixels) {
                for(int tt=0; tt<s.n_temperatures; tt++) {
                    std::cerr << "neighbor samples (" << tt << "):";
                    for(int j=0; j<params.neighbor_pixels->get_n_pix(); j++) {
                        std::cerr << " " << neighbor_idx.at(tt)->at(j);
                    }
                    std::cerr << std::endl;
                }
                
                for(int tt=0; tt<s.n_temperatures; tt++) {
                    std::cerr << "dominant dist (" << tt << "):";
                    uint16_t dist_max;
                    for(int j=0; j<params.neighbor_pixels->get_n_pix(); j++) {
                        dist_max = params.neighbor_pixels->get_dominant_dist(
                                j,
                                neighbor_idx.at(tt)->at(j)
                        );
                        std::cerr << " " << dist_max;
                    }
                    std::cerr << std::endl;
                }
            }
            
            std::cerr << "log(p)_t =";
            for(int t=0; t<s.n_temperatures; t++) {
                std::cerr << " " << log_p.at(t);
            }
            std::cerr << std::endl;
            
            auto t_now = std::chrono::steady_clock::now();
            std::chrono::duration<double> t_elapsed = t_now - t_start;
            std::cerr << std::endl;
            ascii_progressbar(
                swap,
                n_swaps,
                50,
                t_elapsed.count(),
                std::cerr
            );
            std::cerr << std::endl;
        }
        
        // Attempt swap
        if(s.n_temperatures > 1) {
            // Choose temperature pair to swap
            int t1 = r_temperature(params.r); // 1 <= t1 <= n_temperatures-1
            int t0 = t1 - 1;
            
            n_swaps_proposed[t0]++;
            
            // Determine probability density of each temperature
            
            // First, calculate the prior at each temperature
            double logPr_x1s1, logPr_x0s0;
            double logPr_x1s0, logPr_x0s1;
            
            if(params.neighbor_pixels) {
                params.set_central_delta(y_idx.at(t1)->data());
                logPr_x1s1 = params.neighbor_pixels->calc_lnprob_shifted(
                    *(neighbor_idx.at(t1)),
                    shift_weight_ladder.at(t1),
                    false
                );
                logPr_x1s0 = params.neighbor_pixels->calc_lnprob_shifted(
                    *(neighbor_idx.at(t1)),
                    shift_weight_ladder.at(t0),
                    false
                );
                
                params.set_central_delta(y_idx.at(t0)->data());
                logPr_x0s0 = params.neighbor_pixels->calc_lnprob_shifted(
                    *(neighbor_idx.at(t0)),
                    shift_weight_ladder.at(t0),
                    false
                );
                logPr_x0s1 = params.neighbor_pixels->calc_lnprob_shifted(
                    *(neighbor_idx.at(t0)),
                    shift_weight_ladder.at(t1),
                    false
                );
            } else {
                logPr_x1s1 = params.log_prior(
                    y_idx.at(t1)->data(),
                    *(lnP_dy.at(t1))
                );
                logPr_x1s0 = params.log_prior(
                    y_idx.at(t1)->data(),
                    *(lnP_dy.at(t0))
                );
                logPr_x0s0 = params.log_prior(
                    y_idx.at(t0)->data(),
                    *(lnP_dy.at(t0))
                );
                logPr_x0s1 = params.log_prior(
                    y_idx.at(t0)->data(),
                    *(lnP_dy.at(t1))
                );
            }

            // Next, add in the likelihoods
            double logL_t0 = logL.at(0);
            double logL_t1 = logL.at(1);
            //logp_t1 += logL.at(t1);
            //logp_t0 += logL.at(t0);
            
            if(params.neighbor_pixels) {
                for(int neighbor=1; neighbor<n_neighbors; neighbor++) {
                    logL_t1 += params.neighbor_pixels->get_likelihood(
                        neighbor,
                        neighbor_idx.at(t1)->at(neighbor)
                    );
                    logL_t0 += params.neighbor_pixels->get_likelihood(
                        neighbor,
                        neighbor_idx.at(t0)->at(neighbor)
                    );
                }
            }
            
            // Acceptance likelihood term
            double alpha_L = (beta.at(t1) - beta.at(t0))
                             * (logL_t0 - logL_t1);
            
            // Acceptance prior term
            double alpha_Pr = beta.at(t0) * (logPr_x1s0 - logPr_x0s0)
                            + beta.at(t1) * (logPr_x0s1 - logPr_x1s1);
            
            double alpha = alpha_L + alpha_Pr;
            //alpha = -9999.; // TODO: remove this line
            
            if(verbosity >= 2) {
                std::cerr << "Swap " << t1-1 << " <-> " << t1
                          << std::endl
                          << "  alpha_L = " << alpha_L
                          << std::endl
                          << "  alpha_Pr = " << alpha_Pr
                          << std::endl
                          << "  logPr_x0s1 - logPr_x0s0 = " << logPr_x0s1 - logPr_x0s0
                          << std::endl
                          << "  logPr_x1s0 - logPr_x1s1 = " << logPr_x1s0 - logPr_x1s1
                          << std::endl
                          << "  alpha = " << alpha;
            }
            
            if((alpha > 0.)
               || (
                    (alpha > -10.) &&
                    (std::exp(alpha) > gsl_rng_uniform(r))
                  ))
            {
                // Swap accepted
                n_swaps_accepted[t0]++;
                
                // Swap all the relevant pointers
                y_idx.at(t1).swap(y_idx.at(t0));
                line_int.at(t1).swap(line_int.at(t0));
                neighbor_idx.at(t1).swap(neighbor_idx.at(t0));
                lnP_dy.at(t1).swap(lnP_dy.at(t0));
                
                // Swap all the relevant values
                std::swap(logL.at(t1), logL.at(t0));
                logPr.at(t1) = logPr_x0s1;
                logPr.at(t0) = logPr_x1s0;
                log_p.at(t0) = logL.at(t0) + logPr.at(t0);
                log_p.at(t1) = logL.at(t1) + logPr.at(t1);
                
                if(verbosity >= 2) {
                    std::cerr << " (accepted)";
                }
            }
            
            if(verbosity >= 2) { std::cerr << std::endl; }
        }
    } // s (swaps)
    
    //for(int i = 0; i < n_steps + n_burnin; i++) {
    //    // Randomize neighbors
    //    if(params.neighbor_pixels && (--neighbor_step_in == 0)) {
    //        neighbor_step_in = neighbor_step_every;
    //        
    //        //params.randomize_neighbors(neighbor_sample);
    //        //neighbor_sample[0] = 0;

    //        // Gibbs sample neighboring pixels
    //        //params.set_central_delta(y_idx);
    //        //for(int t=1; t<n_temperatures; t++) {
    //        //    for(int k=0; k<neighbor_sample[0]->size(); k++) {
    //        //        (*(neighbor_sample[t]))[k] = (*(neighbor_sample[0]))[k];
    //        //    }
    //        //}

    //        //for(int j=0; j<n_swaps; j++) {
    //        //    for(int t=0; t<n_temperatures; t++) {
    //        //        for(int l=0; l<steps_per_swap; l++) {
    //        //            // Randomize Gibbs step order
    //        //            std::shuffle(gibbs_order.begin(),
    //        //                         gibbs_order.end(),
    //        //                         params.r);

    //        //            // Take a Gibbs step in each neighbor pixel
    //        //            for(auto k : gibbs_order) {
    //        //                log_p_neighbor[t] = neighbor_gibbs_step(
    //        //                    k,
    //        //                    *(params.neighbor_pixels),
    //        //                    *(neighbor_sample[t]),
    //        //                    params.log_p_sample,
    //        //                    params.p_sample,
    //        //                    params.r,
    //        //                    beta.at(t));
    //        //            }
    //        //        }
    //        //    }

    //        //    // Take a swap step between temperatures
    //        //    for(int t=1; t<n_temperatures; t++) {
    //        //        // Try to swap t and t-1
    //        //        n_swaps_proposed++;

    //        //        double lnp_swap = (
    //        //            (beta[t]-beta[t-1]) *
    //        //            (log_p_neighbor[t-1]-log_p_neighbor[t])
    //        //        );
    //        //        
    //        //        //if(t == 1) {
    //        //        //    std::cerr << "p_swap = " << std::exp(lnp_swap) << std::endl;
    //        //        //}

    //        //        if(std::log(uniform_dist(params.r)) < lnp_swap) {
    //        //            //std::cerr << "swap " << t-1 << " <-> " << t << std::endl;
    //        //            neighbor_sample[t].swap(neighbor_sample[t-1]);
    //        //            n_swaps_accepted++;
    //        //        } else {
    //        //            //std::cerr << "no swap " << t-1 << " <-> " << t << std::endl;
    //        //        }
    //        //    }
    //        //}

    //        // Update the pre-computed priors image, in (E, DM)-pixel-space
    //        //params.update_priors_image(
    //        //    *(neighbor_sample.at(0)), 0.,
    //        //    params.priors_subsampling,
    //        //    verbosity);
    //        
    //        int idx = r_neighbor(params.r);
    //        neighbor_idx.push_back(idx);

    //        neighbor_sample_ws.clear();
    //        neighbor_sample_ws.insert(
    //            neighbor_sample_ws.end(),
    //            neighbor_sample.begin() + (n_neighbors+1)*idx,
    //            neighbor_sample.begin() + (n_neighbors+1)*(idx+1)
    //        );

    //        params.update_priors_image(
    //            neighbor_sample_ws, 0.,
    //            params.priors_subsampling,
    //            verbosity);

    //        // Update log(prior) of current state
    //        logPr = params.log_prior(y_idx);
    //    }
    //    
    //    // Smoothly ramp penalty on negative reddening steps up
    //    sigma_dy_neg -= (sigma_dy_neg - sigma_dy_neg_target) / tau_decay;
    //    params.inv_sigma_dy_neg = 1. / sigma_dy_neg;

    //    // Increase weight of current state
    //    w += 1;

    //    // Propose a new state
    //    int x_idx, dy, y_idx_new, dy1;

    //    // Determine what type of proposal to make
    //    proposal_type.roll(r);
    //    // int proposal_type = gsl_rng_uniform_int(r, N_PROPOSAL_TYPES);
    //    // int proposal_type = STEP_PROPOSAL;
    //    // int proposal_type;
    //    // double prop_rand = gsl_rng_uniform(r);
    //    // const double p_swap = 0.8;
    //    // const double p_step = 0.1;
    //    // // const double p_shift = 0.1;
    //    // if(prop_rand < p_swap) {
    //    //  proposal_type = SWAP_PROPOSAL;
    //    // } else if(prop_rand < p_swap + p_step) {
    //    //  proposal_type = STEP_PROPOSAL;
    //    // } else {
    //    //  proposal_type = SHIFT_PROPOSAL;
    //    // }

    //    n_proposals[proposal_type.code]++;

    //    if(proposal_type.step) {
    //        discrete_propose_step(r, n_x, x_idx, dy);
    //        y_idx_new = y_idx[x_idx] + dy;
    //    } else if(proposal_type.swap) {
    //        discrete_propose_swap(r, n_x, x_idx);
    //        dy1 = y_idx[x_idx+1] - y_idx[x_idx];
    //        y_idx_new = y_idx[x_idx-1] + dy1;
    //    } else if(proposal_type.absolute) {
    //        // SHIFT_ABS_L_PROPOSAL or SHIFT_ABS_R_PROPOSAL
    //        discrete_propose_shift_abs(
    //            r, y_idx, n_x, y_shift_abs_mean, y_shift_abs_max,
    //            x_idx, dy, ln_proposal_factor
    //        );
    //    } else {
    //        // SHIFT_L_PROPOSAL or SHIFT_R_PROPOSAL
    //        discrete_propose_shift(r, n_x, x_idx, dy);
    //    }

    //    // int y_idx_new = y_idx[x_idx] + dy;

    //    // Check if the proposal lands in a valid region of parameter space
    //    bool prop_valid = discrete_proposal_valid(
    //        proposal_type, y_idx_new, n_y,
    //        params, x_idx, dy, y_idx);

    //    if(prop_valid) {
    //        n_proposals_valid[proposal_type.code]++;

    //        double dlogL = 0;
    //        double dlogPr, alpha;

    //        // Calculate difference in line integrals and prior (between the
    //        // current and proposed states).
    //        if(proposal_type.step) {
    //            params.los_integral_diff_step(
    //                x_idx,
    //                y_idx[x_idx],
    //                y_idx_new,
    //                delta_line_int
    //            );
    //            dlogPr = params.log_prior_diff_step(x_idx, y_idx, y_idx_new);
    //        } else if(proposal_type.swap) {
    //            params.los_integral_diff_swap(
    //                x_idx, y_idx,
    //                delta_line_int
    //            );
    //            dlogPr = params.log_prior_diff_swap(x_idx, y_idx);
    //        } else if(proposal_type.left) {
    //            dlogPr = params.log_prior_diff_shift_l(x_idx, dy, y_idx);
    //            // No point in calculating line integrals if prior -> -infinity.
    //            if(dlogPr != -std::numeric_limits<double>::infinity()) {
    //                params.los_integral_diff_shift_l(
    //                    x_idx, dy, y_idx,
    //                    delta_line_int
    //                );
    //            }

    //            // std::cerr << dlogPr << std::endl;
    //        } else { // SHIFT_R_PROPOSAL or SHIFT_ABS_R_PROPOSAL
    //            dlogPr = params.log_prior_diff_shift_r(x_idx, dy, y_idx);

    //            // if(x_idx == 0) {
    //            //  std::cerr << "shift(0, "
    //            //            << y_idx[0] << " -> " << y_idx[0] + dy
    //            //            << ") : dln(prior) = " << dlogPr
    //            //            << std::endl;
    //            // }

    //            // No point in calculating line integrals if prior -> -infinity.
    //            if(dlogPr != -std::numeric_limits<double>::infinity()) {
    //                params.los_integral_diff_shift_r(
    //                    x_idx, dy, y_idx,
    //                    delta_line_int
    //                );

    //                // unsigned int n_eval_diff_tmp;
    //                // unsigned int n_eval_cumulative_tmp;
    //                // params.los_integral_diff_shift_compare_operations(
    //                //  x_idx, dy, y_idx,
    //                //  n_eval_diff_tmp,
    //                //  n_eval_cumulative_tmp
    //                // );
    //                //
    //                // n_eval_diff += n_eval_diff_tmp;
    //                // n_eval_cumulative += n_eval_cumulative_tmp;
    //                // n_shift_steps++;


    //            }
    //        }

    //        // Change in likelihood
    //        if(dlogPr != -std::numeric_limits<double>::infinity()) {
    //            for(int k = 0; k < n_stars; k++) {
    //                double zeta = delta_line_int[k] / (line_int[k]+epsilon);
    //                if(std::fabs(zeta) < 1.e-2) {
    //                    dlogL += zeta - 0.5 * zeta*zeta + 0.33333333*zeta*zeta*zeta; // Taylor expansion of log(1+zeta) for zeta << 1.
    //                } else {
    //                    dlogL += std::log(1.0 + zeta);
    //                }
    //            }
    //        }

    //        // Acceptance probability
    //        alpha = dlogL + dlogPr;
    //        //alpha = dlogPr; // TODO: Switch back to log(prior) + log(likelihood)

    //        if(proposal_type.absolute) {
    //            alpha += ln_proposal_factor;
    //        }

    //        // Accept proposal?
    //        if((alpha > 0) || ((alpha > -10.) && (std::exp(alpha) > gsl_rng_uniform(r)))) {
    //            // if((dlogPr < -10000) || (dlogPr > 10000)) {
    //            //  std::cerr << "dlogPr = " << dlogPr << std::endl
    //            //            << "  proposal_type = " << proposal_type << std::endl
    //            //            << "  x_idx = " << x_idx << std::endl
    //            //            << "  dy = " << dy << std::endl
    //            //            << "  y_idx[0] = " << y_idx[0] << std::endl;
    //            //  std::exit(1);
    //            // }

    //            // ACCEPT
    //            n_proposals_accepted[proposal_type.code]++;

    //            // Add old point to chain
    //            // chain.add_point(y_idx_dbl, log_p, (double)w);

    //            // Update state to proposal
    //            if(!proposal_type.shift) {
    //                // STEP_PROPOSAL or SWAP_PROPOSAL
    //                y_idx[x_idx] = y_idx_new;
    //                // y_idx_dbl[x_idx] = (double)y_idx_new;
    //            } else if(proposal_type.left) {
    //                for(int j=0; j<=x_idx; j++) {
    //                    y_idx[j] += dy;
    //                }
    //            } else { // SHIFT_R_PROPOSAL or SHIFT_ABS_R_PROPOSAL
    //                for(int j=x_idx; j<params.n_dists; j++) {
    //                    y_idx[j] += dy;
    //                    // y_idx_dbl[j] = (double)(y_idx[j]);
    //                }
    //            }

    //            // Update line integrals
    //            for(int k = 0; k < n_stars; k++) {
    //                line_int[k] += delta_line_int[k];
    //            }

    //            // Calculate line integrals exactly every certain number of steps
    //            if(--recalculate_in == 0) {
    //                recalculate_in = recalculate_every;
    //                params.los_integral_discrete(y_idx, line_int);
    //            }

    //            // Update prior & likelihood
    //            log_p += alpha;
    //            logL += dlogL;
    //            logPr += dlogPr;

    //            // double log_Pr_tmp = params.log_prior(y_idx);
    //            // std::cerr << std::endl
    //            //        << "log(prior) : "
    //            //        << log_Pr_tmp << " (actual) "
    //            //        << logPr << " (estimate) "
    //            //        << log_Pr_tmp - logPr << " (difference)"
    //            //        << std::endl;
    //            //
    //            // if(fabs(log_Pr_tmp - logPr) > 0.001) {
    //            //  std::cerr << "x_idx = " << x_idx << std::endl;
    //            //  std::exit(1);
    //            // }

    //            // params.los_integral_discrete(y_idx, line_int_test);

    //            // for(int k=0; k<n_stars; k++) {
    //            //  floating_t delta_true = line_int_test[k] - line_int_test_old[k];
    //            //  floating_t delta_resid = delta_line_int[k] - delta_true;
    //            //  // floating_t rel_delta_resid = delta_resid / delta_true;
    //            //
    //            //  if(fabs(delta_resid) > 1.e-10) {
    //            //      floating_t P_old = params.img_stack->img[k]->at<floating_t>(
    //            //          y_idx_new-dy, x_idx);
    //            //      floating_t P_new = params.img_stack->img[k]->at<floating_t>(
    //            //          y_idx_new, x_idx);
    //            //
    //            //      std::cerr << "delta_resid[" << k << "] = "
    //            //                << delta_resid
    //            //                << " , delta = "
    //            //                << delta_true
    //            //                << " , integral = "
    //            //                << line_int_test[k]
    //            //                << " , x_idx = "
    //            //                << x_idx
    //            //                << std::endl;
    //            //      std::cerr << "  P_old = " << P_old << std::endl
    //            //                << "  P_new = " << P_new << std::endl
    //            //                << "  P_new - P_old = " << P_new - P_old << std::endl;
    //            //  }
    //            //  // std::cerr << delta_true << "  " << rel_delta_resid << std::endl;
    //            // }

    //            // std::swap(line_int_test, line_int_test_old);

    //            // Reset weight to zero
    //            w = 0;
    //        }
    //    }

    //    // Add state to chain
    //    if((i >= n_burnin) && (--save_in == 0)) {
    //        for(int k=0; k<n_x; k++) {
    //            y_idx_dbl[k] = (double)y_idx[k];
    //        }
    //        chain.add_point(y_idx_dbl, logPr, 1.);//log_p, 1.);

    //        // Save info needed to calculate marginal probabilities
    //        if(params.neighbor_pixels) {
    //            logL_chain.push_back(logL);
    //            logPr_chain.push_back(logPr);
    //            neighbor_sample_chain.insert(
    //                neighbor_sample_chain.end(),
    //                neighbor_sample_ws.begin()+1,
    //                neighbor_sample_ws.end()
    //            );
    //            for(int k=0; k<n_x; k++) {
    //                y_idx_chain.push_back(y_idx[k]);
    //            }
    //        }

    //        // Reset save counter
    //        save_in = save_every;
    //    }

    //    if((verbosity >= 2) && (i % 10000 == 0)) {
    //        discrete_los_ascii_art(
    //            n_x, n_y, y_idx,
    //            40, 700,
    //            params.img_stack->rect->dx[0],
    //            4., 19.,
    //            std::cerr);
    //        std::cerr << std::endl;

    //        params.los_integral_discrete(y_idx, line_int_test);
    //        double abs_resid_max = -std::numeric_limits<double>::infinity();
    //        double rel_resid_max = -std::numeric_limits<double>::infinity();
    //        for(int k=0; k<n_stars; k++) {
    //            double abs_resid = line_int[k] - line_int_test[k];
    //            double rel_resid = abs_resid / line_int_test[k];
    //            abs_resid_max = std::max(abs_resid_max, abs_resid);
    //            rel_resid_max = std::max(rel_resid_max, rel_resid);
    //        }
    //        std::cerr << std::endl
    //                  << "max. line integral residuals: "
    //                  << abs_resid_max << " (abs) "
    //                  << rel_resid_max << " (rel)"
    //                  << std::endl;

    //        double log_Pr_tmp = params.log_prior(y_idx);
    //        std::cerr << "log(prior) : "
    //                  << log_Pr_tmp << " (actual) "
    //                  << logPr << " (running) "
    //                  << log_Pr_tmp - logPr << " (difference)"
    //                  << std::endl << std::endl;
    //        
    //        if(params.neighbor_pixels) {
    //            std::cerr << "neighbor samples:";
    //            for(int j=0; j<params.neighbor_pixels->get_n_pix(); j++) {
    //                std::cerr << " " << neighbor_sample_ws.at(j);
    //            }
    //            std::cerr << std::endl << std::endl;
    //        }

    //        // std::vector<std::pair<int,double>> delta_line_int_true;
    //        // double delta_lnL_true = 0.;
    //        // for(int k=0; k<n_stars; k++) {
    //        //  double tmp_diff = log(line_int[k]+epsilon) - log(line_int_true[k]+epsilon);
    //        //  delta_line_int_true.emplace_back(k, tmp_diff);
    //        //  delta_lnL_true += tmp_diff;
    //        // }
    //        // std::sort(
    //        //  delta_line_int_true.begin(),
    //        //  delta_line_int_true.end(),
    //        //  [](std::pair<int,double> _l, std::pair<int,double> _r) -> bool {
    //        //      return _l.second < _r.second;
    //        //  }
    //        // );
    //        //
    //        // std::cerr << "Delta ln(L) = " << delta_lnL_true << std::endl;
    //        // int n_show = 4;
    //        // for(int k=0; k<n_show; k++) {
    //        //  std::cerr << "  "
    //        //            << delta_line_int_true.at(k).second
    //        //            << "  (" << delta_line_int_true.at(k).first << ")"
    //        //            << std::endl;
    //        // }
    //        // std::cerr << "  ..." << std::endl;
    //        // for(int k=0; k<n_show; k++) {
    //        //  std::cerr << "  "
    //        //            << delta_line_int_true.at(n_stars-n_show+k).second
    //        //            << "  (" << delta_line_int_true.at(n_stars-n_show+k).first << ")"
    //        //            << std::endl;
    //        // }
    //        // std::cerr << std::endl;
    //        //
    //        // std::cerr << "l.o.s. integrals (true):" << std::endl;
    //        // for(int k=0; k<3; k++) {
    //        //  std::cerr << "  "
    //        //            << line_int[k]
    //        //            << "  (" << line_int_true[k] << ")"
    //        //            << std::endl;
    //        // }
    //        // std::cerr << std::endl;
    //        
    //        auto t_now = std::chrono::steady_clock::now();
    //        std::chrono::duration<double> t_elapsed = t_now - t_start;
    //        ascii_progressbar(i, n_steps+n_burnin, 50, t_elapsed.count(), std::cerr);
    //        std::cerr << std::endl;
    //    }
    //}

    if(verbosity >= 1) {
        std::string prop_name[N_PROPOSAL_TYPES];
        prop_name[STEP_PROPOSAL] = "step";
        prop_name[SWAP_PROPOSAL] = "swap";
        prop_name[SHIFT_L_PROPOSAL] = "shift_l";
        prop_name[SHIFT_R_PROPOSAL] = "shift_r";
        prop_name[SHIFT_ABS_L_PROPOSAL] = "shift_abs_l";
        prop_name[SHIFT_ABS_R_PROPOSAL] = "shift_abs_r";

        uint64_t n_proposals_tot = 0;
        for(int i=0; i<N_PROPOSAL_TYPES; i++) {
            n_proposals_tot += n_proposals[i];
        }

        for(int i=0; i<N_PROPOSAL_TYPES; i++) {
            double p_valid = (double)n_proposals_valid[i]
                             / (double)n_proposals[i];
            double p_accept = (double)n_proposals_accepted[i]
                             / (double)n_proposals[i];
            double p_of_tot = (double)n_proposals[i]
                             / (double)n_proposals_tot;
            std::cerr << prop_name[i] << " proposals "
                      << "(" << 100. * p_of_tot << " %):"
                      << std::endl
                      << " *    valid : " << 100. * p_valid << " %"
                      << std::endl
                      << " * accepted : " << 100. * p_accept << " %"
                      << std::endl;
        }
        
        std::cerr << "Swap acceptance:";
        for(int t=0; t<s.n_temperatures-1; t++) {
            double p_accept = (double)n_swaps_accepted.at(t)
                              / (double)n_swaps_proposed.at(t);
            std::cerr << " " << 100. * p_accept << "%";
        }
        std::cerr << std::endl;
        
        std::cerr << n_saved << " samples saved." << std::endl;
    }

    // std::cerr << std::endl
    //        << "SHIFT STEP EVALUTATIONS:" << std::endl
    //        << "  * differential: "
    //            << (double)n_eval_diff / (double)n_shift_steps
    //            << " eval/step" << std::endl
    //        << "  * cumulative  : "
    //            << (double)n_eval_cumulative / (double)n_shift_steps
    //            << " eval/step" << std::endl
    //        << std::endl;

    // Add final state to chain
    // chain.add_point(y_idx_dbl, log_p, (double)w);

    // Estimate marginal probabilities of central reddening
    if(params.neighbor_pixels) {
        if(verbosity >= 2) {
            std::cerr << std::endl
                      << "Estimating p(alpha_central) ..."
                      << std::endl;
        }
        
        auto t_start_marg = std::chrono::steady_clock::now();
        
        int16_t* chain_ptr = y_idx_chain.at(0).data();
        int chain_len = logL_chain.at(0).size();
        
        // For each temperature, p(alpha_t|neighbors_0) for all
        // samples alpha_t from temperature t, and all neighbors
        // from the temperature=1 chain.
        // shape = (temperature, (chain sample, neighbor sample))
        std::vector<std::vector<double>> prior_chain;
        for(int t=0; t<t_save_max; t++) {
            prior_chain.emplace_back(chain_len*chain_len, 0);
        }
        
        //std::vector<double> prior_avg(chain_len, 0.);
        std::vector<uint16_t> neighbor_sample_ws;
        neighbor_sample_ws.resize(n_neighbors);
        neighbor_sample_ws[0] = 0;
        
        // For each set of neighbor pixels
        for(int i=0; i<chain_len; i++) {
            // Set the neighbor pixel indices
            // (taken from the temperature=1 chain)
            int j0 = (n_neighbors-1) * i;
            for(int k=1; k<n_neighbors; k++) {
                neighbor_sample_ws[k] = neighbor_sample_chain[0][j0+k-1];
            }

            // Update the pre-computed priors image, in (E, DM)-pixel-space,
            // using the neighbors from the temperature=1 chain.
            params.update_priors_image(
                *(lnP_dy.at(0)),
                neighbor_sample_ws,
                0.,
                params.priors_subsampling,
                shift_weight_ladder.at(0),
                verbosity
            );
            
            for(int t=0; t<t_save_max; t++) {
                double* prior_chain_t = prior_chain.at(t).data();
                int16_t* chain_ptr_t = y_idx_chain.at(t).data();
                
                // Calculate the log(prior) of each point in chain
                for(int k=0; k<chain_len; k++) {
                    prior_chain_t[chain_len*k+i] = params.log_prior(
                        chain_ptr_t + n_x*k,
                        *(lnP_dy.at(0))
                    );
                    //std::cerr << "prior = " << prior_chain[chain_len*k+i] << std::endl;
                }
            }

            if((verbosity >= 2) && (i % 10 == 0)) {
                auto t_now = std::chrono::steady_clock::now();
                std::chrono::duration<double> t_elapsed
                    = t_now - t_start_marg;
                ascii_progressbar(
                    i,
                    chain_len,
                    50,
                    t_elapsed.count(),
                    std::cerr,
                    false,
                    true
                );
            }
        }
        
        if(verbosity >= 2) {
            std::cerr << std::endl << std::endl;
        }
        
        double log_chain_len = std::log((double)chain_len);
        std::vector<double> log_p_tmp;
        log_p_tmp.reserve(chain_len);

        for(int t=0; t<t_save_max; t++) {
            log_p_tmp.clear();
            
            for(unsigned int i=0; i<chain_len; i++) {
                // Set log(p) of chain to prior_avg
                auto it_0 = prior_chain.at(t).begin() + i*chain_len;
                auto it_1 = it_0 + chain_len;
                double log_p_max = *std::max_element(it_0, it_1);
                //std::cerr << "log_p_max = " << log_p_max << std::endl;
                double p = std::accumulate(it_0, it_1, 0.,
                    [log_p_max](double a, double b) -> double {
                        return a + std::exp(b-log_p_max);
                    }
                );
                //std::cerr << "p = " << p << std::endl;
                p = log_p_max + std::log(p) - log_chain_len;
                // TODO: Check this correction
                //p -= logL_chain.at(t).at(i);
                //std::cerr << "log(p) = " << p << std::endl;
                log_p_tmp.push_back(p);
            }
            
            double log_p0 = *std::max_element(
                log_p_tmp.begin(),
                log_p_tmp.end()
            );
            std::cerr << "log_p0 = " << log_p0 << std::endl;
            for(unsigned int i=0; i<chain_len; i++) {
                chain.at(t)->set_p(i, log_p_tmp.at(i) - log_p0);
            }
        }
    }

    // Save the chain
    // chain.save(out_fname, group_name, "")
    TChainWriteBuffer chain_write_buffer(n_x, s.n_save, 1);
    chain_write_buffer.reserve(t_save_max+1);

    for(int t=0; t<t_save_max; t++) {
        chain_write_buffer.add(
            *(chain.at(t)),
            true,   // converged
            std::numeric_limits<double>::quiet_NaN(), // ln(Z)
            NULL,   // Gelman-Rubin statistic
            false   // subsample
        );
    }
    
    chain_write_buffer.write(out_fname, group_name, "discrete-los");
    
    std::stringstream dset_name;
    dset_name << group_name << "/discrete-los";

    double dm_min = params.img_stack->rect->min[1];
    double dm_max = params.img_stack->rect->max[1];
    H5Utils::add_watermark<double>(
        out_fname,
        dset_name.str(),
        "DM_min",
        dm_min
    );
    H5Utils::add_watermark<double>(
        out_fname,
        dset_name.str(),
        "DM_max",
        dm_max
    );
    
    auto t_end = std::chrono::steady_clock::now();
    std::chrono::duration<double> t_runtime = t_end - t_start;
    H5Utils::add_watermark<double>(
        out_fname,
        dset_name.str(),
        "runtime",
        t_runtime.count()
    );

    gsl_rng_free(r);
}


//void propose_neighbor_dominant_dist_swap(
//    TNeighborPixels& neighbor_pixels,
//    const std::vector<uint16_t>& samples,
//    std::vector<uint16_t>& prop_samples,
//    int verbosity)
//{
//    // Look up dominant distance of each pixel
//    std::vector<uint16_t> d_curr;
//    d_curr.reserve(n_pix);
//    for(int pix=0; pix<n_pix; pix++) {
//        d_curr[pix] = neighbor_pixels.get_dominant_dist(pix, samples[pix]);
//    }
//
//    // Find # of shared dominant distances in each distance
//    int dist_max;
//    int n_shared_max = -1;
//    for(int dist=0; dist<n_dists; dist++) {
//        for(int pix=0; pix<n_pix; pix++) {
//            if(
//        }
//    }
//}


void sample_neighbors_pt(
    TNeighborPixels& neighbor_pixels,
    std::vector<uint16_t>& chain,
    int verbosity)
{
    // Sampling parameters
    unsigned int n_temperatures = 5;
    double beta_spacing = 0.70; // Spacing of sampling temperatures (0<x<1. 1 = degenerate)
    unsigned int steps_per_swap = 10;
    unsigned int n_swaps = 200;
    double burnin_frac = 0.3;
    unsigned int n_save = 200;
    double log_shift_weight = -1.;

    int n_steps = n_swaps * steps_per_swap;
    int save_every = n_steps / n_save;
    int save_in = save_every;
    
    int n_swaps_burnin = burnin_frac * n_swaps;
    int n_swaps_tot = n_swaps_burnin + n_swaps;

    double shift_weight = std::exp(log_shift_weight);

    // Pseudo-random number generator
    std::mt19937 r;
    seed_prng(r);
    std::uniform_real_distribution<> uniform_dist(0., 1.0);

    // For each temperature, which sample to select for each neighboring pix
    std::vector<std::unique_ptr<std::vector<uint16_t>>> neighbor_sample;
    std::vector<double> beta; // Temperature of each neighbor sampler
    std::vector<int> gibbs_order; // Order in which to update neighboring pixels
    std::vector<int> temp_order; // Order in which to swap between temperatures
    std::vector<double> log_p_neighbor;
    std::vector<int64_t> n_swaps_proposed(n_temperatures-1, 0);
    std::vector<int64_t> n_swaps_accepted(n_temperatures-1, 0);

    // Initialize temperature ladder
    double b = 1.0;
    beta.reserve(n_temperatures);
    log_p_neighbor.resize(n_temperatures);

    for(int t=0; t<n_temperatures; t++, b*=beta_spacing) {
        neighbor_sample.push_back(std::make_unique<std::vector<uint16_t>>());
        neighbor_sample.at(t)->reserve(neighbor_pixels.get_n_pix());

        if(t == 0) {
            randomize_neighbors(
                neighbor_pixels,
                *neighbor_sample.at(0),
                r
            );
        } else {
            std::copy(
                neighbor_sample.at(0)->begin(),
                neighbor_sample.at(0)->end(),
                std::back_inserter(*(neighbor_sample.at(t)))
            );
        }

        //(*(neighbor_sample.at(t)))[0] = 0;

        beta.push_back(b);

        log_p_neighbor.at(t) = 0.;
    }
    
    // Initialize Gibbs sampling order
    gibbs_order.reserve(neighbor_pixels.get_n_pix());
    for(int i=0; i<neighbor_pixels.get_n_pix(); i++) {
        gibbs_order.push_back(i);
    }
    
    // Initialize Gibbs sampling order
    temp_order.reserve(n_temperatures-1);
    for(int t=1; t<n_temperatures; t++) {
        temp_order.push_back(t);
    }
    
    // Chain to store sampled neighbor indices
    std::vector<uint16_t> sample_chain;
    //TChain chain(n_dim, n_save+1);
    
    // Likelihood and prior of samples
    std::vector<double> logL_chain, logPr_chain;

    int n_pix = neighbor_pixels.get_n_pix();
    int n_samples = neighbor_pixels.get_n_samples();

    logL_chain.reserve(n_save+1);
    logPr_chain.reserve(n_save+1);
    sample_chain.reserve(n_pix*n_save);

    std::vector<double> log_p_sample_ws;
    std::vector<double> p_sample_ws;
    log_p_sample_ws.resize(n_samples);
    p_sample_ws.resize(n_samples);
    
    // Clear output chain
    chain.clear();


    // Cache Gibbs-step samplers
    uint64_t cache_capacity = 10000;
    int step_pix;
    std::vector<
        LRUCache::CachedFunction<
            std::vector<uint16_t>,
            std::shared_ptr<std::discrete_distribution<int>>,
            LRUCache::VectorHasher<uint16_t>
        >
    > gibbs_step_cache;
    gibbs_step_cache.reserve(n_temperatures);
    for(int t=0; t<n_temperatures; t++) {
        gibbs_step_cache.push_back(
        LRUCache::CachedFunction<
            std::vector<uint16_t>,
            std::shared_ptr<std::discrete_distribution<int>>,
            LRUCache::VectorHasher<uint16_t>
        >(
            [
                &step_pix,
                &neighbor_pixels,
                &log_p_sample_ws,
                &p_sample_ws,
                bt=beta.at(t),
                shift_weight
            ]
            (const std::vector<uint16_t>& nbor_samp)
            -> std::shared_ptr<std::discrete_distribution<int>>
        {
            //uint16_t s_tmp = nbor_samp[step_pix];
            //nbor_samp[step_pix] = neighbor_pixels.get_n_pix();
            std::unique_ptr<std::discrete_distribution<int>> dd = 
            neighbor_gibbs_step_shifted_factory(
                step_pix,
                neighbor_pixels,
                nbor_samp,
                log_p_sample_ws,
                p_sample_ws,
                bt,
                shift_weight
            );
            //nbor_samp[step_pix] = s_tmp;
            return std::move(dd);
        },
        cache_capacity,
        nullptr)
        );
    }
    
    int disc_distr_res;
    auto roll_disc_distr = [&r, &disc_distr_res](
        std::shared_ptr<std::discrete_distribution<int>>& dd
    ) -> void
    {
        disc_distr_res = (*dd)(r);
    };

    // ln(p) cache
    LRUCache::CachedFunction<
        std::vector<uint16_t>,
        double,
        LRUCache::VectorHasher<uint16_t>
    > lnp_cache(
        [
            &neighbor_pixels,
            shift_weight
        ](const std::vector<uint16_t>& nbor_samp) -> double {
            return neighbor_pixels.calc_lnprob_shifted(
                nbor_samp,
                shift_weight
            );
        },
        cache_capacity
    );
    
    // Sample
    for(int j=0; j<n_swaps_tot; j++) {
        if(verbosity >= 2) {
            std::cerr << "Swap " << j+1 << " of " << n_swaps_tot << std::endl;
        }

        for(int t=0; t<n_temperatures; t++) {
            for(int l=0; l<steps_per_swap; l++) {
                // Randomize Gibbs step order
                std::shuffle(gibbs_order.begin(),
                             gibbs_order.end(),
                             r);

                // Take a Gibbs step in each neighbor pixel
                for(auto k : gibbs_order) {
                    uint16_t sample_tmp = neighbor_sample[t]->at(k);
                    neighbor_sample[t]->at(k) = n_pix;
                    step_pix = k;
                    gibbs_step_cache.at(t)(
                        *(neighbor_sample[t]),
                        roll_disc_distr
                    );
                    neighbor_sample[t]->at(k) = disc_distr_res;
                    //log_p_neighbor[t] += neighbor_gibbs_step_shifted(
                    //    k,
                    //    neighbor_pixels,
                    //    *(neighbor_sample[t]),
                    //    log_p_sample_ws,
                    //    p_sample_ws,
                    //    r,
                    //    beta.at(t),
                    //    shift_weight
                    //);
                }
            }

            // Recalculate log(p) of this temperature
            log_p_neighbor[t] = lnp_cache(*(neighbor_sample.at(t)));
            //log_p_neighbor[t] = neighbor_pixels.calc_lnprob_shifted(
            //    *(neighbor_sample.at(t)),
            //    shift_weight
            //);
        }
        
        std::cerr << "log_p:";
        for(int t=0; t<n_temperatures; t++) {
            std::cerr << " " << log_p_neighbor[t];
        }
        std::cerr << std::endl;

        // Randomize temperature swap order
        std::shuffle(temp_order.begin(),
                     temp_order.end(),
                     r);

        // Take a swap step between temperatures
        for(auto t : temp_order) {
            // Try to swap t and t-1
            n_swaps_proposed.at(t-1)++;

            double lnp_swap = (
                (beta[t]-beta[t-1]) *
                (log_p_neighbor[t-1]-log_p_neighbor[t])
            );
            
            if(t == 1) {
                std::cerr << "p_swap = " << std::exp(lnp_swap) << std::endl;
            }
            
            if((lnp_swap > 0) || ((lnp_swap > -10.) && (uniform_dist(r) < std::exp(lnp_swap)))) {
                //std::cerr << "swap " << t-1 << " <-> " << t << std::endl;
                neighbor_sample[t].swap(neighbor_sample[t-1]);

                double tmp = log_p_neighbor[t-1];
                log_p_neighbor[t-1] = log_p_neighbor[t];
                log_p_neighbor[t] = tmp;

                n_swaps_accepted.at(t-1)++;
            } else {
                //std::cerr << "no swap " << t-1 << " <-> " << t << std::endl;
            }
        }
        
        if((j >= n_swaps_burnin) && (--save_in == 0)) {
            save_in = save_every;

            // Save point
            //sample_chain.insert(
            //    sample_chain.end(),
            //    neighbor_sample.at(0)->begin(),
            //    neighbor_sample.at(0)->end()
            //);
            chain.insert(
                chain.end(),
                neighbor_sample.at(0)->begin(),
                neighbor_sample.at(0)->end()
            );
            
            // Calculate ln(p)
            double lnP = neighbor_pixels.calc_lnprob(*(neighbor_sample.at(0)));
            
            //chain.add_point(neighbor_sample.at(0)->data(), lnP, 1.);
            
            if(verbosity >= 2) {
                for(int t=0; t<n_temperatures; t++) {
                    lnP = neighbor_pixels.calc_lnprob(*(neighbor_sample.at(t)));

                    std::cerr << "neighbor samples:";
                    for(auto s : *(neighbor_sample.at(t))) {
                        std::cerr << " " << s;
                    }
                    std::cerr << " -> " << lnP;
                    std::cerr << std::endl;

                    int dist_max;
                    for(int pix=0; pix<n_pix; pix++) {
                        dist_max = neighbor_pixels.get_dominant_dist(
                                pix,
                                neighbor_sample.at(t)->at(pix)
                        );
                        std::cerr << " " << dist_max;
                    }
                    std::cerr << std::endl;
                }
                std::cerr << std::endl;
            }

            if(j == n_swaps_burnin) {
                for(int t=0; t<n_temperatures-1; t++) {
                    n_swaps_proposed.at(t) = 0.;
                    n_swaps_accepted.at(t) = 0.;
                }
            }
        }
    }

    if(verbosity >= 1) {
        double swap_pct;
        std::cerr << "Swap acceptance %:";
        for(int t=0; t<n_temperatures-1; t++) {
            swap_pct = 100. * (double)n_swaps_accepted.at(t)
                       / (n_swaps_proposed.at(t) + n_swaps_accepted.at(t));
            std::cerr << " " << swap_pct;
        }
        std::cerr << std::endl;
    }
    
    //TChainWriteBuffer chain_write_buffer(n_dim, n_save, 1);
    //chain_write_buffer.add(
    //    chain,
    //    true,   // converged
    //    std::numeric_limits<double>::quiet_NaN(), // ln(Z)
    //    NULL,   // Gelman-Rubin statistic
    //    false   // subsample
    //);
    //chain_write_buffer.write(out_fname, group_name, "neighbor_samples");
    
}


void seed_prng(std::mt19937& r) {
    // Seeds a pseudo-random number generator from the stdlib, using
    // both the system's random device and the high-resolution clock.
    
    std::random_device rd;
    std::vector<long unsigned int> seeds = {
        rd(), 
        static_cast<long unsigned int>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
        )
    };
    std::seed_seq seq(seeds.begin(), seeds.end());

    r.seed(seq);
}


void sample_neighbors(
    TNeighborPixels& neighbors,
    int verbosity)
{
    int n_burnin = 10000;
    int n_steps = 100000;

    unsigned int n_samples_max = 50;
    unsigned int n_samples = std::min(n_samples_max, neighbors.get_n_samples());

    auto f_prob = [&neighbors](const std::vector<uint16_t>& samp_idx) -> double {
        return neighbors.calc_lnprob(samp_idx);
    };

    double lnp0 = 0.;
    std::vector<uint16_t> samp_idx_tmp;
    samp_idx_tmp.resize(neighbors.get_n_pix());
    
    // Pseudo-random number generator
    std::random_device rd;
    std::vector<long unsigned int> seeds = {
        rd(), 
        static_cast<long unsigned int>(std::chrono::high_resolution_clock::now().time_since_epoch().count())
    };
    std::seed_seq seq(seeds.begin(), seeds.end());
    std::mt19937 r(seq);

    std::uniform_int_distribution<uint16_t> d(0, n_samples-1);

    for(int n=0; n<100; n++) {
        for(auto& s : samp_idx_tmp) {
            s = d(r);
        }
        lnp0 += f_prob(samp_idx_tmp);
    }

    lnp0 /= 100.;
    lnp0 *= 0.5;
    std::cout << "lnp0 = " << lnp0 << std::endl;

    bridgesamp::BridgingSampler sampler(
        neighbors.get_n_pix(),
        n_samples,
        f_prob
    );

    sampler.set_logp0(lnp0 + 5.);

    sampler.randomize_state();

    for(int i=0; i<n_burnin; i++) {
        sampler.step();
    }

    //for(auto it=sampler.cbegin(); it != sampler.cend(); ++it) {
    //    for(auto s : it->first) {
    //        if(s == neighbors.get_n_samples()) {
    //            std::cerr << "- ";
    //        } else {
    //            std::cerr << s << " ";
    //        }
    //    }
    //    std::cerr << ": " << it->second.logp << std::endl;
    //}

    std::map<std::vector<uint16_t>, uint32_t> n_visits;
    double logp_max = -std::numeric_limits<double>::infinity();
    double logp;

    for(int n=0; n<n_steps; n++) {
        sampler.step();
        n_visits[sampler.get_state()]++;

        if(sampler.get_state_rank() == 0) {
            logp = sampler.get_logp();
            if(logp > logp_max) {
                logp_max = logp;
            }
            std::cout << "log(p) = " << logp << " (<= " << logp_max << " )" << std::endl;
            std::cout << "  " << 100.*sampler.fill_factor() << "%" << std::endl;
        }
    }

    // Print out the number of visits to each state
    std::cout << "# of visits:"
              << std::endl;

    for(auto& v : n_visits) {
        for(auto s : v.first) {
            if(s == sampler.get_n_samples()) {
                std::cout << "- ";
            } else {
                std::cout << s << " ";
            }
        }
        std::cout << ": " << v.second << std::endl;
    }
}


/****************************************************************************************************************************
 *
 * TImgStack
 *
 ****************************************************************************************************************************/

TImgStack::TImgStack(size_t _N_images) {
    N_images = _N_images;
    img = new cv::Mat*[N_images];
    for(size_t i=0; i<N_images; i++) {
        img[i] = new cv::Mat;
    }
    rect = NULL;
}

TImgStack::TImgStack(size_t _N_images, TRect& _rect) {
    N_images = _N_images;
    img = new cv::Mat*[N_images];
    for(size_t i=0; i<N_images; i++) { img[i] = NULL; }
    rect = new TRect(_rect);
}

TImgStack::~TImgStack() {
    if(img != NULL) {
        for(size_t i=0; i<N_images; i++) {
            if(img[i] != NULL) {
                delete img[i];
            }
        }
        delete[] img;
    }
    if(rect != NULL) { delete rect; }
}

void TImgStack::resize(size_t _N_images) {
    if(img != NULL) {
        for(size_t i=0; i<N_images; i++) {
            if(img[i] != NULL) {
                delete img[i];
            }
        }
        delete[] img;
    }
    // if(rect != NULL) { delete rect; }

    N_images = _N_images;
    img = new cv::Mat*[N_images];
    for(size_t i=0; i<N_images; i++) {
        img[i] = new cv::Mat;
    }
}

void TImgStack::cull(const std::vector<bool> &keep) {
    assert(keep.size() == N_images);

    size_t N_tmp = 0;
    for(std::vector<bool>::const_iterator it = keep.begin(); it != keep.end(); ++it) {
        if(*it) { N_tmp++; }
    }

    cv::Mat **img_tmp = new cv::Mat*[N_tmp];
    size_t i = 0;
    size_t k = 0;
    for(std::vector<bool>::const_iterator it = keep.begin(); it != keep.end(); ++it, ++i) {
        if(*it) {
            img_tmp[k] = img[i];
            k++;
        } else {
            delete img[i];
        }
    }

    delete[] img;
    img = img_tmp;
    N_images = N_tmp;
}

void TImgStack::crop(double x_min, double x_max, double y_min, double y_max) {
    assert(x_min < x_max);
    assert(y_min < y_max);

    uint32_t x0, x1, y0, y1;

    if(x_min <= rect->min[0]) {
        x0 = 0;
    } else {
        x0 = (int)floor((x_min - rect->min[0]) / rect->dx[0]);
    }

    if(x_max >= rect->max[0]) {
        x1 = rect->N_bins[0];
    } else {
        x1 = rect->N_bins[0] - (int)floor((rect->max[0] - x_max) / rect->dx[0]);
    }

    if(y_min <= rect->min[1]) {
        y0 = 0;
    } else {
        y0 = (int)floor((y_min - rect->min[1]) / rect->dx[1]);
    }

    if(y_max >= rect->max[1]) {
        y1 = rect->N_bins[1];
    } else {
        y1 = rect->N_bins[1] - (int)floor((rect->max[1] - y_max) / rect->dx[1]);
    }

    std::cerr << "Cropping images to (" << x0 << ", " << x1 << "), "
                          << "(" << y0 << ", " << y1 << ")" << std::endl;

    assert(x1 > x0);
    assert(y1 > y0);

    cv::Rect crop_region(y0, x0, y1-y0, x1-x0);

    for(int i=0; i<N_images; i++) {
        *(img[i]) = (*(img[i]))(crop_region);
    }

    double xmin_new = rect->min[0] + x0 * rect->dx[0];
    double xmax_new = rect->min[0] + x1 * rect->dx[0];

    double ymin_new = rect->min[1] + y0 * rect->dx[1];
    double ymax_new = rect->min[1] + y1 * rect->dx[1];

    std::cerr << "New image limits: "
              << "(" << xmin_new << ", " << xmax_new << ") "
              << "(" << ymin_new << ", " << ymax_new << ")"
              << std::endl;

    rect->min[0] = xmin_new;
    rect->min[1] = ymin_new;
    rect->max[0] = xmax_new;
    rect->max[1] = ymax_new;
    rect->N_bins[0] = x1 - x0;
    rect->N_bins[1] = y1 - y0;
}

void TImgStack::set_rect(TRect& _rect) {
    if(rect != NULL) {
        delete rect;
    }
    rect = new TRect(_rect);
}

void TImgStack::stack(cv::Mat& dest) {
    if(N_images > 0) {
        dest = *(img[0]);
        for(size_t i=1; i<N_images; i++) {
            dest += *(img[i]);
        }
    } else {
        dest.setTo(0);
    }
}

bool TImgStack::initialize_to_zero(unsigned int img_idx) {
    if(img_idx > N_images) { return false; }
    if(rect == NULL) { return false; }
    if(img[img_idx] == NULL) {
        img[img_idx] = new cv::Mat;
    }
    *(img[img_idx]) = cv::Mat::zeros(rect->N_bins[0], rect->N_bins[1], CV_FLOATING_TYPE);
    return true;
}


void TImgStack::smooth(std::vector<double> sigma, double n_sigma) {
    const int N_rows = rect->N_bins[0];
    const int N_cols = rect->N_bins[1];

    assert(sigma.size() == N_rows);
    assert(n_sigma > 0);

    // Source and destination rows for convolution
    floating_t *src_img_row_up, *src_img_row_down, *dest_img_row;
    int src_row_idx_up, src_row_idx_down;

    // Weight applied to each row
    floating_t *dc = new floating_t[N_rows];
    floating_t a, c;

    // Number of times to shift image (= sigma * n_sigma)
    int m_max;

    // Loop over images
    for(int i=0; i<N_images; i++) {
        // Skip uninitialized images (nothing to smooth)
        if(img[i] == NULL) {
            continue;
        }
        
        // Create copy of image
        cv::Mat *img_s = new cv::Mat(img[i]->clone());

        // Loop over destination rows
        for(int dest_row_idx=0; dest_row_idx<N_rows; dest_row_idx++) {
            // Determine kernel width (based on sigma at destination)
            m_max = int(ceil(sigma[dest_row_idx] * n_sigma));
            if(m_max > N_rows) { m_max = N_rows; }

            // Determine weight to apply to each source pixel
            a = -0.5 / (sigma[dest_row_idx]*sigma[dest_row_idx]);
            c = 1.;

            for(int m=1; m<m_max; m++) {
                dc[m] = exp(a * (floating_t)(m*m));
                c += 2. * dc[m];
            }

            // Normalize weights to sum to 1
            a = 1. / c;

            // Zero row offset
            img_s->row(dest_row_idx) *= a;

            // Loop over row offsets (other than 0)
            for(int m=1; m<m_max; m++) {
                dc[m] *= a;
                src_row_idx_up = dest_row_idx + m;
                src_row_idx_down = dest_row_idx - m;

                if(src_row_idx_up >= N_rows) { src_row_idx_up = N_rows - 1; }
                if(src_row_idx_down < 0) { src_row_idx_down = 0; }

                // Loop over images
                if(img[i] != NULL) {
                    dest_img_row = img_s->ptr<floating_t>(dest_row_idx);
                    src_img_row_up = img[i]->ptr<floating_t>(src_row_idx_up);
                    src_img_row_down = img[i]->ptr<floating_t>(src_row_idx_down);

                    // Loop over columns
                    for(int col=0; col<N_cols; col++) {
                        dest_img_row[col] += dc[m] * (
                            src_img_row_up[col] + src_img_row_down[col]
                        );
                    }
                }
            }
        }
        
        // Swap out old image for smoothed image
        delete img[i];
        img[i] = img_s;
    }

    // Cleanup
    delete[] dc;
}


void TImgStack::normalize(double norm) {
    // Calculate and divide out sum of each matrix
    for(int i=0; i<N_images; i++) {
        double sum_img = cv::sum(*(img[i]))[0];
        
        if(sum_img < 1.e-30) {
            sum_img = 1.;
        }
        
        *(img[i]) *= norm/sum_img;
    }
}


std::unique_ptr<TImgStack> read_img_stack(
    const std::string& fname,
    const std::string& dset
) {
    // Open dataset
	std::unique_ptr<H5::H5File> f = H5Utils::openFile(fname, H5Utils::READ);
    if(!f) { return std::unique_ptr<TImgStack>(nullptr); }
	std::unique_ptr<H5::DataSet> d = H5Utils::openDataSet(*f, dset);
    if(!d) { return std::unique_ptr<TImgStack>(nullptr); }

    // Read dimensions of image stack
    size_t n_images;
    double min[2], max[2];
    uint32_t n_pix[2];
    
    std::cout << "Reading image metadata (nPix,min,max) ..." << std::endl;
    H5::Attribute a_npix = d->openAttribute("nPix");
    H5::Attribute a_min = d->openAttribute("min");
    H5::Attribute a_max = d->openAttribute("max");
    
    std::vector<uint32_t> n_pix_vec = H5Utils::read_attribute_1d<uint32_t>(a_npix);
    std::vector<double> min_vec = H5Utils::read_attribute_1d<double>(a_min);
    std::vector<double> max_vec = H5Utils::read_attribute_1d<double>(a_max);
    
    assert(n_pix_vec.size() == 2);
    assert(min_vec.size() == 2);
    assert(max_vec.size() == 2);
    
    std::copy(n_pix_vec.begin(), n_pix_vec.end(), &(n_pix[0]));
    std::copy(min_vec.begin(), min_vec.end(), &(min[0]));
    std::copy(max_vec.begin(), max_vec.end(), &(max[0]));

    H5::DataSpace dspace = d->getSpace();
    const hsize_t img_n_dims = dspace.getSimpleExtentNdims();
    assert(img_n_dims == 3);
    hsize_t img_shape[3];
    dspace.getSimpleExtentDims(&(img_shape[0]));
    n_images = img_shape[0];
    assert(img_shape[1] == n_pix[0]);
    assert(img_shape[2] == n_pix[1]);
    
    // Initialize image stack
    TRect rect(min, max, n_pix);
    auto img_stack = std::unique_ptr<TImgStack>(new TImgStack(n_images, rect));

    for(size_t i=0; i<n_images; i++) {
        bool res = img_stack->initialize_to_zero(i);
        if(!res) { return std::unique_ptr<TImgStack>(nullptr); }
    }
    
    // Read in images
    std::cout << "Reading image data ..." << std::endl;
	float *buf = new float[n_pix[0] * n_pix[1] * n_images];
    d->read(buf, H5Utils::get_dtype<float>());
    
    for(size_t i=0; i<n_images; i++) {
        cv::Mat *img = img_stack->img[i];
        size_t i0 = i * n_pix[0]*n_pix[1];
        for(size_t j=0; j<n_pix[0]; j++) {
            for(size_t k=0; k<n_pix[1]; k++) {
                img->at<floating_t>(j,k) = buf[i0 + n_pix[1]*j + k];
            }
        }
    }
    
    delete[] buf;

    // Return image stack
    return img_stack;
}


// void shift_image_vertical(cv::Mat& img, int n_pix) {
//
// }



/****************************************************************************************************************************
 *
 * TLOSTransform
 *
 ****************************************************************************************************************************/

TLOSTransform::TLOSTransform(unsigned int ndim)
    : TTransformParamSpace(ndim), _ndim(ndim)
{}

TLOSTransform::~TLOSTransform()
{}

void TLOSTransform::transform(const double *const x, double *const y) {
    y[0] = exp(x[0]);
    for(unsigned int i=1; i<_ndim; i++) {
        y[i] = y[i-1] + exp(x[i]);
    }
}

TLOSCloudTransform::TLOSCloudTransform(unsigned int ndim)
    : TTransformParamSpace(ndim), _ndim(ndim)
{
    assert(!(ndim & 1));
    n_clouds = ndim / 2;
}

TLOSCloudTransform::~TLOSCloudTransform()
{}

void TLOSCloudTransform::transform(const double *const x, double *const y) {
    y[0] = x[0];
    y[n_clouds] = exp(x[n_clouds]);
    for(unsigned int i=1; i<n_clouds; i++) {
        y[i] = x[i];
        y[n_clouds+i] = exp(x[n_clouds+i]);
    }
}
