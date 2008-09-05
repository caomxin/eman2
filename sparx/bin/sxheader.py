#!/usr/bin/env python


def main():
	import os
	import sys
	from optparse import OptionParser
	from global_def import SPARXVERSION
	arglist = []
	for arg in sys.argv:
		arglist.append( arg )

	progname = os.path.basename( arglist[0] )
	usage = progname + " stack --params='parm1 parm2 parm3 ...' --zero --one --randomize --import=file --export=file --print "
	parser = OptionParser(usage, version=SPARXVERSION)

        parser.add_option( "--params", type="string",       default="", help="parameter list" )
	parser.add_option( "--zero",   action="store_true", default=False, help="set parameter to zero" )
	parser.add_option( "--one",   action="store_true", default=False, help="set parameter to one" )
	parser.add_option( "--randomize",   action="store_true", default=False, help="set parameter to randomized value" )
	parser.add_option( "--import", type="string", dest="fimport", default="", help="import parameters from file" )
	parser.add_option( "--export", type="string", dest="fexport", default="", help="export parameters to file" )
	parser.add_option( "--print",  action="store_true", dest="fprint", default=False, help="print parameters" )

	(options,args) = parser.parse_args( arglist[1:] )

       

	if len(args) != 1 :
	    print usage
	    sys.exit(-1)

	if len(options.params)==0:
	    print "Error: no parameters given"
	    exit(-1)

        from applications import header
	header( args[0], options.params, options.zero, options.one, options.randomize, options.fimport, options.fexport, options.fprint )

if __name__ == "__main__":
	main()
