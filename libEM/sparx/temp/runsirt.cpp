#include "mpi.h"

#include "emdata.h"

#include "project3d.h"
#include "sirt.h"
#include "utilcomm.h"

#define PI 3.14159265358979
using namespace EMAN;

// Reconstruct the 3-D density of a macromolecule from
// a stack of 2-D images using the SIRT algorithm

int main(int argc, char ** argv)
{
   MPI_Comm comm = MPI_COMM_WORLD;
   int ncpus, mypid, ierr, mpierr=0;
   int nloc, maxit = 0; 
   double t0;
   FILE *fp;

   MPI_Status mpistatus;
   MPI_Init(&argc,&argv);
   MPI_Comm_size(comm,&ncpus);
   MPI_Comm_rank(comm,&mypid);
   printf("mypid = %d, ncpus = %d\n", mypid, ncpus);

   char  stackfname[100],voutfname[100], angfname[100];
   EMData **expimages;

   // parse the command line and set filenames	
   if (argc < 3) {
     if (mypid == 0) {
         printf("Not enough arguments to the command...\n");
         printf("Usage: runsirt -data=<imagestack> ");
         printf("-angles=<initial 3D volume> "); 
         printf("-out=<output filename base string> ");
         printf("-maxit=<max number of iterations");
     }
     ierr = MPI_Finalize();
     exit(1);
   }
   int ia=0;
   while (ia < argc) {
      if ( !strncmp(argv[ia],"-data",5) ) {
         strcpy(stackfname,&argv[ia][6]);
      }
      else if ( !strncmp(argv[ia],"-angles",7) ) {
         strcpy(angfname,&argv[ia][8]);
      }
      else if ( !strncmp(argv[ia],"-out",4) ) {
         strcpy(voutfname,&argv[ia][5]);
      }
      else if ( !strncmp(argv[ia],"-maxit",5) ) {
         maxit = atoi(&argv[ia][6]);
      }
      ia++;
   }

   // read and distribute a stack of experimental images
   t0 = MPI_Wtime();
   ierr = ReadStackandDist(comm, &expimages, stackfname, &nloc);
   if (ierr == 0) {
	if (mypid == 0) {
	   printf("I/O time for reading image stack = %11.3e\n",
		  MPI_Wtime() - t0);
	}
   }
   else {
      if (mypid == 0) 
         printf("Failed to read the image stack %s! exit...\n",stackfname);
      ierr = MPI_Finalize();
      exit(1);
   }

   // make a copy of the images for removing the background; 
   // this stack will be used for reconstruction
   EMData** cleanimages = new EMData*[nloc];
   for ( int i = 0 ; i < nloc ; ++i) {
	cleanimages[i] = expimages[i]->copy();
   }

   int nx = cleanimages[0]->get_xsize();
   int ny = cleanimages[0]->get_ysize();
   int nz = nx;

   Vec3i volsize;
   Vec3i origin;
   volsize[0] = nx;
   volsize[1] = ny;
   volsize[2] = nz;
   origin[0] = nx/2 + 1;
   origin[1] = ny/2 + 1;
   origin[2] = nz/2 + 1;
   int ri = nx/2 - 2;

   ierr = CleanStack(comm, cleanimages, nloc, ri, volsize, origin);
   if (mypid == 0) printf("image size: nx = %d, ny = %d, ri = %d\n",
                          nx, ny, ri); 


   // read angle and shift data and distribute
   float * angleshift = new float[5*nloc];
   ierr = ReadAngTrandDist(comm, angleshift, angfname, nloc);
   if (ierr!=0) { 
      mpierr = MPI_Finalize();
      return 1;
   }

   // Use xvol to hold reconstructed volume
   EMData * xvol = new EMData();

   // set SIRT parameters
   if (maxit == 0) maxit = 10;
   float lam = 5.0e-6;
   float tol = 1.0e-3;
   std::string symmetry = "c1";

   // call SIRT to reconstruct
   t0 = MPI_Wtime();
   recons3d_sirt_mpi(comm, cleanimages, angleshift, xvol, nloc, ri, 
                     lam, maxit, symmetry, tol);

   if ( mypid == 0 ) {
      printf("Done with SIRT\n");
      printf("SIRT timing = %11.3e\n", MPI_Wtime() - t0);
   }

   // write the reconstructed volume to disk
   EMUtil::ImageType WRITE_SPI = EMUtil::IMAGE_SINGLE_SPIDER;
   if ( mypid == 0 ) {
	xvol->write_image(voutfname, 0, WRITE_SPI);
   }

   // cleanup
   for ( int i = 0 ; i < nloc; ++i ) {
       EMDeletePtr(expimages[i]);
   }
   EMDeleteArray(expimages);
   for ( int i = 0 ; i < nloc; ++i ) {
       EMDeletePtr(cleanimages[i]);
   }
   EMDeleteArray(cleanimages);

   EMDeletePtr(xvol);
   EMDeleteArray(angleshift);

   ierr = MPI_Finalize();

   return 0; // main
}
