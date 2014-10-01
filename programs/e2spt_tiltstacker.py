#!/usr/bin/env python																																																																																																																																																																																																																																																																																																																			#!/usr/bin/python2.7

#====================
#Author: Jesus Galaz-Montoya 2/20/2013 , Last update: September/09/2014
#====================
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


from optparse import OptionParser

from EMAN2 import *
import sys
import math

print "imported stuff"

def main():

	usage = """e2spt_tiltstacker.py <options> . 
	The options should be supplied in "--option=value" format, 
	replacing "option" for a valid option name, and "value" for an acceptable value for that option. 
	
	This program operates in 3 different modes:
	1) It can STACK individual .dm3, .tiff or .hdf images into an .mrc (or .st) stack,
	by supplying a common string to all the images to stack via --stem2stack.
	It must be run in a directory containing the numbered images only.
	It also generates a .rawtlt file with tilt angle values if --lowerend, --upperend and --tiltstep are provided.
	
	2) It can UNSTACK a tilt series into individual files (either all the images, or selected
	images, controlled through the --exclude or --include parameters).
	
	3) It can RESTACK a tilt series; that is, put together a new tilt series that excludes/includes
	specific images
	"""
			
	parser = EMArgumentParser(usage=usage,version=EMANVERSION)	
	
	parser.add_argument("--path",type=str,default='',help="""Directory to store results in. 
		The default is a numbered series of directories containing the prefix 'sptstacker';
		for example, sptstacker_02 will be the directory by default if 'sptstacker_01' 
		already exists.""")

	parser.add_argument("--stem2stack", type=str, default='', help="""String common to all 
		the files to put into an .st stack, which is in .MRC format; for example, --stem2stack=.hdf 
		will process all .hdf files in the current directory.
		If not specified, all valid EM imagefiles in the current directory will be put into 
		an .st stack.""")
	
	parser.add_argument("--tltfile",type=str,default='',help="""".tlt file IF unstacking an
		aligned tilt series with --unstack=<stackfile> or restacking a tiltseries with
		--restack=<stackfile>""")
		
	parser.add_argument("--invert",action="store_true",default=False,help=""""This 
		will multiply the pixel values by -1.""")
	
	parser.add_argument("--outmode", type=str, default="float", help="""All EMAN2 programs 
		write images with 4-byte floating point values when possible by default. This allows 
		specifying an alternate format when supported: float, int8, int16, int32, uint8, 
		uint16, uint32. Values are rescaled to fill MIN-MAX range.""")
	
	parser.add_argument("--bidirectional",action='store_true',default=False,help="""This will
		assume the first image is at 0 degrees and will stack images from --lowerend through 0, 
		and then will stack the rest from 0+tiltstep throgh --upperend. 
		If --negativetiltseries is supplied, images will be stacked from --upperend through 0, then 
		from 0-tiltstep through --lowerend.""")
		 	
	parser.add_argument("--negativetiltseries",action='store_true',default=False,help=""" 
		This indicates that the tilt series goes from -tiltrange to +tiltrange, or
		0 to -tiltrange, then +tiltstep to +tiltrange if --bidirectional is specified.""")
	
	parser.add_argument("--lowesttilt",type=float,default=0.0,help="""Lowest tilt angle.
		If not supplied, it will be assumed to be -1* --tiltrange.""")
	
	parser.add_argument("--highesttilt",type=float,default=0.0,help="""Highest tilt angle.
		If not supplied, it will be assumed to be 1* --tiltrange.""")
	
	parser.add_argument("--tiltrange",type=float,default=0.0,help="""If provided, this
		will make --lowesttilt=-1*tiltrange and --highesttilt=tiltrage.
		If the range is asymmetric, supply --lowesttilt and --highesttilt directly.""")
	
	parser.add_argument("--tiltstep",type=float,default=0.0,help="""Step between tilts.
		Required if using --stem2stack.""")
	
	parser.add_argument("--clip",type=str,default='',help="""Resize the 2-D images in the
		tilt series. If one number is provided, then x and y dimensions will be made the same.
		To specify both dimensions, supply two numbers, --clip=x,y.""")
			
	parser.add_argument("--apix",type=float,default=0.0,help="""True apix of images to be 
		written on final stack.""")
	
	parser.add_argument("--unstack",type=str,default='',help=""".hdf, or 3D .st, .mrc, 
		.ali, or .mrcs stack file to unstack.
		This option can be used with --include or --exclude to unstack only specific images.
		Recall that the FIRST image INDEX is 0 (but unstacked image will be numbered from 1). 
		--exclude=1,5-7,10,12,15-19 will exclude images 1,5,6,7,10,12,15,16,17,18,19""""")
	
	parser.add_argument("--restack",type=str,default='',help=""".hdf, or 3D .st, .mrc, 
		.ali, or .mrcs stack file to restack.
		This option can be used with --include or --exclude to unstack only specific images.
		Recall that the FIRST image INDEX is 0 (but unstacked image will be numbered from 1). 
		--exclude=1,5-7,10,12,15-19 will exclude images 1,5,6,7,10,12,15,16,17,18,19""""")
	
	parser.add_argument("--mirroraxis",type=str,default='',help="""Options are x or y, and the
		mirrored copy of the 2-D images will be generated before being put into the tilt series.""")
	
	parser.add_argument("--exclude",type=str,default='',help="""Comma separated list of numbers
		corresponding to images to exclude. --unstack or --restack must be supplied. 
		You can also exclude by ranges. For example:
		Recall that the FIRST image INDEX is 0. 
		--exclude=1,5-7,10,12,15-19 will exclude images 1,5,6,7,10,12,15,16,17,18,19""")
		
	parser.add_argument("--include",type=str,default='',help="""Comma separated list of numbers
		corresponding to images to include (all others will be excluded). 
		--unstack or --restack must be supplied. 
		Recall that the FIRST image INDEX is 0. 
		--include=1,5-7,10,12,15-19 will include images 1,5,6,7,10,12,15,16,17,18,19""")


	parser.add_argument("--verbose", "-v", dest="verbose", action="store", metavar="n",type=int, default=0, help="verbose level [0-9], higner number means higher level of verboseness.")

	parser.add_argument("--ppid", type=int, help="Set the PID of the parent process, used for cross platform PPID",default=-1)

	(options, args) = parser.parse_args()	
	
	if options.exclude and options.include:
		print "\nERROR: Supplied either exclude or include. Cannot supply both at the same time."
		sys.exit()
	
	print "\nLogging"
	logger = E2init(sys.argv, options.ppid)
	
	mrcs=[]
	mrcsmirror=[]
	hdfs=[]
	hdfsmirror=[]
	
	from e2spt_classaverage import sptmakepath
	options = sptmakepath( options, 'sptstacker')

	options.path = os.getcwd() + '/' + options.path
	
	if options.stem2stack and options.tiltstep == 0.0:
		print "ERROR: --tiltstep required when using --stem2stack"
		sys.exit()
	
	if options.lowesttilt == 0.0 and options.tiltrange:
		options.lowesttilt = -1 * options.tiltrange
		
	if options.highesttilt == 0.0 and options.tiltrange:
		options.highesttilt = options.tiltrange

	if options.unstack:
		if options.tltfile:
			usntacker( options )
		else:
			print "ERROR: --tltfile required when using --unstack"
			sys.exit()

	elif options.restack:
		if options.tltfile:
			restacker( options )
		else:
			print "ERROR: --tltfile required when using --restack"
			sys.exit()

	else:
		kk=0
		intilts = findtiltimgfiles( options )
		
		print "\nWill organize tilt imgs found"
		intiltsdict = organizetilts( intilts, options )		#Get a dictionary in the form { indexintiltseries:[ tilfile,tiltangle,damageRank ]},
		print "\nDone organizing tilt imgs"					#where tiltimagenumbers tell you the order in which the images where acquired
															#regardless of wether the tilt series goes from -tiltrange to +tiltrange, 
															#or 0 to -tiltrange then +tiltstep to +tiltrange, or the opposite of these 
		outstackhdf = options.path + '/stack.hdf' 
		print "\nOutstack is", outstackhdf
		for index in intiltsdict:
			intiltimgfile =	intiltsdict[index][0]
			intiltimg = EMData( intiltimgfile, 0 )
			
			tiltangle = intiltsdict[index][1]
			intiltimg['spt_tiltangle'] = tiltangle
			
			damageRank = intiltsdict[index][2]
			intiltimg['damageRank'] = damageRank
			
			if options.invert:
				intiltimg.mult(-1)
			intiltimg.write_image( outstackhdf, index )
			print "\nWrote image index", index
		
		if options.clip:
			clip = options.clip.split(',')
			clipx = clipy = clip[0]
			if len(clip) > 1:
				clipy = clip[-1]
			
			tmp = options.path + '/tmp.hdf'
			cmdClip = 'e2proc2d.py ' + outstackhdf + ' ' + tmp + ' --clip=' + clipx + ',' + clipy
			cmdClip += ' && rm ' + outstackhdf + ' && mv ' + tmp + ' ' + outstackhdf

			print "\n(e2spt_tiltstacker.py)(main) cmdClip is", cmdClip
			p = subprocess.Popen( cmdClip , shell=True,stdout=subprocess.PIPE, stderr=subprocess.PIPE)
			text = p.communicate()	
			p.stdout.close()
			
			if options.verbose > 9:
				print "\nFeedback from cmdClip:"
				print text
			
			
		outtilthdr = EMData(outstackhdf,0,True)
		currentapix = outtilthdr['apix_x']
		if float(options.apix) and float(options.apix) != float(currentapix):
			print "\nFixing apix"
			cmdapix = 'e2fixheaderparam.py --input=' + outstackhdf + ' --stem=apix --valtype=float --stemval=' + str( options.apix )
			
			print "\n(e2spt_tiltstacker.py)(main) cmdapix is", cmdapix
			p = subprocess.Popen( cmdapix , shell=True,stdout=subprocess.PIPE, stderr=subprocess.PIPE)
			text = p.communicate()	
			p.stdout.close()
			
			if options.verbose > 9:
				print "\nFeedback from cmdapix:"
				print text
			
			
		outstackmrcs = outstackhdf.replace('.hdf','.mrcs')
		mrcscmd = 'e2proc2d.py	' + outstackhdf + ' ' + outstackmrcs
		if options.outmode != 'float':
			mrcscmd += ' --outmode=' + options.outmode
		
		print "\n(e2spt_tiltstacker.py)(main) mrcscmd is", mrcscmd	
		p = subprocess.Popen( mrcscmd , shell=True,stdout=subprocess.PIPE, stderr=subprocess.PIPE)
		text = p.communicate()	
		p.stdout.close()
		
		if options.verbose > 9:
			print "\nFeedback from mrcscmd:"
			print text
		
		if options.mirroraxis:
			print "\nMirroring across axis", options.mirroraxis
			mirrorlabel = options.mirroraxis.upper()
			outstackmrcsmirror = outstackmrcs.replace('.mrcs','_mirror'+ mirrorlabel+ '.mrcs')

			cmdMirror = 'e2proc2d.py ' + outstackmrcs + ' ' + outstackmrcsmirror + ' --process=xform.mirror:axis=' + options.mirroraxis
			p = subprocess.Popen( cmdMirror , shell=True,stdout=subprocess.PIPE, stderr=subprocess.PIPE)
			text = p.communicate()	
			
			if options.verbose > 9:
				print "\nFeedback from cmdMirror:"
				print text
				p.stdout.close()
	
	E2end( logger )
	return


"""
Function to determine which files in the current directory should be used
"""
def findtiltimgfiles( options ):
	
	currentdir = os.getcwd()
	filesindir = os.listdir( currentdir )
	
	intilts = []
	#k=0
	for f in filesindir:
		intilt = ''	
		if '.dm3' in f or '.DM3' in f or '.tif' in f or '.TIF' in f or '.MRC' in f: 
			if '.txt' not in f and '.db' not in f and 'mirror' not in f:
				if options.stem2stack:
					if options.stem2stack in f:
						print "\nFound file", f
						intilt = f
				else:
					print "\nFound file", f
					intilt = f
					
		if intilt:
			intilts.append( intilt )			#k will serve to compensate for damage.
												#It indicates the order in which data collection occured
	print "\n(e2spt_tiltstacker.py)(findtiltimgfiles) These many img files were found", len( intilts )		
	intilts.sort()
	print "\nand they've been sorted"
	return intilts


def getangles( options ):
	
	angles = []
	if options.tltfile:
		f = open( options.tltfile, 'r' )
		lines = f.readlines()
		f.close()
		
		for line in lines:
			line = line.replace('\t','').replace('\n','')
		
		if line:
			angles.append( float(line) )
	else:
		#angles = [a for a in xrange( options.lowesttilt, options.highesttilt, options.tiltstep )]
		
		
		print "There was no .tlt file so I'll generate the angles using lowesttilt=%f, highesttilt=%f, tiltstep=%f" % (options.lowesttilt, options.highesttilt, options.tiltstep)
		generate = floatrange( options.lowesttilt, options.highesttilt, options.tiltstep )
		angles=[ x for x in generate ]
		
	angles.sort()
	print "\n(e2spt_tiltstacker.py)(getangles) angles are", angles

	return angles


def writetlt( angles, options ):
	
	angless = list( angles )
	if options.negativetiltseries:
		angless.sort()
		angless.reverse()
		
	f = open( options.path + '/stack.rawtlt','w')
	lines = []
	for a in angless:
		line = str(a) + '\n'
		lines.append( line )
	
	f.writelines( lines )
	f.close()
		
	return


def organizetilts( intilts, options ):
	
	intiltsdict = {}
	angles = getangles( options )
	orderedangles = list( angles )
	
	#print "\n(organizetilts) angles are", angles
	if not options.tltfile:
		
		writetlt( angles, options )
		if options.bidirectional:
			zeroangle = min( [ math.fabs(a) for a in angles] )		#Find the angle closest to 0 tilt, in the middle of the tiltseries
			indexminangle = None
			try:
				indexminangle = angles.index( zeroangle )			#The middle angle (0 tilt) could have been positive
				zeroangle = angles[ indexminangle ]
			except:
				indexminangle = angles.index( -1*zeroangle )		#Or negative. Either way, we find its index in the list of angles
				zeroangle = angles[ indexminangle ]
			
			print "\nzeroangle=%f, indexminangle=%d" %( zeroangle, indexminangle )
			if not options.negativetiltseries:
				print "\nNegative tilt series is OFF. This is a POSITIVE tilt series"
				firsthalf = angles[ indexminangle: len(angles) ]	#This goes from zero to highest tilt angle, that is, +tiltrange
				#print "Firsthalf is", firsthalf
				#print "because angles are", angles
				secondhalf =  angles[ 0:indexminangle ]				#This should go from most negative angle to zero (without including it)
				secondhalf.sort()									#We order this list to go from 0-tiltstep to the most negative angle, -tiltrange
				secondhalf.reverse()
			
			elif options.negativetiltseries:
				print "T\nhis is a NEGATIVE tiltseries"
				firsthalf = angles[ 0:indexminangle+1 ]				#This goes from the most negative angle to zero (INCLUDING it)
				firsthalf.sort()									#We order this list to go from 0 to -tiltrange
				firsthalf.reverse()
				
				secondhalf = angles[ indexminangle+1: len(angles) ]	#This goes from 0+tiltstep to the most positive angle, that is, +tiltrange
			
			orderedangles = firsthalf + secondhalf
			print "\n(organizetilts) Ordered angles are", orderedangles
			print "\n(organizetilts) Because firsthalf is", firsthalf
			print "\n(organizetilts) and secondhalf is", secondhalf
	
	#else:
	
	if options.negativetiltseries:			#Change angles to go from +tiltrange to -tiltrange if that's the order of the images
		#orderedangles.sort()
		#orderedangles.reverse()
		
		angles.sort()
		angles.reverse()
		
		#print "However, after reversal, they are", orderedangles
		#print "and angles are", angles
			
			
	if len( intilts ) != len( orderedangles ):
		print """\n(e2spt_tiltstacker.py)(organizetilts) ERROR: Number of tilt angles 
		and tilt images is not equal."""
		print """Number of tilt angles = %d ; number of tilt images = %d """ % ( len( orderedangles ), len( intilts ) )
		sys.exit()
					
	for k in range(len(intilts)):
		tiltangle = orderedangles[k]
		indexintiltseries = angles.index( orderedangles[k] )
		intiltsdict.update( { indexintiltseries:[ intilts[k],tiltangle,k ]} )
		print "\ncollectionIndex=%d, tiltangle=%f, indexintiltseries=%d" % ( k, tiltangle, indexintiltseries )
 				
	return intiltsdict


def usntacker( options ):
	
	outname = options.path + '/' + options.unstack.replace('.mrc','.hdf')
	outname = options.path + '/' + options.unstack.replace('.mrcs','.hdf')
	outname = options.path + '/' + options.unstack.replace('.st','.hdf')	
	outname = options.path + '/' + options.unstack.replace('.ali','.hdf')
	
	outname = options.path + '/' + options.unstack.replace('.hdf','_UNSTACKED.hdf')
	
	print "\nOutname of unstacked tilt will be", outname
	
	cmdun = 'e2proc2d.py ' + options.unstack + ' ' + outname + ' --unstacking '
	if options.outmode:
		cmdun += ' --outmode=' + options.outmode
	
	if options.exclude or options.include:
		lst = makeimglist( options.unstack, options ) 
		cmdun += ' --list=' + lst
	
	print "\ncmdun is", cmdun	
	p = subprocess.Popen( cmdun , shell=True,stdout=subprocess.PIPE, stderr=subprocess.PIPE)
	text = p.communicate()	
	p.stdout.close()

	return outname
		

def restacker( options ):

	outname = options.path + '/' + options.restack.replace('.mrc','.hdf')
	outname = options.path + '/' + options.restack.replace('.mrcs','.hdf')
	outname = options.path + '/' + options.restack.replace('.st','.hdf')	
	outname = options.path + '/' + options.restack.replace('.ali','.hdf')
	
	outname = options.path + '/' + options.restack.replace('.hdf','_RESTACKED.hdf')
	
	tmp = options.path + '/tmp.hdf'
	cmdre = 'e2proc2d.py ' + options.restack + ' ' + tmp
	
	if options.outmode:
		cmdre += ' --outmode=' + options.outmode
	
	if options.exclude or options.include:
		lst = makeimglist( options.restack, options )
		cmdre += ' --list=' + lst
		
	cmdre += ' && mv ' + tmp + ' ' + outname
	
	print "\ncmdre is", cmdre
	p = subprocess.Popen( cmdre , shell=True,stdout=subprocess.PIPE, stderr=subprocess.PIPE)
	text = p.communicate()	
	p.stdout.close()
	
	return


def getindxs( string ):
	
	parsedindxs = set([])
	stringList = list( string.split(',') )
	#print "\nstringList is", stringList
	
	for i in range( len(stringList) ):
		print "\n\nstringList[i] is", stringList[i]
		if '-' in stringList[i]:
			stringList[i] = stringList[i].split('-')
			
			x1 = int( stringList[i][0] )
			x2 = int( stringList[i][-1] ) + 1
			
			stringList[i] = [ str( ele ) for ele in xrange( x1, x2 ) ]
		
		parsedindxs = parsedindxs.union( set( list( stringList[i] ) ) )
	
	parsedindxs = list( parsedindxs )
	parsedindxs.sort()
	
	print "\nParsed indexes are", parsedindxs
	return parsedindxs
	

def makeimglist( input, options ):
	
	n = EMUtil.get_image_count( input )
	
	allindxs = set( [ str(i) for i in range(n) ] )
	finalindxs = set( list( allindxs ) )
	
	if options.exclude:
		print "\nPrint there's EXCLUDE!!"
		eindxs = getindxs( options.exclude )
		finalindxs = list( allindxs.difference( eindxs ) )
		
	elif options.include:
		print "\nPrint there's INCLUDE!!"
		iindxs = getindxs( options.include )
		finalindxs = list( iindxs )
	
	ints = []
	for id in finalindxs:
		ints.append( int( id ) )
	
	ints.sort()
	
	#final = []
	#for fi in ints:
	#	final.append( str( fi ) )
	
	
	print "\nFinalindxs are", finalindxs
	
	lines = []
	for indx in ints:
		lines.append( str(indx) + '\n' )
		
	listfile = options.path + '/list.lst'
	f= open( listfile,'w' )
	f.writelines( lines )
	f.close()
	
	print "\nlistfile is", listfile
	return listfile


def floatrange(start, stop, step):
	print "\nInside floatrange, start, stop and step are", start, stop, step
	
	r = start
	kkk=0
	while r <= stop:
		yield r
		print "r is", r
		r += step
		kkk+=1
		
		if kkk > 180:
			print "ERROR: Something is wrong with your tiltrange, lowesttilt or highesttilt"
			sys.exit()
	
	return


if __name__ == "__main__":
	main()