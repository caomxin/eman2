#!/usr/bin/env python
#
# Author: Pawel A.Penczek and Edward H. Egelman 05/27/2009 (Pawel.A.Penczek@uth.tmc.edu)
# Copyright (c) 2000-2006 The University of Texas - Houston Medical School
# Copyright (c) 2008-Forever The University of Virginia
#
# This software is issued under a joint BSD/GNU license. You may use the
# source code in this file under either license. However, note that the
# complete EMAN2 and SPARX software packages have some GPL dependencies,
# so you are responsible for compliance with the licenses of these packages
# if you opt to use BSD licensing. The warranty disclaimer below holds
# in either instance.
#
# This complete copyright notice must be included in any revised version of the
# source code. Additional authorship citations may be added, but existing
# author citations must be preserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  2111-1307 USA
#
#


import os
import global_def
from global_def import *
from optparse import OptionParser
import sys
def main():
        arglist = []
        for arg in sys.argv:
        	arglist.append( arg )
	progname = os.path.basename(arglist[0])
	usage = progname + " stack ref_vol outdir <maskfile> --ir=inner_radius --ou=outer_radius --rs=ring_step --xr=x_range --yr=y_range  --ts=translational_search_step  --delta=angular_step --an=angular_neighborhood --center=1 --maxit=max_iter --CTF --snr=1.0  --ref_a=S --sym=c1 --datasym=symdoc"
	parser = OptionParser(usage,version=SPARXVERSION)
	parser.add_option("--ir",       type= "int",   default= 1,                  help="  inner radius for rotational correlation > 0 (set to 1)")
	parser.add_option("--ou",       type= "int",   default= -1,                 help="  outer radius for rotational correlation < int(nx/2)-1 (set to the radius of the particle)")
	parser.add_option("--rs",       type= "int",   default= 1,                  help="  step between rings in rotational correlation >0  (set to 1)" ) 
	parser.add_option("--min_cc_peak", type="float", default= 200.0,            help="  threshold for the CC peak")
	parser.add_option("--xr",       type="string", default= " 4  2 1  1   1",   help="  range for translation search in x direction, search is +/xr ")
	parser.add_option("--max_x_shift", type="float", default= 5.8,              help="  threshold for translation in x direction")
	parser.add_option("--yr",       type="string", default= "-1",               help="  range for translation search in y direction, search is +/yr (if = -1 then same as xr)")
	parser.add_option("--max_y_shift", type="float", default= 6.0,              help="  threshold for translation in y direction")
	parser.add_option("--max_tilt", type="float",  default= 10.0,               help="  threshold for in-plane rotation angle")
	parser.add_option("--ts", 	type="string", default= "1 1 1 0.5 0.25",                help="  step size of the translation search in both directions, search is -xr, -xr+ts, 0, xr-ts, xr ")
	parser.add_option("--delta",    type="string", default= " 10 6 4  3   2",   help="  angular step of reference projections")
	parser.add_option("--an",       type="string", default= "-1",               help="  angular neighborhood for local searches")
	parser.add_option("--maxit",    type="float",  default= 30,                 help="  maximum number of iterations performed for each angular step (set to 5) ")
	parser.add_option("--CTF",      action="store_true", default=False,         help="  Consider CTF correction during the alignment ")
	parser.add_option("--snr",      type="float",  default= 1.0,                help="  Signal-to-Noise Ratio of the data")   
	parser.add_option("--MPI",      action="store_true", default=False,         help="whether to use MPI version")
	parser.add_option("--Fourvar",  action="store_true", default=False,         help="compute Fourier variance")
	parser.add_option("--dp",       type="float",  default= 1.0,                help="  delta z - translation in Angstroms")   
	parser.add_option("--dphi",     type="float",  default= 1.0,                help="  delta phi - rotation in degrees")   
	parser.add_option("--pixel",    type="float",  default= 2.39,               help="  Pixel size in Angstroms")   
	parser.add_option("--rmin",     type="float",  default= 0.0,                help="  minimal radius for hsearch")   
	parser.add_option("--rmax",     type="float",  default= 80.0,               help="  maximal radius for hsearch")
	parser.add_option("--fract",    type="float",  default= 0.7,                help="  fraction of the volume used for helical search")
	parser.add_option("--pol_ang_step",type="int", default= 1,                  help="  angular increment for alignment of volume(i) with volume(i-1) in order to keep polarity fixed, integer, in degrees")
	parser.add_option("--step_a",   type="float",  default= 0.1,                help="  search step for hsearch - angle")
	parser.add_option("--step_r",   type="float",  default= 0.1,                help="  search step for hsearch - rise")
	parser.add_option("--sym",      type="string", default= "c1",               help="  symmetry of the structure")
	parser.add_option("--function", type="string", default="ref_ali3d",  	    help="  name of the reference preparation function")
	parser.add_option("--datasym",  type="string", default= " ",                help="  symdoc")
	(options, args) = parser.parse_args(arglist[1:])
	if len(args) < 3 or len(args) > 4:
    		print "usage: " + usage
    		print "Please run '" + progname + " -h' for detailed options"
	else:
		if len(args) == 3 :
			mask = None
		else:
			mask = args[3]
		if options.MPI:
			from mpi import mpi_init
			sys.argv = mpi_init(len(sys.argv), sys.argv)

			from utilities import init_mpi_bdb
			init_mpi_bdb()

		from applications import ihrsr
		global_def.BATCH = True
		ihrsr(args[0], args[1], args[2], mask, options.ir, options.ou, options.rs, options.min_cc_peak, options.xr, 
			options.max_x_shift, options.yr, options.max_y_shift, options.max_tilt, options.ts, options.delta, 
			options.an, options.maxit, options.CTF, options.snr, options.dp, options.dphi, options.pixel, 
			options.rmin, options.rmax, options.fract, options.pol_ang_step, options.step_a, options.step_r, 
			options.sym, options.function, options.datasym, options.Fourvar, options.MPI) 
		global_def.BATCH = False

if __name__ == "__main__":
	main()
