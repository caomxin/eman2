#!/usr/bin/env python
from EMAN2 import *
from sparx import *
from string import atoi, replace, split, atof

class variancer:
    def __init__(self):
        self.nimg = 0
        self.sum1 = None
        self.sum2 = None

    def insert(self, img ):
        self.nimg += 1

        if self.sum1 is None:
            assert self.sum2 is None
            self.sum1 = img.copy()
            self.sum2 = img.copy()
            Util.mul_img( self.sum2, img )
        else:
            Util.add_img( self.sum1, img )
            Util.add_img2(self.sum2, img )

    def getvar(self):
        avg1 = self.sum1/self.nimg
        avg2 = self.sum2/self.nimg

        tmp = avg1.copy()
        Util.mul_img( tmp, avg1 )
        avg2 -= tmp

        avg2 *= (float(self.nimg)/float(self.nimg-1))
         
        return avg2


    def getavg(self):
	return self.sum1/self.nimg


def circumference( img, inner, outer ):
    nx = 75
    ny = 75
    nz = 75
    outer_sphere = model_circle( outer, nx, ny, nz )
    inner_sphere = model_circle( inner, nx, ny, nz )
    inner_rest = model_blank( nx, ny, nz, 1.0 ) - inner_sphere
    shell = outer_sphere - inner_sphere

    [mean_a,sigma,imin,imax]=Util.infomask(img,shell, True)
    b = img * inner_sphere + mean_a * inner_rest 
 
    return b

from sys import argv, exit


if len(argv) != 6:
    print "incvar.py prefix nfile output filter_a filter_b"
    exit(-1)

prefix = argv[1]
nfile  = atoi( argv[2] )
output = argv[3]
fa = atof( argv[4] )
fb = atof( argv[5] )

all_varer = variancer()
odd_varer = variancer()
eve_varer = variancer()

filname = prefix + "0000.hdf"
img = get_im(filname, 0)
n = img.get_xsize()

r = n//2-2
mask = model_circle(r, n, n, n)

totnimg = 0
iwrite = 0
for i in xrange(nfile):
    filename = prefix + ('%4d.hdf' % i )
    filename = replace( filename, ' ', '0' )
    outfilename = 'btwl_cir_' + filename
    print 'loading file ', filename
    nimg = EMUtil.get_image_count( filename )
    for j in xrange(nimg):

        img = get_im( filename, j )
        img *= 285.81
        img = circumference( img, 31, 32 )
        img = filt_btwl( img, fa, fb )
        img.write_image( "btwl_cir_"+filename, j )


        if totnimg%2==0:
            odd_varer.insert( img )
        else:
            eve_varer.insert( img )

        all_varer.insert( img )

        totnimg += 1

        if totnimg%100==0:
            odd_var = odd_varer.getvar()
            eve_var = eve_varer.getvar()
            all_var = all_varer.getvar()
          
            odd_var.write_image( 'odd_' + output, iwrite )
            eve_var.write_image( 'eve_' + output, iwrite )
            all_var.write_image( output, iwrite )
            iwrite += 1  
            print 'ntot, ccc: %6d %10.3f' % ( totnimg, ccc(odd_var, eve_var,m) )  

all_var = all_varer.getvar()
odd_var = odd_varer.getvar()
eve_var = eve_varer.getvar()
print 'ntot, ccc: %6d %10.3f' % ( totnimg, ccc(odd_var, eve_var,m) )  

avg = all_varer.getavg()
avg.write_image( 'avg_' + output, 0 )

all_var.write_image( output, iwrite  )
odd_var.write_image( 'odd_' + output, iwrite )
eve_var.write_image( 'eve_' + output, iwrite )
