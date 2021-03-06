/* camtiff.c - A TIFF image writing library for spectroscopic data.
 * This file is part of camtiff.
 *
 * Created by Ryan Orendorff <ro265@cam.ac.uk>
 * Date: 28/10/11 21:00:27
 *
 * The Laser Analytics Tiff Writer University of Cambridge (camtiff) is a
 * library designed to, given an input 16 bit 3D array and some additional
 * comments, produce a TIFF image stack. It is designed to work with a piece
 * of LabVIEW software within the Laser Analytics group codenamed Apollo, a
 * front end for acquiring spectroscopic images.
 *
 * Copyright (GPL V3):
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>   // strftime
#include <tiffio.h> // libTIFF (preferably 3.9.5+)

#include "camtiff.h"

// Autoclosing error function
#define ERR(e, img) {printf("An error occured. Code %d\n", e); \
                     TIFFClose(img); return e;}

#ifdef WIN32
#define inline __inline // Microsoft, I hate you (uses C89).
#endif


static inline void* moveArrayPtr(const void* const ptr,
                                   unsigned int distance, uint8_t element_size)
{
  return (void *) (((char *) ptr)+(distance*element_size/8));
}

int writeSubFile(TIFF *image, struct page page, struct metadata meta)
{
  int retval;
  uint32_t i;
  void* strip_buffer;

  // function called for each file, iterates to create pages.
  static int file_num = 0;

  // Time vars
  time_t local_current_time;
  char time_str[20];
#ifdef __WIN32
  struct tm gmt_current_time;

  // Get time of slice
  time(&local_current_time);

  retval = gmtime_s(&gmt_current_time, &local_current_time);
  if (retval) {printf("Could not get UTC.\n"); return retval;}

  strftime(time_str, 20, "%Y:%m:%d %H:%M:%S", &gmt_current_time);
#else
  struct tm* gmt_current_time;

  // Get time of slice
  time(&local_current_time);
  gmt_current_time = gmtime(&local_current_time);
  strftime(time_str, 20, "%Y:%m:%d %H:%M:%S", gmt_current_time);
#endif

  // Set directory (subfile) to next number since last function call.
  TIFFSetDirectory(image, file_num);

  // Set up next subimage in case the current one craps out.
  file_num++;

  // We need to set some values for basic tags before we can add any data
  TIFFSetField(image, TIFFTAG_IMAGEWIDTH, page.width);
  TIFFSetField(image, TIFFTAG_IMAGELENGTH, page.height);
  TIFFSetField(image, TIFFTAG_BITSPERSAMPLE, page.pixel_bit_depth);
  TIFFSetField(image, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
  TIFFSetField(image, TIFFTAG_ROWSPERSTRIP, 1);

  // # components per pixel. RGB is 3, gray is 1.
  TIFFSetField(image, TIFFTAG_SAMPLESPERPIXEL, 1);

  // LZW lossless compression
  TIFFSetField(image, TIFFTAG_COMPRESSION, COMPRESSION_LZW);

  // Black is 0x0000, white is 0xFFFF
  TIFFSetField(image, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);

  // Big-endian
  TIFFSetField(image, TIFFTAG_FILLORDER, FILLORDER_MSB2LSB);
  TIFFSetField(image, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);

  // Datetime in format "Y:m:D H:M:S"
  TIFFSetField(image, TIFFTAG_DATETIME, time_str);

  // Image metadata
  TIFFSetField(image, TIFFTAG_ARTIST, meta.artist);
  TIFFSetField(image, TIFFTAG_COPYRIGHT, meta.copyright);
  /*TIFFSetField(image, TIFFTAG_HOSTCOMPUTER, "Windows XP");*/
  TIFFSetField(image, TIFFTAG_MAKE, meta.make);
  TIFFSetField(image, TIFFTAG_MODEL, meta.model);
  TIFFSetField(image, TIFFTAG_SOFTWARE, meta.software);
  TIFFSetField(image, TIFFTAG_IMAGEDESCRIPTION, meta.image_desc);

  // Pixel density and units. Set to std screen resolution size.
  TIFFSetField(image, TIFFTAG_XRESOLUTION, 72);
  TIFFSetField(image, TIFFTAG_YRESOLUTION, 72);
  TIFFSetField(image, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);

  // Write the information to the file
  // -1 on error, (width*height)*(RESISBITS/8) on success
  for (i=0; i < page.height; i++) {
    strip_buffer = moveArrayPtr(page.buffer,
                                     i*page.width, page.pixel_bit_depth);
    if ((retval = TIFFWriteEncodedStrip(image, i,
                                        strip_buffer,
                                        page.width*page.pixel_bit_depth/8))
        == -1){
      printf("Could not write strip to tiff.\n");
      return retval;
    }
  }

  // 1 on error, 0 on success
  if (!(retval = TIFFWriteDirectory(image))){
    printf("Could not write directory to tiff.\n");
    return EWRITEDIR;
  }

  return 0;
}


void setMetadata(struct metadata* data,
                 const char* artist, const char* copyright, const char* make,
                 const char* model, const char* software,
                 const char* image_desc)
{
  data->artist = artist;
  data->copyright = copyright;
  data->make = make;
  data->model = model;
  data->software = software;
  data->image_desc = image_desc;
}

int tiffWrite(uint32_t width, uint32_t height,
              uint32_t pages, uint8_t pixel_bit_depth,
              const char* artist, const char* copyright, const char* make,
              const char* model, const char* software, const char* image_desc,
              const char* output_path, const void* const buffer)
{
  int retval;
  uint32_t k;
  struct page page;
  struct metadata meta;

  if (!(pixel_bit_depth == 8 || pixel_bit_depth == 16 || pixel_bit_depth == 32))
    return EBITDEPTH;

  TIFF *image;
  // Open the TIFF file
  if((image = TIFFOpen(output_path, "w")) == NULL){
    printf("Could not open %s for writing\n", output_path);
    return ETIFFOPEN;
  }

  page.width = width;
  page.height = height;
  page.pixel_bit_depth=pixel_bit_depth;
  setMetadata(&meta, artist, copyright, make, model, software, image_desc);


  // Write pages from combined buffer
  for (k = 0; k < pages; k++){
    page.buffer = moveArrayPtr(buffer,
                                    k*width*height, page.pixel_bit_depth);
    if ((retval = writeSubFile(image, page, meta)))
      ERR(retval, image)
  }

  // Close the file
  TIFFClose(image);

  return 0;
}
