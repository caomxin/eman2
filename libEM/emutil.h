#ifndef eman__emutil__h__
#define eman__emutil__h__ 1

#include "emobject.h"
#include <string>
#include <vector>
#include <map>

using std::string;
using std::vector;
using std::map;

namespace EMAN {
    class ImageIO;
    class Region;

    class EMUtil {
    public:
	enum EMDataType {
	    EM_CHAR,
	    EM_UCHAR,
	    EM_SHORT,
	    EM_USHORT,
	    EM_INT,
	    EM_UINT,
	    EM_FLOAT,
	    EM_DOUBLE,
	    EM_SHORT_COMPLEX,
	    EM_USHORT_COMPLEX,
	    EM_FLOAT_COMPLEX,
	    EM_UNKNOWN
	};
	
	enum ImageType {
	    IMAGE_MRC,
	    IMAGE_SPIDER,
	    IMAGE_SINGLE_SPIDER,
	    IMAGE_IMAGIC,
	    IMAGE_HDF,
	    IMAGE_DM3,
	    IMAGE_TIFF,
	    IMAGE_PGM,
	    IMAGE_LST,
	    IMAGE_PIF,
	    IMAGE_VTK,
	    IMAGE_PNG,
	    IMAGE_SAL,    
	    IMAGE_ICOS,
	    IMAGE_EMIM,
	    IMAGE_GATAN2,
	    IMAGE_AMIRA,
	    IMAGE_XPLOR,
	    IMAGE_EM,
	    IMAGE_UNKNOWN
	};
	
	static EMData* vertical_acf(const EMData& image, int maxdy);
	
	static ImageType get_image_type(string filename);
	static int get_image_count(string filename);
	static ImageIO* get_imageio(string filename, int rw_mode, ImageType image_type = IMAGE_UNKNOWN);

	static const char* get_imagetype_name(EMUtil::ImageType type);
	static const char* get_datatype_string(EMDataType type);
	
	static void get_region_dims(Region* area, int nx, int* area_x, int ny, 
				    int* area_y, int nz = 1, int* area_z = 0);

	static void get_region_origins(Region* area, int* p_x0, int* p_y0, int* p_z0 = 0, 
				       int nz = 1, int image_index = 0);
	
	static int get_region_data(unsigned char* cdata, FILE* in, int image_index, 
				   int mode_size, int nx, int ny, int nz = 1,
				   Region* area = 0, bool need_flip = false,
				   int pre_row = 0, int post_row = 0);

	static void dump_dict(const Dict& dict);

	static bool is_same_size(EMData* em1, EMData* em2);
	
	
	
    private:
	static ImageType fast_get_image_type(string filename, const void* first_block, long file_size);
    };
}

#endif

    
