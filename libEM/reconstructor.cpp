/**
 * $Id$
 */

/*
 * Author: Steven Ludtke, 04/10/2003 (sludtke@bcm.edu)
 * Copyright (c) 2000-2006 Baylor College of Medicine
 * 
 * This software is issued under a joint BSD/GNU license. You may use the
 * source code in this file under either license. However, note that the
 * complete EMAN2 and SPARX software packages have some GPL dependencies,
 * so you are responsible for compliance with the licenses of these packages
 * if you opt to use BSD licensing. The warranty disclaimer below holds
 * in either instance.
 * 
 * This complete copyright notice must be included in any revised version of the
 * source code. Additional authorship citations may be added, but existing
 * author citations must be preserved.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * */

#include <fstream>
#include <iomanip>
#include <boost/bind.hpp>
#include <boost/format.hpp>
#include "reconstructor.h"
#include "ctf.h"

#include <gsl_statistics_double.h>
#include <gsl_fit.h>

using namespace EMAN;
using std::complex;


#include <iostream>
using std::cerr;
using std::endl;
using std::cout; // for debug 

#include <algorithm>
// find


// Static init
map<unsigned int, FourierPixelInserterMaker* > FourierPixelInserterMaker::maker_registry;
const FourierInserter3DMode7Maker::FourierInserter3DMode7Maker registerMode7Maker;
const FourierInserter3DMode6Maker::FourierInserter3DMode6Maker registerMode6Maker;
const FourierInserter3DMode5Maker::FourierInserter3DMode5Maker registerMode5Maker;
const FourierInserter3DMode4Maker::FourierInserter3DMode4Maker registerMode4Maker;
const FourierInserter3DMode3Maker::FourierInserter3DMode3Maker registerMode3Maker;
const FourierInserter3DMode2Maker::FourierInserter3DMode2Maker registerMode2Maker;
const FourierInserter3DMode1Maker::FourierInserter3DMode1Maker registerMode1Maker;

template < typename T > void checked_delete( T*& x )
{
    typedef char type_must_be_complete[ sizeof(T)? 1: -1 ];
    (void) sizeof(type_must_be_complete);
    delete x;
    x = NULL;
}


template <> Factory < Reconstructor >::Factory()
{
	force_add(&FourierReconstructor::NEW);
	force_add(&WienerFourierReconstructor::NEW);
	force_add(&BackProjectionReconstructor::NEW);
	force_add(&nn4Reconstructor::NEW);
	force_add(&nnSSNR_Reconstructor::NEW);
	force_add(&nn4_ctfReconstructor::NEW);
	force_add(&nnSSNR_ctfReconstructor::NEW);
	force_add(&bootstrap_nnReconstructor::NEW); 
	force_add(&bootstrap_nnctfReconstructor::NEW); 
}


Reconstructor::Reconstructor( const Reconstructor& that ) : ReconstructorWithVolumeData(that)
{
	copyData(that);
}

Reconstructor& Reconstructor::operator=( const Reconstructor& that )
{
	if ( this != &that )
	{
		copyData(that);
		// Make sure the base class performs the assignment also.
		ReconstructorWithVolumeData::operator=(that);
	}
	else
	{
		cerr << "Warning: attempted to assign a reconstructor to itself, no action taken" << endl;
	}
	return *this;
}

void Reconstructor::copyData( const Reconstructor& that )
{
	params = that.params;
}

ReconstructorWithVolumeData& ReconstructorWithVolumeData::operator=( const ReconstructorWithVolumeData& that )
{
	if ( this != &that )
	{
		copyData(that);
	}
	else
	{
		cerr << "Warning: attempted to assign a ReconstructorWithVolumeData to itself, no action taken" << endl;
	}

	return *this;
}

void ReconstructorWithVolumeData::copyData( const ReconstructorWithVolumeData& that )
{
	nx = that.nx;
	ny = that.ny;
	nz = that.nz;

	// No matter what the image pointera and tmp_data pointer must be freed
	free_memory();

	if ( that.image != 0 )
	{
		image = new EMData(*that.image);
	}

	if ( that.tmp_data != 0 )
	{
		tmp_data = new EMData(*that.tmp_data);
	}
}

FourierReconstructor& FourierReconstructor::operator=( const FourierReconstructor& that )
{
	if ( this != &that )
	{
		// Make sure the base class performs the assignment also.
		Reconstructor::operator=(that);
	}
	else
	{
		cerr << "Warning: attempted to assign a FourierReconstructor to itself, no action taken" << endl;
	}

	return *this;
}

void FourierReconstructor::setup()
{
	int size = params["size"];
	if ( params.find("pad") != params.end() )
	{
		if ( (int)(params["pad"]) > (int)(params["size"]) )
			size = params["pad"];
		else
		{
			throw InvalidValueException((int)params["pad"], "pad must be > than image size");
		}
	}
	else
		size = params["size"];

	image = new EMData();
	image->set_size(size + 2, size, size);
	image->set_complex(true);
	image->set_ri(true);

	nx = image->get_xsize();
	ny = image->get_ysize();
	nz = image->get_zsize();

	int n = nx * ny * nz;
	float *rdata = image->get_data();

	for (int i = 0; i < n; i += 2) {
		float f = Util::get_frand(0.0, 2.0 * M_PI);
		rdata[i] = 1.0e-10f * sin(f);
		rdata[i + 1] = 1.0e-10f * cos(f);
	}
	image->update();

	tmp_data = new EMData();
	tmp_data->set_size(size + 2, size, size);
	tmp_data->to_zero();
	tmp_data->update();
}

EMData* FourierReconstructor::preprocess_slice( const EMData* const slice )
{
	EMData* return_slice;

	// Apply padding if the option has been set, and it's sensible
	if ( params.find("pad") != params.end() )
	{
		int pad = params["pad"];
		int x_size = slice->get_xsize();
		int y_size = slice->get_ysize();
		if ( x_size >= pad || y_size >= pad )
		{
			cerr << "You specified a pad size " <<  pad << " that was smaller than or equal to the x " << x_size << " or y " << y_size << " dimension" << endl;
			throw InvalidValueException(pad, "pad must be > than image size");	
		}
		else
		{
			// x_size and y_size should always be the same size atm, but there may come a time when they are not.
			return_slice = slice->get_clip(Region((x_size-pad)/2,(y_size-pad)/2,pad,pad));
		}
	}
	else
	{
		return_slice = new EMData(*slice);
	}

	// Shift the image pixels so the real space origin is now located at the phase origin (to work with FFTW) (d.woolford)
	return_slice->process_inplace("xform.phaseorigin");

	// Fourier transform the slice
	return_slice->do_fft_inplace();
	
	// Shift the Fourier transform so that it's origin is in the center (bottom) of the image.
	return_slice->process_inplace("xform.fourierorigin");

	return return_slice;
}

int FourierReconstructor::insert_slice(const EMData* const input_slice, const Transform3D & arg)
{
	if (!input_slice) {
		LOGERR("Insertion of NULL slice in FourierReconstructor::insert_slice");
		return 1;
	}

	if (input_slice->is_complex()) {
		LOGERR("Do not Fourier transform the image before it is passed to insert_slice in the FourierReconstructor, this is performed internally");
		return 1;
	}

	// Get the proprecessed slice - there are some things that always happen to a slice,
	// such as as Fourier conversion and optional padding etc.
	EMData* slice = preprocess_slice( input_slice );

	if (!slice->is_complex()) {
		LOGERR("Only a complex slice can be inserted. The preprocessing of the input slice in FourierReconstructor::insert_slice failed");
		return 1;
	}

	int mode = params["mode"];
	float weight = params["weight"];

	float *norm = tmp_data->get_data();
	float *dat = slice->get_data();
	float *rdata = image->get_data();

	int rl = Util::square(ny / 2 - 1);

	FourierPixelInserter3D* inserter = FourierPixelInserterMaker::get_inserter(mode,norm, rdata, nx, ny, nz);
	if ( inserter == 0 )
	{
		LOGERR("no such insert slice mode: '%d'", mode);
		// FIXME is there a exception thing to throw?
		throw InvalidCallException("no such insert slice mode:");
	}

	if ( idx < quality_scores.size() )
	{
		//cout << "The quality score of " << quality_scores[idx].second << " was compared to " << (float) params["hard"];
		if ( quality_scores[idx].get_snr_normed_frc_integral() < (float) params["hard"] )
		{
			cout << quality_scores[idx].get_snr_normed_frc_integral() << " was less than " << (float) params["hard"] << endl;
			//cout << "....fail";
			idx++;
			return 1;
		}
		//cout << endl;
		idx++;
	}
	
	for ( int i = 0; i < Transform3D::get_nsym((string)params["sym"]); ++i)
	{
		Transform3D euler = arg.get_sym((string) params["sym"], i);

		for (int y = 0; y < ny; y++) {
			for (int x = 0; x < nx / 2; x++) {
				if ((x * x + Util::square(y - ny / 2)) >= rl)
					continue;
	
				float xx = (float) (x * euler[0][0] + (y - ny / 2) * euler[1][0]);
				float yy = (float) (x * euler[0][1] + (y - ny / 2) * euler[1][1]);
				float zz = (float) (x * euler[0][2] + (y - ny / 2) * euler[1][2]);
				float cc = 1;
	
				if (xx < 0) {
					xx = -xx;
					yy = -yy;
					zz = -zz;
					cc = -1.0;
				}
				
				yy += ny / 2;
				zz += nz / 2;
				
				float dt[2];
				dt[0] = dat[x * 2 + y * nx];
				dt[1] = cc * dat[x * 2 + 1 + y * nx];

				inserter->insert_pixel(xx,yy,zz,dt,weight);
			}
		}
	}

	delete inserter;
	delete slice;	

	image->update();

	return 0;
}

InterpolatedFRC::InterpolatedFRC(float* const rdata, const int xsize, const int ysize, const int zsize, const float& sampling ) :
	threed_rdata(rdata), nx(xsize), ny(ysize), nz(zsize), nxy(xsize*ysize), bin(sampling)
{
	if ( sampling <= 0 )
	{	
		throw InvalidValueException(sampling, "Error: sampling must be greater than 0");
	}

	pixel_radius_max = ny/2;
	pixel_radius_max_square = Util::square( (int) pixel_radius_max );

	size = static_cast<int>(pixel_radius_max*bin);
	// The parentheses effectively cause initialization by 0 (apparently) - this should be tested because without 0 initialization this objects behavior will be unexpected. 
	frc = new float[size]();
	frc_norm_rdata = new float[size]();
	frc_norm_dt = new float[size]();

	off[0] = 0;
	off[1] = 2;
	off[2] = nx;
	off[3] = nx + 2;
	off[4] = nxy;
	off[5] = nxy + 2;
	off[6] = nxy + nx;
	off[7] = nxy + nx + 2;
}

InterpolatedFRC::InterpolatedFRC( const InterpolatedFRC& that ) :
	threed_rdata(that.threed_rdata), nx(that.nx), ny(that.ny), nz(that.nz), nxy(that.nxy), bin(that.bin),
	 size(that.size), pixel_radius_max(that.pixel_radius_max), pixel_radius_max_square(that.pixel_radius_max_square)
{
	frc = new float[size]();
	frc_norm_rdata = new float[size]();
	frc_norm_dt = new float[size]();

	off[0] = 0;
	off[1] = 2;
	off[2] = nx;
	off[3] = nx + 2;
	off[4] = nxy;
	off[5] = nxy + 2;
	off[6] = nxy + nx;
	off[7] = nxy + nx + 2;
}

InterpolatedFRC& InterpolatedFRC::operator=( const InterpolatedFRC& that)
{
	if (this != &that)
	{
		threed_rdata = that.threed_rdata;
		nx = that.nx; ny = that.ny; nz = that.nz; nxy = that.nxy; bin = that.bin;
	 	size = that.size; pixel_radius_max = that.pixel_radius_max; pixel_radius_max_square = that.pixel_radius_max_square;

		free_memory();

		frc = new float[size]();
		frc_norm_rdata = new float[size]();
		frc_norm_dt = new float[size]();
	
		off[0] = 0;
		off[1] = 2;
		off[2] = nx;
		off[3] = nx + 2;
		off[4] = nxy;
		off[5] = nxy + 2;
		off[6] = nxy + nx;
		off[7] = nxy + nx + 2;
	}

	return *this;
}


void InterpolatedFRC::reset()
{
	memset(frc, 0, size*sizeof(float));
	memset(frc_norm_rdata, 0, size*sizeof(float));
	memset(frc_norm_dt, 0, size*sizeof(float));
}

bool InterpolatedFRC::continue_frc_calc(const float& xx, const float& yy, const float& zz, const float dt[], const float& weight)
{
	int x0 = (int) floor(xx);
	int y0 = (int) floor(yy);
	int z0 = (int) floor(zz);
 
	if (x0 > nx - 2 || y0 > ny - 1 || z0 > nz - 1)
	{
		return false;
	}
	
	// Have to get radial coordinates - x is fine as it is but the other two need translation ( after testing it seems like z does not need translation - more testing required)
	int offset = (ny%2 == 0? 1:0);
	int yt = y0 - ny/2 - offset;
	int zt = z0 - nz/2;

	int radius =  x0*x0 + yt*yt + zt*zt;
	radius = static_cast<int>(sqrtf(radius)*bin);

	// debug
	if ( radius > (size-1) )
	{
		//cout is debug
		//cout << "radius " << radius << " was greater than or equal to size " << size  << endl;
		return false;
	}

	float dx = xx - x0;
	float dy = yy - y0;
	float dz = zz - z0;

	int i = (int) (x0 * 2 + y0 * nx + z0 * nxy);

	g[0] = Util::agauss(1, dx, dy, dz, EMConsts::I2G);
	g[1] = Util::agauss(1, 1 - dx, dy, dz, EMConsts::I2G);
	g[2] = Util::agauss(1, dx, 1 - dy, dz, EMConsts::I2G);
	g[3] = Util::agauss(1, 1 - dx, 1 - dy, dz, EMConsts::I2G);
	g[4] = Util::agauss(1, dx, dy, 1 - dz, EMConsts::I2G);
	g[5] = Util::agauss(1, 1 - dx, dy, 1 - dz, EMConsts::I2G);
	g[6] = Util::agauss(1, dx, 1 - dy, 1 - dz, EMConsts::I2G);
	g[7] = Util::agauss(1, 1 - dx, 1 - dy, 1 - dz, EMConsts::I2G);


	// The reverse interpolated point
	float interp_real = 0, interp_comp = 0;
	
	for (int j = 0; j < 8; j++) {
		int k = i + off[j];
		interp_real += (threed_rdata[k] - weight * dt[0] * g[j]) * g[j];
		interp_comp += (threed_rdata[k+1] - weight * dt[1] * g[j]) * g[j];
	}
	
// 	if ( radius == 0 )
// 		cout << "interp was " << interp_real << " " << interp_comp << " actual was " << dt[0] << " " << dt[1] << endl;
	
	frc[radius] += interp_real*dt[0] + interp_comp*dt[1];

	frc_norm_rdata[radius] += interp_real*interp_real + interp_comp*interp_comp;
	
	frc_norm_dt[radius] +=  dt[0] * dt[0] + dt[1] * dt[1];

	return true;
}

QualityScores InterpolatedFRC::finish(const unsigned int num_particles)
{
	float frc_integral = 0, snr_normed_frc_intergral = 0, normed_snr_integral = 0;

	int contrib = 0;

	float contrib_thresh = 0.01;

	for( int i = 0; i < size; ++i )
	{
		if ( frc_norm_rdata[i] == 0 || frc_norm_dt[i] == 0 )
			frc[i] = 0;
		else
			frc[i] /= sqrtf(frc_norm_rdata[i]*frc_norm_dt[i]);

		if ( frc[i] < contrib_thresh ) continue;
		contrib++;
		// Accumulate the frc integral - atm this is for testing purposes but could change
		frc_integral += frc[i];
		
		

		float tmp = frc[i]*frc[i];
		if ( tmp == 1 )
		{
			tmp = 0.999;
			//cout << "one encountered" << endl;
		}

		// This shouldn't happen and at the moment is for debug only
		if ( tmp > 1 )
		{
			cout << " tmp " << tmp << " div by " << (1.0-tmp) << " equals " << (tmp/(1.0-tmp));
		}
		
		float adjusted_ssnr = tmp/((1.0-tmp)*num_particles);
		normed_snr_integral += adjusted_ssnr;
		snr_normed_frc_intergral += sqrtf(adjusted_ssnr/( 1.0 + adjusted_ssnr ));
	}

	frc_integral /= contrib;
	snr_normed_frc_intergral /= contrib;
	normed_snr_integral /= contrib;

	QualityScores quality_scores;
	quality_scores.set_frc_integral( frc_integral );
	quality_scores.set_snr_normed_frc_integral( snr_normed_frc_intergral );
	quality_scores.set_normed_snr_integral( normed_snr_integral );
	
	return quality_scores;
}


int FourierReconstructor::determine_slice_agreement(const EMData* const input_slice, const Transform3D & euler, const unsigned int  num_particles_in_slice )
{
	if (!input_slice) {
		LOGERR("Insertion of NULL slice in FourierReconstructor::insert_slice");
		return 1;
	}

	if (input_slice->is_complex()) {
		LOGERR("Do not Fourier transform the image before it is passed to determine_slice_agreement in FourierReconstructor, this is performed internally");
		return 1;
	}

	// Get the proprecessed slice - there are some things that always happen to a slice,
	// such as as Fourier conversion and optional padding etc.
	EMData* slice = preprocess_slice( input_slice );

	float *dat = slice->get_data();

	int rl = Util::square(ny / 2 - 1);

	InterpolatedFRC ifrc = InterpolatedFRC(image->get_data(), nx, ny, nz );
	ifrc.reset();

	for (int y = 0; y < ny; y++) {
		for (int x = 0; x < nx / 2; x++) {
			if ((x * x + Util::square(y - ny / 2)) >= rl)
				continue;

			float xx = (float) (x * euler[0][0] + (y - ny / 2) * euler[1][0]);
			float yy = (float) (x * euler[0][1] + (y - ny / 2) * euler[1][1]);
			float zz = (float) (x * euler[0][2] + (y - ny / 2) * euler[1][2]);
			float cc = 1;

			if (xx < 0) {
				xx = -xx;
				yy = -yy;
				zz = -zz;
				cc = -1.0;
			}

			yy += ny / 2;
			zz += nz / 2;
			
			float dt[2];
			dt[0] = dat[x * 2 + y * nx];
			dt[1] = cc * dat[x * 2 + 1 + y * nx];

			ifrc.continue_frc_calc(xx, yy, zz, dt, (float)params["weight"]);
		}
	}

	QualityScores q_scores = ifrc.finish( num_particles_in_slice );
// 	q_scores.debug_print();
	quality_scores.push_back(q_scores);

	delete slice;
	return 0;
}

EMData *FourierReconstructor::finish()
{
	float *norm = tmp_data->get_data();
	float *rdata = image->get_data();

	if (params["dlog"]) {
		for (int i = 0; i < nx * ny * nz; i += 2) {
			float d = norm[i];
			if (d == 0) {
				rdata[i] = 0;
				rdata[i + 1] = 0;
			}
			else {
				float in = norm[i + 1] / norm[i];
				float cin = Util::square_sum(rdata[i], rdata[i + 1]);
				rdata[i] *= sqrt(in / cin);
				rdata[i + 1] *= sqrt(in / cin);
			}
		}
	}
	else {
		for (int i = 0; i < nx * ny * nz; i += 2) {
			float d = norm[i];
			if (d == 0) {
				rdata[i] = 0;
				rdata[i + 1] = 0;
			}
			else {
				rdata[i] /= d;
				rdata[i + 1] /= d;
			}
		}
	}

	if( tmp_data ) {
		delete tmp_data;
		tmp_data = 0;
		norm = 0;
	}
	
	// 
	image->process_inplace("xform.fourierorigin");
	image->do_ift_inplace();
	// FIXME - when the memory issue is sorted this depad call should probably not be necessary
	image->postift_depad_corner_inplace();
	image->process_inplace("xform.phaseorigin");
	
	// If the image was padded it should be age size, as the client would expect
	if ( params.find("pad") != params.end() )
	{
		int pad = params["pad"];
		int size = params["size"];
		FloatPoint point((pad-size)/2, (pad-size)/2, (pad-size)/2 );
		Region clip_region((pad-size)/2, (pad-size)/2, (pad-size)/2, size, size, size );
		image->clip_inplace( clip_region );
	}


	bool want_stats = true;
	if ( want_stats )
	{
		unsigned int size = quality_scores.size();
		double* norm_frc = new double[size];
		double* frc = new double[size];
		double* norm_snr = new double[size];

		for( unsigned int i = 0; i < size; ++ i )		
		{
			norm_frc[i] = quality_scores[i].get_snr_normed_frc_integral();
			frc[i] = quality_scores[i].get_frc_integral();
			norm_snr[i] = quality_scores[i].get_normed_snr_integral();
		}
		
		double mean = gsl_stats_mean(norm_frc, 1, size);
		double variance = gsl_stats_variance_m(norm_frc, 1, size, mean);
	
		cout << "Normalized FRC mean " << mean << " variance " << variance << endl;

		mean = gsl_stats_mean(frc, 1, size);
		variance = gsl_stats_variance_m(frc, 1, size, mean);
		
		cout << "FRC mean " << mean << " variance " << variance << endl;

		mean = gsl_stats_mean(norm_snr, 1, size);
		variance = gsl_stats_variance_m(norm_snr, 1, size, mean);
		cout << "SNR mean " << mean << " variance " << variance << endl;

		double c0, c1, cov00, cov01, cov11, sumsq;
		gsl_fit_linear (norm_frc, 1, frc, 1, size, &c0, &c1, &cov00, &cov01, &cov11, &sumsq);
		cout << "The correlation between frc and norm_frc is " << c0 << " + " << c1 << "x" << endl;

		gsl_fit_linear (norm_frc, 1, norm_snr, 1, size, &c0, &c1, &cov00, &cov01, &cov11, &sumsq);
		cout << "The correlation between norm_snr and norm_frc is " << c0 << " + " << c1 << "x" << endl;

		gsl_fit_linear (norm_snr, 1, frc, 1, size, &c0, &c1, &cov00, &cov01, &cov11, &sumsq);
		cout << "The correlation between frc and norm_snr is " << c0 << " + " << c1 << "x" << endl;

		delete [] norm_frc;
		delete [] frc;
		delete [] norm_snr;
	}
	
	image->update();
	
	image->write_image("tmp.mrc");
	
	return image;
}

WienerFourierReconstructor& WienerFourierReconstructor::operator=( const WienerFourierReconstructor& that )
{
	if ( this != &that )
	{
		Reconstructor::operator=(that);
	}
	else
	{
		cerr << "Warning: attempted to assign a WienerFourierReconstructor to itself, no action taken" << endl;
	}

	return *this;
}


void WienerFourierReconstructor::setup()
{
	int size = params["size"];
	image = new EMData();
	image->set_size(size + 2, size, size);
	image->set_complex(true);
	image->set_ri(true);

	nx = image->get_xsize();
	ny = image->get_ysize();
	nz = image->get_zsize();

	int n = nx * ny * nz;
	float *rdata = image->get_data();

	for (int i = 0; i < n; i += 2) {
		float f = Util::get_frand(0.0, 2.0 * M_PI);
		rdata[i] = 1.0e-10f * sin(f);
		rdata[i + 1] = 1.0e-10f * cos(f);
	}
	image->update();

	tmp_data = new EMData();
	tmp_data->set_size(size + 1, size, size);
}


EMData *WienerFourierReconstructor::finish()
{
	float *norm = tmp_data->get_data();
	float *rdata = image->get_data();

	for (int i = 0; i < nx * ny * nz; i += 2) {
		float d = norm[i];
		if (d == 0) {
			rdata[i] = 0;
			rdata[i + 1] = 0;
		}
		else {
			float w = 1 + 1 / d;
			rdata[i] /= d * w;
			rdata[i + 1] /= d * w;
		}
	}

	if( tmp_data ) {
		delete tmp_data;
		tmp_data = 0;
	}
	image->update();
	return image;
}


int WienerFourierReconstructor::insert_slice(const EMData* const slice, const Transform3D & euler)
{
	if (!slice) {
		LOGERR("try to insert NULL slice");
		return 1;
	}

	int mode = params["mode"];
	float padratio = params["padratio"];
	vector < float >snr = params["snr"];

	if (!slice->is_complex()) {
		LOGERR("Only complex slice can be inserted.");
		return 1;
	}
	float *gimx = 0;
	if (mode == 5) {
		gimx = Interp::get_gimx();
	}

	int nxy = nx * ny;
	int off[8];
	if (mode == 2) {
		off[0] = 0;
		off[1] = 2;
		off[2] = nx;
		off[3] = nx + 2;
		off[4] = nxy;
		off[5] = nxy + 2;
		off[6] = nxy + nx;
		off[7] = nxy + nx + 2;
	}

	float *norm = tmp_data->get_data();
	float *dat = slice->get_data();
	float *rdata = image->get_data();

	int rl = Util::square(ny / 2 - 1);
	float dt[2];
	float g[8];
	
	for (int y = 0; y < ny; y++) {
		for (int x = 0; x < nx / 2; x++) {
			if ((x * x + Util::square(y - ny / 2)) >= rl)
			{
				continue;
			}

			int r = Util::round((float)hypot(x, (float) y - ny / 2) * Ctf::CTFOS / padratio);
			if (r >= Ctf::CTFOS * ny / 2) {
				r = Ctf::CTFOS * ny / 2 - 1;
			}

			float weight = snr[r];

			float xx = (x * euler[0][0] + (y - ny / 2) * euler[0][1]);
			float yy = (x * euler[1][0] + (y - ny / 2) * euler[1][1]);
			float zz = (x * euler[2][0] + (y - ny / 2) * euler[2][1]);
			float cc = 1;

			if (xx < 0) {
				xx = -xx;
				yy = -yy;
				zz = -zz;
				cc = -1.0;
			}

			yy += ny / 2;
			zz += nz / 2;

			dt[0] = dat[x * 2 + y * nx] * (1 + 1.0f / weight);
			dt[1] = cc * dat[x * 2 + 1 + y * nx] * (1 + 1.0f / weight);

			int x0 = 0;
			int y0 = 0;
			int z0 = 0;
			int i = 0;
			int l = 0;
			float dx = 0;
			float dy = 0;
			float dz = 0;

			int mx0 = 0;
			int my0 = 0;
			int mz0 = 0;

			switch (mode) {
			case 1:
				x0 = 2 * (int) floor(xx + 0.5f);
				y0 = (int) floor(yy + 0.5f);
				z0 = (int) floor(zz + 0.5f);

				rdata[x0 + y0 * nx + z0 * nxy] += weight * dt[0];
				rdata[x0 + y0 * nx + z0 * nxy + 1] += weight * dt[1];
				norm[x0 + y0 * nx + z0 * nxy] += weight;
				break;

			case 2:
				x0 = (int) floor(xx);
				y0 = (int) floor(yy);
				z0 = (int) floor(zz);

				dx = xx - x0;
				dy = yy - y0;
				dz = zz - z0;

				weight /= (float)pow((float)(EMConsts::I2G * M_PI), 1.5f);

				if (x0 > nx - 2 || y0 > ny - 1 || z0 > nz - 1) {
					break;
				}

				i = (int) (x0 * 2 + y0 * nx + z0 * nxy);


				g[0] = Util::agauss(1, dx, dy, dz, EMConsts::I2G);
				g[1] = Util::agauss(1, 1 - dx, dy, dz, EMConsts::I2G);
				g[2] = Util::agauss(1, dx, 1 - dy, dz, EMConsts::I2G);
				g[3] = Util::agauss(1, 1 - dx, 1 - dy, dz, EMConsts::I2G);
				g[4] = Util::agauss(1, dx, dy, 1 - dz, EMConsts::I2G);
				g[5] = Util::agauss(1, 1 - dx, dy, 1 - dz, EMConsts::I2G);
				g[6] = Util::agauss(1, dx, 1 - dy, 1 - dz, EMConsts::I2G);
				g[7] = Util::agauss(1, 1 - dx, 1 - dy, 1 - dz, EMConsts::I2G);

				for (int j = 0; j < 8; j++) {
					int k = i + off[j];
					rdata[k] += weight * dt[0] * g[j];
					rdata[k + 1] += weight * dt[1] * g[j];
					norm[k] += weight * g[j];
				}

				break;
			case 3:
				x0 = 2 * (int) floor(xx + 0.5f);
				y0 = (int) floor(yy + 0.5f);
				z0 = (int) floor(zz + 0.5f);

				weight /= (float)pow((float)(EMConsts::I3G * M_PI), 1.5f);

				if (x0 >= nx - 4 || y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2) {
					break;
				}

				l = x0 - 2;
				if (x0 == 0) {
					l = x0;
				}
				for (int k = z0 - 1; k <= z0 + 1; k++) {
					for (int j = y0 - 1; j <= y0 + 1; j++) {
						for (int i = l; i <= x0 + 2; i += 2) {
							float r = Util::hypot3((float) i / 2 - xx, j - yy, k - zz);
							float gg = exp(-r / EMConsts::I3G);

							rdata[i + j * nx + k * nxy] += weight * gg * dt[0];
							rdata[i + j * nx + k * nxy + 1] += weight * gg * dt[1];
							norm[i + j * nx + k * nxy] += weight * gg;
						}
					}
				}
				break;

			case 4:
				x0 = 2 * (int) floor(xx);
				y0 = (int) floor(yy);
				z0 = (int) floor(zz);

				weight /= (float)pow((float)(EMConsts::I4G * M_PI), 1.5f);

				if (x0 >= nx - 4 || y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2) {
					break;
				}

				l = x0 - 2;
				if (x0 == 0) {
					l = x0;
				}
				for (int k = z0 - 1; k <= z0 + 2; k++) {
					for (int j = y0 - 1; j <= y0 + 2; j++) {
						for (int i = l; i <= x0 + 4; i += 2) {
							float r = Util::hypot3((float) i / 2 - xx, j - yy, k - zz);
							float gg = exp(-r / EMConsts::I4G);

							rdata[i + j * nx + k * nxy] += weight * gg * dt[0];
							rdata[i + j * nx + k * nxy + 1] += weight * gg * dt[1];
							norm[i + j * nx + k * nxy] += weight * gg;
						}
					}
				}
				break;

			case 5:
				x0 = (int) floor(xx + .5);
				y0 = (int) floor(yy + .5);
				z0 = (int) floor(zz + .5);

				weight /= (float)pow((float)(EMConsts::I5G * M_PI), 1.5f);

				mx0 = -(int) floor((xx - x0) * 39.0f + 0.5) - 78;
				my0 = -(int) floor((yy - y0) * 39.0f + 0.5) - 78;
				mz0 = -(int) floor((zz - z0) * 39.0f + 0.5) - 78;
				x0 *= 2;

				if (x0 >= nx - 4 || y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2)
					break;

				if (x0 == 0) {
					l = 0;
					mx0 += 78;
				}
				else if (x0 == 2) {
					l = 0;
					mx0 += 39;
				}
				else
					l = x0 - 4;
				for (int k = z0 - 2, mmz = mz0; k <= z0 + 2; k++, mmz += 39) {
					for (int j = y0 - 2, mmy = my0; j <= y0 + 2; j++, mmy += 39) {
						for (int i = l, mmx = mx0; i <= x0 + 4; i += 2, mmx += 39) {
							int ii = i + j * nx + k * nxy;
							float gg = weight * gimx[abs(mmx) + abs(mmy) * 100 + abs(mmz) * 10000];

							rdata[ii] += gg * dt[0];
							rdata[ii + 1] += gg * dt[1];
							norm[ii] += gg;
						}
					}
				}

				if (x0 <= 2) {
					xx = -xx;
					yy = -(yy - ny / 2) + ny / 2;
					zz = -(zz - nz / 2) + nz / 2;
					x0 = (int) floor(xx + 0.5f);
					y0 = (int) floor(yy + 0.5f);
					z0 = (int) floor(zz + 0.5f);
					int mx0 = -(int) floor((xx - x0) * 39.0f + .5);
					x0 *= 2;

					if (y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2)
						break;

					for (int k = z0 - 2, mmz = mz0; k <= z0 + 2; k++, mmz += 39) {
						for (int j = y0 - 2, mmy = my0; j <= y0 + 2; j++, mmy += 39) {
							for (int i = 0, mmx = mx0; i <= x0 + 4; i += 2, mmx += 39) {
								int ii = i + j * nx + k * nxy;
								float gg =
									weight * gimx[abs(mmx) + abs(mmy) * 100 + abs(mmz) * 10000];

								rdata[ii] += gg * dt[0];
								rdata[ii + 1] -= gg * dt[1];
								norm[ii] += gg;
							}
						}
					}
				}
				break;
				// mode 6 is now mode 5 without the fast interpolation
			case 6:
				x0 = 2 * (int) floor(xx + .5);
				y0 = (int) floor(yy + .5);
				z0 = (int) floor(zz + .5);

				weight /= (float)pow((float)(EMConsts::I5G * M_PI), 1.5f);

				if (x0 >= nx - 4 || y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2)
					break;

				if (x0 <= 2)
					l = 0;
				else
					l = x0 - 4;
				for (int k = z0 - 2; k <= z0 + 2; k++) {
					for (int j = y0 - 2; j <= y0 + 2; j++) {
						for (int i = l; i <= x0 + 4; i += 2) {
							int ii = i + j * nx + k * nxy;
							float r = Util::hypot3((float) i / 2 - xx, (float) j - yy,
												   (float) k - zz);
							float gg = weight * exp(-r / EMConsts::I5G);

							rdata[ii] += gg * dt[0];
							rdata[ii + 1] += gg * dt[1];
							norm[ii] += gg;
						}
					}
				}

				if (x0 <= 2) {
					xx = -xx;
					yy = -(yy - ny / 2) + ny / 2;
					zz = -(zz - nz / 2) + nz / 2;
					x0 = 2 * (int) floor(xx + 0.5f);
					y0 = (int) floor(yy + 0.5f);
					z0 = (int) floor(zz + 0.5f);

					if (y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2)
						break;

					for (int k = z0 - 2; k <= z0 + 2; k++) {
						for (int j = y0 - 2; j <= y0 + 2; j++) {
							for (int i = 0; i <= x0 + 4; i += 2) {
								int ii = i + j * nx + k * nxy;
								float r = Util::hypot3((float) i / 2 - xx, (float) j - yy,
													   (float) k - zz);
								float gg = weight * exp(-r / EMConsts::I5G);

								rdata[ii] += gg * dt[0];
								rdata[ii + 1] -= gg * dt[1];	// note the -, complex conj.
								norm[ii] += gg;
							}
						}
					}
				}
				break;

			case 7:
				x0 = 2 * (int) floor(xx + .5);
				y0 = (int) floor(yy + .5);
				z0 = (int) floor(zz + .5);

				if (x0 >= nx - 4 || y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2)
					break;

				if (x0 <= 2)
					l = 0;
				else
					l = x0 - 4;
				for (int k = z0 - 2; k <= z0 + 2; k++) {
					for (int j = y0 - 2; j <= y0 + 2; j++) {
						for (int i = l; i <= x0 + 4; i += 2) {
							int ii = i + j * nx + k * nxy;
							float r = (float)sqrt(Util::hypot3((float) i / 2 - xx,
															   (float) j - yy,
															   (float) k - zz));
							float gg = weight * Interp::hyperg(r);

							rdata[ii] += gg * dt[0];
							rdata[ii + 1] += gg * dt[1];
							norm[ii] += gg;
						}
					}
				}

				if (x0 <= 2) {
					xx = -xx;
					yy = -(yy - ny / 2) + ny / 2;
					zz = -(zz - nz / 2) + nz / 2;
					x0 = 2 * (int) floor(xx + .5);
					y0 = (int) floor(yy + .5);
					z0 = (int) floor(zz + .5);

					if (y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2)
						break;

					for (int k = z0 - 2; k <= z0 + 2; k++) {
						for (int j = y0 - 2; j <= y0 + 2; j++) {
							for (int i = 0; i <= x0 + 4; i += 2) {
								int ii = i + j * nx + k * nxy;
								float r = sqrt(Util::hypot3((float) i / 2 - xx, (float) j - yy,
															(float) k - zz));
								float gg = weight * Interp::hyperg(r);

								rdata[ii] += gg * dt[0];
								rdata[ii + 1] -= gg * dt[1];
								norm[ii] += gg;
							}
						}
					}
				}
				break;
			}

		}
	}

	image->update();
	tmp_data->update();
//	slice->update();

	return 0;
}

BackProjectionReconstructor& BackProjectionReconstructor::operator=( const BackProjectionReconstructor& that )
{
	if ( this != &that )
	{
		Reconstructor::operator=(that);
	}
	else
	{
		cerr << "Warning: attempted to assign a BackProjectionReconstructor to itself, no action taken" << endl;
	}

	return *this;
}

void BackProjectionReconstructor::setup()
{
	int size = params["size"];
	image = new EMData();
	image->set_size(size, size, size);
	nx = size;
	ny = size;
	nz = size;
}

int BackProjectionReconstructor::insert_slice(const EMData* const slice, const Transform3D &transform)
{
	if (!slice) {
		LOGERR("try to insert NULL slice");
		return 1;
	}

	if (slice->get_xsize() != slice->get_ysize() || slice->get_xsize() != nx) {
		LOGERR("try to insert improve size slice");
		return 1;
	}

	float weight = params["weight"];

	EMData *slice_copy = slice->copy();
	slice_copy->mult(weight);

	EMData *tmp = new EMData();
	tmp->set_size(nx, ny, nz);

	float *slice_data = slice_copy->get_data();
	float *tmp_data = tmp->get_data();
	int nxy = nx * ny;
	size_t nxy_size = nxy * sizeof(float);

	for (int i = 0; i < nz; i++) {
		memcpy(&tmp_data[nxy * i], slice_data, nxy_size);
	}

	tmp->update();

	// I am not sure why this was here (next 3 commented lines) - I think they are wrong
	//	Dict slice_euler = slice->get_transform().get_rotation(Transform3D::EMAN);
	// Dict slice_euler = transform.get_rotation(Transform3D::EMAN)
	//tmp->rotate(-(float)slice_euler["az"], -(float)slice_euler["alt"], -(float)slice_euler["phi"]);
	// This solution fixed the problems I was having in e2make3d.py - d.woolford
	tmp->rotate(transform);

	image->add(*tmp);
	if( slice_copy )
	{
		delete slice_copy;
		slice_copy = 0;
	}

	if( tmp )
	{
		delete tmp;
		tmp = 0;
	}
	
	return 0;
}

EMData *BackProjectionReconstructor::finish()
{
	return image;
}

EMData* EMAN::padfft_slice( const EMData* const slice, int npad )
{
	if ( slice->get_xsize() != slice->get_ysize() )
	{
		// FIXME: What kind of exception should we throw here?
		throw std::logic_error("Tried to padfft a slice that is not square.");
	}

	// process 2-d slice -- subtract the average outside of the circle, zero-pad, fft extend, and fft
	EMData* temp = slice->average_circ_sub();

	Assert( temp != NULL );
	// Need to use zeropad_ntimes instead of pad_fft here for zero padding
	// because only the former centers the original image in the 
	// larger area.  FIXME!
	
	EMData* zeropadded = temp->zeropad_ntimes(npad);
	Assert( zeropadded != NULL );

	checked_delete( temp );

	EMData* padfftslice = zeropadded->do_fft();
	checked_delete( zeropadded );

	// shift the projection
	float sx = slice->get_attr("s2x");
	float sy = slice->get_attr("s2y");
	if(sx != 0.0f || sy != 0.0)
		padfftslice->process_inplace("filter.shift", Dict("x_shift", sx, "y_shift", sy, "z_shift", 0.0f));

	padfftslice->center_origin_fft();

	return padfftslice;
}

nn4Reconstructor::nn4Reconstructor() 
{
    m_volume = NULL; 
    m_wptr   = NULL;
    m_result = NULL;
}

nn4Reconstructor::nn4Reconstructor( const string& symmetry, int size, int npad )
{
    m_volume = NULL; 
    m_wptr   = NULL;
    m_result = NULL;
	setup( symmetry, size, npad );
	std::cout << "printing in constructor" << std::endl;
	load_default_settings();
	print_params();
}

nn4Reconstructor::~nn4Reconstructor()
{
    if( m_delete_volume )
        checked_delete(m_volume);

    if( m_delete_weight )
        checked_delete( m_wptr );

    checked_delete( m_result );
}

enum weighting_method { NONE, ESTIMATE, VORONOI };

float max3d( int kc, const vector<float>& pow_a )
{
	float max = 0.0;
	for( int i=-kc; i <= kc; ++i )
	{
		for( int j=-kc; j <= kc; ++j )
		{
			for( int k=-kc; k <= kc; ++k )
			{
					if( i==0 && j==0 && k==0 ) continue;
	
				// if( i!=0 ) 
				{
					int c = 3*kc+1 - std::abs(i) - std::abs(j) - std::abs(k);
					max = max + pow_a[c];
					// max = max + c * c;
					// max = max + c; 
				}
			}
		}
	}

	return max;
}


void nn4Reconstructor::setup() 
{
    int size = params["size"];
    int npad = params["npad"];

    string symmetry;
    if( params.has_key("symmetry") )
    {
   	    symmetry = params["symmetry"].to_str();
    }
    else
    {
	    symmetry = "c1";
    }


    setup( symmetry, size, npad );
}

void nn4Reconstructor::setup( const string& symmetry, int size, int npad )
{
    m_weighting = ESTIMATE;
    m_wghta = 0.2;
    m_wghtb = 0.004;
 
    m_symmetry = symmetry;
    m_npad = npad;
    m_nsym = Transform3D::get_nsym(m_symmetry);

    m_vnx = size;
    m_vny = size;
    m_vnz = size;

    m_vnxp = size*npad;
    m_vnyp = size*npad;
    m_vnzp = size*npad;

    m_vnxc = m_vnxp/2;
    m_vnyc = m_vnyp/2;
    m_vnzc = m_vnzp/2;

    buildFFTVolume();
    buildNormVolume();

}

void nn4Reconstructor::buildFFTVolume() {
	int offset = 2 - m_vnxp%2;

        if( params.has_key("fftvol") )
        {
            m_volume = params["fftvol"];
            m_delete_volume = false;
        }
        else
        {
	    m_volume = new EMData();
            m_delete_volume = true;
        }

        if( m_volume->get_xsize() != m_vnxp+offset &&
            m_volume->get_ysize() != m_vnyp &&
	    m_volume->get_zsize() != m_vnzp )
        {
            m_volume->set_size(m_vnxp+offset,m_vnyp,m_vnzp);
            m_volume->to_zero();
        }
        // ----------------------------------------------------------------
	// Added by Zhengfan Yang on 03/15/07
	// Original author: please check whether my revision is correct and 
	// other Reconstructor need similiar revision.
	if ( m_vnxp % 2 == 0 ) { m_volume->set_fftodd(0); }
			else   { m_volume->set_fftodd(1); }
	// ----------------------------------------------------------------
	
	m_volume->set_nxc(m_vnxp/2);
	m_volume->set_complex(true);
	m_volume->set_ri(true);
	m_volume->set_fftpad(true);
	m_volume->set_attr("npad", m_npad);
	m_volume->set_array_offsets(0,1,1);
}

void nn4Reconstructor::buildNormVolume() {

    if( params.has_key("weight") )
    {
        m_wptr = params["weight"];
        m_delete_weight = false;
    }
    else
    {
	m_wptr = new EMData();
        m_delete_weight = true;
    }

    if( m_wptr->get_xsize() != m_vnxc+1 &&
        m_wptr->get_ysize() != m_vnyp &&
	m_wptr->get_zsize() != m_vnzp )
    {
	m_wptr->set_size(m_vnxc+1,m_vnyp,m_vnzp);
	m_wptr->to_zero();
    }

    m_wptr->set_array_offsets(0,1,1);
}

int nn4Reconstructor::insert_slice(const EMData* const slice, const Transform3D& t) {
	// sanity checks
	if (!slice) {
		LOGERR("try to insert NULL slice");
		return 1;
	}

        int padffted=0;
        try {
	    padffted= slice->get_attr("padffted");
        }
        catch(_NotExistingObjectException) {
	    padffted= 0;
        }

	if ( padffted==0 && (slice->get_xsize()!=slice->get_ysize() || slice->get_xsize()!=m_vnx)  )
        {
		// FIXME: Why doesn't this throw an exception?
		LOGERR("Tried to insert a slice that is the wrong size.");
		return 1;
	}

    EMData* padfft = NULL;

    if( padffted != 0 )
    {	   
        padfft = new EMData(*slice);
    }
    else
    {
        padfft = padfft_slice( slice, m_npad );
    }

    int mult=0;
    try {
    mult = slice->get_attr("mult");
    }
    catch(_NotExistingObjectException) {
    mult = 1;
    }

        Assert( mult > 0 );
	insert_padfft_slice( padfft, t, mult );

	checked_delete( padfft );
	return 0;
}

int nn4Reconstructor::insert_padfft_slice( EMData* padfft, const Transform3D& t, int mult )
{
	Assert( padfft != NULL );
	// insert slice for all symmetry related positions
	for (int isym=0; isym < m_nsym; isym++) {
		Transform3D tsym = t.get_sym(m_symmetry, isym);
		    m_volume->nn( m_wptr, padfft, tsym, mult);
        }
	return 0;
}


#define  tw(i,j,k)      tw[ i-1 + (j-1+(k-1)*iy)*ix ]
EMData* nn4Reconstructor::finish() 
{

    m_volume->symplane0(m_wptr);

    int box = 7;
    int vol = box*box*box;
    int kc = (box-1)/2;
    vector< float > pow_a( 3*kc+1, 1.0 );
    for( unsigned int i=1; i < pow_a.size(); ++i ) pow_a[i] = pow_a[i-1] * exp(m_wghta);
    pow_a[3*kc] = 0.0;

    vector< float > pow_b( 3*m_vnyc+1, 1.0 );
    for( unsigned int i=1; i < pow_b.size(); ++i ) pow_b[i] = pow_b[i-1] * exp(m_wghtb);

    float max = max3d( kc, pow_a );
    float alpha = ( 1.0 - 1.0/vol ) / max;

	for (int iz = 1; iz <= m_vnzp; iz++) {
		for (int iy = 1; iy <= m_vnyp; iy++) {
			for (int ix = 0; ix <= m_vnxc; ix++) {
				if ( (*m_wptr)(ix,iy,iz) > 0) {//(*v) should be treated as complex!!
					float tmp = (-2*((ix+iy+iz)%2)+1)/(*m_wptr)(ix,iy,iz);

					if( m_weighting == ESTIMATE ) 
					{
                                            int cx = ix;
					    int cy = (iy<=m_vnyc) ? iy - 1 : iy - 1 - m_vnyp;
					    int cz = (iz<=m_vnzc) ? iz - 1 : iz - 1 - m_vnzp;

					    float sum = 0.0;
					    for( int ii = -kc; ii <= kc; ++ii )
                                            { 
					        int nbrcx = cx + ii;
						if( nbrcx >= m_vnxc ) continue;

					        for( int jj= -kc; jj <= kc; ++jj )
						{
						    int nbrcy = cy + jj;
						    if( nbrcy <= -m_vnyc || nbrcy >= m_vnyc ) continue;

						    for( int kk = -kc; kk <= kc; ++kk )
						    {
						        int nbrcz = cz + jj;
                                                        if( nbrcz <= -m_vnyc || nbrcz >= m_vnyc ) continue;

							if( nbrcx < 0 )
							{
							    nbrcx = -nbrcx;
							    nbrcy = -nbrcy;
							    nbrcz = -nbrcz;
							}

                                                        int nbrix = nbrcx;
							int nbriy = nbrcy >= 0 ? nbrcy + 1 : nbrcy + 1 + m_vnyp;
							int nbriz = nbrcz >= 0 ? nbrcz + 1 : nbrcz + 1 + m_vnzp;

							if( (*m_wptr)( nbrix, nbriy, nbriz ) == 0 )
							{
                                                            int c = 3*kc+1 - std::abs(ii) - std::abs(jj) - std::abs(kk);
							    sum = sum + pow_a[c];
							}
                                                    }
                                                }
                                            }

					    int r = std::abs(cx) + std::abs(cy) + std::abs(cz);
					    Assert( r >=0 && r < (int)pow_b.size() );
                                            float wght = pow_b[r] / ( 1.0 - alpha * sum );
					    tmp = tmp * wght;
				        }

					(*m_volume)(2*ix,iy,iz) *= tmp;
					(*m_volume)(2*ix+1,iy,iz) *= tmp;

				  }
			}
		}
	}

	// back fft
	m_volume->do_ift_inplace();
	EMData* win = m_volume->window_center(m_vnx);

	float *tw = win->get_data();
	//  mask and subtract circumference average
	int ix = win->get_xsize();
	int iy = win->get_ysize();
	int iz = win->get_zsize();
	int L2 = (ix/2)*(ix/2);
	int L2P = (ix/2-1)*(ix/2-1);
	int IP = ix/2+1;
	float  TNR = 0.0f;
	int m = 0;
	for (int k = 1; k <= iz; k++) {
		for (int j = 1; j <= iy; j++) {
			for (int i = 1; i <= ix; i++) {
				int LR = (k-IP)*(k-IP)+(j-IP)*(j-IP)+(i-IP)*(i-IP);
				if (LR<=L2) {
					if(LR >= L2P && LR <= L2) {
						TNR += tw(i,j,k);
						m++;
					}
				}
			}
		}
	}

	TNR /=float(m);
	for (int k = 1; k <= iz; k++) {
		for (int j = 1; j <= iy; j++) {
			for (int i = 1; i <= ix; i++) {
				int LR = (k-IP)*(k-IP)+(j-IP)*(j-IP)+(i-IP)*(i-IP);
				if (LR<=L2) tw(i,j,k) -= TNR; else tw(i,j,k) = 0.0f;
			}
		}
	}

        m_result = win;
	return win;
}
#undef  tw

// Added By Zhengfan Yang on 03/16/07
// Beginning of the addition
// --------------------------------------------------------------------------------

nnSSNR_Reconstructor::nnSSNR_Reconstructor() 
{
    m_volume = NULL; 
    m_wptr   = NULL;
    m_wptr2  = NULL;
    m_result = NULL;
}

nnSSNR_Reconstructor::nnSSNR_Reconstructor( const string& symmetry, int size, int npad)
{
    m_volume = NULL; 
    m_wptr   = NULL;
    m_wptr2  = NULL;
    m_result = NULL;

    setup( symmetry, size, npad );
}

nnSSNR_Reconstructor::~nnSSNR_Reconstructor()
{
   if( m_delete_volume )
        checked_delete(m_volume);
 
    if( m_delete_weight )
        checked_delete( m_wptr );
    
    if ( m_delete_weight2 )
        checked_delete( m_wptr2 );

    checked_delete( m_result );
}

void nnSSNR_Reconstructor::setup() 
{
    int size = params["size"];
    int npad = params["npad"];

    string symmetry;
    if( params.has_key("symmetry") )
    {
   	    symmetry = params["symmetry"].to_str();
    }
    else
    {
	    symmetry = "c1";
    }
    setup( symmetry, size, npad );
}

void nnSSNR_Reconstructor::setup( const string& symmetry, int size, int npad )
{
   
    m_weighting = ESTIMATE;
    m_wghta = 0.2;
    m_wghtb = 0.004;
 
    m_symmetry = symmetry;
    m_npad = npad;
    m_nsym = Transform3D::get_nsym(m_symmetry);

    m_vnx = size;
    m_vny = size;
    m_vnz = size;

    m_vnxp = size*npad;
    m_vnyp = size*npad;
    m_vnzp = size*npad;

    m_vnxc = m_vnxp/2;
    m_vnyc = m_vnyp/2;
    m_vnzc = m_vnzp/2;

    buildFFTVolume();
    buildNormVolume();
    buildNorm2Volume();

}

void nnSSNR_Reconstructor::buildFFTVolume() {
	
	int offset = 2 - m_vnxp%2;
	
        if( params.has_key("fftvol") )
	{
		m_volume = params["fftvol"]; /* volume should be defined in python when PMI is turned on*/
		m_delete_volume = false;
	}
	else
	{
		m_volume = new EMData();
		m_delete_volume = true;	
	}
        m_volume->set_size(m_vnxp+offset,m_vnyp,m_vnzp);
        m_volume->to_zero();
	if ( m_vnxp % 2 == 0 ) { m_volume->set_fftodd(0); }
			else   { m_volume->set_fftodd(1); }
	
	m_volume->set_nxc(m_vnxc);
	m_volume->set_complex(true);
	m_volume->set_ri(true);
	m_volume->set_fftpad(true);
	m_volume->set_attr("npad", m_npad);
	m_volume->set_array_offsets(0,1,1);
}

void nnSSNR_Reconstructor::buildNormVolume() {
	if( params.has_key("weight") )
	{
	 	m_wptr          = params["weight"]; 
	 	m_delete_weight = false;
	}
	else
	{	m_wptr = new EMData();
        	m_delete_weight = true;
	}

	m_wptr->set_size(m_vnxc+1,m_vnyp,m_vnzp);
	m_wptr->to_zero();

	m_wptr->set_array_offsets(0,1,1);
}

void nnSSNR_Reconstructor::buildNorm2Volume() {

	if( params.has_key("weight2") )
	{
		m_wptr2          = params["weight2"]; 
		m_delete_weight2 = false;
	}
	else
	{
		m_wptr2 = new EMData();
		m_delete_weight2 = true;
	}		
	m_wptr2->set_size(m_vnxc+1,m_vnyp,m_vnzp);
	m_wptr2->to_zero();
	m_wptr2->set_array_offsets(0,1,1);
}


int nnSSNR_Reconstructor::insert_slice(const EMData* const slice, const Transform3D& t) {
	// sanity checks
	if (!slice) {
		LOGERR("try to insert NULL slice");
		return 1;
	}

	int padffted=0;
	try {
	padffted= slice->get_attr("padffted");
	}
	catch(_NotExistingObjectException) {
	padffted= 0;
	}

	if ( padffted==0 && (slice->get_xsize()!=slice->get_ysize() || slice->get_xsize()!=m_vnx)  )
        {
		// FIXME: Why doesn't this throw an exception?
		LOGERR("Tried to insert a slice that is the wrong size.");
		return 1;
	}

    EMData* padfft = NULL;

    if( padffted != 0 )
    {
        padfft = new EMData(*slice);
    }
    else
    {
        padfft = padfft_slice( slice, m_npad );
    }

    int mult=0;
    try {
    mult = slice->get_attr("mult");
    }
    catch(_NotExistingObjectException) {
    mult = 1;
    }

        Assert( mult > 0 );
	insert_padfft_slice( padfft, t, mult );

	checked_delete( padfft );
	return 0;
}

int nnSSNR_Reconstructor::insert_padfft_slice( EMData* padfft, const Transform3D& t, int mult )
{
	Assert( padfft != NULL );
	// insert slice for all symmetry related positions
	for (int isym=0; isym < m_nsym; isym++) {
		Transform3D tsym = t.get_sym(m_symmetry, isym);
		    m_volume->nn_SSNR( m_wptr, m_wptr2, padfft, tsym, mult);
        }
	return 0;
}


#define  tw(i,j,k)      tw[ i-1 + (j-1+(k-1)*iy)*ix ]
EMData* nnSSNR_Reconstructor::finish() 
{
	int kz, ky;
 	int box = 7;
    	int kc = (box-1)/2;
	float alpha = 0.0;
	float argx, argy, argz;
	vector< float > pow_a( 3*kc+1, 1.0 );
	vector< float > pow_b( 3*m_vnyc+1, 1.0 );
	EMData* vol_ssnr = new EMData();
	vol_ssnr->set_size(m_vnxp+2 - m_vnxp%2, m_vnyp, m_vnzp);
	vol_ssnr->set_array_offsets(0,1,1);
	vol_ssnr->to_zero();
        float w = params["w"];
	EMData* SSNR = params["SSNR"];

	float dx2 = 1.0f/float(m_vnxc)/float(m_vnxc); 
	float dy2 = 1.0f/float(m_vnyc)/float(m_vnyc);
	float dz2 = 1.0f/std::max(float(m_vnzc),1.0f)/std::max(float(m_vnzc),1.0f);	
	int inc = Util::round(float(std::max(std::max(m_vnxc,m_vnyc),m_vnzc))/w);
	SSNR->set_size(inc+1,4,1);

	float *nom    = new float[inc+1];
	float *denom  = new float[inc+1];
	int *nn = new int[inc+1];
	int *ka = new int[inc+1];
	float wght = 1.0f;
	for (int i = 0; i <= inc; i++) {
		nom[i] = 0.0f;
		denom[i] = 0.0f;
		nn[i] = 0;
		ka[i] = 0;
	}

	m_volume->symplane1(m_wptr, m_wptr2);

	if ( m_weighting == ESTIMATE ) {
		int vol = box*box*box;
		for( unsigned int i=1; i < pow_a.size(); ++i ) pow_a[i] = pow_a[i-1] * exp(m_wghta);
		pow_a[3*kc] = 0.0;
		for( unsigned int i=1; i < pow_b.size(); ++i ) pow_b[i] = pow_b[i-1] * exp(m_wghtb);
		float max = max3d( kc, pow_a );
		alpha = ( 1.0 - 1.0/vol ) / max;
	}

	for (int iz = 1; iz <= m_vnzp; iz++) {
		if ( iz-1 > m_vnzc ) kz = iz-1-m_vnzp; else kz = iz-1;
		argz = float(kz*kz)*dz2;  
		for (int iy = 1; iy <= m_vnyp; iy++) {
			if ( iy-1 > m_vnyc ) ky = iy-1-m_vnyp; else ky = iy-1;
			argy = argz + float(ky*ky)*dy2;
			for (int ix = 0; ix <= m_vnxc; ix++) {
				float Kn = (*m_wptr)(ix,iy,iz);
				if ( Kn > 0.0f ) {
					argx = std::sqrt(argy + float(ix*ix)*dx2);
					int r = Util::round(float(inc)*argx);
					/***if ( r >= 0 && r <= inc && Kn > 1.5f && ( ix > 0 || kz > 0 || kz == 0 && ky >= 0 )) {
						float nominator = std::norm(m_volume->cmplx(ix,iy,iz)/Kn);
						float denominator = ((*m_wptr2)(ix,iy,iz)-std::norm(m_volume->cmplx(ix,iy,iz))/Kn)/(Kn*(Kn-1.0f));
						nom[r] += nominator;
						denom[r] += denominator;
						nn[r] += 2;
						ka[r] += int(Kn);
					}***/

					float tmp = (-2*((ix+iy+iz)%2)+1)/(*m_wptr)(ix,iy,iz);
					if ( m_weighting == ESTIMATE ) {
						int cx = ix;
						int cy = (iy<=m_vnyc) ? iy - 1 : iy - 1 - m_vnyp;
						int cz = (iz<=m_vnzc) ? iz - 1 : iz - 1 - m_vnzp;

						float sum = 0.0;
						for( int ii = -kc; ii <= kc; ++ii ) { 
							int nbrcx = cx + ii;
							if( nbrcx >= m_vnxc ) continue;
						        for ( int jj= -kc; jj <= kc; ++jj ) {
								int nbrcy = cy + jj;
								if( nbrcy <= -m_vnyc || nbrcy >= m_vnyc ) continue;
								for( int kk = -kc; kk <= kc; ++kk ) {
									int nbrcz = cz + jj;
		                                                        if ( nbrcz <= -m_vnyc || nbrcz >= m_vnyc ) continue;
									if( nbrcx < 0 ) {
										nbrcx = -nbrcx;
										nbrcy = -nbrcy;
										nbrcz = -nbrcz;
									}
		                                                        int nbrix = nbrcx;
									int nbriy = nbrcy >= 0 ? nbrcy + 1 : nbrcy + 1 + m_vnyp;
									int nbriz = nbrcz >= 0 ? nbrcz + 1 : nbrcz + 1 + m_vnzp;
									if( (*m_wptr)( nbrix, nbriy, nbriz ) == 0 ) {
										int c = 3*kc+1 - std::abs(ii) - std::abs(jj) - std::abs(kk);
										sum = sum + pow_a[c];
									}
                                                    		}
                                                	}
						}
						int r = std::abs(cx) + std::abs(cy) + std::abs(cz);
						Assert( r >=0 && r < (int)pow_b.size() );
						wght = pow_b[r] / ( 1.0 - alpha * sum );
						tmp = tmp * wght;
				        } // end of ( m_weighting == ESTIMATE )
					if ( r >= 0 && r <= inc && Kn > 1.5f && ( ix > 0 || kz > 0 || kz == 0 && ky >= 0 )) {
						float nominator = std::norm(m_volume->cmplx(ix,iy,iz)/Kn)*wght;
						float denominator = ((*m_wptr2)(ix,iy,iz)-std::norm(m_volume->cmplx(ix,iy,iz))/Kn)*wght/(Kn*(Kn-1.0f));
						nom[r]   += nominator;
						denom[r] += denominator;
						nn[r] += 2;
						ka[r] += int(Kn);
					}
					
										}
				if ( Kn > 1.0f)
				{
				 int iiy, iiz;
				 if (iy <= m_vnyc )
				 {
				  	 iiy = m_vnyc +iy;
				  }
				 else
				 {
					 iiy = iy - m_vnyc+1;
				 }
				 if (iz<= m_vnzc )
				{
       					 iiz = m_vnzc +iz;
 				}
				else
				{
       					 iiz = iz - m_vnzc+1;
				}
				 (*vol_ssnr)(m_vnxc+(2 - m_vnxp%2)/2+ix,iiy,iiz)=
				 std::norm(m_volume->cmplx(ix,iy,iz))/((*m_wptr2)(ix,iy,iz)-std::norm(m_volume->cmplx(ix,iy,iz))/Kn)*(Kn*(Kn-1.0f)/Kn);
				 (*vol_ssnr)(m_vnxc+(2 - m_vnxp%2)/2-ix,iiy,iiz)=
				 std::norm(m_volume->cmplx(ix,iy,iz))/((*m_wptr2)(ix,iy,iz)-std::norm(m_volume->cmplx(ix,iy,iz))/Kn)*(Kn*(Kn-1.0f)/Kn);
									
				}
			}
		}
	}

	for (int i = 0; i <= inc; i++)  { 
	        (*SSNR)(i,0,0)  = nom[i];  ///(*SSNR)(i,0,0) = nom[i]/denom[i] - 1;///	
		(*SSNR)(i,1,0) = denom[i];    // variance
		(*SSNR)(i,2,0) = nn[i];
		(*SSNR)(i,3,0) = ka[i];
	}

	return vol_ssnr;
}
#undef  tw

// -----------------------------------------------------------------------------------
// End of this addition

//####################################################################################
//** bootstrap_nn reconstructor

bootstrap_nnReconstructor::bootstrap_nnReconstructor()
{
}

bootstrap_nnReconstructor::~bootstrap_nnReconstructor()
{
    for_each( m_padffts.begin(), m_padffts.end(), boost::ptr_fun( checked_delete< EMData > ) );
    for_each( m_transes.begin(), m_transes.end(), boost::ptr_fun( checked_delete< Transform3D > ) );
}

void bootstrap_nnReconstructor::setup()
{
    m_ctf  = params["ctf"].to_str();
    m_npad = params["npad"];
    m_size = params["size"];
    m_media = params["media"].to_str();

    try {
	m_symmetry = params["symmetry"].to_str();
	if ("" == m_symmetry) m_symmetry = "c1";
    }
    catch(_NotExistingObjectException) {
        m_symmetry = "c1";
    }
        
    m_nsym = Transform3D::get_nsym(m_symmetry);
}

int bootstrap_nnReconstructor::insert_slice(const EMData* const slice, const Transform3D& euler)
{
    EMData* padfft = padfft_slice( slice, m_npad );
    Assert( padfft != NULL );

    if( m_media == "memory" )
    {
        m_padffts.push_back( padfft );
    }
    else
    {
        padfft->write_image( m_media, m_transes.size() );
	checked_delete( padfft );
    }

    Transform3D* trans = new Transform3D( euler );
    m_transes.push_back( trans );
    return 0;
}

EMData* bootstrap_nnReconstructor::finish()
{
    nn4Reconstructor* r( new nn4Reconstructor(m_symmetry, m_size, m_npad) );
    vector<int> mults = params["mult"];
//    assert( mults != NULL );
    Assert( m_transes.size() == mults.size() );

    int total = 0;
    for( unsigned int i=0; i < mults.size(); ++i )
    {
        int mult = mults.at(i);
	if( mult > 0 )
	{
	    if( m_media == "memory" )
	    {
                r->insert_padfft_slice( m_padffts[i], *m_transes[i], mult );
	    }
	    else
	    {
	        EMData* padfft = new EMData();
		padfft->read_image( m_media, i );
		r->insert_padfft_slice( padfft, *m_transes[i], mult );
		checked_delete( padfft );
            }
	}
	total += mult;
    }

    EMData* w = r->finish();
    checked_delete(r);
    return w;
}

//####################################################################################
//** nn4 ctf reconstructor 

nn4_ctfReconstructor::nn4_ctfReconstructor() 
{
    m_volume  = NULL;
    m_wptr    = NULL;
    m_result  = NULL;
}

nn4_ctfReconstructor::nn4_ctfReconstructor( const string& symmetry, int size, int npad, float snr, int sign )
{
    setup( symmetry, size, npad, snr, sign );
}

nn4_ctfReconstructor::~nn4_ctfReconstructor()
{
    if( m_delete_volume )
        checked_delete(m_volume);

    if( m_delete_weight )
        checked_delete( m_wptr );

    checked_delete( m_result );
}

void nn4_ctfReconstructor::setup() 
{
    if( ! params.has_key("size") )
    {
        throw std::logic_error("Error: image size is not given");
    }

    int size = params["size"];
    int npad = 4;
    int sign = params.has_key("sign") ? int(params["sign"]) : 1;
    string symmetry = params.has_key("symmetry")? params["symmetry"].to_str() : "c1";
    
    float snr = params["snr"];
    setup( symmetry, size, npad, snr, sign );

}

void nn4_ctfReconstructor::setup( const string& symmetry, int size, int npad, float snr, int sign )
{
    m_weighting = ESTIMATE;
    m_wghta = 0.2;
    m_wghtb = 0.004;
 
    m_symmetry = symmetry;
    m_npad = npad;
    m_sign = sign;
    m_nsym = Transform3D::get_nsym(m_symmetry);

    m_snr = snr;

    m_vnx = size;
    m_vny = size;
    m_vnz = size;

    m_vnxp = size*npad;
    m_vnyp = size*npad;
    m_vnzp = size*npad;

    m_vnxc = m_vnxp/2;
    m_vnyc = m_vnyp/2;
    m_vnzc = m_vnzp/2;

    buildFFTVolume();
    buildNormVolume();
}

void nn4_ctfReconstructor::buildFFTVolume() {
    int offset = 2 - m_vnxp%2;
    if( params.has_key("fftvol") )
    {
        m_volume = params["fftvol"];
        m_delete_volume = false;
    }
    else
    {
	m_volume = new EMData();
        m_delete_volume = true;
    }

    if( m_volume->get_xsize() != m_vnxp+offset &&
        m_volume->get_ysize() != m_vnyp &&
	m_volume->get_zsize() != m_vnzp )
    {
        m_volume->set_size(m_vnxp+offset,m_vnyp,m_vnzp);
        m_volume->to_zero();
    }

    m_volume->set_nxc(m_vnxp/2);
    m_volume->set_complex(true);
    m_volume->set_ri(true);
    m_volume->set_fftpad(true);
    m_volume->set_attr("npad", m_npad);
    m_volume->set_array_offsets(0,1,1);
}

void nn4_ctfReconstructor::buildNormVolume() 
{
    if( params.has_key("weight") )
    {
        m_wptr = params["weight"];
        m_delete_weight = false;
    }
    else
    {
	m_wptr = new EMData();
        m_delete_weight = true;
    }

    if( m_wptr->get_xsize() != m_vnxc+1 &&
        m_wptr->get_ysize() != m_vnyp &&
	m_wptr->get_zsize() != m_vnzp )
    {
	m_wptr->set_size(m_vnxc+1,m_vnyp,m_vnzp);
	m_wptr->to_zero();
    }

    m_wptr->set_array_offsets(0,1,1);

}

int nn4_ctfReconstructor::insert_slice(const EMData* const slice, const Transform3D& t) 
{
	// sanity checks
	if (!slice) {
		LOGERR("try to insert NULL slice");
		return 1;
	}

    int padffted=0;
    try {
    padffted= slice->get_attr("padffted");
    }
    catch(_NotExistingObjectException) {
    padffted= 0;
    }


	if ( padffted==0 && (slice->get_xsize()!=slice->get_ysize() || slice->get_xsize()!=m_vnx)  )
        {
		// FIXME: Why doesn't this throw an exception?
		LOGERR("Tried to insert a slice that is the wrong size.");
		return 1;
	}

    EMData* padfft = NULL;

    if( padffted != 0 )
    {
        padfft = new EMData(*slice);
    }
    else
    {
        padfft = padfft_slice( slice, m_npad );
    }

    int mult=0;
    try {
    mult = slice->get_attr("mult");
    }
    catch(_NotExistingObjectException) {
    mult = 1;
    }

	Assert( mult > 0 );
	insert_padfft_slice( padfft, t, mult );

	checked_delete( padfft );

	return 0;
}

int nn4_ctfReconstructor::insert_padfft_slice( EMData* padfft, const Transform3D& t, int mult )
{
    Assert( padfft != NULL );
    float tmp = padfft->get_attr("ctf_applied");
    int   ctf_applied = (int) tmp;
	
    for( int isym=0; isym < m_nsym; isym++) 
    {
        Transform3D tsym = t.get_sym( m_symmetry, isym );

	if(ctf_applied) 
	{ 
	    m_volume->nn_ctf_applied(m_wptr, padfft, tsym, mult);
	}
	else 
	{
	    m_volume->nn_ctf(m_wptr, padfft, tsym, mult);
	}
    }

    return 0;

}

#define  tw(i,j,k)      tw[ i-1 + (j-1+(k-1)*iy)*ix ]
EMData* nn4_ctfReconstructor::finish() 
{
    m_volume->set_array_offsets(0, 1, 1);
    m_wptr->set_array_offsets(0, 1, 1);
    m_volume->symplane0_ctf(m_wptr);

    int box = 7;
    int vol = box*box*box;
    int kc = (box-1)/2;
    vector< float > pow_a( 3*kc+1, 1.0 );
	for( unsigned int i=1; i < pow_a.size(); ++i ) pow_a[i] = pow_a[i-1] * exp(m_wghta);
    pow_a[3*kc]=0.0;


	vector< float > pow_b( 3*m_vnyc, 1.0 );
	for( unsigned int i=1; i < pow_b.size(); ++i ) pow_b[i] = pow_b[i-1] * exp(m_wghtb);

    float max = max3d( kc, pow_a );
    float alpha = ( 1.0 - 1.0/vol ) / max;
	float osnr = 1.0f/m_snr;

	// normalize
	for (int iz = 1; iz <= m_vnzp; iz++) {
		for (int iy = 1; iy <= m_vnyp; iy++) {
			for (int ix = 0; ix <= m_vnxc; ix++) {
				if ( (*m_wptr)(ix,iy,iz) > 0.f) {//(*v) should be treated as complex!!
					float  tmp = (-2*((ix+iy+iz)%2)+1)/((*m_wptr)(ix,iy,iz)+osnr)*m_sign;

					if( m_weighting == ESTIMATE ) 
					{
                        int cx = ix;
					    int cy = (iy<=m_vnyc) ? iy - 1 : iy - 1 - m_vnyp;
					    int cz = (iz<=m_vnzc) ? iz - 1 : iz - 1 - m_vnzp;

					    float sum = 0.0;
					    for( int ii = -kc; ii <= kc; ++ii )
                        { 
					        int nbrcx = cx + ii;
					    	if( nbrcx >= m_vnxc ) continue;

					        for( int jj= -kc; jj <= kc; ++jj )
						    {
						        int nbrcy = cy + jj;
						        if( nbrcy <= -m_vnyc || nbrcy >= m_vnyc ) continue;

						        for( int kk = -kc; kk <= kc; ++kk )
						        {
						            int nbrcz = cz + jj;
                                    if( nbrcz <= -m_vnyc || nbrcz >= m_vnyc ) continue;

						            if( nbrcx < 0 )
							        {
							            nbrcx = -nbrcx;
							            nbrcy = -nbrcy;
							            nbrcz = -nbrcz;
							        }

                                    int nbrix = nbrcx;
							        int nbriy = nbrcy >= 0 ? nbrcy + 1 : nbrcy + 1 + m_vnyp;
							        int nbriz = nbrcz >= 0 ? nbrcz + 1 : nbrcz + 1 + m_vnzp;

							        if( (*m_wptr)( nbrix, nbriy, nbriz ) == 0.0 )
							        {
                                        int c = 3*kc+1 - std::abs(ii) - std::abs(jj) - std::abs(kk);
							            sum = sum + pow_a[c];
							            // if(ix%20==0 && iy%20==0 && iz%20==0)
							            //   std::cout << boost::format( "%4d %4d %4d %4d %10.3f" ) % nbrix % nbriy % nbriz % c % sum << std::endl;
							        }
                                }
                            }
                        }

                               
					    int r = std::abs(cx) + std::abs(cy) + std::abs(cz);
					    Assert( r >=0 && r < (int)pow_b.size() );
                        float wght = pow_b[r] / ( 1.0 - alpha * sum );
/*
                        if(ix%10==0 && iy%10==0)
                        {
                            std::cout << boost::format( "%4d %4d %4d " ) % ix % iy %iz;
                            std::cout << boost::format( "%10.3f %10.3f %10.3f " )  % tmp % wght % sum; 
                            std::cout << boost::format( "%10.3f %10.3e " ) % pow_b[r] % alpha;
                            std::cout << std::endl;
                        } 
 */
					    tmp = tmp * wght;
             	    }


                    (*m_volume)(2*ix,iy,iz) *= tmp;
	  	    (*m_volume)(2*ix+1,iy,iz) *= tmp;
				}
			}
		}
	}

	// back fft
    m_volume->do_ift_inplace();
    EMData* win = m_volume->window_center(m_vnx);

    float *tw = win->get_data();
	int ix = win->get_xsize(),iy = win->get_ysize(),iz = win->get_zsize();
	int L2 = (ix/2)*(ix/2);
	int L2P = (ix/2-1)*(ix/2-1);
	int IP = ix/2+1;
	float  TNR = 0.0f;
	int m = 0;
	for (int k = 1; k <= iz; k++) {
		for (int j = 1; j <= iy; j++) {
			for (int i = 1; i <= ix; i++) {
				int LR = (k-IP)*(k-IP)+(j-IP)*(j-IP)+(i-IP)*(i-IP);
				if (LR<=L2) {
					if(LR >= L2P && LR <= L2) {
						TNR += tw(i,j,k);
						m++;
					}
				}
			}
		}
	}
	TNR /=float(m);
	for (int k = 1; k <= iz; k++) {
		for (int j = 1; j <= iy; j++) {
			for (int i = 1; i <= ix; i++) {
				int LR = (k-IP)*(k-IP)+(j-IP)*(j-IP)+(i-IP)*(i-IP);
				if (LR<=L2) tw(i,j,k) -= TNR; else tw(i,j,k) = 0.0f;
			}
		}
	}

    // add m_result = win here because the reconstructor is responsible for the memory of m_volume
	// which I think is strange
    m_result = win;

	return win;
	// clean up
}
#undef  tw




// Added By Zhengfan Yang on 04/11/07
// Beginning of the addition
// --------------------------------------------------------------------------------

nnSSNR_ctfReconstructor::nnSSNR_ctfReconstructor() 
{
    m_volume  = NULL;
    m_wvolume = NULL; 
    m_wptr    = NULL;
    m_wptr2   = NULL;
    m_wptr3   = NULL;
    m_wptr4   = NULL;	
    m_wptr5   = NULL;
    m_result  = NULL;
}

nnSSNR_ctfReconstructor::nnSSNR_ctfReconstructor( const string& symmetry, int size, int npad, float snr, int sign)
{
    m_volume  = NULL;
    m_wvolume = NULL; 
    m_wptr    = NULL;
    m_wptr2   = NULL;
    m_wptr3   = NULL;
    m_wptr4   = NULL;
    m_wptr5   = NULL;
    m_result  = NULL;

    setup( symmetry, size, npad, snr, sign );
}

nnSSNR_ctfReconstructor::~nnSSNR_ctfReconstructor()
{

   if( m_delete_volume )
        checked_delete(m_volume);
/*
   if( m_delete_wvolume )
        checked_delete(m_wvolume);
	*/

   if( m_delete_weight )
        checked_delete( m_wptr );
   if ( m_delete_weight2 )
        checked_delete( m_wptr2 );
   if ( m_delete_weight3 )
        checked_delete( m_wptr3 );
   if ( m_delete_weight4 )
        checked_delete( m_wptr4 );
   if ( m_delete_weight5 )
        checked_delete( m_wptr5 );	
   checked_delete( m_result );
}

void nnSSNR_ctfReconstructor::setup() 
{
    int  size = params["size"];
    int  npad = params["npad"];
    int  sign = params["sign"];
    float snr = params["snr"];
    string symmetry;
    if( params.has_key("symmetry") )
    {
   	    symmetry = params["symmetry"].to_str();
    }
    else
    {
	    symmetry = "c1";
    }
    setup( symmetry, size, npad, snr, sign );
}
void nnSSNR_ctfReconstructor::setup( const string& symmetry, int size, int npad, float snr, int sign )
{
   
    m_weighting = ESTIMATE;
    m_wghta     = 0.2;
    m_wghtb     = 0.004;
    wiener      = 1;
 
    m_symmetry  = symmetry;
    m_npad      = npad;
    m_nsym      = Transform3D::get_nsym(m_symmetry);
    
    m_sign      = sign;
    m_snr       = snr;

    m_vnx       = size;
    m_vny       = size;
    m_vnz       = size;

    m_vnxp      = size*npad;
    m_vnyp      = size*npad;
    m_vnzp      = size*npad;

    m_vnxc      = m_vnxp/2;
    m_vnyc      = m_vnyp/2;
    m_vnzc      = m_vnzp/2;

    buildFFTVolume();
    buildWFFTVolume();
    buildNormVolume();
    buildNorm2Volume();
    buildNorm3Volume();
    buildNorm4Volume();
    buildNorm5Volume();
}

void nnSSNR_ctfReconstructor::buildFFTVolume() {
	
	int offset = 2 - m_vnxp%2;
	if( params.has_key("fftvol") )
	{
        	m_volume = params["fftvol"]; /* volume should be defined in python when PMI is turned on*/
        	m_delete_volume = false;
    	}
    	else
    	{
		m_volume = new EMData();
        	m_delete_volume = true;
    	}
        
        m_volume->set_size(m_vnxp+offset,m_vnyp,m_vnzp);
        m_volume->to_zero();

	if ( m_vnxp % 2 == 0 ) { m_volume->set_fftodd(0); }
			else   { m_volume->set_fftodd(1); }
	
	m_volume->set_nxc(m_vnxc);
	m_volume->set_complex(true);
	m_volume->set_ri(true); //(real, imaginary) instead of polar coordinate
	m_volume->set_fftpad(true);
	m_volume->set_attr("npad", m_npad);
	m_volume->set_array_offsets(0,1,1);
}
void nnSSNR_ctfReconstructor::buildWFFTVolume() {	
	int offset = 2 - m_vnxp%2;        
	if( params.has_key("fftwvol") )
	{
		m_wvolume = params["fftwvol"]; 
		m_delete_volume = false;
	}
	else
	{
		m_wvolume = new EMData();
		m_delete_wvolume = true;
	}		
        m_wvolume->set_size(m_vnxp+offset,m_vnyp,m_vnzp);
        m_wvolume->to_zero();

	if ( m_vnxp % 2 == 0 ) { m_wvolume->set_fftodd(0); }
			else   { m_wvolume->set_fftodd(1); }
	
	m_wvolume->set_nxc(m_vnxc);
	m_wvolume->set_complex(true);
	m_wvolume->set_ri(true); //(real, imaginary) instead of polar coordinate
	m_wvolume->set_fftpad(true);
	m_wvolume->set_attr("npad", m_npad);
	m_wvolume->set_array_offsets(0,1,1);
}


void nnSSNR_ctfReconstructor::buildNormVolume() 
{
	if( params.has_key("weight") )
	{
		 m_wptr          = params["weight"]; 
		 m_delete_weight = false;
	}
	else
	{
		m_wptr = new EMData();
		m_delete_weight = true;
	}		
	m_wptr->set_size(m_vnxc+1,m_vnyp,m_vnzp);
	m_wptr->to_zero();
	m_wptr->set_array_offsets(0,1,1);
}

void nnSSNR_ctfReconstructor::buildNorm2Volume() {

	if( params.has_key("weight2") )
	{
		m_wptr2          = params["weight2"]; 
		m_delete_weight2 = false;
	}
	else
	{
		m_wptr2 = new EMData();
		m_delete_weight2 = true;
	}		
	m_wptr2->set_size(m_vnxc+1,m_vnyp,m_vnzp);
	m_wptr2->to_zero();
	m_wptr2->set_array_offsets(0,1,1);
}

void nnSSNR_ctfReconstructor::buildNorm3Volume() {
	
	if( params.has_key("weight3") )
	{
		m_wptr3          = params["weight3"]; 
		m_delete_weight3 = false;
	}
	else
	{
		m_wptr3 = new EMData();
		m_delete_weight3 = true;
	}		
	m_wptr3->set_size(m_vnxc+1,m_vnyp,m_vnzp);
	m_wptr3->to_zero();
	m_wptr3->set_array_offsets(0,1,1);
}
void nnSSNR_ctfReconstructor::buildNorm4Volume() {

	if( params.has_key("weight4") )
	{
		m_wptr4          = params["weight4"]; 
		m_delete_weight4 = false;
	}
	else
	{
		m_wptr4 = new EMData();
		m_delete_weight4 = true;
	}		
	m_wptr4->set_size(m_vnxc+1,m_vnyp,m_vnzp);
	m_wptr4->to_zero();
	m_wptr4->set_array_offsets(0,1,1);
}
void nnSSNR_ctfReconstructor::buildNorm5Volume() {

	if( params.has_key("weight5") )
	{
		m_wptr5          = params["weight5"]; 
		m_delete_weight5 = false;
	}
	else
	{
		m_wptr5 = new EMData();
		m_delete_weight5 = true;
	}		
	m_wptr5->set_size(m_vnxc+1,m_vnyp,m_vnzp);
	m_wptr5->to_zero();
	m_wptr5->set_array_offsets(0,1,1);
}
int nnSSNR_ctfReconstructor::insert_slice(const EMData *const  slice, const Transform3D& t) {
	// sanity checks
	if (!slice) 
	{
		LOGERR("try to insert NULL slice");
		return 1;
	}
        int padffted=0;
        try 
	{
	    padffted= slice->get_attr("padffted");
        }
        catch(_NotExistingObjectException) 
	{
	    padffted= 0;
        }
	if ( padffted==0 && (slice->get_xsize()!=slice->get_ysize() || slice->get_xsize()!=m_vnx)  )
        {
		// FIXME: Why doesn't this throw an exception?
		LOGERR("Tried to insert a slice that is the wrong size.");
		return 1;
	}
    EMData* padfft = NULL;

    if( padffted != 0 )
    {	   
        padfft = new EMData(*slice);
    }
    else
    {
        padfft = padfft_slice( slice, m_npad );
    }

    int mult=0;
    try 
	{
	    mult = slice->get_attr("mult");
        }
        catch(_NotExistingObjectException) 
	{
	    mult = 1;
        }

	Assert( mult > 0 );
	insert_padfft_slice( padfft, t, mult );

	checked_delete( padfft );
	return 0;
}
int nnSSNR_ctfReconstructor::insert_padfft_slice( EMData* padfft, const Transform3D& t, int mult )
{
	Assert( padfft != NULL );
	// insert slice for all symmetry related positions
	if ( params.has_key("fftvol"))
		wiener =  m_volume->get_attr("wiener");
	for (int isym=0; isym < m_nsym; isym++) 
	{
		Transform3D tsym = t.get_sym(m_symmetry, isym);
		if ( wiener == 0 )
		m_volume->nn_SSNR_ctf( m_wptr, m_wptr2, m_wptr3, m_wptr4, m_wptr5, padfft, m_wvolume, tsym, mult);
		else 
		m_wvolume->nn_wiener(m_wptr, m_wptr3, padfft, tsym, mult);	
	}
	
	return 0;
}

#define  tw(i,j,k)      tw[ i-1 + (j-1+(k-1)*iy)*ix ]
EMData* nnSSNR_ctfReconstructor::finish() 
{
    /***
    	m_wptr   ctf^2
   	m_wptr5  ctf^2*|P^2D->3D(F^3D)|^2 
   	m_wptr4  -2*Real(conj(F_k^2D)*ctf*P^2D->3D(F^3D))
  	m_wptr2  F_k^2D*conj(F_k^2D) or |F_k^2D|^2 
	m_wptr3  Kn
	variance  = Gamma^2d->3d [ |F_k^2D|^2   +  ctf^2*|P^2D->3D(F^3D)|^2 -2*Real(conj(F_k^2D)*ctf*P^2D->3D(F^3D))]
	signal    = Gamma^2d->3d [ |F_k^2D|^2  ]     
	nominator = sum_rot [ wght*signal ]
	denominator  = sum_rot[ wght*variance ]                        
						      ***/
	int kz, ky;
 	int box = 7;
        int kc  = (box-1)/2;
	float alpha = 0.0;
	float argx, argy, argz;
	vector< float > pow_a( 3*kc+1, 1.0 );
	vector< float > pow_b( 3*m_vnyc+1, 1.0 );
        float w = params["w"];
	EMData* SSNR = params["SSNR"];
	EMData* vol_ssnr = new EMData();
	vol_ssnr->set_size(m_vnxp+2 - m_vnxp%2, m_vnyp, m_vnzp);
	vol_ssnr->set_array_offsets(0,1,1);
	vol_ssnr->to_zero();
	float dx2 = 1.0f/float(m_vnxc)/float(m_vnxc); 
	float dy2 = 1.0f/float(m_vnyc)/float(m_vnyc);
	float dz2 = 1.0f/std::max(float(m_vnzc),1.0f)/std::max(float(m_vnzc),1.0f);	
	int inc = Util::round(float(std::max(std::max(m_vnxc,m_vnyc),m_vnzc))/w);		
	if (wiener == 1) // pre-calculate Wiener volume for SSNR calculation, Weighting factor is NOT applied in this step!
	{  
		
		
		m_wvolume->symplane0(m_wptr);
		float osnr = 1.0f/m_snr;
		for (int iz = 1; iz <= m_vnzp; iz++) 
		{
			if ( iz-1 > m_vnzc ) kz = iz-1-m_vnzp; else kz = iz-1;
			argz = float(kz*kz)*dz2;  
			for (int iy = 1; iy <= m_vnyp; iy++) 
			{
				if ( iy-1 > m_vnyc ) ky = iy-1-m_vnyp; else ky = iy-1;
				argy = argz + float(ky*ky)*dy2;
				for (int ix = 0; ix <= m_vnxc; ix++) 
				{
					float Kn = (*m_wptr3)(ix,iy,iz);
					if ( Kn > 0.0f ) 
					{  
						argx = std::sqrt(argy + float(ix*ix)*dx2);
						float tmp = (-2*((ix+iy+iz)%2)+1)/((*m_wptr)(ix,iy,iz)+osnr)*m_sign;
												   
						 /* if ( ix ==1 && iy ==1)
	 						    {  std::cout<<"****"<<m_wvolume->cmplx(ix,iy,iz)<<"  "<< osnr
							    <<std::endl;}*/
						m_wvolume->cmplx(ix,iy,iz) *= tmp; 
 
				        if (m_wvolume->is_fftodd()) 
						{
							float temp = float(iz-1+iy-1+ix)/float(m_vnyp)*M_PI;
							complex<float> temp2 = complex<float>(cos(temp),sin(temp));
							m_wvolume->cmplx(ix,iy,iz) *= temp2;
						}
					}
				}
			}
		}
		EMData* win = m_wvolume->do_ift();
		win->window_center(m_vnx);
		m_wptr->to_zero(); 
		m_wptr->set_array_offsets(0,1,1);
		wiener = 0 ; // Turn off flag
		return win; // The function requires a returned object, otherwise is not neccessary
	}		
    else  //Calculate SSNR 
    {   	
 	float wght = 1.f;
	SSNR->set_size(inc+1,4,1);
	float *nom    = new float[inc+1];
	float *denom  = new float[inc+1];
	int *ka = new int[inc+1];
	int *nn = new int[inc+1];
	for (int i = 0; i <= inc; i++) 
	{
		nom[i]   = 0.0f;
		denom[i] = 0.0f;
		nn[i]    = 0;
		ka[i]    = 0;
	}
	m_volume->symplane2(m_wptr, m_wptr2, m_wptr3);
	if ( m_weighting == ESTIMATE ) 
	{
		int vol = box*box*box;
		for( unsigned int i=1; i < pow_a.size(); ++i ) pow_a[i] = pow_a[i-1] * exp(m_wghta);
		pow_a[3*kc] = 0.0;
		for( unsigned int i=1; i < pow_b.size(); ++i ) pow_b[i] = pow_b[i-1] * exp(m_wghtb);
		float max = max3d( kc, pow_a );
		alpha = ( 1.0 - 1.0/vol ) / max;
	}	
	float osnr = 1.0f/m_snr;
	for (int iz = 1; iz <= m_vnzp; iz++) 
	{
		if ( iz-1 > m_vnzc ) kz = iz-1-m_vnzp; else kz = iz-1;
		argz = float(kz*kz)*dz2;  
		for (int iy = 1; iy <= m_vnyp; iy++) 
		{
			if ( iy-1 > m_vnyc ) ky = iy-1-m_vnyp; else ky = iy-1;
			argy = argz + float(ky*ky)*dy2;
			for (int ix = 0; ix <= m_vnxc; ix++) 
			{
				float Kn = (*m_wptr3)(ix,iy,iz);	  
				if ( Kn > 0.0f ) 
				{
					argx = std::sqrt(argy + float(ix*ix)*dx2);
					int r = Util::round(float(inc)*argx);
					float tmp = (-2*((ix+iy+iz)%2)+1)/((*m_wptr)(ix,iy,iz)+osnr)*m_sign;
					if ( m_weighting == ESTIMATE ) 
					{
						int cx = ix;
						int cy = (iy<=m_vnyc) ? iy - 1 : iy - 1 - m_vnyp;
						int cz = (iz<=m_vnzc) ? iz - 1 : iz - 1 - m_vnzp;
						float sum = 0.0;
						for( int ii = -kc; ii <= kc; ++ii ) 
						{ 
							int nbrcx = cx + ii;
							if( nbrcx >= m_vnxc ) continue;
						        for ( int jj= -kc; jj <= kc; ++jj ) 
							{
								int nbrcy = cy + jj;
								if( nbrcy <= -m_vnyc || nbrcy >= m_vnyc ) continue;
								for( int kk = -kc; kk <= kc; ++kk ) 
								{
									int nbrcz = cz + jj;
		                                                        if ( nbrcz <= -m_vnyc || nbrcz >= m_vnyc ) continue;
									if( nbrcx < 0 ) 
									{
										nbrcx = -nbrcx;
										nbrcy = -nbrcy;
										nbrcz = -nbrcz;
									}
		                                                        int nbrix = nbrcx;
									int nbriy = nbrcy >= 0 ? nbrcy + 1 : nbrcy + 1 + m_vnyp;
									int nbriz = nbrcz >= 0 ? nbrcz + 1 : nbrcz + 1 + m_vnzp;
									if( (*m_wptr)( nbrix, nbriy, nbriz ) == 0 ) 
									{
										int c = 3*kc+1 - std::abs(ii) - std::abs(jj) - std::abs(kk);
										sum = sum + pow_a[c];
									}
                                                    		}
                                                	}
						}
						int r = std::abs(cx) + std::abs(cy) + std::abs(cz);
						Assert( r >=0 && r < (int)pow_b.size() );
						wght = pow_b[r] / ( 1.0 - alpha * sum );
						tmp = tmp * wght;
				        } // end of ( m_weighting == ESTIMATE )
					if ( r >= 0 && r <= inc && Kn > 1.5f && ( ix > 0 || kz > 0 || kz == 0 && ky >= 0 )) 
					{
						nom[r]   += (*m_wptr2)(ix,iy,iz)*wght/Kn;

						denom[r] += ((*m_wptr2)(ix,iy,iz)+(*m_wptr4)(ix,iy,iz)+(*m_wptr5)(ix,iy,iz))*wght/(Kn*(Kn-1.0f));
						nn[r]    += 2;
						ka[r]    += int(Kn);
					}
				if ( Kn > 1.0f)
				{
				 int iiy, iiz;
				 if (iy <= m_vnyc )
				 {
				  	 iiy = m_vnyc +iy;
				  }
				 else
				 {
					 iiy = iy - m_vnyc+1;
				 }
				 if (iz<= m_vnzc )
				{
       					 iiz = m_vnzc +iz;
 				}
				else
				{
       					 iiz = iz - m_vnzc+1;
				}
				 (*vol_ssnr)(m_vnxc+(2 - m_vnxp%2)/2+ix,iiy,iiz)=(*m_wptr2)(ix,iy,iz)/((*m_wptr2)(ix,iy,iz)+(*m_wptr4)(ix,iy,iz)+(*m_wptr5)(ix,iy,iz))*(Kn*(Kn-1.0f)/Kn);
				 (*vol_ssnr)(m_vnxc+(2 - m_vnxp%2)/2-ix,iiy,iiz)=(*m_wptr2)(ix,iy,iz)/((*m_wptr2)(ix,iy,iz)+(*m_wptr4)(ix,iy,iz)+(*m_wptr5)(ix,iy,iz))*(Kn*(Kn-1.0f)/Kn);
				}
					
				}
			}
		}
	}
	for (int i = 0; i <= inc; i++)  
	{ 
		(*SSNR)(i,0,0) = nom[i]; 
		(*SSNR)(i,1,0) = denom[i];
		(*SSNR)(i,2,0) = nn[i];
		(*SSNR)(i,3,0) = ka[i];
				
	}
	
	vol_ssnr->update();
	return vol_ssnr;
   }

}
#undef  tw
// -----------------------------------------------------------------------------------
// End of this addition



// bootstrap_nnctfReconstructor
bootstrap_nnctfReconstructor::bootstrap_nnctfReconstructor()
{
}

bootstrap_nnctfReconstructor::~bootstrap_nnctfReconstructor()
{
    for_each( m_padffts.begin(), m_padffts.end(), boost::ptr_fun( checked_delete< EMData > ) );
    for_each( m_transes.begin(), m_transes.end(), boost::ptr_fun( checked_delete< Transform3D > ) );
}

void bootstrap_nnctfReconstructor::setup()
{
    m_npad = params["npad"];
    m_size = params["size"];
    m_sign = params["sign"];
    m_media = params["media"].to_str();
    m_snr = params["snr"];

    try {
	m_symmetry = params["symmetry"].to_str();
	if ("" == m_symmetry) m_symmetry = "c1";
    }
    catch(_NotExistingObjectException) {
        m_symmetry = "c1";
    }
        
    m_nsym = Transform3D::get_nsym(m_symmetry);
}

int bootstrap_nnctfReconstructor::insert_slice(const EMData* const slice, const Transform3D& euler)
{
    EMData* padfft = padfft_slice( slice, m_npad );
    Assert( padfft != NULL );

    if( m_media == "memory" )
    {
        m_padffts.push_back( padfft );
    }
    else
    {
        padfft->write_image( m_media, m_transes.size() );
	checked_delete( padfft );
    }

    Transform3D* trans = new Transform3D( euler );
    m_transes.push_back( trans );
    return 0;
}

EMData* bootstrap_nnctfReconstructor::finish()
{
    nn4_ctfReconstructor* r( new nn4_ctfReconstructor(m_symmetry, m_size, m_npad, m_snr, m_sign) );
    vector<int> mults = params["mult"];
//    assert( mults != NULL );
    Assert( m_transes.size() == mults.size() );

    int total = 0;
    for( unsigned int i=0; i < mults.size(); ++i )
    {
        int mult = mults.at(i);
	if( mult > 0 )
	{
	    if( m_media == "memory" )
	    {
                r->insert_padfft_slice( m_padffts[i], *m_transes[i], mult );
	    }
	    else
	    {
	        EMData* padfft = new EMData();
		padfft->read_image( m_media, i );
		r->insert_padfft_slice( padfft, *m_transes[i], mult );
		checked_delete( padfft );
            }
	}
	total += mult;
    }

    EMData* w = r->finish()->copy();
    checked_delete(r);
    return w;
}


bool FourierInserter3DMode1::insert_pixel(const float& xx, const float& yy, const float& zz, const float dt[], const float& weight)
{
	int x0 = 2 * (int) floor(xx + 0.5f);
	int y0 = (int) floor(yy + 0.5f);
	int z0 = (int) floor(zz + 0.5f);

	rdata[x0 + y0 * nx + z0 * nxy] += weight * dt[0];
	rdata[x0 + y0 * nx + z0 * nxy + 1] += weight * dt[1];
	norm[x0 + y0 * nx + z0 * nxy] += weight;
	
	return true;
}

FourierInserter3DMode2::FourierInserter3DMode2(float * const normalize_values, float * const real_data, const unsigned int xsize, const unsigned int ysize, const unsigned int zsize) :
	FourierPixelInserter3D( normalize_values, real_data, xsize, ysize, zsize )
{
	off[0] = 0;
	off[1] = 2;
	off[2] = nx;
	off[3] = nx + 2;
	off[4] = nxy;
	off[5] = nxy + 2;
	off[6] = nxy + nx;
	off[7] = nxy + nx + 2;
}
		
bool FourierInserter3DMode2::insert_pixel(const float& xx, const float& yy, const float& zz, const float dt[], const float& weight)
{
	int x0 = (int) floor(xx);
	int y0 = (int) floor(yy);
	int z0 = (int) floor(zz);

	float dx = xx - x0;
	float dy = yy - y0;
	float dz = zz - z0;

	if (x0 > nx - 2 || y0 > ny - 1 || z0 > nz - 1) {
		return false;
	}

	int i = (int) (x0 * 2 + y0 * nx + z0 * nxy);

	g[0] = Util::agauss(1, dx, dy, dz, EMConsts::I2G);
	g[1] = Util::agauss(1, 1 - dx, dy, dz, EMConsts::I2G);
	g[2] = Util::agauss(1, dx, 1 - dy, dz, EMConsts::I2G);
	g[3] = Util::agauss(1, 1 - dx, 1 - dy, dz, EMConsts::I2G);
	g[4] = Util::agauss(1, dx, dy, 1 - dz, EMConsts::I2G);
	g[5] = Util::agauss(1, 1 - dx, dy, 1 - dz, EMConsts::I2G);
	g[6] = Util::agauss(1, dx, 1 - dy, 1 - dz, EMConsts::I2G);
	g[7] = Util::agauss(1, 1 - dx, 1 - dy, 1 - dz, EMConsts::I2G);

	// debug
//	Have to get radial coordinates - x is fine as it is but the other two need translation
// 	int yt = (int) floor(yy - ny/2);
// 	int zt = (int) floor(zz - nz/2);
// 
// 	int radius =  x0*x0 + yt*yt + zt*zt;
// 	radius = int(sqrtf(radius));
	// end debug
	
// 	int center = (int) (nx + ny * nx / 2 + nz * nxy / 2);

	for (int j = 0; j < 8; j++) {
		int k = i + off[j];
// 		if ( k == center )
// 		{
// 			cout << j << " at radius " << radius << " real and complex are " << rdata[k] << " " << rdata[k + 1] << endl;
// 			cout << "Adding real and complex to center pixel " << dt[0] << " " << dt[1] << " weighted by " << g[j] << endl;
// 		}


		rdata[k] += weight * dt[0] * g[j];
		rdata[k + 1] += weight * dt[1] * g[j];
// 		if (radius == 0)
			//cout << j << " first real " << dt[0] << " " << rdata[k] << " imag " << dt[1] << " " << rdata[k + 1] << " " << g[j] << endl;
		norm[k] += weight * g[j];
		// FIXME: this second normalization value was original used in make3d in EMAN1 to generate an orientation dependent measure of SNR
		// at the time of coding it was not being explicitly supported.
		// Eventually if may be necessary to halve the size of the norm array to ensure efficient usage of memory
		// - d.woolford June 2007
//		norm[k + 1] += weight * g[j] * dt[0] * dt[0] * dt[1] * dt[1];
	}
	
	return true;
}
		
bool FourierInserter3DMode3::insert_pixel(const float& xx, const float& yy, const float& zz, const float dt[], const float& weight)
{
	int x0 = 2 * (int) floor(xx + 0.5f);
	int y0 = (int) floor(yy + 0.5f);
	int z0 = (int) floor(zz + 0.5f);

	if (x0 >= nx - 4 || y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2) {
		return false;
	}

	int l = x0 - 2;
	if (x0 == 0) {
		l = x0;
	}

	for (int k = z0 - 1; k <= z0 + 1; k++) {
		for (int j = y0 - 1; j <= y0 + 1; j++) {
			for (int i = l; i <= x0 + 2; i += 2) {
				float r = Util::hypot3((float) i / 2 - xx, j - yy, k - zz);
				float gg = exp(-r / EMConsts::I3G);

				rdata[i + j * nx + k * nxy] += weight * gg * dt[0];
				rdata[i + j * nx + k * nxy + 1] += weight * gg * dt[1];
				norm[i + j * nx + k * nxy] += weight * gg;
			}
		}
	}
	return true;
}

bool FourierInserter3DMode4::insert_pixel(const float& xx, const float& yy, const float& zz, const float dt[], const float& weight)
{
	int x0 = 2 * (int) floor(xx);
	int y0 = (int) floor(yy);
	int z0 = (int) floor(zz);

	if (x0 >= nx - 4 || y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2) {
		return false;
	}

	int l = x0 - 2;
	if (x0 == 0) {
		l = x0;
	}

	for (int k = z0 - 1; k <= z0 + 2; k++) {
		for (int j = y0 - 1; j <= y0 + 2; j++) {
			for (int i = l; i <= x0 + 4; i += 2) {
				float r = Util::hypot3((float) i / 2 - xx, j - yy, k - zz);
				float gg = exp(-r / EMConsts::I4G);

				rdata[i + j * nx + k * nxy] += weight * gg * dt[0];
				rdata[i + j * nx + k * nxy + 1] += weight * gg * dt[1];
				norm[i + j * nx + k * nxy] += weight * gg;
			}
		}
	}
	
	return true;
}

bool FourierInserter3DMode5::insert_pixel(const float& xx, const float& yy, const float& zz, const float dt[], const float& weight)
{
	int x0 = (int) floor(xx + 0.5f);
	int y0 = (int) floor(yy + 0.5f);
	int z0 = (int) floor(zz + 0.5f);

	int mx0 = -(int) floor((xx - x0) * 39.0f + 0.5f) - 78;
	int my0 = -(int) floor((yy - y0) * 39.0f + 0.5f) - 78;
	int mz0 = -(int) floor((zz - z0) * 39.0f + 0.5f) - 78;

	x0 *= 2;

	if (x0 >= nx - 4 || y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2) {
		return false;
	}

	int l = 0;
	if (x0 == 0) {
		mx0 += 78;
	}
	else if (x0 == 2) {
		mx0 += 39;
	}
	else {
		l = x0 - 4;
	}

	for (int k = z0 - 2, mmz = mz0; k <= z0 + 2; k++, mmz += 39) {
		for (int j = y0 - 2, mmy = my0; j <= y0 + 2; j++, mmy += 39) {
			for (int i = l, mmx = mx0; i <= x0 + 4; i += 2, mmx += 39) {
				int ii = i + j * nx + k * nxy;
				float gg = weight * gimx[abs(mmx) + abs(mmy) * 100 + abs(mmz) * 10000];

				rdata[ii] += gg * dt[0];
				rdata[ii + 1] += gg * dt[1];
				norm[ii] += gg;
			}
		}
	}

	if (x0 <= 2) {
		float xx_b = -xx;
		float yy_b = -(yy - ny / 2) + ny / 2;
		float zz_b = -(zz - nz / 2) + nz / 2;

		x0 = (int) floor(xx_b + 0.5f);
		y0 = (int) floor(yy_b + 0.5f);
		z0 = (int) floor(zz_b + 0.5f);

		int mx0 = -(int) floor((xx_b - x0) * 39.0f + 0.5f);
		x0 *= 2;

		if (y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2)
			return false;

		for (int k = z0 - 2, mmz = mz0; k <= z0 + 2; k++, mmz += 39) {
			for (int j = y0 - 2, mmy = my0; j <= y0 + 2; j++, mmy += 39) {
				for (int i = 0, mmx = mx0; i <= x0 + 4; i += 2, mmx += 39) {
					int ii = i + j * nx + k * nxy;
					float gg =
						weight * gimx[abs(mmx) + abs(mmy) * 100 + abs(mmz) * 10000];
					rdata[ii] += gg * dt[0];
					rdata[ii + 1] -= gg * dt[1];
					norm[ii] += gg;
				}
			}
		}
	}

	return true;
}

bool FourierInserter3DMode6::insert_pixel(const float& xx, const float& yy, const float& zz, const float dt[], const float& weight)
{
	int	x0 = 2 * (int) floor(xx + 0.5f);
	int y0 = (int) floor(yy + 0.5f);
	int z0 = (int) floor(zz + 0.5f);

	if (x0 >= nx - 4 || y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2)
		return false;

	int l = x0 - 4;
	if (x0 <= 2) {
		l = 0;
	}

	for (int k = z0 - 2; k <= z0 + 2; k++) {
		for (int j = y0 - 2; j <= y0 + 2; j++) {
			for (int i = l; i <= x0 + 4; i += 2) {
				int ii = i + j * nx + k * nxy;
				float r = Util::hypot3((float) i / 2 - xx, j - yy, k - zz);
				float gg = weight * exp(-r / EMConsts::I5G);

				rdata[ii] += gg * dt[0];
				rdata[ii + 1] += gg * dt[1];
				norm[ii] += gg;
			}
		}
	}

	if (x0 <= 2) {
		float xx_b = -xx;
		float yy_b = -(yy - ny / 2) + ny / 2;
		float zz_b = -(zz - nz / 2) + nz / 2;

		x0 = 2 * (int) floor(xx_b + 0.5f);
		y0 = (int) floor(yy_b + 0.5f);
		z0 = (int) floor(zz_b + 0.5f);

		if (y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2)
			return false;

		for (int k = z0 - 2; k <= z0 + 2; k++) {
			for (int j = y0 - 2; j <= y0 + 2; j++) {
				for (int i = 0; i <= x0 + 4; i += 2) {
					int ii = i + j * nx + k * nxy;
					float r = Util::hypot3((float) i / 2 - xx_b, (float) j - yy_b,
										   (float) k - zz_b);
					float gg = weight * exp(-r / EMConsts::I5G);

					rdata[ii] += gg * dt[0];
					rdata[ii + 1] -= gg * dt[1];
					norm[ii] += gg;
				}
			}
		}
	}
	
	return true;
}

bool FourierInserter3DMode7::insert_pixel(const float& xx, const float& yy, const float& zz, const float dt[], const float& weight)
{
	int x0 = 2 * (int) floor(xx + 0.5f);
	int y0 = (int) floor(yy + 0.5f);
	int z0 = (int) floor(zz + 0.5f);

	if (x0 >= nx - 4 || y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2)
		return false;

	int l = x0 - 4;
	if (x0 <= 2)
		l = 0;

	for (int k = z0 - 2; k <= z0 + 2; k++) {
		for (int j = y0 - 2; j <= y0 + 2; j++) {
			for (int i = l; i <= x0 + 4; i += 2) {
				int ii = i + j * nx + k * nxy;
				float r =
					sqrt(Util::
						 hypot3((float) i / 2 - xx, (float) j - yy, (float) k - zz));
				float gg = weight * Interp::hyperg(r);

				rdata[ii] += gg * dt[0];
				rdata[ii + 1] += gg * dt[1];
				norm[ii] += gg;
			}
		}
	}

	if (x0 <= 2) {
		float xx_b = -xx;
		float yy_b = -(yy - ny / 2) + ny / 2;
		float zz_b = -(zz - nz / 2) + nz / 2;
		x0 = 2 * (int) floor(xx_b + 0.5f);
		y0 = (int) floor(yy_b + 0.5f);
		z0 = (int) floor(zz_b + 0.5f);

		if (y0 > ny - 3 || z0 > nz - 3 || y0 < 2 || z0 < 2)
			return false;

		for (int k = z0 - 2; k <= z0 + 2; k++) {
			for (int j = y0 - 2; j <= y0 + 2; j++) {
				for (int i = 0; i <= x0 + 4; i += 2) {
					int ii = i + j * nx + k * nxy;
					float r = sqrt(Util::hypot3((float) i / 2 - xx_b, (float) j - yy_b,
												(float) k - zz_b));
					float gg = weight * Interp::hyperg(r);

					rdata[ii] += gg * dt[0];
					rdata[ii + 1] -= gg * dt[1];
					norm[ii] += gg;
				}
			}
		}
	}
	
	return true;
}

void EMAN::dump_reconstructors()
{
	dump_factory < Reconstructor > ();
}

map<string, vector<string> > EMAN::dump_reconstructors_list()
{
	return dump_factory_list < Reconstructor > ();
}

file_store::file_store(const string& filename, int npad, int write)
    : m_bin_file(filename + ".bin"), 
      m_txt_file(filename + ".txt")
{
    m_prev = -1;
    m_npad = npad;
    m_write = write;
}

file_store::~file_store()
{
}

using std::ofstream;
using std::ifstream;

void file_store::add_image(  EMData* emdata )
{
    EMData* padfft = padfft_slice( emdata, m_npad );

    float* data = padfft->get_data();

    if( m_write && m_bin_ohandle == NULL )
    {
        m_bin_ohandle = shared_ptr< ofstream >( new ofstream(m_bin_file.c_str(), std::ios::out | std::ios::binary) );
        m_txt_ohandle = shared_ptr< ofstream >( new ofstream(m_txt_file.c_str() ) );
        *m_txt_ohandle << "#Cs pixel voltage ctf_applied amp_contrast defocus phi theta psi" << std::endl;
    }

    m_xsize = padfft->get_xsize();
    m_ysize = padfft->get_ysize();
    m_zsize = padfft->get_zsize();
    m_totsize = m_xsize*m_ysize*m_zsize;  
 
        
    m_Cs = padfft->get_attr( "Cs" );
    m_pixel = padfft->get_attr( "Pixel_size" );
    m_voltage = padfft->get_attr( "voltage" );
    m_ctf_applied = padfft->get_attr( "ctf_applied" );
    m_amp_contrast = padfft->get_attr( "amp_contrast" );
    m_defocuses.push_back( (float)(padfft->get_attr( "defocus" )) );
    m_phis.push_back( (float)(padfft->get_attr( "phi" )) );
    m_thetas.push_back( (float)(padfft->get_attr( "theta" )) );
    m_psis.push_back( (float)(padfft->get_attr( "psi" )) );


    if( m_write )
    {
        m_bin_ohandle->write( (char*)data, sizeof(float)*m_totsize );
        *m_txt_ohandle << m_Cs << " ";
        *m_txt_ohandle << m_pixel << " ";
        *m_txt_ohandle << m_voltage << " ";
        *m_txt_ohandle << m_ctf_applied << " ";
        *m_txt_ohandle << m_amp_contrast << " ";
        *m_txt_ohandle << m_defocuses.back() << " ";
        *m_txt_ohandle << m_phis.back() << " ";
        *m_txt_ohandle << m_thetas.back() << " ";
        *m_txt_ohandle << m_psis.back() << " ";
        *m_txt_ohandle << m_xsize << " ";
        *m_txt_ohandle << m_ysize << " ";
        *m_txt_ohandle << m_zsize << " ";
        *m_txt_ohandle << m_totsize << std::endl;
    }

    checked_delete(padfft);
}

void file_store::get_image( int id, EMData* padfft )
{
    if( m_defocuses.size() == 0 )
    {
        ifstream m_txt_ifs( m_txt_file.c_str() );
        m_txt_ifs.ignore( 4096, '\n' );

        float defocus, phi, theta, psi;

        while( m_txt_ifs >> m_Cs )
        {
            m_txt_ifs >> m_pixel >> m_voltage;
            m_txt_ifs >> m_ctf_applied >> m_amp_contrast;
            m_txt_ifs >> defocus >> phi >> theta >> psi;
            m_txt_ifs >> m_xsize >> m_ysize >> m_zsize >> m_totsize;
            m_defocuses.push_back( defocus );
            m_phis.push_back( phi );
            m_thetas.push_back( theta );
            m_psis.push_back( psi );
        }
    }

    Assert( m_ihandle != NULL );

    std::istream::off_type offset = id*sizeof(float)*m_totsize;
    Assert( offset >= 0 );

    if( offset > 0 )
    {
        m_ihandle->seekg(offset, std::ios::beg);
    }

    if( m_ihandle->bad() )
    {
        std::cout << "bad while fetching id, offset: " << id << " " << offset << std::endl;
        throw std::logic_error( "bad happen" );
    }

    if( m_ihandle->fail() )
    {
        std::cout << "fail while fetching id, offset, curoff: " << id << " " << offset << std::endl;
        throw std::logic_error( "fail happen" );
    }

    if( m_ihandle->eof() )
    {
        std::cout << "eof while fetching id, offset: " << id << " " << offset << std::endl;
        throw std::logic_error( "eof happen" );
    }

    if( padfft->get_xsize() != m_xsize ||
        padfft->get_ysize() != m_ysize ||
        padfft->get_zsize() != m_zsize )
    {
        padfft->set_size(m_xsize, m_ysize, m_zsize);
    }

    char* data = (char*)(padfft->get_data());
    m_ihandle->read( data, sizeof(float)*m_totsize );
    padfft->update();

    padfft->set_attr( "Cs", m_Cs );
    padfft->set_attr( "Pixel_size", m_pixel );
    padfft->set_attr( "voltage", m_voltage );
    padfft->set_attr( "ctf_applied", m_ctf_applied );
    padfft->set_attr( "amp_contrast", m_amp_contrast );
    padfft->set_attr( "defocus", m_defocuses[id] );
    padfft->set_attr( "phi", m_phis[id] );
    padfft->set_attr( "theta", m_thetas[id] );
    padfft->set_attr( "psi", m_psis[id] );
    padfft->set_attr( "padffted", 1 );
}

void file_store::restart( )
{
    if( m_ihandle == NULL )
    {
        m_ihandle = shared_ptr< ifstream >( new ifstream(m_bin_file.c_str(), std::ios::in | std::ios::binary) );
    }
    
    if( m_ihandle->bad() || m_ihandle->fail() || m_ihandle->eof() )
    {
        m_ihandle->open( m_bin_file.c_str(), std::ios::binary );
    }
   
    m_ihandle->seekg( 0, std::ios::beg );
}
 

/* vim: set ts=4 noet: */
