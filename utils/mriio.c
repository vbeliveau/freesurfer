/*
 *       FILE NAME:   mriio.c
 *
 *       DESCRIPTION: utilities for reading/writing MRI data structure
 *
 *       AUTHOR:      Bruce Fischl
 *       DATE:        4/12/97
 *
*/

/*-----------------------------------------------------
                    INCLUDE FILES
-------------------------------------------------------*/
#define USE_ELECTRIC_FENCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <memory.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>

#include "utils.h"
#include "error.h"
#include "proto.h"
#include "mri.h"
#include "macros.h"
#include "diag.h"
#include "volume_io.h"
#include "region.h"
#include "machine.h"
#include "analyze.h"
#include "fio.h"
#include "mri_identify.h"
#include "fio.h"
#include "matfile.h"
#include "math.h"
#include "matrix.h"
#include "diag.h"
#include "chklc.h"
#include "mriColorLookupTable.h"

#define MM_PER_METER  1000.0f
#define INFO_FNAME    "COR-.info"

#ifdef Linux
extern void swab(const void *from, void *to, size_t n);
#endif

static MRI *mri_read(char *fname, int type, int volume_flag, int start_frame, int end_frame);
static MRI *corRead(char *fname, int read_volume);
static int corWrite(MRI *mri, char *fname);
static MRI *siemensRead(char *fname, int read_volume);
static MRI *mincRead(char *fname, int read_volume);
static int mincWrite(MRI *mri, char *fname);
static int bshortWrite(MRI *mri, char *fname_passed);
static int bfloatWrite(MRI *mri, char *stem);
static int write_bhdr(MRI *mri, FILE *fp);
#ifdef READ_BHDR
static int read_bhdr(MRI *mri, FILE *fp);
#endif
static MRI *bshortRead(char *fname_passed, int read_volume);
static MRI *bfloatRead(char *fname_passed, int read_volume);
static MRI *genesisRead(char *stem, int read_volume);
static MRI *gelxRead(char *stem, int read_volume);
static MRI *analyzeRead(char *fname, int read_volume);
static int analyzeWrite(MRI *mri, char *fname);
static MRI *afniRead(char *fname, int read_volume);
static int afniWrite(MRI *mri, char *fname);
static void swap_analyze_header(dsr *hdr);
static void nflip(unsigned char *buf, int b, int n);
static MRI *read_afni_header(FILE *fp, int *big_endian_flag);
static char *get_afni_string(FILE *fp, int count, char *name);
static int *get_afni_int(FILE *fp, int count, char *name);
static float *get_afni_float(FILE *fp, int count, char *name);
static int decompose_b_fname(char *fname_passed, char *directory, char *stem);
static int orient_with_register(MRI *mri);
static int nan_inf_check(MRI *mri);
#ifdef VC_TO_CV
static int voxel_center_to_center_voxel(MRI *mri, float *x, float *y, float *z);
#endif
static int center_voxel_to_voxel_center(MRI *mri, float x, float y, float z);
static int bad_ras_fill(MRI *mri);
static MRI *gdfRead(char *fname, int read_volume);
static int gdfWrite(MRI *mri, char *fname);

/********************************************/

static void short_buffer_to_image(short *buf, MRI *mri, int slice, int frame) ;
static void int_buffer_to_image(int *buf, MRI *mri, int slice, int frame) ;
static void long_buffer_to_image(long *buf, MRI *mri, int slice, int frame) ;
static void float_buffer_to_image(float *buf, MRI *mri, int slice, int frame) ;
static void buffer_to_image(BUFTYPE *buf, MRI *mri, int slice, int frame) ;
static MRI *sdtRead(char *fname, int read_volume);
static MRI *mghRead(char *fname, int read_volume, int frame) ;
static int mghWrite(MRI *mri, char *fname, int frame) ;
static int mghAppend(MRI *mri, char *fname, int frame) ;

/********************************************/

extern char *Progname;

static char *command_line;
static char *subject_name;

static float afni_orientations[][3] = { { -1.0,  0.0,  0.0 }, 
                                        {  1.0,  0.0,  0.0 }, 
                                        {  0.0,  1.0,  0.0 }, 
                                        {  0.0, -1.0,  0.0 }, 
                                        {  0.0,  0.0,  1.0 }, 
                                        {  0.0,  0.0, -1.0 } };

#define MAX_UNKNOWN_LABELS  100

static short cma_field[512][512];
static char unknown_labels[MAX_UNKNOWN_LABELS][STRLEN];
static int n_unknown_labels;

int mriio_command_line(int argc, char *argv[])
{

  int i;
  int length;
  char *c;

  length = 0;
  for(i = 0;i < argc;i++)
    length += strlen(argv[i]);

  /* --- space for spaces and \0 --- */
  length += argc;

  command_line = (char *)malloc(length);

  c = command_line;
  for(i = 0;i < argc;i++)
  {
    strcpy(c, argv[i]);
    c += strlen(argv[i]);
    *c = (i == argc-1 ? '\0' : ' ');
    c++;
  }

  return(NO_ERROR);

} /* end mriio_command_line() */

int mriio_set_subject_name(char *name)
{

  if(subject_name == NULL)
    subject_name = (char *)malloc(STRLEN);

  if(subject_name == NULL)
    ErrorReturn(ERROR_NO_MEMORY, (ERROR_NO_MEMORY, "mriio_set_subject_name(): could't allocate %d bytes...!", STRLEN));

  if(name == NULL)
    strcpy(subject_name, name);
  else
  {
    free(subject_name);
    subject_name = NULL;
  }

  return(NO_ERROR);

} /* end mriio_set_subject_name() */

int MRIgetVolumeName(char *string, char *name_only)
{

  char *at, *pound;

  strcpy(name_only, string);

  if((at = strrchr(name_only, '@')) != NULL)
    *at = '\0';

  if((pound = strrchr(name_only, '#')) != NULL)
    *pound = '\0';

  return(NO_ERROR);

} /* end MRIgetVolumeName() */

static MRI *mri_read(char *fname, int type, int volume_flag, int start_frame, int end_frame)
{

  MRI *mri, *mri2;
  char fname_copy[STRLEN];
  char *at, *pound, *colon;
  char *ep;
  int i, j, k, t;
  int volume_frames;

  strcpy(fname_copy, fname);

  at = strrchr(fname_copy, '@');
  pound = strrchr(fname_copy, '#');

  if(at != NULL)
  {
    *at = '\0';
    at++;
  }

  if(pound != NULL)
  {
    *pound = '\0';
    pound++;
  }

  if(at != NULL)
  {
    type = string_to_type(at);
    if(type == MRI_VOLUME_TYPE_UNKNOWN)
    {
      ErrorReturn(NULL, (ERROR_BADPARM, "mri_read(): unknown type '%s'\n", at));
    }
  }
  else if(type == MRI_VOLUME_TYPE_UNKNOWN)
  {
    type = mri_identify(fname_copy);
    if(type == MRI_VOLUME_TYPE_UNKNOWN)
    {
      ErrorReturn(NULL, (ERROR_BADPARM, "mri_read(): couldn't determine type of file %s", fname_copy));
    }
  }

  if(pound != NULL)
  {
    colon = strchr(pound, ':');
    if(colon != NULL)
    {
      *colon = '\0';
      colon++;
      if(*colon == '\0')
      {
        ErrorReturn(NULL, (ERROR_BADPARM, "mri_read(): bad frame specification ('%s:')\n", pound));
      }

      start_frame = strtol(pound, &ep, 10);
      if(*ep != '\0')
      {
        ErrorReturn(NULL, (ERROR_BADPARM, "mri_read(): bad start frame ('%s')\n", pound));
      }

      end_frame = strtol(colon, &ep, 10);
      if(*ep != '\0')
      {
        ErrorReturn(NULL, (ERROR_BADPARM, "mri_read(): bad end frame ('%s')\n", colon));
      }

    }
    else
    {
      start_frame = end_frame = strtol(pound, &ep, 10);
      if(*ep != '\0')
      {
        ErrorReturn(NULL, (ERROR_BADPARM, "mri_read(): bad frame ('%s')\n", pound));
      }
    }

    if(start_frame < 0)
    {
      ErrorReturn(NULL, (ERROR_BADPARM, "mri_read(): frame (%d) is less than zero\n", start_frame));
    }

    if(end_frame < 0)
    {
      ErrorReturn(NULL, (ERROR_BADPARM, "mri_read(): frame (%d) is less than zero\n", end_frame));
    }

    if(start_frame > end_frame)
    {
      ErrorReturn(NULL, (ERROR_BADPARM, "mri_read(): the start frame (%d) is greater than the end frame (%d)\n", start_frame, end_frame));
    }

  }

  if(type == MRI_CORONAL_SLICE_DIRECTORY)
  {
    mri = corRead(fname_copy, volume_flag);
  }
  else if(type == SIEMENS_FILE)
  {
    mri = siemensRead(fname_copy, volume_flag);
  }
  else if(type == BSHORT_FILE)
  {
    mri = bshortRead(fname_copy, volume_flag);
  }
  else if(type == BFLOAT_FILE)
  {
    mri = bfloatRead(fname_copy, volume_flag);
  }
  else if(type == GENESIS_FILE)
  {
    mri = genesisRead(fname_copy, volume_flag);
  }
  else if(type == GE_LX_FILE)
  {
    mri = gelxRead(fname_copy, volume_flag);
  }
  else if(type == MRI_ANALYZE_FILE)
  {
    mri = analyzeRead(fname_copy, volume_flag);
  }
  else if(type == BRIK_FILE)
  {
    mri = afniRead(fname_copy, volume_flag);
  }
  else if(type == MRI_MINC_FILE)
  {
    mri = mincRead(fname_copy, volume_flag);
  }
  else if(type == SDT_FILE)
  {
    mri = sdtRead(fname_copy, volume_flag);
  }
  else if(type == MRI_MGH_FILE)
  {
    mri = mghRead(fname_copy, volume_flag, -1);
  }
  else if(type == GDF_FILE)
  {
    mri = gdfRead(fname_copy, volume_flag);
  }
  else
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "mri_read(): code inconsistency (file type recognized but not caught)"));
  }

  if(start_frame == -1)
    return(mri);

  /* --- select frames --- */

  if(start_frame >= mri->nframes)
  {
    volume_frames = mri->nframes;
    MRIfree(&mri);
    ErrorReturn(NULL, (ERROR_BADPARM, "mri_read(): start frame (%d) is out of range (%d frames in volume)", start_frame, volume_frames));
  }

  if(end_frame >= mri->nframes)
  {
    volume_frames = mri->nframes;
    MRIfree(&mri);
    ErrorReturn(NULL, (ERROR_BADPARM, "mri_read(): end frame (%d) is out of range (%d frames in volume)", end_frame, volume_frames));
  }

  if(!volume_flag)
  {

    if(nan_inf_check(mri) != NO_ERROR)
    {
      MRIfree(&mri);
      return(NULL);
    }
    mri2 = MRIcopy(mri, NULL);
    MRIfree(&mri);
    mri2->nframes = (end_frame - start_frame + 1);
    return(mri2);

  }

  if(start_frame == 0 && end_frame == mri->nframes-1)
  {
    if(nan_inf_check(mri) != NO_ERROR)
    {
      MRIfree(&mri);
      return(NULL);
    }
    return(mri);
  }

  /* reading the whole volume and then copying only some frames is a bit inelegant, but it's easier for now
     to do this than to insert the appropriate code into the read function for each format... */

  mri2 = MRIallocSequence(mri->width, mri->height, mri->depth, mri->type, (end_frame - start_frame + 1));
  MRIcopyHeader(mri, mri2);
  mri2->nframes = (end_frame - start_frame + 1);
  mri2->imnr0 = 1;
  mri2->imnr0 = mri2->nframes;

  if(mri2->type == MRI_UCHAR)
    for(t = 0;t < mri2->nframes;t++)
      for(i = 0;i < mri2->width;i++)
        for(j = 0;j < mri2->height;j++)
          for(k = 0;k < mri2->depth;k++)
            MRIseq_vox(mri2, i, j, k, t) = MRIseq_vox(mri, i, j, k, t + start_frame);

  if(mri2->type == MRI_SHORT)
    for(t = 0;t < mri2->nframes;t++)
      for(i = 0;i < mri2->width;i++)
        for(j = 0;j < mri2->height;j++)
          for(k = 0;k < mri2->depth;k++)
            MRISseq_vox(mri2, i, j, k, t) = MRISseq_vox(mri, i, j, k, t + start_frame);

  if(mri2->type == MRI_INT)
    for(t = 0;t < mri2->nframes;t++)
      for(i = 0;i < mri2->width;i++)
        for(j = 0;j < mri2->height;j++)
          for(k = 0;k < mri2->depth;k++)
            MRIIseq_vox(mri2, i, j, k, t) = MRIIseq_vox(mri, i, j, k, t + start_frame);

  if(mri2->type == MRI_LONG)
    for(t = 0;t < mri2->nframes;t++)
      for(i = 0;i < mri2->width;i++)
        for(j = 0;j < mri2->height;j++)
          for(k = 0;k < mri2->depth;k++)
            MRILseq_vox(mri2, i, j, k, t) = MRILseq_vox(mri, i, j, k, t + start_frame);

  if(mri2->type == MRI_FLOAT)
    for(t = 0;t < mri2->nframes;t++)
      for(i = 0;i < mri2->width;i++)
        for(j = 0;j < mri2->height;j++)
          for(k = 0;k < mri2->depth;k++)
            MRIFseq_vox(mri2, i, j, k, t) = MRIFseq_vox(mri, i, j, k, t + start_frame);

  if(nan_inf_check(mri) != NO_ERROR)
  {
    MRIfree(&mri);
    return(NULL);
  }

  MRIfree(&mri);

  return(mri2);

} /* end mri_read() */

static int nan_inf_check(MRI *mri)
{

  int i, j, k, t;

  if(mri->type != MRI_FLOAT)
    return(NO_ERROR);

    for(i = 0;i < mri->width;i++)
      for(j = 0;j < mri->height;j++)
        for(k = 0;k < mri->depth;k++)
          for(t = 0;t < mri->nframes;t++)
            if(!devFinite((MRIFseq_vox(mri, i, j, k, t))))
            {
              if(devIsinf((MRIFseq_vox(mri, i, j, k, t))) != 0)
              {
                ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "nan_inf_check(): Inf at voxel %d, %d, %d, %d", i, j, k, t));
              }
              else if(devIsnan((MRIFseq_vox(mri, i, j, k, t))))
              {
                ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "nan_inf_check(): NaN at voxel %d, %d, %d, %d", i, j, k, t));
              }
              else
              {
                ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "nan_inf_check(): bizarre value (not Inf, not NaN, but not finite) at %d, %d, %d, %d", i, j, k, t));
              }
          }

  return(NO_ERROR);

} /* end nan_inf_check() */

MRI *MRIreadType(char *fname, int type)
{

  MRI *mri;

  chklc();

  mri = mri_read(fname, type, TRUE, -1, -1);

  return(mri);

} /* end MRIreadType() */

MRI *MRIread(char *fname)
{

  MRI *mri = NULL;

  chklc() ;

  mri = mri_read(fname, MRI_VOLUME_TYPE_UNKNOWN, TRUE, -1, -1);

  return(mri);

} /* end MRIread() */

MRI *MRIreadInfo(char *fname)
{

  MRI *mri = NULL;

  mri = mri_read(fname, MRI_VOLUME_TYPE_UNKNOWN, TRUE, -1, -1);

  return(mri);

} /* end MRIreadInfo() */

int MRIwriteType(MRI *mri, char *fname, int type)
{

  int error;

  if(type == MRI_CORONAL_SLICE_DIRECTORY)
  {
    error = corWrite(mri, fname);
  }
  else if(type == MRI_MINC_FILE)
  {
    error = mincWrite(mri, fname);
  }
  else if(type == BSHORT_FILE)
  {
    error = bshortWrite(mri, fname);
  }
  else if(type == BFLOAT_FILE)
  {
    error = bfloatWrite(mri, fname);
  }
  else if(type == MRI_ANALYZE_FILE)
  {
    error = analyzeWrite(mri, fname);
  }
  else if(type == BRIK_FILE)
  {
    error = afniWrite(mri, fname);
  }
  else if(type == MRI_MGH_FILE)
  {
    error = mghWrite(mri, fname, 0);
  }
  else if(type == GDF_FILE)
  {
    error = gdfWrite(mri, fname);
  }
  else
  {
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "MRIwriteType(): code inconsistency (file type recognized but not caught)"));
  }

  return(error);

} /* end MRIwriteType() */

int MRIwrite(MRI *mri, char *fname)
{

  int int_type = -1;
  int error;

  chklc() ;
  if((int_type = mri_identify(fname)) < 0)
  {
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "unknown file type for file (%s)", fname));
  }

  error = MRIwriteType(mri, fname, int_type);

  return(error);

} /* end MRIwrite() */


/* ----- required header fields ----- */

#define COR_ALL_REQUIRED 0x00001fff

#define IMNR0_FLAG   0x00000001
#define IMNR1_FLAG   0x00000002
#define PTYPE_FLAG   0x00000004
#define X_FLAG       0x00000008
#define Y_FLAG       0x00000010
#define THICK_FLAG   0x00000020
#define PSIZ_FLAG    0x00000040
#define STRTX_FLAG   0x00000080
#define ENDX_FLAG    0x00000100
#define STRTY_FLAG   0x00000200
#define ENDY_FLAG    0x00000400
#define STRTZ_FLAG   0x00000800
#define ENDZ_FLAG    0x00001000

/* trivially time course clean */
static MRI *corRead(char *fname, int read_volume)
{

  MRI *mri;
  struct stat stat_buf;
  char fname_use[STRLEN];
  char *fbase;
  FILE *fp;
  int i, j;
  char line[STRLEN];
  int imnr0, imnr1, x, y, ptype;
  double fov, thick, psiz, locatn; /* using floats to read creates problems when checking values (e.g. thick = 0.00100000005) */
  float strtx, endx, strty, endy, strtz, endz;
  float tr, te, ti;
  int ras_good_flag;
  float x_r, x_a, x_s;
  float y_r, y_a, y_s;
  float z_r, z_a, z_s;
  float c_r, c_a, c_s;
  char xform[STRLEN];
  long gotten;
  char xform_use[STRLEN];

  /* ----- check that it is a directory we've been passed ----- */
  if(stat(fname, &stat_buf) < 0)
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "corRead(): can't stat %s", fname));
  }

  if(!S_ISDIR(stat_buf.st_mode))
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "corRead(): %s isn't a directory", fname));
  }

  /* ----- copy the directory name and remove any trailing '/' ----- */
  strcpy(fname_use, fname);
  fbase = &fname_use[strlen(fname_use)];
  if(*(fbase-1) != '/')
  {
    *fbase = '/';
    fbase++;
  }

  /* ----- read the header file ----- */
  sprintf(fbase, "COR-.info");
  if((fp = fopen(fname_use, "r")) == NULL)
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "corRead(): can't open file %s", fname_use));
  }

  /* ----- defaults (a good idea for non-required values...) ----- */
  xform[0] = '\0';
  ras_good_flag = 0;
  x_r = x_a = x_s = 0.0;
  y_r = y_a = y_s = 0.0;
  z_r = z_a = z_s = 0.0;
  c_r = c_a = c_s = 0.0;
  tr = te = ti = 0.0;
  fov = 0.0;
  locatn = 0.0;

  gotten = 0x00;

  while(fgets(line, STRLEN, fp) != NULL)
  {
    if(strncmp(line, "imnr0 ", 6) == 0)
    {
      sscanf(line, "%*s %d", &imnr0);
      gotten = gotten | IMNR0_FLAG;
    }
    else if(strncmp(line, "imnr1 ", 6) == 0)
    {
      sscanf(line, "%*s %d", &imnr1);
      gotten = gotten | IMNR1_FLAG;
    }
    else if(strncmp(line, "ptype ", 6) == 0)
    {
      sscanf(line, "%*s %d", &ptype);
      gotten = gotten | PTYPE_FLAG;
    }
    else if(strncmp(line, "x ", 2) == 0)
    {
      sscanf(line, "%*s %d", &x);
      gotten = gotten | X_FLAG;
    }
    else if(strncmp(line, "y ", 2) == 0)
    {
      sscanf(line, "%*s %d", &y);
      gotten = gotten | Y_FLAG;
    }
    else if(strncmp(line, "fov ", 4) == 0)
    {
      sscanf(line, "%*s %lf", &fov);
    }
    else if(strncmp(line, "thick ", 6) == 0)
    {
      sscanf(line, "%*s %lf", &thick);
      gotten = gotten | THICK_FLAG;
    }
    else if(strncmp(line, "psiz ", 5) == 0)
    {
      sscanf(line, "%*s %lf", &psiz);
      gotten = gotten | PSIZ_FLAG;
    }
    else if(strncmp(line, "locatn ", 7) == 0)
    {
      sscanf(line, "%*s %lf", &locatn);
    }
    else if(strncmp(line, "strtx ", 6) == 0)
    {
      sscanf(line, "%*s %f", &strtx);
      gotten = gotten | STRTX_FLAG;
    }
    else if(strncmp(line, "endx ", 5) == 0)
    {
      sscanf(line, "%*s %f", &endx);
      gotten = gotten | ENDX_FLAG;
    }
    else if(strncmp(line, "strty ", 6) == 0)
    {
      sscanf(line, "%*s %f", &strty);
      gotten = gotten | STRTY_FLAG;
    }
    else if(strncmp(line, "endy ", 5) == 0)
    {
      sscanf(line, "%*s %f", &endy);
      gotten = gotten | ENDY_FLAG;
    }
    else if(strncmp(line, "strtz ", 6) == 0)
    {
      sscanf(line, "%*s %f", &strtz);
      gotten = gotten | STRTZ_FLAG;
    }
    else if(strncmp(line, "endz ", 5) == 0)
    {
      sscanf(line, "%*s %f", &endz);
      gotten = gotten | ENDZ_FLAG;
    }
    else if(strncmp(line, "tr ", 3) == 0)
    {
      sscanf(line, "%*s %f", &tr);
    }
    else if(strncmp(line, "te ", 3) == 0)
    {
      sscanf(line, "%*s %f", &te);
    }
    else if(strncmp(line, "ti ", 3) == 0)
    {
      sscanf(line, "%*s %f", &ti);
    }
    else if(strncmp(line, "ras_good_flag ", 14) == 0)
    {
      sscanf(line, "%*s %d", &ras_good_flag);
    }
    else if(strncmp(line, "x_ras ", 6) == 0)
    {
      sscanf(line, "%*s %f %f %f", &x_r, &x_a, &x_s);
    }
    else if(strncmp(line, "y_ras ", 6) == 0)
    {
      sscanf(line, "%*s %f %f %f", &y_r, &y_a, &y_s);
    }
    else if(strncmp(line, "z_ras ", 6) == 0)
    {
      sscanf(line, "%*s %f %f %f", &z_r, &z_a, &z_s);
    }
    else if(strncmp(line, "c_ras ", 6) == 0)
    {
      sscanf(line, "%*s %f %f %f", &c_r, &c_a, &c_s);
    }
    else if(strncmp(line, "xform", 5) == 0 || strncmp(line, "transform", 9) == 0)
    {
      sscanf(line, "%*s %s", xform);
    }
  }

  fclose(fp);

  /* ----- check for required fields ----- */
  if((gotten & COR_ALL_REQUIRED) != COR_ALL_REQUIRED)
  {
    ErrorPrintf(ERROR_BADFILE, "missing fields in file %s:", fname_use);
    if(!(gotten & IMNR0_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  imnr0 field missing");
    if(!(gotten & IMNR1_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  imnr1 field missing");
    if(!(gotten & PTYPE_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  ptype field missing");
    if(!(gotten & X_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  x field missing");
    if(!(gotten & Y_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  y field missing");
    if(!(gotten & THICK_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  thick field missing");
    if(!(gotten & PSIZ_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  psiz field missing");
    if(!(gotten & STRTX_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  strtx field missing");
    if(!(gotten & ENDX_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  endx field missing");
    if(!(gotten & STRTY_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  strty field missing");
    if(!(gotten & ENDY_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  endy field missing");
    if(!(gotten & STRTZ_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  strtz field missing");
    if(!(gotten & ENDZ_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  endz field missing");
    return(NULL);
  }

  /* ----- check for required but forced (constant) values ----- */

  if(imnr0 != 1)
  {
    printf("warning: non-standard value for imnr0 (%d, usually 1) in file %s\n", imnr0, fname_use);
  }

  if(imnr1 != 256)
  {
    printf("warning: non-standard value for imnr1 (%d, usually 256) in file %s\n", imnr1, fname_use);
  }

  if(ptype != 2)
  {
    printf("warning: non-standard value for ptype (%d, usually 2) in file %s\n", ptype, fname_use);
  }

  if(x != 256)
  {
    printf("warning: non-standard value for x (%d, usually 256) in file %s\n", x, fname_use);
  }

  if(y != 256)
  {
    printf("warning: non-standard value for y (%d, usually 256) in file %s\n", y, fname_use);
  }

  if(thick != 0.001)
  {
    printf("warning: non-standard value for thick (%g, usually 0.001) in file %s\n", thick, fname_use);
  }

  if(psiz != 0.001)
  {
    printf("warning: non-standard value for psiz (%g, usually 0.001) in file %s\n", psiz, fname_use);
  }

  /* ----- copy header information to an mri structure ----- */

  if(read_volume)
    mri = MRIalloc(x, y, imnr1 - imnr0 + 1, MRI_UCHAR);
  else
    mri = MRIallocHeader(x, y, imnr1 - imnr0 + 1, MRI_UCHAR);

/* hack */
/*
printf("%g, %g, %g\n", x_r, x_a, x_s);
printf("%g, %g, %g\n", y_r, y_a, y_s);
printf("%g, %g, %g\n", z_r, z_a, z_s);
*/
if(x_r == 0.0 && x_a == 0.0 && x_s == 0.0 && y_r == 0.0 && y_a == 0.0 && y_s == 0.0 && z_r == 0.0 && z_a == 0.0 && z_s == 0.0)
{
  x_r = -1.0;
  y_s = -1.0;
  z_a = 1.0;
}

  mri->imnr0 = imnr0;
  mri->imnr1 = imnr1;
  mri->fov = (float)(fov * 1000);
  mri->thick = (float)(thick * 1000);
  mri->ps = (float)(psiz * 1000);
  mri->xsize = mri->ps;
  mri->ysize = mri->ps;
  mri->zsize = (float)(mri->thick);
  mri->xstart = strtx * 1000;
  mri->xend = endx * 1000;
  mri->ystart = strty * 1000;
  mri->yend = endy * 1000;
  mri->zstart = strtz * 1000;
  mri->zend = endz * 1000;
  strcpy(mri->fname, fname);
  mri->tr = tr;
  mri->te = te;
  mri->ti = ti;
  mri->ras_good_flag = ras_good_flag;
  mri->x_r = x_r;  mri->x_a = x_a;  mri->x_s = x_s;
  mri->y_r = y_r;  mri->y_a = y_a;  mri->y_s = y_s;
  mri->z_r = z_r;  mri->z_a = z_a;  mri->z_s = z_s;
  mri->c_r = c_r;  mri->c_a = c_a;  mri->c_s = c_s;

  if(strlen(xform) > 0)
  {

    if(xform[0] == '/')
      strcpy(mri->transform_fname, xform);
    else
      sprintf(mri->transform_fname, "%s/%s", fname, xform);

    strcpy(xform_use, mri->transform_fname);

    if(!FileExists(xform_use))
    {

      sprintf(xform_use, "%s/../transforms/talairach.xfm", fname);

      if(!FileExists(xform_use))
        printf("can't talairach file\n");

    }

    if(FileExists(xform_use))
    {
      if(input_transform_file(xform_use, &(mri->transform)) == NO_ERROR)
      {
        mri->linear_transform = get_linear_transform_ptr(&mri->transform);
        mri->inverse_linear_transform = get_inverse_linear_transform_ptr(&mri->transform);
        mri->free_transform = 1;
        strcpy(mri->transform_fname, xform_use);
      }
      else
      {
        ErrorPrintf(ERROR_BAD_FILE, "error loading transform from %s", xform_use);
        mri->linear_transform = NULL;
        mri->inverse_linear_transform = NULL;
        mri->free_transform = 1;
        (mri->transform_fname)[0] = '\0';
      }
    }

  }

  if(!read_volume)
    return(mri);

  /* ----- read the data files ----- */
  for(i = mri->imnr0;i <= imnr1;i++)
  {
    sprintf(fbase, "COR-%03d", i);
    if((fp = fopen(fname_use, "r")) == NULL)
    {
      MRIfree(&mri);
      ErrorReturn(NULL, (ERROR_BADFILE, "corRead(): can't open file %s", fname_use));
    }
    for(j = 0;j < mri->height;j++)
    {
      if(fread(mri->slices[i-mri->imnr0][j], 1, mri->width, fp) < mri->width)
      {
        MRIfree(&mri);
        ErrorReturn(NULL, (ERROR_BADFILE, "corRead(): error reading from file %s", fname_use));
      }
    }
    fclose(fp);
  }

  return(mri);

} /* end corRead() */

static int corWrite(MRI *mri, char *fname)
{

  struct stat stat_buf;
  char fname_use[STRLEN];
  char *fbase;
  FILE *fp;
  int i, j;

  /* ----- check the mri structure for COR file compliance ----- */

  if(mri->slices == NULL)
  {
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "corWrite(): mri structure to be written contains no voxel data"));
  }

  if(mri->imnr0 != 1)
  {
    printf("non-standard value for imnr0 (%d, usually 1) in volume structure\n", mri->imnr0);
  }

  if(mri->imnr1 != 256)
  {
    printf("non-standard value for imnr1 (%d, usually 256) in volume structure\n", mri->imnr1);
  }

  if(mri->type != MRI_UCHAR)
  {
    printf("non-standard value for type (%d, usually %d) in volume structure\n", mri->type, MRI_UCHAR);
  }

  if(mri->width != 256)
  {
    printf("non-standard value for width (%d, usually 256) in volume structure\n", mri->width);
  }

  if(mri->height != 256)
  {
    printf("non-standard value for height (%d, usually 256) in volume structure\n", mri->height);
  }

  if(mri->thick != 1)
  {
    printf("non-standard value for thick (%g, usually 1) in volume structure\n", mri->thick);
  }

  if(mri->ps != 1)
  {
    printf("non-standard value for ps (%g, usually 1) in volume structure\n", mri->ps);
  }

  /* ----- check that it is a directory we've been passed ----- */
  if(stat(fname, &stat_buf) < 0)
  {
    ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "corWrite(): can't stat %s", fname));
  }

  if(!S_ISDIR(stat_buf.st_mode))
  {
    ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "corWrite(): %s isn't a directory", fname));
  }

  /* ----- copy the directory name and remove any trailing '/' ----- */
  strcpy(fname_use, fname);
  fbase = &fname_use[strlen(fname_use)];
  if(*(fbase-1) != '/')
  {
    *fbase = '/';
    fbase++;
  }

  sprintf(fbase, "COR-.info");
  if((fp = fopen(fname_use, "w")) == NULL)
  {
    ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "corWrite(): can't open file %s for writing", fname_use));
  }

  fprintf(fp, "imnr0 %d\n", mri->imnr0);
  fprintf(fp, "imnr1 %d\n", mri->imnr1);
  fprintf(fp, "ptype %d\n", 2);
  fprintf(fp, "x %d\n", mri->width);
  fprintf(fp, "y %d\n", mri->height);
  fprintf(fp, "fov %g\n", mri->fov / 1000.0);
  fprintf(fp, "thick %g\n", mri->xsize / 1000.0);
  fprintf(fp, "psiz %g\n", mri->zsize / 1000.0);
  fprintf(fp, "locatn %g\n", 0.0);
  fprintf(fp, "strtx %g\n", mri->xstart / 1000.0);
  fprintf(fp, "endx %g\n", mri->xend / 1000.0);
  fprintf(fp, "strty %g\n", mri->ystart / 1000.0);
  fprintf(fp, "endy %g\n", mri->yend / 1000.0);
  fprintf(fp, "strtz %g\n", mri->zstart / 1000.0);
  fprintf(fp, "endz %g\n", mri->zend / 1000.0);
  fprintf(fp, "tr %f\n", mri->tr);
  fprintf(fp, "te %f\n", mri->te);
  fprintf(fp, "ti %f\n", mri->ti);
  fprintf(fp, "ras_good_flag %d\n", mri->ras_good_flag);
  fprintf(fp, "x_ras %f %f %f\n", mri->x_r, mri->x_a, mri->x_s);
  fprintf(fp, "y_ras %f %f %f\n", mri->y_r, mri->y_a, mri->y_s);
  fprintf(fp, "z_ras %f %f %f\n", mri->z_r, mri->z_a, mri->z_s);
  fprintf(fp, "c_ras %f %f %f\n", mri->c_r, mri->c_a, mri->c_s);
  if(strlen(mri->transform_fname) > 0)
    fprintf(fp, "xform %s\n", mri->transform_fname);

  fclose(fp);

  for(i = mri->imnr0;i <= mri->imnr1;i++)
  {
    sprintf(fbase, "COR-%03d", i);
    if((fp = fopen(fname_use, "w")) == NULL)
    {
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "corWrite(): can't open file %s for writing", fname_use));
    }
    for(j = 0;j < mri->height;j++)
    {
      if(fwrite(mri->slices[i-mri->imnr0][j], 1, mri->width, fp) < mri->width)
      {
        fclose(fp);
        ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "corWrite(): error writing to file %s ", fname_use));
      }
    }
    fclose(fp);
  }

  return(NO_ERROR);

} /* end corWrite() */

static MRI *siemensRead(char *fname, int read_volume_flag)
{

  int file_n, n_low, n_high;
  char fname_use[STRLEN];
  MRI *mri;
  FILE *fp;
  char *c, *c2;
  short rows, cols;
  int n_slices;
  double d, d2;
  double im_c_r, im_c_a, im_c_s;
  int i, j;
  int n_files, base_raw_matrix_size, number_of_averages;
  int mosaic_size;
  int mosaic;
  int mos_r, mos_c;
  char pulse_sequence_name[STRLEN], ps2[STRLEN];
  int n_t;
  int n_dangling_images, n_full_mosaics, mosaics_per_volume;
  int br, bc;
  MRI *mri_raw;
  int t, s;
  int slice_in_mosaic;
  int file;
  char ima[4];

  /* ----- stop compiler complaints ----- */
  mri = NULL;
  mosaic_size = 0;

  strcpy(fname_use, fname);

  /* ----- point to ".ima" ----- */
  c = fname_use + (strlen(fname_use) - 4);

  if(c < fname_use)
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "siemensRead(): bad file name %s (must end in '.ima' or '.IMA')", fname_use));
  }
  if(strcmp(".ima", c) == 0)
    sprintf(ima, "ima");
  else if(strcmp(".IMA", c) == 0)
    sprintf(ima, "IMA");
  else
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "siemensRead(): bad file name %s (must end in '.ima' or '.IMA')", fname_use));
  }

  c2 = c;
  for(c--;isdigit((int)(*c));c--);
  c++;

  /* ----- c now points to the first digit in the last number set (e.g. to the "5" in 123-4-567.ima) ----- */

  /* ----- count down and up from this file -- max and min image number within the series ----- */
  *c2 = '\0';
  file_n = atol(c);
  *c2 = '.';

  if(!FileExists(fname_use))
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "siemensRead(): file %s doesn't exist", fname_use));
  }

  /* --- get the low image number --- */
  n_low = file_n - 1;
  sprintf(c, "%d.%s", n_low, ima);
  while(FileExists(fname_use))
  {
    n_low--;
    sprintf(c, "%d.%s", n_low, ima);
  }
  n_low++;

  /* --- get the high image number --- */
  n_high = file_n + 1;
  sprintf(c, "%d.%s", n_high, ima);
  while(FileExists(fname_use))
  {
    n_high++;
    sprintf(c, "%d.%s", n_high, ima);
  }
  n_high--;

  n_files = n_high - n_low + 1;

  sprintf(c, "%d.%s", n_low, ima);
  if((fp = fopen(fname_use, "r")) == NULL)
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "siemensRead(): can't open file %s (low = %d, this = %d, high = %d)", fname_use, n_low, file_n, n_high));
  }

  fseek(fp, 4994, SEEK_SET);
  fread(&rows, 2, 1, fp);
  rows = orderShortBytes(rows);
  fseek(fp, 4996, SEEK_SET);
  fread(&cols, 2, 1, fp);
  cols = orderShortBytes(cols);
  fseek(fp, 4004, SEEK_SET);
  fread(&n_slices, 4, 1, fp);
  n_slices = orderIntBytes(n_slices);
  fseek(fp, 2864, SEEK_SET);
  fread(&base_raw_matrix_size, 4, 1, fp);
  base_raw_matrix_size = orderIntBytes(base_raw_matrix_size);
  fseek(fp, 1584, SEEK_SET);
  fread(&number_of_averages, 4, 1, fp);
  number_of_averages = orderIntBytes(number_of_averages);
  memset(pulse_sequence_name, 0x00, STRLEN);
  fseek(fp, 3009, SEEK_SET);
  fread(&pulse_sequence_name, 1, 65, fp);

  /* --- scout --- */
  strcpy(ps2, pulse_sequence_name);
  StrLower(ps2);
  if(strstr(ps2, "scout") != NULL)
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "siemensRead(): series appears to be a scout (sequence file name is %s)", pulse_sequence_name));
  }

  /* --- structural --- */
  if(n_slices == 1)
  {
    n_slices = n_files;
    n_t = 1;
    if(base_raw_matrix_size != rows || base_raw_matrix_size != cols)
    {
      ErrorReturn(NULL, (ERROR_BADPARM, "siemensRead(): bad file/base matrix sizes"));
    }
    mos_r = mos_c = 1;
    mosaic_size = 1;
  }
  else
  {

    if(rows % base_raw_matrix_size != 0)
    {
      ErrorReturn(NULL, (ERROR_BADPARM, "siemensRead(): file rows (%hd) not divisible by image rows (%d)", rows, base_raw_matrix_size));
    }
    if(cols % base_raw_matrix_size != 0)
    {
      ErrorReturn(NULL, (ERROR_BADPARM, "siemensRead(): file cols (%hd) not divisible by image cols (%d)", cols, base_raw_matrix_size));
    }

    mos_r = rows / base_raw_matrix_size;
    mos_c = cols / base_raw_matrix_size;
    mosaic_size = mos_r * mos_c;

    n_dangling_images = n_slices % mosaic_size;
    n_full_mosaics = (n_slices - n_dangling_images) / mosaic_size;

    mosaics_per_volume = n_full_mosaics + (n_dangling_images == 0 ? 0 : 1);

    if(n_files % mosaics_per_volume != 0)
    {
      ErrorReturn(NULL, (ERROR_BADPARM, "siemensRead(): files in volume (%d) not divisible by mosaics per volume (%d)", n_files, mosaics_per_volume));
    }

    n_t = n_files / mosaics_per_volume;

  }

  if(read_volume_flag)
    mri = MRIallocSequence(base_raw_matrix_size, base_raw_matrix_size, n_slices, MRI_SHORT, n_t);
  else
  {
    mri = MRIallocHeader(base_raw_matrix_size, base_raw_matrix_size, n_slices, MRI_SHORT);
    mri->nframes = n_t;
  }

  /* --- pixel sizes --- */
  /* --- mos_r and mos_c factors are strange, but they're there... --- */
  fseek(fp, 5000, SEEK_SET);
  fread(&d, 8, 1, fp);
  mri->xsize = mos_r * orderDoubleBytes(d);
  fseek(fp, 5008, SEEK_SET);
  fread(&d, 8, 1, fp);
  mri->ysize = mos_c * orderDoubleBytes(d);

  /* --- slice distance factor --- */
  fseek(fp, 4136, SEEK_SET);
  fread(&d, 8, 1, fp);
  d = orderDoubleBytes(d);
  if(d == -19222) /* undefined (Siemens code) -- I assume this to mean 0 */
    d = 0.0;
  /* --- slice thickness --- */
  fseek(fp, 1544, SEEK_SET);
  fread(&d2, 8, 1, fp);
  d2 = orderDoubleBytes(d2);
  /* --- distance between slices --- */
  mri->zsize = (1.0 + d) * d2;

  /* --- field of view (larger of height, width fov) --- */
  fseek(fp, 3744, SEEK_SET);
  fread(&d, 8, 1, fp);
  d = orderDoubleBytes(d);
  fseek(fp, 3752, SEEK_SET);
  fread(&d2, 8, 1, fp);
  d2 = orderDoubleBytes(d);
  mri->fov = (d > d2 ? d : d2);

  mri->thick = mri->zsize;
  mri->ps = mri->xsize;

  strcpy(mri->fname, fname);

  mri->location = 0.0;

  fseek(fp, 1560, SEEK_SET);
  fread(&d, 8, 1, fp);
  mri->tr = orderDoubleBytes(d);
  fseek(fp, 1568, SEEK_SET);
  fread(&d, 8, 1, fp);
  mri->te = orderDoubleBytes(d);
  fseek(fp, 1576, SEEK_SET);
  fread(&d, 8, 1, fp);
  mri->ti = orderDoubleBytes(d);

  fseek(fp, 3792, SEEK_SET);
  fread(&d, 8, 1, fp);  mri->z_r = -orderDoubleBytes(d);
  fread(&d, 8, 1, fp);  mri->z_a =  orderDoubleBytes(d);
  fread(&d, 8, 1, fp);  mri->z_s = -orderDoubleBytes(d);

  fseek(fp, 3832, SEEK_SET);
  fread(&d, 8, 1, fp);  mri->x_r = -orderDoubleBytes(d);
  fread(&d, 8, 1, fp);  mri->x_a =  orderDoubleBytes(d);
  fread(&d, 8, 1, fp);  mri->x_s = -orderDoubleBytes(d);

  fseek(fp, 3856, SEEK_SET);
  fread(&d, 8, 1, fp);  mri->y_r = -orderDoubleBytes(d);
  fread(&d, 8, 1, fp);  mri->y_a =  orderDoubleBytes(d);
  fread(&d, 8, 1, fp);  mri->y_s = -orderDoubleBytes(d);

  fseek(fp, 3768, SEEK_SET);
  fread(&im_c_r, 8, 1, fp);  im_c_r = -orderDoubleBytes(im_c_r);
  fread(&im_c_a, 8, 1, fp);  im_c_a =  orderDoubleBytes(im_c_a);
  fread(&im_c_s, 8, 1, fp);  im_c_s = -orderDoubleBytes(im_c_s);

  mri->c_r = im_c_r - (mosaic_size - 1) * mri->z_r * mri->zsize + ((mri->depth - 1.0) / 2.0) * mri->z_r * mri->zsize;
  mri->c_a = im_c_a - (mosaic_size - 1) * mri->z_a * mri->zsize + ((mri->depth - 1.0) / 2.0) * mri->z_a * mri->zsize;
  mri->c_s = im_c_s - (mosaic_size - 1) * mri->z_s * mri->zsize + ((mri->depth - 1.0) / 2.0) * mri->z_s * mri->zsize;

  mri->ras_good_flag = 1;

  fseek(fp, 3760, SEEK_SET);
  fread(&i, 4, 1, fp);
  i = orderIntBytes(i);
  if(i == 1 || i == 2)
    mri->slice_direction = MRI_HORIZONTAL;
  else if(i == 3 || i == 5)
    mri->slice_direction = MRI_CORONAL;
  else if(i == 4 || i == 6)
    mri->slice_direction = MRI_SAGITTAL;
  else
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "siemensRead(): bad slice direction (%d) in file %s", i, fname_use));
  }

  mri->xstart = -mri->width * mri->xsize / 2.0;
  mri->xend = -mri->xstart;
  mri->ystart = -mri->height * mri->ysize / 2.0;
  mri->yend = -mri->ystart;
  mri->zstart = -mri->depth * mri->zsize / 2.0;
  mri->zend = -mri->zstart;

/*
printf("%d, %d; %d, %hd, %hd; %d\n", n_files, number_of_averages,
                                     base_raw_matrix_size, rows, cols,
                                     slices);
*/
/*
rows, cols, brms, mosaic i, j, mosaic size, n slices, n files, n_t
*/
  fclose(fp);

  mri->imnr0 = 1;
  mri->imnr1 = mri->depth;
/*
printf("%d, %d, %d, %d\n", mri->width, mri->height, mri->depth, mri->nframes);
*/
  if(read_volume_flag)
  {

    mri_raw = MRIalloc(rows, cols, n_files, MRI_SHORT);

    for(file_n = n_low;file_n <= n_high;file_n++)
    {

      sprintf(c, "%d.%s", file_n, ima);
      if((fp = fopen(fname_use, "r")) == NULL)
      {
        MRIfree(&mri);
        ErrorReturn(NULL, (ERROR_BADFILE, "siemensRead(): can't open file %s (low = %d, this = %d, high = %d)", fname_use, n_low, file_n, n_high));
      }

      fseek(fp, 6144, SEEK_SET);

      for(i = 0;i < rows;i++)
      {
        fread(&MRISvox(mri_raw, 0, i, file_n - n_low), sizeof(short), cols, fp);
#ifdef Linux
        swab(&MRISvox(mri_raw, 0, i, file_n - n_low), &MRISvox(mri_raw, 0, i, file_n - n_low), sizeof(short) * cols);
#endif
      }

      fclose(fp);

    }

    for(t = 0;t < mri->nframes;t++)
    {
      for(s = 0;s < mri->depth;s++)
      {
        slice_in_mosaic = s % mosaic_size;
        mosaic = (s - slice_in_mosaic) / mosaic_size;
        file = mri->nframes * mosaic + t;
/*
printf("s, t = %d, %d; f, sm = %d, %d\n", s, t, file, slice_in_mosaic);
*/
bc = slice_in_mosaic % mos_r;
br = (slice_in_mosaic - bc) / mos_r;

        for(i = 0;i < mri->width;i++)
        {
          for(j = 0;j < mri->height;j++)
          {
            MRISseq_vox(mri, i, j, s, t) = MRISvox(mri_raw, mri->width * bc + i, mri->height * br + j, file);
          }
        }

      }
    }

    MRIfree(&mri_raw);

  }

  return(mri);

} /* end siemensRead() */

static MRI *mincRead(char *fname, int read_volume)
{

  MRI *mri;
  Volume vol;
  Status status;
  char *dim_names[4];
  int dim_sizes[4];
  int ndims;
  int dtype;
  volume_input_struct input_info;
  Real separations[4];
  Real voxel[4];
  Real worldr, worlda, worlds;
  Real val;
  int i, j, k, t;
  float xfov, yfov, zfov;
  Real f;

  /* ----- read the volume ----- */
  dim_names[0] = MIxspace;
  dim_names[1] = MIyspace;
  dim_names[2] = MIzspace;
  dim_names[3] = MItime;

  status = start_volume_input(fname, 0, dim_names, NC_UNSPECIFIED, 0, 0, 0, TRUE, &vol, NULL, &input_info);

/*
printf("%d\n", status);
printf("%d\n", get_volume_n_dimensions(vol));
printf("%d\n", vol->nc_data_type);
*/

  if(status != OK)
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "mincRead(): error reading volume from file %s", fname));
  }

  /* ----- check the number of dimensions ----- */
  ndims = get_volume_n_dimensions(vol);
  if(ndims != 3 && ndims != 4)
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "mincRead(): %d dimensions in file; expecting 3 or 4", ndims));
  }

  /* ----- get the dimension sizes ----- */
  get_volume_sizes(vol, dim_sizes);

  /* --- one time point if there are only three dimensions in the file --- */
  if(ndims == 3)
    dim_sizes[3] = 1;

/*
printf("%d, %d, %d, %d, %d\n", ndims, dim_sizes[0], dim_sizes[1], dim_sizes[2], dim_sizes[3]);
printf("%d\n", vol->nc_data_type);
*/

  /* ----- get the data type ----- */
  if(vol->nc_data_type == NC_BYTE || vol->nc_data_type == NC_CHAR)
    dtype = MRI_UCHAR;
  else if(vol->nc_data_type == NC_SHORT)
    dtype = MRI_SHORT;
  else if(vol->nc_data_type == NC_LONG)
    dtype = MRI_LONG;
  else if(vol->nc_data_type == NC_FLOAT || vol->nc_data_type == NC_DOUBLE)
    dtype = MRI_FLOAT;
  else
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "mincRead(): bad data type (%d) in input file %s", vol->nc_data_type, fname));
  }

  /* ----- allocate the mri structure ----- */
  if(read_volume)
    mri = MRIallocSequence(dim_sizes[0], dim_sizes[1], dim_sizes[2], dtype, dim_sizes[3]);
  else
  {
    mri = MRIallocHeader(dim_sizes[0], dim_sizes[1], dim_sizes[2], dtype);
    mri->nframes = dim_sizes[3];
  }

  /* ----- set up the mri structure ----- */
  get_volume_separations(vol, separations);
  mri->xsize = fabs(separations[0]);
  mri->ysize = fabs(separations[1]);
  mri->zsize = fabs(separations[2]);
  mri->ps = mri->xsize;
  mri->thick = mri->zsize;

  mri->x_r = vol->direction_cosines[0][0];  mri->x_a = vol->direction_cosines[0][1];  mri->x_s = vol->direction_cosines[0][2];
  mri->y_r = vol->direction_cosines[1][0];  mri->y_a = vol->direction_cosines[1][1];  mri->y_s = vol->direction_cosines[1][2];
  mri->z_r = vol->direction_cosines[2][0];  mri->z_a = vol->direction_cosines[2][1];  mri->z_s = vol->direction_cosines[2][2];

  if(separations[0] < 0)
  {
    mri->x_r = -mri->x_r;  mri->x_a = -mri->x_a;  mri->x_s = -mri->x_s;
  }
  if(separations[1] < 0)
  {
    mri->y_r = -mri->y_r;  mri->y_a = -mri->y_a;  mri->y_s = -mri->y_s;
  }
  if(separations[2] < 0)
  {
    mri->z_r = -mri->z_r;  mri->z_a = -mri->z_a;  mri->z_s = -mri->z_s;
  }

  voxel[0] = (mri->width - 1) / 2.0;
  voxel[1] = (mri->height - 1) / 2.0;
  voxel[2] = (mri->depth - 1) / 2.0;
  voxel[3] = 0.0;
  convert_voxel_to_world(vol, voxel, &worldr, &worlda, &worlds);
  mri->c_r = worldr;
  mri->c_a = worlda;
  mri->c_s = worlds;

  mri->ras_good_flag = 1;

  mri->xend = mri->xsize * mri->width / 2.0;   mri->xstart = -mri->xend;
  mri->yend = mri->ysize * mri->height / 2.0;  mri->ystart = -mri->yend;
  mri->zend = mri->zsize * mri->depth / 2.0;   mri->zstart = -mri->zend;

  xfov = mri->xend - mri->xstart;
  yfov = mri->yend - mri->ystart;
  zfov = mri->zend - mri->zstart;

  mri->fov = ( xfov > yfov ? (xfov > zfov ? xfov : zfov ) : (yfov > zfov ? yfov : zfov ) );

  strcpy(mri->fname, fname);

  /* ----- copy the data from the file to the mri structure ----- */
  if(read_volume)
  {

    while(input_more_of_volume(vol, &input_info, &f));

    for(i = 0;i < mri->width;i++)
    {
      for(j = 0;j < mri->height;j++)
      {
        for(k = 0;k < mri->depth;k++)
        {
          for(t = 0;t < mri->nframes;t++)
          {
            val = get_volume_voxel_value(vol, i, j, k, t, 0);
            if(mri->type == MRI_UCHAR)
              MRIseq_vox(mri, i, j, k, t) = (unsigned char)val;
            if(mri->type == MRI_SHORT)
              MRISseq_vox(mri, i, j, k, t) = (short)val;
            if(mri->type == MRI_LONG)
              MRILseq_vox(mri, i, j, k, t) = (long)val;
            if(mri->type == MRI_FLOAT)
              MRIFseq_vox(mri, i, j, k, t) = (float)val;
          }
        }
      }
    }
  }

  delete_volume_input(&input_info);

  delete_volume(vol);

  return(mri);

} /* end mincRead() */

/* time course clean */
static int mincWrite(MRI *mri, char *fname)
{

  Volume minc_volume;
  STRING dimension_names[4] = { "xspace", "yspace", "zspace", "time" };
  nc_type nc_data_type;
  Real min, max;
  float fmin, fmax;
  int dimension_sizes[4];
  Real separations[4];
  Real dir_cos[4];
  int return_value;
  Real voxel[4], world[4];
  int signed_flag;
  int di_x, di_y, di_z;
  int vi[4];
  int r, a, s;
  float r_max;

/* di gives the bogus minc index */
/* di[0] is width, 1 is height, 2 is depth, 3 is time if there is a time dimension of length > 1 */
/* minc wants these to be ras */

/* here: minc volume index 0 is r, 1 is a, 2 is s */
/* mri is lia */

  /* ----- get the orientation of the volume ----- */
  if(mri->ras_good_flag == 0)
  {
    mri->x_r = -1;  mri->x_a = 0;  mri->x_s =  0;
    mri->y_r =  0;  mri->y_a = 0;  mri->y_s = -1;
    mri->z_r =  0;  mri->z_a = 1;  mri->z_s =  0;
  }

  r = 0;
  r_max = fabs(mri->x_r);
  if(fabs(mri->y_r) > r_max)
  {
    r_max = fabs(mri->y_r);
    r = 1;
  }
  if(fabs(mri->z_r) > r_max)
    r = 2;

  if(r == 0)
    a = (fabs(mri->y_a) > fabs(mri->z_a) ? 1 : 2);
  else if(r == 1)
    a = (fabs(mri->x_a) > fabs(mri->z_a) ? 0 : 2);
  else
    a = (fabs(mri->x_a) > fabs(mri->y_a) ? 0 : 1);

  s = 3 - r - a;

  /* ----- set the appropriate minc axes to this orientation ----- */
/* r gives the mri structure axis of the lr coordinate *//* lr = minc xspace = 0 */
/* a ..... of the pa coordinate *//* pa = minc yspace = 1 */
/* s ..... of the is coordinate *//* is = minc zspace = 2 */

/* di of this axis must be set to 2 */
/* ... and so on */

  if(r == 0)
  {
    if(a == 1)
    {
      di_x = 0;
      di_y = 1;
      di_z = 2;
    }
    else
    {
      di_x = 0;
      di_y = 2;
      di_z = 1;
    }
  }
  else if(r == 1)
  {
    if(a == 0)
    {
      di_x = 1;
      di_y = 0;
      di_z = 2;
    }
    else
    {
      di_x = 2;
      di_y = 0;
      di_z = 1;
    }
  }
  else
  {
    if(a == 0)
    {
      di_x = 1;
      di_y = 2;
      di_z = 0;
    }
    else
    {
      di_x = 2;
      di_y = 1;
      di_z = 0;
    }
  }

  /* ----- set the data type ----- */
  if(mri->type == MRI_UCHAR)
  {
    nc_data_type = NC_BYTE;
    signed_flag = 0;
  }
  else if(mri->type == MRI_SHORT)
  {
    nc_data_type = NC_SHORT;
    signed_flag = 1;
  }
  else if(mri->type == MRI_INT)
  {
    nc_data_type = NC_LONG;
    signed_flag = 1;
  }
  else if(mri->type == MRI_LONG)
  {
    nc_data_type = NC_LONG;
    signed_flag = 1;
  }
  else if(mri->type == MRI_FLOAT)
  {
    nc_data_type = NC_FLOAT;
    signed_flag = 1;
  }
  else
  {
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "mincWrite(): bad data type (%d) in mri structure", mri->type));
  }

  if((return_value = MRIlimits(mri, &fmin, &fmax)) != NO_ERROR)
    return(return_value);

  min = (Real)fmin;
  max = (Real)fmax;

  if(mri->nframes == 1)
    minc_volume = create_volume(3, dimension_names, nc_data_type, signed_flag, min, max);
  else
    minc_volume = create_volume(4, dimension_names, nc_data_type, signed_flag, min, max);

  dimension_sizes[di_x] = mri->width;
  dimension_sizes[di_y] = mri->height;
  dimension_sizes[di_z] = mri->depth;
  dimension_sizes[3] = mri->nframes;

  set_volume_sizes(minc_volume, dimension_sizes);

  alloc_volume_data(minc_volume);

  dir_cos[0] = (Real)mri->x_r;
  dir_cos[1] = (Real)mri->x_a;
  dir_cos[2] = (Real)mri->x_s;
  set_volume_direction_cosine(minc_volume, di_x, dir_cos);

  dir_cos[0] = (Real)mri->y_r;
  dir_cos[1] = (Real)mri->y_a;
  dir_cos[2] = (Real)mri->y_s;
  set_volume_direction_cosine(minc_volume, di_y, dir_cos);

  dir_cos[0] = (Real)mri->z_r;
  dir_cos[1] = (Real)mri->z_a;
  dir_cos[2] = (Real)mri->z_s;
  set_volume_direction_cosine(minc_volume, di_z, dir_cos);

  voxel[di_x] = (Real)(((float)(mri->width) - 1.0) / 2.0);
  voxel[di_y] = (Real)(((float)(mri->height) - 1.0) / 2.0);
  voxel[di_z] = (Real)(((float)(mri->depth) - 1.0) / 2.0);
  voxel[3] = 0.0;
  world[0] = (Real)(mri->c_r);
  world[1] = (Real)(mri->c_a);
  world[2] = (Real)(mri->c_s);
  world[3] = 0.0;
  set_volume_translation(minc_volume, voxel, world);

  separations[di_x] = (Real)(mri->xsize);
  separations[di_y] = (Real)(mri->ysize);
  separations[di_z] = (Real)(mri->zsize);
  separations[3] = 1.0;
  set_volume_separations(minc_volume, separations);

/* vi[n] gives the index of the variable along minc axis x */
/* vi[di_x] gives the index of the variable along minc axis di_x, or along mri axis x */
  for(vi[3] = 0;vi[3] < mri->nframes;vi[3]++)
  {
    for(vi[di_x] = 0;vi[di_x] < mri->width;vi[di_x]++)
    {
      for(vi[di_y] = 0;vi[di_y] < mri->height;vi[di_y]++)
      {
        for(vi[di_z] = 0;vi[di_z] < mri->depth;vi[di_z]++)
        {

          if(mri->type == MRI_UCHAR)
            set_volume_voxel_value(minc_volume, vi[0], vi[1], vi[2], vi[3], 0, (Real)MRIseq_vox(mri, vi[di_x], vi[di_y], vi[di_z], vi[3]));
          if(mri->type == MRI_SHORT)
            set_volume_voxel_value(minc_volume, vi[0], vi[1], vi[2], vi[3], 0, (Real)MRISseq_vox(mri, vi[di_x], vi[di_y], vi[di_z], vi[3]));
          if(mri->type == MRI_INT)
            set_volume_voxel_value(minc_volume, vi[0], vi[1], vi[2], vi[3], 0, (Real)MRIIseq_vox(mri, vi[di_x], vi[di_y], vi[di_z], vi[3]));
          if(mri->type == MRI_LONG)
            set_volume_voxel_value(minc_volume, vi[0], vi[1], vi[2], vi[3], 0, (Real)MRILseq_vox(mri, vi[di_x], vi[di_y], vi[di_z], vi[3]));
          if(mri->type == MRI_FLOAT)
            set_volume_voxel_value(minc_volume, vi[0], vi[1], vi[2], vi[3], 0, (Real)MRIFseq_vox(mri, vi[di_x], vi[di_y], vi[di_z], vi[3]));

        }
      }
    }
  }

  output_volume((STRING)fname, nc_data_type, signed_flag, min, max, minc_volume, (STRING)"", NULL);

  delete_volume(minc_volume);

  return(NO_ERROR);

} /* end mincWrite() */

static int bshortWrite(MRI *mri, char *fname_passed)
{

  int i, j, t;
  char fname[STRLEN];
  short *buf;
  FILE *fp;
  int result;
  MRI *subject_info = NULL;
  char subject_volume_dir[STRLEN];
  char *subjects_dir;
  char *sn;
  char analyse_fname[STRLEN], register_fname[STRLEN];
  char output_dir[STRLEN];
  char *c;
  int od_length;
  char t1_path[STRLEN];
  MATRIX *cf, *bf, *ibf, *af, *iaf, *as, *bs, *cs, *ics, *r;
  MATRIX *r1, *r2, *r3, *r4;
  float det;
  int bad_flag;
  int l;
  char stem[STRLEN];
  char *c1, *c2, *c3;
  struct stat stat_buf;
  char subject_dir[STRLEN];

  if(mri->type != MRI_SHORT)
  {
    ErrorReturn(ERROR_UNSUPPORTED, (ERROR_UNSUPPORTED, "bshortWrite(): data type %d unsupported", mri->type));
  }

  /* ----- get the stem from the passed file name ----- */

/*

  four options:

    1. stem_xxx.bshort
       ^^^^

    2. stem.bshort
       ^^^^

    3. stem_xxx
       ^^^^

    4. stem
       ^^^^

other possibles:

    stem_.bshort

*/

  l = strlen(fname_passed);

  c1 = fname_passed + l - 11;
  c2 = fname_passed + l - 7;
  c3 = fname_passed + l - 4;

  strcpy(stem, fname_passed);

  if(c1 > fname_passed)
  {
    if(*c1 == '_' && strcmp(c1+4, ".bshort") == 0)
      stem[(int)(c1-fname_passed)] = '\0';
  }
  if(c2 > fname_passed)
  {
    if(strcmp(c2, ".bshort") == 0)
      stem[(int)(c2-fname_passed)] = '\0';
  }
  if(c3 > fname_passed)
  {
    if(*c3 == '_')
      stem[(int)(c3-fname_passed)] = '\0';
  }

  c = strrchr(stem, '/');
  if(c == NULL)
    output_dir[0] = '\0';
  else
  {
    od_length = (int)(c - stem);
    strncpy(output_dir, stem, od_length);
    /* -- leaving the trailing '/' on a directory is not my usual convention, but here it's a load easier if there's no directory in stem... -ch -- */
    output_dir[od_length] = '/';
    output_dir[od_length+1] = '\0';
  }

  sprintf(analyse_fname, "%s%s", output_dir, "analyse.dat");
  sprintf(register_fname, "%s%s", output_dir, "register.dat");

  buf = (short *)malloc(mri->width * mri->height * sizeof(short));

  for(i = 0;i < mri->depth;i++)
  {

    /* ----- write the header file ----- */
    sprintf(fname, "%s_%03d.hdr", stem, i);
    if((fp = fopen(fname, "w")) == NULL)
    {
      free(buf);
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "bshortWrite(): can't open file %s", fname));
    }
    fprintf(fp, "%d %d %d %d\n", mri->width, mri->height, mri->nframes, 0);
    fclose(fp);

    /* ----- write the data file ----- */
    sprintf(fname, "%s_%03d.bshort", stem, i);
    if((fp = fopen(fname, "w")) == NULL)
    {
      free(buf);
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "bshortWrite(): can't open file %s", fname));
    }

    for(t = 0;t < mri->nframes;t++)
    {
      for(j = 0;j < mri->height;j++)
      {

#ifdef Linux
        swab(mri->slices[t*mri->depth + i][j], buf, mri->width * sizeof(short));
#else
        memcpy(buf, mri->slices[t*mri->depth + i][j], mri->width * sizeof(short));
#endif

        fwrite(buf, sizeof(short), mri->width, fp);

      }

    }

    fclose(fp);

  }

  free(buf);

  sn = subject_name;
  if(mri->subject_name[0] != '\0')
    sn = mri->subject_name;

  if(sn != NULL)
  {
    if((subjects_dir = getenv("SUBJECTS_DIR")) == NULL)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): environment variable SUBJECTS_DIR unset");
    }
    else
    {

      sprintf(subject_dir, "%s/%s", subjects_dir, sn);
      if(stat(subject_dir, &stat_buf) < 0)
      {
        fprintf(stderr, "can't stat %s; writing to bhdr instead\n", subject_dir);
      }
      else
      {
        if(!S_ISDIR(stat_buf.st_mode))
        {
          fprintf(stderr, "%s is not a directory; writing to bhdr instead\n", subject_dir);
        }
        else
        {
          sprintf(subject_volume_dir, "%s/mri/T1", subject_dir);
          subject_info = MRIreadInfo(subject_volume_dir);
          if(subject_info == NULL)
          {
            sprintf(subject_volume_dir, "%s/mri/orig", subject_dir);
            subject_info = MRIreadInfo(subject_volume_dir);
            if(subject_info == NULL)
              fprintf(stderr, "can't read the subject's orig or T1 volumes; writing to bhdr instead\n");
          }
        }
      }

    }

  }

  if(subject_info != NULL)
  {
    if(subject_info->ras_good_flag == 0)
    {
      subject_info->x_r = -1.0;  subject_info->x_a = 0.0;  subject_info->x_s =  0.0;
      subject_info->y_r =  0.0;  subject_info->y_a = 0.0;  subject_info->y_s = -1.0;
      subject_info->z_r =  0.0;  subject_info->z_a = 1.0;  subject_info->z_s =  0.0;
      subject_info->c_r =  0.0;  subject_info->c_a = 0.0;  subject_info->c_s =  0.0;
    }
  }

  cf = bf = ibf = af = iaf = as = bs = cs = ics = r = NULL;
  r1 = r2 = r3 = r4 = NULL;

  /* ----- write the register.dat and analyse.dat  or bhdr files ----- */
  if(subject_info != NULL)
  {

    bad_flag = FALSE;

    if((as = MatrixAlloc(4, 4, MATRIX_REAL)) == NULL)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): error creating matrix");
      bad_flag = TRUE;
    }
    stuff_four_by_four(as, subject_info->x_r, subject_info->y_r, subject_info->z_r, subject_info->c_r,
                           subject_info->y_r, subject_info->y_r, subject_info->y_r, subject_info->c_r,
                           subject_info->z_r, subject_info->z_r, subject_info->z_r, subject_info->c_r,
                                         0.0,               0.0,               0.0,               1.0);

    if((af = MatrixAlloc(4, 4, MATRIX_REAL)) == NULL)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): error creating matrix");
      bad_flag = TRUE;
    }
    stuff_four_by_four(af, mri->x_r, mri->y_r, mri->z_r, mri->c_r,
                           mri->y_r, mri->y_r, mri->y_r, mri->c_r,
                           mri->z_r, mri->z_r, mri->z_r, mri->c_r,
                                0.0,      0.0,      0.0,      1.0);

    if((bs = MatrixAlloc(4, 4, MATRIX_REAL)) == NULL)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): error creating matrix");
      bad_flag = TRUE;
    }
    stuff_four_by_four(bs, 1, 0, 0, (subject_info->width  - 1) / 2.0, 
                           0, 1, 0, (subject_info->height - 1) / 2.0, 
                           0, 0, 1, (subject_info->depth  - 1) / 2.0, 
                           0, 0, 0,                              1.0);

    if((bf = MatrixAlloc(4, 4, MATRIX_REAL)) == NULL)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): error creating matrix");
      bad_flag = TRUE;
    }
    stuff_four_by_four(bf, 1, 0, 0, (mri->width  - 1) / 2.0, 
                           0, 1, 0, (mri->height - 1) / 2.0, 
                           0, 0, 1, (mri->depth  - 1) / 2.0, 
                           0, 0, 0,                     1.0);

    if((cs = MatrixAlloc(4, 4, MATRIX_REAL)) == NULL)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): error creating matrix");
      bad_flag = TRUE;
    }
    stuff_four_by_four(cs, -subject_info->xsize,                    0,                   0,  (subject_info->width  * mri->xsize) / 2.0,
                                              0,                    0, subject_info->zsize, -(subject_info->depth  * mri->zsize) / 2.0,
                                              0, -subject_info->ysize,                   0,  (subject_info->height * mri->ysize) / 2.0,
                                              0,                    0,                   0,                                          1);

    if((cf = MatrixAlloc(4, 4, MATRIX_REAL)) == NULL)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): error creating matrix");
      bad_flag = TRUE;
    }
    stuff_four_by_four(cf, -mri->xsize,           0,          0,  (mri->width  * mri->xsize) / 2.0,
                                     0,           0, mri->zsize, -(mri->depth  * mri->zsize) / 2.0,
                                     0, -mri->ysize,          0,  (mri->height * mri->ysize) / 2.0,
                                     0,           0,          0,                                 1);

    if(bad_flag)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): error creating one or more matrices; aborting register.dat write and writing bhdr instead");
      MRIfree(&subject_info);
    }

  }

  if(subject_info != NULL)
  {

    bad_flag = FALSE;

    if((det = MatrixDeterminant(as)) == 0.0)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): bad determinant in matrix (check structural volume)");
      bad_flag = TRUE;
    }
    if((det = MatrixDeterminant(bs)) == 0.0)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): bad determinant in matrix (check structural volume)");
      bad_flag = TRUE;
    }
    if((det = MatrixDeterminant(cs)) == 0.0)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): bad determinant in matrix (check structural volume)");
      bad_flag = TRUE;
    }

    if((det = MatrixDeterminant(af)) == 0.0)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): bad determinant in matrix (check functional volume)");
      bad_flag = TRUE;
    }
    if((det = MatrixDeterminant(bf)) == 0.0)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): bad determinant in matrix (check functional volume)");
      bad_flag = TRUE;
    }
    if((det = MatrixDeterminant(cf)) == 0.0)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): bad determinant in matrix (check functional volume)");
      bad_flag = TRUE;
    }

    if(bad_flag)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): one or more zero determinants; aborting register.dat write and writing bhdr instead");
      MRIfree(&subject_info);
    }

  }

  if(subject_info != NULL)
  {

    bad_flag = FALSE;

    if((iaf = MatrixInverse(af, NULL)) == NULL)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): error inverting matrix");
      bad_flag = TRUE;
    }
    if((ibf = MatrixInverse(bf, NULL)) == NULL)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): error inverting matrix");
      bad_flag = TRUE;
    }
    if((ics = MatrixInverse(cs, NULL)) == NULL)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): error inverting matrix");
      bad_flag = TRUE;
    }

    if(bad_flag)
    {
      ErrorPrintf(ERROR_BADPARM, "bshortWrite(): one or more zero determinants; aborting register.dat write and writing bhdr instead");
      MRIfree(&subject_info);
    }

  }

  bad_flag = FALSE;

  if(subject_info != NULL)
  {

    if((r1 = MatrixMultiply(bs, ics, NULL)) == NULL)
    {
      bad_flag = TRUE;
      MRIfree(&subject_info);
    }

  }

  if(subject_info != NULL)
  {

    if((r2 = MatrixMultiply(as, r1, NULL)) == NULL)
    {
      bad_flag = TRUE;
      MRIfree(&subject_info);
    }

  }

  if(subject_info != NULL)
  {

    if((r3 = MatrixMultiply(iaf, r2, NULL)) == NULL)
    {
      bad_flag = TRUE;
      MRIfree(&subject_info);
    }

  }

  if(subject_info != NULL)
  {

    if((r4 = MatrixMultiply(ibf, r3, NULL)) == NULL)
    {
      bad_flag = TRUE;
      MRIfree(&subject_info);
    }

  }

  if(subject_info != NULL)
  {

    if((r = MatrixMultiply(cf, r4, NULL)) == NULL)
    {
      bad_flag = TRUE;
      MRIfree(&subject_info);
    }

  }

  if(bad_flag)
  {
    ErrorPrintf(ERROR_BADPARM, "bshortWrite(): error during matrix multiplications; aborting register.dat write and writing bhdr instead");
  }

  if( as != NULL)  MatrixFree( &as);
  if( bs != NULL)  MatrixFree( &bs);
  if( cs != NULL)  MatrixFree( &cs);
  if( af != NULL)  MatrixFree( &af);
  if( bf != NULL)  MatrixFree( &bf);
  if( cf != NULL)  MatrixFree( &cf);
  if(iaf != NULL)  MatrixFree(&iaf);
  if(ibf != NULL)  MatrixFree(&ibf);
  if(ics != NULL)  MatrixFree(&ics);
  if( r1 != NULL)  MatrixFree( &r1);
  if( r2 != NULL)  MatrixFree( &r2);
  if( r3 != NULL)  MatrixFree( &r3);
  if( r4 != NULL)  MatrixFree( &r4);

  if(subject_info != NULL)
  {

    if(mri->path_to_t1[0] == '\0')
      sprintf(t1_path, ".");
    else
      strcpy(t1_path, mri->path_to_t1);

    if(FileExists(analyse_fname))
      fprintf(stderr, "warning: overwriting file %s\n", analyse_fname);

    if((fp = fopen(analyse_fname, "w")) == NULL)
    {
      MRIfree(&subject_info);
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "bshortWrite(): couldn't open file %s for writing", analyse_fname));
    }

    fprintf(fp, "%s\n", t1_path);
    fprintf(fp, "%s_%%03d.bshort\n", stem);
    fprintf(fp, "%d %d\n", mri->depth, mri->nframes);
    fprintf(fp, "%d %d\n", mri->width, mri->height);

    fclose(fp);

    if(FileExists(analyse_fname))
      fprintf(stderr, "warning: overwriting file %s\n", register_fname);

    if((fp = fopen(register_fname, "w")) == NULL)
    {
      MRIfree(&subject_info);
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "bshortWrite(): couldn't open file %s for writing", register_fname));
    }

    fprintf(fp, "%s\n", sn);
    fprintf(fp, "%g\n", mri->xsize);
    fprintf(fp, "%g\n", mri->zsize);
    fprintf(fp, "%g\n", 1.0);
    fprintf(fp, "%g %g %g %g\n", *MATRIX_RELT(r, 1, 1), *MATRIX_RELT(r, 1, 2), *MATRIX_RELT(r, 1, 3), *MATRIX_RELT(r, 1, 4));
    fprintf(fp, "%g %g %g %g\n", *MATRIX_RELT(r, 2, 1), *MATRIX_RELT(r, 2, 2), *MATRIX_RELT(r, 2, 3), *MATRIX_RELT(r, 2, 4));
    fprintf(fp, "%g %g %g %g\n", *MATRIX_RELT(r, 3, 1), *MATRIX_RELT(r, 3, 2), *MATRIX_RELT(r, 3, 3), *MATRIX_RELT(r, 3, 4));
    fprintf(fp, "%g %g %g %g\n", *MATRIX_RELT(r, 4, 1), *MATRIX_RELT(r, 4, 2), *MATRIX_RELT(r, 4, 3), *MATRIX_RELT(r, 4, 4));

    fclose(fp);

    MatrixFree(&r);

  }

  if(subject_info == NULL)
  {
    sprintf(fname, "%s.bhdr", stem);
    if((fp = fopen(fname, "w")) == NULL)
    {
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "bshortWrite(): can't open file %s", fname));
    }

    result = write_bhdr(mri, fp);

    fclose(fp);

    if(result != NO_ERROR)
      return(result);

  }
  else
    MRIfree(&subject_info);

  return(NO_ERROR);

} /* end bshortWrite() */

static int bfloatWrite(MRI *mri, char *stem)
{

  int i, j, t;
  char fname[STRLEN];
  float *buf;
  FILE *fp;
  int result;
#ifdef Linux
  char swap_buf[4];
  int pos;
  char *c;
#endif

  if(mri->type != MRI_FLOAT)
  {
    ErrorReturn(ERROR_UNSUPPORTED, (ERROR_UNSUPPORTED, "bfloatWrite(): data type %d unsupported", mri->type));
  }

  buf = (float *)malloc(mri->width * mri->height * sizeof(float));

  for(i = 0;i < mri->depth;i++)
  {

    /* ----- write the header file ----- */
    sprintf(fname, "%s_%03d.hdr", stem, i);
    if((fp = fopen(fname, "r")) == NULL)
    {
      free(buf);
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "bfloatWrite(): can't open file %s", fname));
    }
    fprintf(fp, "%d %d %d %d\n", mri->width, mri->height, mri->nframes, 0);
    fclose(fp);

    /* ----- write the data file ----- */
    sprintf(fname, "%s_%03d.bfloat", stem, i);
    if((fp = fopen(fname, "r")) == NULL)
    {
      free(buf);
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "bfloatWrite(): can't open file %s", fname));
    }

    for(t = 0;t < mri->nframes;t++)
    {
      for(j = 0;j < mri->height;j++)
      {

        memcpy(buf, mri->slices[t*mri->depth + i][j], mri->width * sizeof(float));
#ifdef Linux
        for(pos = 0;pos < mri->width * sizeof(float);mri->width += sizeof(float))
        {
          c = (char *)&(buf[pos]);
          memcpy(swap_buf, &c, 4);
          c[0] = swap_buf[3];
          c[1] = swap_buf[2];
          c[2] = swap_buf[1];
          c[3] = swap_buf[0];
        }
#endif

        fwrite(buf, sizeof(float), mri->width, fp);

      }

    }

    fclose(fp);

  }

  free(buf);

  /* ----- write the bhdr file ----- */
  sprintf(fname, "%s.bhdr", stem);
  if((fp = fopen(fname, "r")) == NULL)
  {
    ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "bfloatWrite(): can't open file %s", fname));
  }

  result = write_bhdr(mri, fp);

  fclose(fp);

  return(result);

} /* end bfloatWrite() */

static MRI *get_b_info(char *fname_passed, int read_volume, char *directory, char *stem, int type)
{

  MRI *mri, *mri2;
  FILE *fp;
  float ipr, st, intensity;
  float m11, m12, m13, m14;
  float m21, m22, m23, m24;
  float m31, m32, m33, m34;
  float m41, m42, m43, m44;
  char t1_path[STRLEN];
  char fname_descrip[STRLEN];
  int nslices, nt;
  int nx, ny;
  float fov_x, fov_y, fov_z;
  MATRIX *m;
  int res;
  int result;
  char register_fname[STRLEN], analyse_fname[STRLEN];
  char sn[STRLEN];
  char fname[STRLEN];
  char extension[STRLEN];
/*
  char bhdr_name[STRLEN];
*/

  if(type == MRI_SHORT)
    sprintf(extension, "bshort");
  else if(type == MRI_FLOAT)
    sprintf(extension, "bfloat");
  else
  {
    ErrorReturn(NULL, (ERROR_UNSUPPORTED, "internal error: get_b_info() passed type %d", type));
  }

  result = decompose_b_fname(fname_passed, directory, stem);
  if(result != NO_ERROR)
    return(NULL);

  if(directory[0] == '\0')
    sprintf(directory, ".");

  /* ----- try register.dat and analyse.dat, then bhdr, then defaults ----- */

  sprintf(register_fname, "%s/register.dat", directory);
  sprintf(analyse_fname, "%s/analyse.dat", directory);

  if((fp = fopen(register_fname, "r")) != NULL)
  {

    fscanf(fp, "%s", sn);
    fscanf(fp, "%f", &ipr);
    fscanf(fp, "%f", &st);
    fscanf(fp, "%f", &intensity);
    fscanf(fp, "%f %f %f %f", &m11, &m12, &m13, &m14);
    fscanf(fp, "%f %f %f %f", &m21, &m22, &m23, &m24);
    fscanf(fp, "%f %f %f %f", &m31, &m32, &m33, &m34);
    fscanf(fp, "%f %f %f %f", &m41, &m42, &m43, &m44);
    fclose(fp);

    if((fp = fopen(analyse_fname, "r")) != NULL)
    {

      fscanf(fp, "%s", t1_path);
      fscanf(fp, "%s", fname_descrip);
      fscanf(fp, "%d %d", &nslices, &nt);
      fscanf(fp, "%d %d", &nx, &ny);
      fclose(fp);

      if(read_volume)
      {
        mri = MRIallocSequence(nx, ny, nslices, MRI_SHORT, nt);
      }
      else
      {
        mri = MRIallocHeader(nx, ny, nslices, MRI_SHORT);
        mri->nframes = nt;
      }

      strcpy(mri->fname, fname_passed);
      mri->slice_direction = MRI_UNDEFINED;
      mri->imnr0 = 1;
      mri->imnr1 = nslices;
      mri->xsize = mri->ysize = mri->ps = ipr;
      mri->zsize = mri->thick = st;

      fov_x = mri->xsize * mri->width;
      fov_y = mri->ysize * mri->height;
      fov_z = mri->zsize * mri->depth;

      mri->fov = (fov_x > fov_y ? (fov_x > fov_z ? fov_x : fov_z) : (fov_y > fov_z ? fov_y : fov_z));

      mri->xend = fov_x / 2.0;
      mri->xstart = -mri->xend;
      mri->yend = fov_y / 2.0;
      mri->ystart = -mri->yend;
      mri->zend = fov_z / 2.0;
      mri->zstart = -mri->zend;

      mri->brightness = intensity;
      strcpy(mri->subject_name, sn);
      strcpy(mri->path_to_t1, t1_path);
      strcpy(mri->fname_format, fname_descrip);

      m = MatrixAlloc(4, 4, MATRIX_REAL);
      if(m == NULL)
      {
        MRIfree(&mri);
        ErrorReturn(NULL, (ERROR_NO_MEMORY, "error allocating matrix in %s read", extension));
      }

      stuff_four_by_four(m, m11, m12, m13, m14, 
                            m21, m22, m23, m24, 
                            m31, m32, m33, m34, 
                            m41, m42, m43, m44);

      mri->register_mat = m;

      res = orient_with_register(mri);
      if(res != NO_ERROR)
      {
        MRIfree(&mri);
        return(NULL);
      }

      return(mri);

    }

  }


  /* ----- now try reading the bhdr ----- */

  mri = MRIallocHeader(1, 1, 1, type);
/*
  sprintf(bhdr_name, "%s/%s.bhdr", directory, stem);
  if((fp = fopen(bhdr_name, "r")) != NULL)
  {
    read_bhdr(mri, fp);
  }
*/
  if(FALSE)
  {
  }
  else  /* ----- failed -- defaults ----- */
  {

    fprintf(stderr, "can't find %s.bhdr or register.dat and analyse.dat; using defaults\n", stem);

    sprintf(fname, "%s/%s_000.hdr", directory, stem);
    if((fp = fopen(fname, "r")) == NULL)
    {
      MRIfree(&mri);
      ErrorReturn(NULL, (ERROR_BADFILE, "can't find file %s (last resort); bailing out on read", fname));
    }
    fscanf(fp, "%d %d %d %*d", &nx, &ny, &nt);
    fclose(fp);

    /* --- get the number of slices --- */

    
    sprintf(fname, "%s/%s_000.%s", directory, stem, extension);
    if(!FileExists(fname))
    {
      MRIfree(&mri);
      ErrorReturn(NULL, (ERROR_BADFILE, "can't find file %s; bailing out on read", fname));
    }
    for(nslices = 0;FileExists(fname);nslices++)
      sprintf(fname, "%s/%s_%03d.%s", directory, stem, nslices, extension);

    nslices--;

    mri->width   = nx;
    mri->height  = ny;
    mri->depth   = nslices;
    mri->nframes = nt;

    mri->imnr0 = 1;
    mri->imnr1 = nslices;
    mri->slice_direction = MRI_HORIZONTAL;

    mri->thick = mri->ps = 1.0;
    mri->xsize = mri->ysize = mri->zsize = 1.0;

    mri->xend = mri->width  * mri->xsize / 2.0;
    mri->xstart = -mri->xend;
    mri->yend = mri->height * mri->ysize / 2.0;
    mri->ystart = -mri->yend;
    mri->zend = mri->depth  * mri->zsize / 2.0;
    mri->zstart = -mri->zend;

    mri->fov = ((mri->xend - mri->xstart) > (mri->yend - mri->ystart) ? (mri->xend - mri->xstart) : (mri->yend - mri->ystart));

    mri->ras_good_flag = 0;
    strcpy(mri->fname, fname_passed);

  }

  if(read_volume)
  {
    mri2 = MRIallocSequence(mri->width, mri->height, mri->depth, mri->type, mri->nframes);
    MRIcopyHeader(mri, mri2);
    MRIfree(&mri);
    mri = mri2;
  }

  return(mri);

} /* end get_b_info() */

static MRI *bshortRead(char *fname_passed, int read_volume)
{

  MRI *mri;
  FILE *fp;
  char fname[STRLEN];
  char directory[STRLEN];
  char stem[STRLEN];
  int swap_bytes_flag;
  int i, j;

  mri = get_b_info(fname_passed, read_volume, directory, stem, MRI_SHORT);
  if(mri == NULL)
    return(NULL);

  if(read_volume)
  {

    sprintf(fname, "%s/%s_%03d.hdr", directory, stem, 0);
    if((fp = fopen(fname, "r")) == NULL)
    {
      fprintf(stderr, "can't open file %s; assuming big-endian bshorts\n", fname);
      swap_bytes_flag = 0;
    }
    else
    {
      fscanf(fp, "%*d %*d %*d %d", &swap_bytes_flag);
#ifdef Linux
      swap_bytes_flag = !swap_bytes_flag;
#endif
      fclose(fp);
    }

    for(i = 0;i < mri->depth;i++)
    {

      sprintf(fname, "%s/%s_%03d.bshort", directory, stem, i);

      if((fp = fopen(fname, "r")) == NULL)
      {
        MRIfree(&mri);
        ErrorReturn(NULL, (ERROR_BADFILE, "bshortRead(): error opening file %s", fname));
      }

      for(j = 0;j < mri->height;j++)
      {
        if(fread(mri->slices[i][j], sizeof(short), mri->width, fp) != mri->width)
        {
          fclose(fp);
          MRIfree(&mri);
          ErrorReturn(NULL, (ERROR_BADFILE, "bshortRead(): error reading from file %s", fname));
        }
        if(swap_bytes_flag)
          swab(mri->slices[i][j], mri->slices[i][j], mri->width * sizeof(short));
      }

      fclose(fp);

    }

  }

  return(mri);

} /* end bshortRead() */

static int orient_with_register(MRI *mri)
{

  MRI *subject_mri;
  char *subjects_dir;
  char subject_directory[STRLEN];
  MATRIX *sa, *fa;
  MATRIX *sr, *fr;
  MATRIX *rinv, *sainv;
  MATRIX *r1, *r2;
  int res;
  float det;

  subject_mri = NULL;
  if((subjects_dir = getenv("SUBJECTS_DIR")) != NULL)
  {
    sprintf(subject_directory, "%s/%s/mri/T1", subjects_dir, mri->subject_name);
    if((subject_mri = MRIreadInfo(subject_directory)) == NULL)
    {
      ErrorPrintf(ERROR_BADPARM, "can't get get information from %s; ", subject_directory);
      sprintf(subject_directory, "%s/%s/mri/T1", subjects_dir, mri->subject_name);
      ErrorPrintf(ERROR_BADPARM, "trying %s instead...\n", subject_directory);
      if((subject_mri = MRIreadInfo(subject_directory)) == NULL)
        ErrorPrintf(ERROR_BADPARM, "can't get information from %s\n", subject_directory);
    }
  }
  else
    ErrorPrintf(ERROR_BADPARM, "can't get environment variable SUBJECTS_DIR");

  if(subject_mri == NULL)
  {

    ErrorPrintf(ERROR_BADPARM, "guessing at COR- orientation...\n");

    subject_mri = MRIallocHeader(256, 256, 256, MRI_UCHAR);
    subject_mri->fov = 256.0;
    subject_mri->thick = subject_mri->ps = 1.0;
    subject_mri->xsize = subject_mri->ysize = subject_mri->zsize = 1.0;
    subject_mri->x_r = -1.0;  subject_mri->x_a =  0.0;  subject_mri->x_s =  0.0;
    subject_mri->y_r =  0.0;  subject_mri->y_a =  0.0;  subject_mri->y_s = -1.0;
    subject_mri->z_r =  0.0;  subject_mri->z_a =  1.0;  subject_mri->z_s =  0.0;
    subject_mri->c_r =  0.0;  subject_mri->c_a =  0.0;  subject_mri->c_s =  0.0;
    subject_mri->ras_good_flag = 1;

  }

  det = MatrixDeterminant(mri->register_mat);
  if(det == 0.0)
  {
    MRIfree(&subject_mri);
    ErrorPrintf(ERROR_BADPARM, "orient_with_register(): registration matrix has zero determinant");
    ErrorPrintf(ERROR_BADPARM, "matrix is:");
    MatrixPrint(stderr, mri->register_mat);
    return(ERROR_BADPARM);
  }

  rinv = MatrixInverse(mri->register_mat, NULL);

  if(rinv == NULL)
  {
    MRIfree(&subject_mri);
    ErrorPrintf(ERROR_BADPARM, "orient_with_register(): error inverting registration matrix");
    ErrorPrintf(ERROR_BADPARM, "matrix is:");
    MatrixPrint(stderr, mri->register_mat);
    return(ERROR_BADPARM);
  }

  sr = extract_i_to_r(subject_mri);

  sa = MatrixAlloc(4, 4, MATRIX_REAL);
  fa = MatrixAlloc(4, 4, MATRIX_REAL);

  if(sr == NULL || sa == NULL || fa == NULL)
  {
    if(sr != NULL)
      MatrixFree(&sr);
    if(sa != NULL)
      MatrixFree(&sa);
    if(fa != NULL)
      MatrixFree(&fa);
    MatrixFree(&rinv);
    MRIfree(&subject_mri);
    ErrorReturn(ERROR_NO_MEMORY, (ERROR_NO_MEMORY, "orient_with_register(): error allocating matrix"));
  }

  stuff_four_by_four(sa, -subject_mri->xsize, 0.0, 0.0, subject_mri->width * subject_mri->xsize / 2.0,
                         0.0, 0.0, subject_mri->zsize, -subject_mri->depth * subject_mri->zsize / 2.0,
                         0.0, -subject_mri->ysize, 0.0, subject_mri->height * subject_mri->ysize / 2.0,
                         0.0, 0.0, 0.0, 1.0);

  MRIfree(&subject_mri);

  stuff_four_by_four(fa, -mri->xsize, 0.0, 0.0, mri->width * mri->xsize / 2.0,
                         0.0, 0.0, mri->zsize, -mri->depth * mri->zsize / 2.0,
                         0.0, -mri->ysize, 0.0, mri->height * mri->ysize / 2.0,
                         0.0, 0.0, 0.0, 1.0);
  det = MatrixDeterminant(sa);
  if(det == 0.0)
  {
    MatrixFree(&sr);
    MatrixFree(&sa);
    MatrixFree(&fa);
    MatrixFree(&rinv);
    ErrorPrintf(ERROR_BADPARM, "orient_with_register(): destination (ijk) -> r space matrix has zero determinant");
    ErrorPrintf(ERROR_BADPARM, "matrix is:");
    MatrixPrint(stderr, sa);
    return(ERROR_BADPARM);
  }

  sainv = MatrixInverse(sa, NULL);
  MatrixFree(&sa);

  if(sainv == NULL)
  {
    MatrixFree(&sr);
    MatrixFree(&sa);
    MatrixFree(&fa);
    MatrixFree(&rinv);
    ErrorPrintf(ERROR_BADPARM, "orient_with_register(): error inverting destination (ijk) -> r space matrix");
    ErrorPrintf(ERROR_BADPARM, "matrix is:");
    MatrixPrint(stderr, sainv);
    return(ERROR_BADPARM);
  }

  r1 = MatrixMultiply(rinv, fa, NULL);
  MatrixFree(&rinv);
  MatrixFree(&fa);
  if(r1 == NULL)
  {
    MatrixFree(&sr);
    MatrixFree(&sa);
    MatrixFree(&sainv);
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "orient_with_register(): error multiplying matrices"));
  }

  r2 = MatrixMultiply(sainv, r1, NULL);
  MatrixFree(&r1);
  MatrixFree(&sainv);
  if(r2 == NULL)
  {
    MatrixFree(&sr);
    MatrixFree(&sa);
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "orient_with_register(): error multiplying matrices"));
  }

  fr = MatrixMultiply(sr, r2, NULL);
  MatrixFree(&sr);
  MatrixFree(&r2);
  if(fr == NULL)
  {
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "orient_with_register(): error multiplying matrices"));
  }

  res = apply_i_to_r(mri, fr);
  MatrixFree(&fr);

  if(res != NO_ERROR)
    return(res);

  return(NO_ERROR);

} /* end orient_with_register() */

static int decompose_b_fname(char *fname, char *dir, char *stem)
{

  char *slash, *dot, *stem_start, *underscore;
  int fname_length;
  int und_pos;
  char fname_copy[STRLEN];

  /*

  options for file names:

  stem_xxx.bshort
  stem_xxx
  stem_.bshort
  stem_
  stem.bshort
  stem

  with or without a preceding directory

  */

  strcpy(fname_copy, fname);

  slash = strrchr(fname_copy, '/');
  dot = strrchr(fname_copy, '.');
  underscore = strrchr(fname_copy, '_');

  if(slash == NULL)
  {
    stem_start = fname_copy;
    sprintf(dir, ".");
  }
  else
  {
    *slash = '\0';
    strcpy(dir, fname_copy);
    stem_start = slash + 1;
  }

  if(*stem_start == '\0')
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "decompose_b_fname(): bad bshort/bfloat file specifier \"%s\"", fname));

  if(dot != NULL)
    if(strcmp(dot, ".bshort") == 0 || strcmp(dot, ".bfloat") == 0)
      *dot = '\0';

  fname_length = strlen(stem_start);

  if(underscore == NULL)
    strcpy(stem, stem_start);
  else
  {
    und_pos = (underscore - stem_start);
    if(und_pos == fname_length - 1 || und_pos == fname_length - 4)
      *underscore = '\0';
    strcpy(stem, stem_start);
  }

  return(NO_ERROR);

} /* end decompose_b_fname() */

static MRI *bfloatRead(char *fname_passed, int read_volume)
{

  MRI *mri;
  FILE *fp;
  char fname[STRLEN];
  char directory[STRLEN];
  char stem[STRLEN];
  int swap_bytes_flag;
  int i, j, k;

  mri = get_b_info(fname_passed, read_volume, directory, stem, MRI_FLOAT);
  if(mri == NULL)
    return(NULL);

  if(read_volume)
  {

    sprintf(fname, "%s/%s_%03d.hdr", directory, stem, 0);
    if((fp = fopen(fname, "r")) == NULL)
    {
      fprintf(stderr, "can't open file %s; assuming big-endian bshorts\n", fname);
      swap_bytes_flag = 0;
    }
    else
    {
      fscanf(fp, "%*d %*d %*d %d", &swap_bytes_flag);
#ifdef Linux
      swap_bytes_flag = !swap_bytes_flag;
#endif
      fclose(fp);
    }

    for(i = 0;i < mri->depth;i++)
    {

      sprintf(fname, "%s/%s_%03d.bfloat", directory, stem, i);

      if((fp = fopen(fname, "r")) == NULL)
      {
        MRIfree(&mri);
        ErrorReturn(NULL, (ERROR_BADFILE, "bfloatRead(): error opening file %s", fname));
      }

      for(j = 0;j < mri->height;j++)
      {
        if(fread(mri->slices[i][j], sizeof(float), mri->width, fp) != mri->width)
        {
          fclose(fp);
          MRIfree(&mri);
          ErrorReturn(NULL, (ERROR_BADFILE, "bfloatRead(): error reading from file %s", fname));
        }
        if(swap_bytes_flag)
        {
          for(k = 0;k < mri->depth;k++)
            mri->slices[i][j][k] = swapFloat(mri->slices[i][j][k]);
        }
      }

      fclose(fp);

    }

  }

  return(mri);

} /* end bfloatRead() */

static int write_bhdr(MRI *mri, FILE *fp)
{

  time_t time_now;
  float tlr, tla, tls; /* top left coordinates */
  float trr, tra, trs; /* top right coordinates */
  float brr, bra, brs; /* bottom right coordinates */
  float nr, na, ns;    /* normal coordinates */
  float vl;            /* vector length */
  float cr, ca, cs;    /* first slice center coordinates */

  /* ----- normalize this just in case ----- */
  vl = sqrt(mri->z_r*mri->z_r + mri->z_a*mri->z_a + mri->z_s*mri->z_s);
  nr = mri->z_r / vl;
  na = mri->z_a / vl;
  ns = mri->z_s / vl;

  cr = mri->c_r - (mri->depth - 1) / 2.0 * mri->z_r * mri->zsize;
  ca = mri->c_a - (mri->depth - 1) / 2.0 * mri->z_a * mri->zsize;
  cs = mri->c_s - (mri->depth - 1) / 2.0 * mri->z_s * mri->zsize;

  tlr = cr - mri->width / 2.0 * mri->x_r * mri->xsize - mri->height / 2.0 * mri->y_r * mri->ysize;
  tla = ca - mri->width / 2.0 * mri->x_a * mri->xsize - mri->height / 2.0 * mri->y_a * mri->ysize;
  tls = cs - mri->width / 2.0 * mri->x_s * mri->xsize - mri->height / 2.0 * mri->y_s * mri->ysize;

  trr = cr + mri->width / 2.0 * mri->x_r * mri->xsize - mri->height / 2.0 * mri->y_r * mri->ysize;
  tra = ca + mri->width / 2.0 * mri->x_a * mri->xsize - mri->height / 2.0 * mri->y_a * mri->ysize;
  trs = cs + mri->width / 2.0 * mri->x_s * mri->xsize - mri->height / 2.0 * mri->y_s * mri->ysize;

  brr = cr + mri->width / 2.0 * mri->x_r * mri->xsize + mri->height / 2.0 * mri->y_r * mri->ysize;
  bra = ca + mri->width / 2.0 * mri->x_a * mri->xsize + mri->height / 2.0 * mri->y_a * mri->ysize;
  brs = cs + mri->width / 2.0 * mri->x_s * mri->xsize + mri->height / 2.0 * mri->y_s * mri->ysize;

  time(&time_now);

  fprintf(fp, "bhdr generated by %s\n", Progname);
  fprintf(fp, "%s\n", ctime(&time_now));
  fprintf(fp, "\n");
  fprintf(fp, "          cols: %d\n", mri->width);
  fprintf(fp, "          rows: %d\n", mri->height);
  fprintf(fp, "       nslices: %d\n", mri->depth);
  fprintf(fp, " n_time_points: %d\n", mri->nframes);
  fprintf(fp, "   slice_thick: %g\n", mri->zsize);
  fprintf(fp, "    top_left_r: %g\n", tlr);
  fprintf(fp, "    top_left_a: %g\n", tla);
  fprintf(fp, "    top_left_s: %g\n", tls);
  fprintf(fp, "   top_right_r: %g\n", trr);
  fprintf(fp, "   top_right_a: %g\n", tra);
  fprintf(fp, "   top_right_s: %g\n", trs);
  fprintf(fp, "bottom_right_r: %g\n", brr);
  fprintf(fp, "bottom_right_a: %g\n", bra);
  fprintf(fp, "bottom_right_s: %g\n", brs);
  fprintf(fp, "      normal_r: %g\n", nr);
  fprintf(fp, "      normal_a: %g\n", na);
  fprintf(fp, "      normal_s: %g\n", ns);
  fprintf(fp, "      image_te: %g\n", mri->te);
  fprintf(fp, "      image_tr: %g\n", mri->tr);
  fprintf(fp, "      image_ti: %g\n", mri->ti);

  return(NO_ERROR);

} /* end write_bhdr() */

#ifdef READ_BHDR
int read_bhdr(MRI *mri, FILE *fp)
{

  char line[STRLEN];
  char *l;
  float tlr, tla, tls; /* top left coordinates */
  float trr, tra, trs; /* top right coordinates */
  float brr, bra, brs; /* bottom right coordinates */
  float xr, xa, xs;
  float yr, ya, ys;

  while(!feof(fp))
  {

    /* --- read the line --- */
    fgets(line, STRLEN, fp);

    /* --- remove the newline --- */
    if(line[strlen(line)-1] == '\n')
      line[strlen(line)-1] = '\0';

    /* --- skip the initial spaces --- */
    for(l = line;isspace((int)(*l));l++);

    /* --- get the varible name and value(s) --- */
    if(strlen(l) > 0)
    {
      if(strncmp(l, "cols: ", 6) == 0)
      {
        sscanf(l, "%*s %d", &mri->width);
      }
      else if(strncmp(l, "rows: ", 6) == 0)
      {
        sscanf(l, "%*s %d", &mri->height);
      }
      else if(strncmp(l, "nslices: ", 9) == 0)
      {
        sscanf(l, "%*s %d", &mri->depth);
      }
      else if(strncmp(l, "n_time_points: ", 15) == 0)
      {
        sscanf(l, "%*s %d", &mri->nframes);
      }
      else if(strncmp(l, "slice_thick: ", 13) == 0)
      {
        sscanf(l, "%*s %f", &mri->zsize);
      }
      else if(strncmp(l, "image_te: ", 10) == 0)
      {
        sscanf(l, "%*s %f", &mri->te);
      }
      else if(strncmp(l, "image_tr: ", 10) == 0)
      {
        sscanf(l, "%*s %f", &mri->tr);
      }
      else if(strncmp(l, "image_ti: ", 10) == 0)
      {
        sscanf(l, "%*s %f", &mri->ti);
      }
      else if(strncmp(l, "top_left_r: ", 12) == 0)
      {
        sscanf(l, "%*s %g", &tlr);
      }
      else if(strncmp(l, "top_left_a: ", 12) == 0)
      {
        sscanf(l, "%*s %g", &tla);
      }
      else if(strncmp(l, "top_left_s: ", 12) == 0)
      {
        sscanf(l, "%*s %g", &tls);
      }
      else if(strncmp(l, "top_right_r: ", 13) == 0)
      {
        sscanf(l, "%*s %g", &trr);
      }
      else if(strncmp(l, "top_right_a: ", 13) == 0)
      {
        sscanf(l, "%*s %g", &tra);
      }
      else if(strncmp(l, "top_right_s: ", 13) == 0)
      {
        sscanf(l, "%*s %g", &trs);
      }
      else if(strncmp(l, "bottom_right_r: ", 16) == 0)
      {
        sscanf(l, "%*s %g", &brr);
      }
      else if(strncmp(l, "bottom_right_a: ", 16) == 0)
      {
        sscanf(l, "%*s %g", &bra);
      }
      else if(strncmp(l, "bottom_right_s: ", 16) == 0)
      {
        sscanf(l, "%*s %g", &brs);
      }
      else if(strncmp(l, "normal_r: ", 10) == 0)
      {
        sscanf(l, "%*s %g", &mri->z_r);
      }
      else if(strncmp(l, "normal_a: ", 10) == 0)
      {
        sscanf(l, "%*s %g", &mri->z_a);
      }
      else if(strncmp(l, "normal_s: ", 10) == 0)
      {
        sscanf(l, "%*s %g", &mri->z_s);
      }
      else
      {
        /* --- ignore it --- */
      }
    }

  }

  xr = (trr - tlr) / mri->width;
  xa = (tra - tla) / mri->width;
  xs = (trs - tls) / mri->width;
  mri->xsize = sqrt(xr*xr + xa*xa + xs*xs);
  mri->x_r = xr / mri->xsize;
  mri->x_a = xa / mri->xsize;
  mri->x_s = xs / mri->xsize;

  yr = (brr - trr) / mri->height;
  ya = (bra - tra) / mri->height;
  ys = (brs - trs) / mri->height;
  mri->ysize = sqrt(yr*yr + ya*ya + ys*ys);
  mri->y_r = yr / mri->ysize;
  mri->y_a = ya / mri->ysize;
  mri->y_s = ys / mri->ysize;

  mri->c_r = (brr - tlr) / 2.0 + (mri->depth - 1) / 2.0 * mri->z_r * mri->zsize;
  mri->c_a = (bra - tla) / 2.0 + (mri->depth - 1) / 2.0 * mri->z_a * mri->zsize;
  mri->c_s = (brs - tls) / 2.0 + (mri->depth - 1) / 2.0 * mri->z_s * mri->zsize;

  mri->ras_good_flag = 1;

  mri->slice_direction = MRI_UNDEFINED;

  return(NO_ERROR);

} /* end read_bhdr() */
#endif

static MRI *genesisRead(char *fname, int read_volume)
{

  char fname_format[STRLEN];
  char fname_dir[STRLEN];
  char fname_base[STRLEN];
  char *c;
  MRI *mri = NULL;
  int im_init;
  int im_low, im_high;
  char fname_use[STRLEN];
  char temp_string[STRLEN];
  FILE *fp;
  int width, height;
  int pixel_data_offset;
  int image_header_offset;
  float tl_r, tl_a, tl_s;
  float tr_r, tr_a, tr_s;
  float br_r, br_a, br_s;
  float c_r, c_a, c_s;
  float n_r, n_a, n_s;
  float xlength, ylength, zlength;
  int i, y;
  MRI *header;
  float xfov, yfov, zfov;
  float nlength;

  /* ----- check the first (passed) file ----- */
  if(!FileExists(fname))
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "genesisRead(): error opening file %s", fname));
  }

  /* ----- split the file name into name and directory ----- */
  c = strrchr(fname, '/');
  if(c == NULL)
  {
    fname_dir[0] = '\0';
    strcpy(fname_base, fname);
  }
  else
  {
    strncpy(fname_dir, fname, (c - fname + 1));
    fname_dir[c-fname+1] = '\0';
    strcpy(fname_base, c+1);
  }

  /* ----- derive the file name format (for sprintf) ----- */
  if(strncmp(fname_base, "I.", 2) == 0)
  {
    im_init = atoi(&fname_base[2]);
    sprintf(fname_format, "I.%%03d");
  }
  else if(strlen(fname_base) >= 3) /* avoid core dumps below... */
  {
    c = &fname_base[strlen(fname_base)-3];
    if(strcmp(c, ".MR") == 0)
    {
      *c = '\0';
      for(c--;isdigit(*c) && c >= fname_base;c--);
      c++;
      im_init = atoi(c);
      *c = '\0';
      sprintf(fname_format, "%s%%d.MR", fname_base);
    }
    else
    {
      ErrorReturn(NULL, (ERROR_BADPARM, "genesisRead(): can't determine file name format for %s", fname));
    }
  }
  else
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "genesisRead(): can't determine file name format for %s", fname));
  }

  strcpy(temp_string, fname_format);
  sprintf(fname_format, "%s%s", fname_dir, temp_string);

  /* ----- find the low and high files ----- */
  im_low = im_init;
  do
  {
    im_low--;
    sprintf(fname_use, fname_format, im_low);
  } while(FileExists(fname_use));
  im_low++;

  im_high = im_init;
  do
  {
    im_high++;
    sprintf(fname_use, fname_format, im_high);
  } while(FileExists(fname_use));
  im_high--;

  /* ----- allocate the mri structure ----- */
  header = MRIallocHeader(1, 1, 1, MRI_SHORT);

  header->depth = im_high - im_low + 1;
  header->imnr0 = 1;
  header->imnr1 = header->depth;

  /* ----- get the header information from the first file ----- */
  sprintf(fname_use, fname_format, im_low);
  if((fp = fopen(fname_use, "r")) == NULL)
  {
    MRIfree(&header);
    ErrorReturn(NULL, (ERROR_BADFILE, "genesisRead(): error opening file %s\n", fname_use));
  }

  fseek(fp, 8, SEEK_SET);
  fread(&width, 4, 1, fp);  width = orderIntBytes(width);
  fread(&height, 4, 1, fp);  height = orderIntBytes(height);
  fseek(fp, 148, SEEK_SET);
  fread(&image_header_offset, 4, 1, fp);  image_header_offset = orderIntBytes(image_header_offset);

  header->width = width;
  header->height = height;

  strcpy(header->fname, fname);

  fseek(fp, image_header_offset + 26, SEEK_SET);
  fread(&(header->thick), 4, 1, fp);  header->thick = orderFloatBytes(header->thick);
  header->zsize = header->thick;

  fseek(fp, image_header_offset + 50, SEEK_SET);
  fread(&(header->xsize), 4, 1, fp);  header->xsize = orderFloatBytes(header->xsize);
  fread(&(header->ysize), 4, 1, fp);  header->ysize = orderFloatBytes(header->ysize);
  header->ps = header->xsize;

  fseek(fp, image_header_offset + 130, SEEK_SET);
  fread(&c_r,  4, 1, fp);  c_r  = orderFloatBytes(c_r);
  fread(&c_a,  4, 1, fp);  c_a  = orderFloatBytes(c_a);
  fread(&c_s,  4, 1, fp);  c_s  = orderFloatBytes(c_s);
  fread(&n_r,  4, 1, fp);  n_r  = orderFloatBytes(n_r);
  fread(&n_a,  4, 1, fp);  n_a  = orderFloatBytes(n_a);
  fread(&n_s,  4, 1, fp);  n_s  = orderFloatBytes(n_s);
  fread(&tl_r, 4, 1, fp);  tl_r = orderFloatBytes(tl_r);
  fread(&tl_a, 4, 1, fp);  tl_a = orderFloatBytes(tl_a);
  fread(&tl_s, 4, 1, fp);  tl_s = orderFloatBytes(tl_s);
  fread(&tr_r, 4, 1, fp);  tr_r = orderFloatBytes(tr_r);
  fread(&tr_a, 4, 1, fp);  tr_a = orderFloatBytes(tr_a);
  fread(&tr_s, 4, 1, fp);  tr_s = orderFloatBytes(tr_s);
  fread(&br_r, 4, 1, fp);  br_r = orderFloatBytes(br_r);
  fread(&br_a, 4, 1, fp);  br_a = orderFloatBytes(br_a);
  fread(&br_s, 4, 1, fp);  br_s = orderFloatBytes(br_s);

  nlength = sqrt(n_r*n_r + n_a*n_a + n_s*n_s);
  n_r = n_r / nlength;
  n_a = n_a / nlength;
  n_s = n_s / nlength;

  header->x_r = (tr_r - tl_r);  header->x_a = (tr_a - tl_a);  header->x_s = (tr_s - tl_s);
  header->y_r = (br_r - tr_r);  header->y_a = (br_a - tr_a);  header->y_s = (br_s - tr_s);

  /* --- normalize -- the normal vector from the file should have length 1, but just in case... --- */
  xlength = sqrt(header->x_r*header->x_r + header->x_a*header->x_a + header->x_s*header->x_s);
  ylength = sqrt(header->y_r*header->y_r + header->y_a*header->y_a + header->y_s*header->y_s);
  zlength = sqrt(n_r*n_r + n_a*n_a + n_s*n_s);

  header->x_r = header->x_r / xlength;  header->x_a = header->x_a / xlength;  header->x_s = header->x_s / xlength;
  header->y_r = header->y_r / ylength;  header->y_a = header->y_a / ylength;  header->y_s = header->y_s / ylength;
  header->z_r = n_r / zlength;          header->z_a = n_a / zlength;          header->z_s = n_s / zlength;

  header->c_r = (tl_r + br_r) / 2.0 + n_r * header->zsize * (header->depth - 1.0) / 2.0;
  header->c_a = (tl_a + br_a) / 2.0 + n_a * header->zsize * (header->depth - 1.0) / 2.0;
  header->c_s = (tl_s + br_s) / 2.0 + n_s * header->zsize * (header->depth - 1.0) / 2.0;

  header->ras_good_flag = 1;

  header->xend = header->xsize * header->width / 2;   header->xstart = -header->xend;
  header->yend = header->ysize * header->height / 2;  header->ystart = -header->yend;
  header->zend = header->zsize * header->depth / 2;   header->zstart = -header->zend;

  xfov = header->xend - header->xstart;
  yfov = header->yend - header->ystart;
  zfov = header->zend - header->zstart;

  header->fov = ( xfov > yfov ? (xfov > zfov ? xfov : zfov ) : (yfov > zfov ? yfov : zfov ) );

  fclose(fp);

  if(read_volume)
    mri = MRIalloc(header->width, header->height, header->depth, header->type);
  else
    mri = MRIallocHeader(header->width, header->height, header->depth, header->type);

  MRIcopyHeader(header, mri);
  MRIfree(&header);

  /* ----- read the volume if required ----- */
  if(read_volume)
  {

    for(i = im_low;i <= im_high;i++)
    {

      sprintf(fname_use, fname_format, i);
      if((fp = fopen(fname_use, "r")) == NULL)
      {
        MRIfree(&mri);
        ErrorReturn(NULL, (ERROR_BADFILE, "genesisRead(): error opening file %s", fname_use));
      }

      fseek(fp, 4, SEEK_SET);
      fread(&pixel_data_offset, 4, 1, fp);  pixel_data_offset = orderIntBytes(pixel_data_offset);
      fseek(fp, pixel_data_offset, SEEK_SET);

      for(y = 0;y < mri->height;y++)
      {
        if(fread(mri->slices[i-im_low][y], 2, mri->width, fp) != mri->width)
        {
          fclose(fp);
          MRIfree(&mri);
          ErrorReturn(NULL, (ERROR_BADFILE, "genesisRead(): error reading from file file %s", fname_use));
        }
#ifdef Linux
        swab(mri->slices[i-im_low][y], mri->slices[i-im_low][y], 2 * mri->width);
#endif
      }

      fclose(fp);

    }

  }

  return(mri);

} /* end genesisRead() */

static MRI *gelxRead(char *fname, int read_volume)
{

  char fname_format[STRLEN];
  char fname_dir[STRLEN];
  char fname_base[STRLEN];
  char *c;
  MRI *mri = NULL;
  int im_init;
  int im_low, im_high;
  char fname_use[STRLEN];
  char temp_string[STRLEN];
  FILE *fp;
  int width, height;
  float tl_r, tl_a, tl_s;
  float tr_r, tr_a, tr_s;
  float br_r, br_a, br_s;
  float c_r, c_a, c_s;
  float n_r, n_a, n_s;
  float xlength, ylength, zlength;
  int i, y;
  int ecount, scount, icount;
  int good_flag;
  MRI *header;
  float xfov, yfov, zfov;

  /* ----- check the first (passed) file ----- */
  if(!FileExists(fname))
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "genesisRead(): error opening file %s", fname));
  }

  /* ----- split the file name into name and directory ----- */
  c = strrchr(fname, '/');
  if(c == NULL)
  {
    fname_dir[0] = '\0';
    strcpy(fname_base, fname);
  }
  else
  {
    strncpy(fname_dir, fname, (c - fname + 1));
    fname_dir[c-fname+1] = '\0';
    strcpy(fname_base, c+1);
  }

  ecount = scount = icount = 0;
  good_flag = TRUE;
  for(c = fname_base;*c != '\0';c++)
  {
    if(*c == 'e')
      ecount++;
    else if(*c == 's')
      scount++;
    else if(*c == 'i')
      icount++;
    else if(!isdigit(*c))
      good_flag = FALSE;
  }
  if(good_flag && ecount == 1 && scount == 1 && icount == 1)
  {
    c = strrchr(fname_base, 'i');
    im_init = atoi(c+1);
    *c = '\0';
    sprintf(fname_format, "%si%%d", fname_base);
  }
  else
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "genesisRead(): can't determine file name format for %s", fname));
  }

  strcpy(temp_string, fname_format);
  sprintf(fname_format, "%s%s", fname_dir, temp_string);

  /* ----- find the low and high files ----- */
  im_low = im_init;
  do
  {
    im_low--;
    sprintf(fname_use, fname_format, im_low);
  } while(FileExists(fname_use));
  im_low++;

  im_high = im_init;
  do
  {
    im_high++;
    sprintf(fname_use, fname_format, im_high);
  } while(FileExists(fname_use));
  im_high--;

  /* ----- allocate the mri structure ----- */
  header = MRIallocHeader(1, 1, 1, MRI_SHORT);

  header->depth = im_high - im_low + 1;
  header->imnr0 = 1;
  header->imnr1 = header->depth;

  /* ----- get the header information from the first file ----- */
  sprintf(fname_use, fname_format, im_low);
  if((fp = fopen(fname_use, "r")) == NULL)
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "genesisRead(): error opening file %s\n", fname_use));
  }

  fseek(fp, 3236, SEEK_SET);
  fread(&width, 4, 1, fp);  width = orderIntBytes(width);
  fread(&height, 4, 1, fp);  height = orderIntBytes(height);
  header->width = width;
  header->height = height;

  strcpy(header->fname, fname);

  fseek(fp, 2184 + 28, SEEK_SET);
  fread(&(header->thick), 4, 1, fp);  header->thick = orderFloatBytes(header->thick);
  header->zsize = header->thick;

  fseek(fp, 2184 + 52, SEEK_SET);
  fread(&(header->xsize), 4, 1, fp);  header->xsize = orderFloatBytes(header->xsize);
  fread(&(header->ysize), 4, 1, fp);  header->ysize = orderFloatBytes(header->ysize);
  header->ps = header->xsize;

  fseek(fp, 2184 + 136, SEEK_SET);
  fread(&c_r,  4, 1, fp);  c_r  = orderFloatBytes(c_r);
  fread(&c_a,  4, 1, fp);  c_a  = orderFloatBytes(c_a);
  fread(&c_s,  4, 1, fp);  c_s  = orderFloatBytes(c_s);
  fread(&n_r,  4, 1, fp);  n_r  = orderFloatBytes(n_r);
  fread(&n_a,  4, 1, fp);  n_a  = orderFloatBytes(n_a);
  fread(&n_s,  4, 1, fp);  n_s  = orderFloatBytes(n_s);
  fread(&tl_r, 4, 1, fp);  tl_r = orderFloatBytes(tl_r);
  fread(&tl_a, 4, 1, fp);  tl_a = orderFloatBytes(tl_a);
  fread(&tl_s, 4, 1, fp);  tl_s = orderFloatBytes(tl_s);
  fread(&tr_r, 4, 1, fp);  tr_r = orderFloatBytes(tr_r);
  fread(&tr_a, 4, 1, fp);  tr_a = orderFloatBytes(tr_a);
  fread(&tr_s, 4, 1, fp);  tr_s = orderFloatBytes(tr_s);
  fread(&br_r, 4, 1, fp);  br_r = orderFloatBytes(br_r);
  fread(&br_a, 4, 1, fp);  br_a = orderFloatBytes(br_a);
  fread(&br_s, 4, 1, fp);  br_s = orderFloatBytes(br_s);

  header->x_r = (tr_r - tl_r);  header->x_a = (tr_a - tl_a);  header->x_s = (tr_s - tl_s);
  header->y_r = (br_r - tr_r);  header->y_a = (br_a - tr_a);  header->y_s = (br_s - tr_s);

  /* --- normalize -- the normal vector from the file should have length 1, but just in case... --- */
  xlength = sqrt(header->x_r*header->x_r + header->x_a*header->x_a + header->x_s*header->x_s);
  ylength = sqrt(header->y_r*header->y_r + header->y_a*header->y_a + header->y_s*header->y_s);
  zlength = sqrt(n_r*n_r + n_a*n_a + n_s*n_s);

  header->x_r = header->x_r / xlength;  header->x_a = header->x_a / xlength;  header->x_s = header->x_s / xlength;
  header->y_r = header->y_r / ylength;  header->y_a = header->y_a / ylength;  header->y_s = header->y_s / ylength;
  header->z_r = n_r / zlength;          header->z_a = n_a / zlength;          header->z_s = n_s / zlength;

  header->c_r = (tl_r + br_r) / 2.0 + n_r * header->zsize * (header->depth - 1.0) / 2.0;
  header->c_a = (tl_a + br_a) / 2.0 + n_a * header->zsize * (header->depth - 1.0) / 2.0;
  header->c_s = (tl_s + br_s) / 2.0 + n_s * header->zsize * (header->depth - 1.0) / 2.0;

  header->ras_good_flag = 1;

  header->xend = header->xsize * header->width / 2;   header->xstart = -header->xend;
  header->yend = header->ysize * header->height / 2;  header->ystart = -header->yend;
  header->zend = header->zsize * header->depth / 2;   header->zstart = -header->zend;

  xfov = header->xend - header->xstart;
  yfov = header->yend - header->ystart;
  zfov = header->zend - header->zstart;

  header->fov = ( xfov > yfov ? (xfov > zfov ? xfov : zfov ) : (yfov > zfov ? yfov : zfov ) );
 
  fclose(fp);

  if(read_volume)
    mri = MRIalloc(header->width, header->height, header->depth, MRI_SHORT);
  else
    mri = MRIallocHeader(header->width, header->height, header->depth, MRI_SHORT);

  MRIcopyHeader(header, mri);
  MRIfree(&header);

  /* ----- read the volume if required ----- */
  if(read_volume)
  {

    for(i = im_low;i <= im_high;i++)
    {

      sprintf(fname_use, fname_format, i);
      if((fp = fopen(fname_use, "r")) == NULL)
      {
        MRIfree(&mri);
        ErrorReturn(NULL, (ERROR_BADFILE, "genesisRead(): error opening file %s", fname_use));
      }

      fseek(fp, 8432, SEEK_SET);

      for(y = 0;y < mri->height;y++)
      {
        if(fread(mri->slices[i-im_low][y], 2, mri->width, fp) != mri->width)
        {
          fclose(fp);
          MRIfree(&mri);
          ErrorReturn(NULL, (ERROR_BADFILE, "genesisRead(): error reading from file file %s", fname_use));
        }
#ifdef Linux
        swab(mri->slices[i-im_low][y], mri->slices[i-im_low][y], 2 * mri->width);
#endif
      }

      fclose(fp);

    }

  }

  return(mri);

} /* end gelxRead() */

static MRI *analyzeRead(char *fname, int read_volume)
{

  MRI *mri = NULL;
  FILE *fp;
  char hdr_fname[STRLEN];
  char mat_fname[STRLEN];
  char *c;
  dsr hdr;
  int dtype;
  int flip_flag = 0;
  int i, j, k;
  float dx, dy, dz;
  int nread;
  unsigned char *buf;
  int bytes_per_voxel;
  int bufsize;
  MATRIX *m;
  MATRIX *center_index_mat;
  MATRIX *center_ras_mat;
  float xfov, yfov, zfov;

  c = strrchr(fname, '.');
  if(c == NULL)
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "analyzeRead(): bad file name %s", fname));
  }
  if(strcmp(c, ".img") != 0)
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "analyzeRead(): bad file name %s", fname));
  }

  strcpy(hdr_fname, fname);
  sprintf(hdr_fname + (c - fname), ".hdr");

  strcpy(mat_fname, fname);
  sprintf(mat_fname + (c - fname), ".mat");

  if((fp = fopen(hdr_fname, "r")) == NULL)
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "read_analyze_header(): error opening file %s", fname));
  }
  fread(&hdr, sizeof(hdr), 1, fp);
  fclose(fp);

  if(hdr.hk.sizeof_hdr != sizeof(hdr))
  {
    flip_flag = 1;
    swap_analyze_header(&hdr);
  }

  if(hdr.dime.datatype == DT_UNSIGNED_CHAR)
  {
    dtype = MRI_UCHAR;
    bytes_per_voxel = 1;
  }
  else if(hdr.dime.datatype == DT_SIGNED_SHORT)
  {
    dtype = MRI_SHORT;
    bytes_per_voxel = 2;
  }
  else if(hdr.dime.datatype == DT_SIGNED_INT)
  {
    dtype = MRI_INT;
    bytes_per_voxel = 4;
  }
  else if(hdr.dime.datatype == DT_FLOAT)
  {
    dtype = MRI_FLOAT;
    bytes_per_voxel = 4;
  }
  else if(hdr.dime.datatype == DT_DOUBLE)
  {
    dtype = MRI_FLOAT;
    bytes_per_voxel = 8;
  }
  else
  {
    ErrorReturn(NULL, (ERROR_UNSUPPORTED, "analyzeRead: unsupported data type %d", hdr.dime.datatype));
  }

  /* ----- allocate the mri structure ----- */
  if(read_volume)
    mri = MRIalloc(hdr.dime.dim[1], hdr.dime.dim[2], hdr.dime.dim[3], dtype);
  else
    mri = MRIalloc(hdr.dime.dim[1], hdr.dime.dim[2], hdr.dime.dim[3], dtype);

  mri->xsize = hdr.dime.pixdim[1];
  mri->ysize = hdr.dime.pixdim[2];
  mri->zsize = hdr.dime.pixdim[3];
  mri->thick = mri->zsize;
  mri->ps = mri->xsize;

  mri->xend = mri->width * mri->xsize / 2.0;   mri->xstart = -mri->xend;
  mri->yend = mri->height * mri->ysize / 2.0;  mri->ystart = -mri->yend;
  mri->zend = mri->depth * mri->zsize / 2.0;   mri->zstart = -mri->zend;

  xfov = mri->xend - mri->xstart;
  yfov = mri->yend - mri->ystart;
  zfov = mri->zend - mri->zstart;

  mri->fov = ( xfov > yfov ? (xfov > zfov ? xfov : zfov ) : (yfov > zfov ? yfov : zfov ) );

  /* --- default (no .mat file) --- */
  mri->x_r =  1.0;  mri->x_a = 0.0;  mri->x_s = 0.0;
  mri->y_r =  0.0;  mri->y_a = 1.0;  mri->y_s = 0.0;
  mri->z_r =  0.0;  mri->z_a = 0.0;  mri->z_s = 1.0;

  /* --- originator gives the voxel index of (r, a, s) = (0, 0, 0) --- */
  dx = (mri->width  - 1.0) / 2. - (float)(((short *)hdr.hist.originator)[0]);
  dy = (mri->height - 1.0) / 2. - (float)(((short *)hdr.hist.originator)[1]);
  dz = (mri->depth  - 1.0) / 2. - (float)(((short *)hdr.hist.originator)[2]);

  mri->c_r = (dx * mri->x_r) + (dy * mri->y_r) + (dz * mri->z_r);
  mri->c_a = (dx * mri->x_a) + (dy * mri->y_a) + (dz * mri->z_a);
  mri->c_s = (dx * mri->x_s) + (dy * mri->y_s) + (dz * mri->z_s);

  mri->ras_good_flag = 1;

  strcpy(mri->fname, fname);

  if(read_volume)
  {

    if((fp = fopen(fname, "r")) == NULL)
    {
      MRIfree(&mri);
      ErrorReturn(NULL, (ERROR_BADFILE, "analyzeRead: error opening file %s", fname));
    }

    fseek(fp, (int)(hdr.dime.vox_offset), SEEK_SET);

    bufsize = mri->width * bytes_per_voxel;
    buf = (unsigned char *)malloc(bufsize);

    for(k = 0;k < mri->depth;k++)
    {
      for(j = 0;j < mri->height;j++)
      {

        nread = fread(buf, bytes_per_voxel, mri->width, fp);
        if(nread != mri->width)
        {
          free(buf);
          fclose(fp);
          ErrorReturn(NULL, (ERROR_BADFILE, "analyzeRead: error reading from file %s\n", fname));
        }

        if(flip_flag)
          nflip(buf, bytes_per_voxel, mri->width);


        for(i = 0;i < mri->width;i++)
        {
          if(hdr.dime.datatype == DT_UNSIGNED_CHAR)
            MRIvox(mri, i, j, k) = buf[i];
          if(hdr.dime.datatype == DT_SIGNED_SHORT)
            MRISvox(mri, i, j, k) = ((short *)buf)[i];
          if(hdr.dime.datatype == DT_SIGNED_INT)
            MRIIvox(mri, i, j, k) = ((int *)buf)[i];
          if(hdr.dime.datatype == DT_FLOAT)
            MRIFvox(mri, i, j, k) = ((float *)buf)[i];
          if(hdr.dime.datatype == DT_DOUBLE)
            MRIFvox(mri, i, j, k) = (float)(((double *)buf)[i]);
        }

      }
    }

  free(buf);
  fclose(fp);

  }

  /* ----- read mat file ----- */
  if(FileExists(mat_fname))
  {

    m = MatlabRead(mat_fname);

    if(m == NULL)
    {
      MRIfree(&mri);
      return(NULL);
    }

    if(m->rows != 4 || m->cols != 4)
    {
      MRIfree(&mri);
      ErrorReturn(NULL, (ERROR_BADFILE, "analyzeRead(): not a 4 by 4 matrix in file %s", mat_fname));
    }

    mri->x_r = *MATRIX_RELT(m, 1, 1);  mri->y_r = *MATRIX_RELT(m, 1, 2);  mri->z_r = *MATRIX_RELT(m, 1, 3);
    mri->x_a = *MATRIX_RELT(m, 2, 1);  mri->y_a = *MATRIX_RELT(m, 2, 2);  mri->z_a = *MATRIX_RELT(m, 2, 3);
    mri->x_s = *MATRIX_RELT(m, 3, 1);  mri->y_s = *MATRIX_RELT(m, 3, 2);  mri->z_s = *MATRIX_RELT(m, 3, 3);

    mri->xsize = sqrt(mri->x_r * mri->x_r + mri->x_a * mri->x_a + mri->x_s * mri->x_s);
    mri->ysize = sqrt(mri->y_r * mri->y_r + mri->y_a * mri->y_a + mri->y_s * mri->y_s);
    mri->zsize = sqrt(mri->z_r * mri->z_r + mri->z_a * mri->z_a + mri->z_s * mri->z_s);

    mri->x_r = mri->x_r / mri->xsize;  mri->x_a = mri->x_a / mri->xsize;  mri->x_s = mri->x_s / mri->xsize;
    mri->y_r = mri->y_r / mri->ysize;  mri->y_a = mri->y_a / mri->ysize;  mri->y_s = mri->y_s / mri->ysize;
    mri->z_r = mri->z_r / mri->zsize;  mri->z_a = mri->z_a / mri->zsize;  mri->z_s = mri->z_s / mri->zsize;

    center_index_mat = MatrixAlloc(4, 1, MATRIX_REAL);

    /* --- matlab matrices start at 1, so the middle index is [(width, height, depth)+(1, 1, 1)]/2, (not -) --- */
    *MATRIX_RELT(center_index_mat, 1, 1) = (mri->width + 1.0) / 2.0;
    *MATRIX_RELT(center_index_mat, 2, 1) = (mri->height + 1.0) / 2.0;
    *MATRIX_RELT(center_index_mat, 3, 1) = (mri->depth + 1.0) / 2.0;
    *MATRIX_RELT(center_index_mat, 4, 1) = 1.0;

    center_ras_mat = MatrixMultiply(m, center_index_mat, NULL);
    if(center_ras_mat == NULL)
    {

      ErrorPrintf(ERROR_BADPARM, "multiplying: m * cim:\n");
      ErrorPrintf(ERROR_BADPARM, "m = \n");
      MatrixPrint(stderr, m);
      ErrorPrintf(ERROR_BADPARM, "cim = \n");
      MatrixPrint(stderr, center_index_mat);

      MatrixFree(&m);
      MatrixFree(&center_index_mat);
      MatrixFree(&center_ras_mat);
      MRIfree(&mri);
      ErrorReturn(NULL, (ERROR_BADPARM, "analyzeRead(): error in matrix multiplication"));


    }

    mri->c_r = *MATRIX_RELT(center_ras_mat, 1, 1);
    mri->c_a = *MATRIX_RELT(center_ras_mat, 2, 1);
    mri->c_s = *MATRIX_RELT(center_ras_mat, 3, 1);

    MatrixFree(&m);
    MatrixFree(&center_index_mat);
    MatrixFree(&center_ras_mat);

  }

  return(mri);

} /* end analyzeRead() */

static int analyzeWrite(MRI *mri, char *fname)
{

  dsr hdr;
  float max, min;
  MATRIX *m;
  MATRIX *index;
  MATRIX *ras;
  MATRIX *minv;
  char hdr_fname[STRLEN];
  char mat_fname[STRLEN];
  char *c;
  FILE *fp;
  int error_value;
  int i, j;
  int bytes_per_voxel;

  c = strrchr(fname, '.');
  if(c == NULL)
  {
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "analyzeRead(): bad file name %s", fname));
  }
  if(strcmp(c, ".img") != 0)
  {
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "analyzeRead(): bad file name %s", fname));
  }

  strcpy(hdr_fname, fname);
  sprintf(hdr_fname + (c - fname), ".hdr");

  strcpy(mat_fname, fname);
  sprintf(mat_fname + (c - fname), ".mat");

  memset(&hdr, 0x00, sizeof(hdr));

  hdr.hk.sizeof_hdr = sizeof(hdr);

  hdr.dime.vox_offset = 0.0;

  if(mri->type == MRI_UCHAR)
  {
    hdr.dime.datatype = DT_UNSIGNED_CHAR;
    bytes_per_voxel = 1;
  }
  else if(mri->type == MRI_SHORT)
  {
    hdr.dime.datatype = DT_SIGNED_SHORT;
    bytes_per_voxel = 2;
  }
  /* --- assuming long and int are identical --- */
  else if(mri->type == MRI_INT || mri->type == MRI_LONG)
  {
    hdr.dime.datatype = DT_SIGNED_INT;
    bytes_per_voxel = 4;
  }
  else if(mri->type == MRI_FLOAT)
  {
    hdr.dime.datatype = DT_FLOAT;
    bytes_per_voxel = 4;
  }
  else
  {
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "analyzeWrite(): bad data type %d", mri->type));
  }

  hdr.dime.dim[1] = mri->width;
  hdr.dime.dim[2] = mri->height;
  hdr.dime.dim[3] = mri->depth;

  hdr.dime.pixdim[1] = mri->xsize;
  hdr.dime.pixdim[2] = mri->ysize;
  hdr.dime.pixdim[3] = mri->zsize;

  MRIlimits(mri, &min, &max);
  hdr.dime.glmin = (int)min;
  hdr.dime.glmax = (int)max;

  m = MatrixAlloc(4, 4, MATRIX_REAL);

  *MATRIX_RELT(m, 1, 1) = mri->x_r * mri->xsize;  *MATRIX_RELT(m, 1, 2) = mri->y_r * mri->ysize;  *MATRIX_RELT(m, 1, 3) = mri->z_r * mri->zsize;  *MATRIX_RELT(m, 1, 4) = mri->c_r;
  *MATRIX_RELT(m, 2, 1) = mri->x_a * mri->xsize;  *MATRIX_RELT(m, 2, 2) = mri->y_a * mri->ysize;  *MATRIX_RELT(m, 2, 3) = mri->z_a * mri->zsize;  *MATRIX_RELT(m, 2, 4) = mri->c_a;
  *MATRIX_RELT(m, 3, 1) = mri->x_s * mri->xsize;  *MATRIX_RELT(m, 3, 2) = mri->y_s * mri->ysize;  *MATRIX_RELT(m, 3, 3) = mri->z_s * mri->zsize;  *MATRIX_RELT(m, 3, 4) = mri->c_s;
  *MATRIX_RELT(m, 4, 1) = 0.0;                    *MATRIX_RELT(m, 4, 2) = 0.0;                    *MATRIX_RELT(m, 4, 3) = 0.0;                    *MATRIX_RELT(m, 4, 4) = 1.0;

  ras = MatrixAlloc(4, 1, MATRIX_REAL);

  *MATRIX_RELT(ras, 1, 1) = 0.0;
  *MATRIX_RELT(ras, 2, 1) = 0.0;
  *MATRIX_RELT(ras, 3, 1) = 0.0;
  *MATRIX_RELT(ras, 4, 1) = 1.0;

  minv = MatrixInverse(m, NULL);
  if(minv == NULL)
  {
    ErrorPrintf(ERROR_BADPARM, "analyzeWrite(): error inverting matrix\n");
    MatrixPrint(stdout, m);
    MatrixFree(&m);
    MatrixFree(&ras);
    return(ERROR_BADPARM);
  }

  index = MatrixMultiply(minv, ras, NULL);
  if(minv == NULL)
  {
    MatrixFree(&m);
    MatrixFree(&ras);
    MatrixFree(&minv);
    return(ERROR_BADPARM);
  }

  /* --- matlab matrices start at index 1; hence the +1 --- */
/* width-1, height-1, depth-1 hacks */
  ((short *)hdr.hist.originator)[0] = (int)(*MATRIX_RELT(index, 1, 1) + ((mri->width-1) / 2.0)) + 1;
  ((short *)hdr.hist.originator)[1] = (int)(*MATRIX_RELT(index, 2, 1) + ((mri->height-1) / 2.0)) + 1;
  ((short *)hdr.hist.originator)[2] = (int)(*MATRIX_RELT(index, 3, 1) + ((mri->depth-1) / 2.0)) + 1;

  /* --- solve 0 = m*orig element by element --- */
  *MATRIX_RELT(m, 1, 4) = -(*MATRIX_RELT(m, 1, 1) * ((short *)hdr.hist.originator)[0] + *MATRIX_RELT(m, 1, 2) * ((short *)hdr.hist.originator)[1] + *MATRIX_RELT(m, 1, 3) * ((short *)hdr.hist.originator)[2]);
  *MATRIX_RELT(m, 2, 4) = -(*MATRIX_RELT(m, 2, 1) * ((short *)hdr.hist.originator)[0] + *MATRIX_RELT(m, 2, 2) * ((short *)hdr.hist.originator)[1] + *MATRIX_RELT(m, 2, 3) * ((short *)hdr.hist.originator)[2]);
  *MATRIX_RELT(m, 3, 4) = -(*MATRIX_RELT(m, 3, 1) * ((short *)hdr.hist.originator)[0] + *MATRIX_RELT(m, 3, 2) * ((short *)hdr.hist.originator)[1] + *MATRIX_RELT(m, 3, 3) * ((short *)hdr.hist.originator)[2]);

  MatrixFree(&ras);
  MatrixFree(&minv);
  MatrixFree(&index);

  /* ----- write the header ----- */
  if((fp = fopen(hdr_fname, "w")) == NULL)
  {
    MatrixFree(&m);
    ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "analyzeWrite(): error opening file %s for writing", hdr_fname));
  }
  if(fwrite(&hdr, sizeof(hdr), 1, fp) != 1)
  {
    MatrixFree(&m);
    ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "analyzeWrite(): error writing to file %s", hdr_fname));
  }
  fclose(fp);

  /* ----- write the .mat file ----- */
  error_value = MatlabWrite(m, mat_fname, "M");
  MatrixFree(&m);
  if(error_value != NO_ERROR)
  {
    return(error_value);
  }

  /* ----- write the data ----- */
  if((fp = fopen(fname, "w")) == NULL)
  {
    ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "analyzeWrite(): error opening file %s for writing", fname));
  }

  for(i = 0;i < mri->depth;i++)
  {
    for(j = 0;j < mri->height;j++)
    {
      if(fwrite(mri->slices[i][j], bytes_per_voxel, mri->width, fp) != mri->width)
      {
        ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "analyzeWrite(): error writing to file %s", fname));
      }
    }
  }

  fclose(fp);

  return(0);

} /* end analyzeWrite() */

static void swap_analyze_header(dsr *hdr)
{

  int i;
  char c;

  hdr->hk.sizeof_hdr = swapInt(hdr->hk.sizeof_hdr);
  hdr->hk.extents = swapShort(hdr->hk.extents);
  hdr->hk.session_error = swapShort(hdr->hk.session_error);

  for(i = 0;i < 5;i++)
    hdr->dime.dim[i] = swapShort(hdr->dime.dim[i]);
  hdr->dime.unused1 = swapShort(hdr->dime.unused1);
  hdr->dime.datatype = swapShort(hdr->dime.datatype);
  hdr->dime.bitpix = swapShort(hdr->dime.bitpix);
  hdr->dime.dim_un0 = swapShort(hdr->dime.dim_un0);
  hdr->dime.vox_offset = swapFloat(hdr->dime.vox_offset);
  hdr->dime.roi_scale = swapFloat(hdr->dime.roi_scale);
  hdr->dime.funused1 = swapFloat(hdr->dime.funused1);
  hdr->dime.funused2 = swapFloat(hdr->dime.funused2);
  hdr->dime.cal_max = swapFloat(hdr->dime.cal_max);
  hdr->dime.cal_min = swapFloat(hdr->dime.cal_min);
  hdr->dime.compressed = swapInt(hdr->dime.compressed);
  hdr->dime.verified = swapInt(hdr->dime.verified);
  hdr->dime.glmin = swapInt(hdr->dime.glmin);
  hdr->dime.glmax = swapInt(hdr->dime.glmax);
  for(i = 0;i < 8;i++)
    hdr->dime.pixdim[i] = swapFloat(hdr->dime.pixdim[i]);

  hdr->hist.views = swapInt(hdr->hist.views);
  hdr->hist.vols_added = swapInt(hdr->hist.vols_added);
  hdr->hist.start_field = swapInt(hdr->hist.start_field);
  hdr->hist.field_skip = swapInt(hdr->hist.field_skip);
  hdr->hist.omax = swapInt(hdr->hist.omax);
  hdr->hist.omin = swapInt(hdr->hist.omin);
  hdr->hist.smax = swapInt(hdr->hist.smax);
  hdr->hist.smin = swapInt(hdr->hist.smin);

  /* spm uses the originator char[10] as shorts */
  for(i = 0;i < 5;i++)
  {
    c = hdr->hist.originator[2*i+1];
    hdr->hist.originator[2*i+1] = hdr->hist.originator[2*i];
    hdr->hist.originator[2*i] = c;
  }

} /* end swap_analyze_header */

static MRI *afniRead(char *fname, int read_volume)
{

  FILE *fp;
  char header_fname[STRLEN];
  char *c;
  MRI *mri, *header;
  int big_endian_flag;
  long brik_file_length;
  long nvoxels;
  int bytes_per_voxel;
  int i, j, k;
  int swap_flag;
  int nframes;

  strcpy(header_fname, fname);
  c = strrchr(header_fname, '.');

  if(c == NULL)
  {
      ErrorReturn(NULL, (ERROR_BADPARM, "afniRead(): bad file name %s", fname));
  }

  if(strcmp(c, ".BRIK") != 0)
  {
      ErrorReturn(NULL, (ERROR_BADPARM, "afniRead(): bad file name %s", fname));
  }

  sprintf(c, ".HEAD");

  if((fp = fopen(header_fname, "r")) == NULL)
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "afniRead(): error opening file %s", header_fname));
  }

  if((header = read_afni_header(fp, &big_endian_flag)) == NULL)
    return(NULL);

  fclose(fp);

#ifdef Linux
  swap_flag = big_endian_flag;
#else
  swap_flag = !big_endian_flag;
#endif

  if(header->nframes != 1)
  {
    nframes = header->nframes;
    MRIfree(&header);
    ErrorReturn(NULL, (ERROR_UNSUPPORTED, "afniRead(): nframes = %d (only 1 frame supported)", nframes));
  }

  if((fp = fopen(fname, "r")) == NULL)
  {
    MRIfree(&header);
    ErrorReturn(NULL, (ERROR_BADFILE, "afniRead(): error opening file %s", header_fname));
  }

  fseek(fp, 0, SEEK_END);
  brik_file_length = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  nvoxels = header->width * header->height * header->depth * header->nframes;

  if(brik_file_length % nvoxels)
  {
    fclose(fp);
    MRIfree(&header);
    ErrorReturn(NULL, (ERROR_BADFILE, "afniRead(): BRIK file length (%d) is not divisible by the number of voxels (%d)", brik_file_length, nvoxels));
  }

  bytes_per_voxel = brik_file_length / nvoxels;

  if(bytes_per_voxel == 1)
    header->type = MRI_UCHAR;
  else if(bytes_per_voxel == 2)
    header->type = MRI_SHORT;
  else if(bytes_per_voxel == 4)
    header->type = MRI_FLOAT;
  else
  {
    fclose(fp);
    MRIfree(&header);
    ErrorReturn(NULL, (ERROR_UNSUPPORTED, "afniRead(): don't know what to do with %d bytes per voxel", bytes_per_voxel));
  }

  if(read_volume)
  {

    mri = MRIalloc(header->width, header->height, header->depth, header->type);
    MRIcopyHeader(header, mri);

    for(k = 0;k < mri->depth;k++)
    {
      for(j = 0;j < mri->height;j++)
      {

        if(fread(mri->slices[k][j], bytes_per_voxel, mri->width, fp) != mri->width)
        {
          fclose(fp);
          MRIfree(&header);
          ErrorReturn(NULL, (ERROR_BADFILE, "afniRead(): error reading from file %s", fname));
        }

        if(swap_flag)
        {
          if(mri->type == MRI_SHORT)
          {
            swab(mri->slices[k][j], mri->slices[k][j], mri->width * 2);
          }
          if(mri->type == MRI_FLOAT)
          {
            for(i = 0;i < mri->width;i++)
              MRIFvox(mri, i, j, k) = swapFloat(MRIFvox(mri, i, j, k));
          }
        }

      }
    }

  }
  else
    mri = MRIcopy(header, NULL);

  strcpy(mri->fname, fname);
  
  fclose(fp);

  MRIfree(&header);

  return(mri);

} /* end afniRead() */

/* ----- flags for keeping track of what we've gotten from the header ----- */
#define AFNI_ALL_REQUIRED       0x0000003f

#define ORIENT_SPECIFIC_FLAG    0x00000001
#define BRICK_TYPES_FLAG        0x00000002
#define DATASET_DIMENSIONS_FLAG 0x00000004
#define DELTA_FLAG              0x00000008
#define ORIGIN_FLAG             0x00000010
#define BYTEORDER_STRING_FLAG   0x00000020

static MRI *read_afni_header(FILE *fp, int *big_endian_flag)
{

  char line[STRLEN], line2[STRLEN];
  int i, j;
  char type[STRLEN], name[STRLEN];
  int count;
  char *s;
  float *f;
  int *ip;
  MRI *mri;
  float det;
  float origin[3];
  long gotten = 0;
  float xfov, yfov, zfov;

  *big_endian_flag = 1;

  fseek(fp, 0, SEEK_SET);

  mri = MRIallocHeader(1, 1, 1, MRI_UCHAR);

  while(!feof(fp))
  {

    fgets(line, STRLEN, fp);

    if(!feof(fp))
    {

      i = -1;
      j = -1;
      do
      {
        i++;
        j++;
        for(;isspace(line[j]) && line[j] != '\0';j++);
        line2[i] = line[j];
      } while(line[j] != '\0');

      if(strlen(line2) > 0)
        if(line2[strlen(line2)-1] == '\n')
          line2[strlen(line2)-1] = '\0';

      if(strncmp(line2, "type=", 5) == 0)
        strcpy(type, &line2[5]);
      if(strncmp(line2, "name=", 5) == 0)
        strcpy(name, &line2[5]);
      if(strncmp(line2, "count=", 5) == 0)
      {

        count = atoi(&line2[6]);

        s = NULL;
        f = NULL;
        ip = NULL;

        if(strncmp(type, "string-attribute", 16) == 0)
        {
          s = get_afni_string(fp, count, name);
          if(s == NULL)
          {
            MRIfree(&mri);
            return(NULL);
          }
        }
        else if(strncmp(type, "float-attribute", 15) == 0)
        {
          f = get_afni_float(fp, count, name);
          if(f == NULL)
          {
            MRIfree(&mri);
            return(NULL);
          }
        }
        else if(strncmp(type, "integer-attribute", 17) == 0)
        {
          ip = get_afni_int(fp, count, name);
          if(ip == NULL)
          {
            MRIfree(&mri);
            return(NULL);
          }
        }
        else
        {
          MRIfree(&mri);
          ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): unknown type %s", type));
        }

        if(strcmp(name, "ORIENT_SPECIFIC") == 0)
        {
          if(strncmp(type, "integer-attribute", 17) != 0)
          {
            MRIfree(&mri);
            ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): variable %s listed as %s (expecting integer-attribute)", name, type));
          }
          if(count < 3)
          {
            MRIfree(&mri);
            ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): not enough variables in %s (need 3, have %d", name, count));
          }
          if(ip[0] < 0 || ip[0] > 5 ||
             ip[1] < 0 || ip[1] > 5 ||
             ip[2] < 0 || ip[2] > 5)
          {
            MRIfree(&mri);
            ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): %s variables should be 0 to 5, inclusive (are %d, %d, %d here)", name, ip[0], ip[1], ip[2]));
          }

          mri->x_r = afni_orientations[ip[0]][0];  mri->x_a = afni_orientations[ip[0]][1];  mri->x_s = afni_orientations[ip[0]][2];
          mri->y_r = afni_orientations[ip[1]][0];  mri->y_a = afni_orientations[ip[1]][1];  mri->y_s = afni_orientations[ip[1]][2];
          mri->z_r = afni_orientations[ip[2]][0];  mri->z_a = afni_orientations[ip[2]][1];  mri->z_s = afni_orientations[ip[2]][2];

          /* --- quick determinant check --- */
          det = + mri->x_r * (mri->y_a * mri->z_s - mri->z_a * mri->y_s)
                - mri->x_a * (mri->y_r * mri->z_s - mri->z_r * mri->y_s)
                + mri->x_s * (mri->y_r * mri->z_a - mri->z_r * mri->y_a);

          if(det == 0)
          {
            MRIfree(&mri);
            ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): error in orientations %d, %d, %d (direction cosine matrix has determinant zero)", ip[0], ip[1], ip[2]));
          }

          gotten = gotten | ORIENT_SPECIFIC_FLAG;

        }
        else if(strcmp(name, "BRICK_TYPES") == 0)
        {
          mri->nframes = count;
          gotten = gotten | BRICK_TYPES_FLAG;
        }
        else if(strcmp(name, "DATASET_DIMENSIONS") == 0)
        {
          if(strncmp(type, "integer-attribute", 17) != 0)
          {
            MRIfree(&mri);
            ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): variable %s listed as %s (expecting integer-attribute)", name, type));
          }
          if(count < 3)
          {
            MRIfree(&mri);
            ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): not enough variables in %s (need 3, have %d", name, count));
          }

          mri->width = ip[0];
          mri->height = ip[1];
          mri->depth = ip[2];

          gotten = gotten | DATASET_DIMENSIONS_FLAG;

        }
        else if(strcmp(name, "DELTA") == 0)
        {
          if(strncmp(type, "float-attribute", 15) != 0)
          {
            MRIfree(&mri);
            ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): variable %s listed as %s (expecting float-attribute)", name, type));
          }
          if(count < 3)
          {
            MRIfree(&mri);
            ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): not enough variables in %s (need 3, have %d", name, count));
          }

          mri->xsize = f[0];
          mri->ysize = f[1];
          mri->zsize = f[2];

          gotten = gotten | DELTA_FLAG;

        }
        else if(strcmp(name, "ORIGIN") == 0)
        {
          if(count < 3)
          {
            MRIfree(&mri);
            ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): not enough variables in %s (need 3, have %d", name, count));
          }
          if(strncmp(type, "float-attribute", 15) != 0)
          {
            MRIfree(&mri);
            ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): variable %s listed as %s (expecting float-attribute)", name, type));
          }

          origin[0] = f[0];
          origin[1] = f[1];
          origin[2] = f[2];

          gotten = gotten | ORIGIN_FLAG;

        }
        else if(strcmp(name, "BYTEORDER_STRING") == 0)
        {
          if(strncmp(type, "string-attribute", 16) != 0)
          {
            MRIfree(&mri);
            ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): variable %s listed as %s (expecting string-attribute)", name, type));
          }
          if(strcmp(s, "MSB_FIRST") == 0)
            *big_endian_flag = 1;
          else if(strcmp(s, "LSB_FIRST") == 0)
            *big_endian_flag = 0;
          else
          {
            MRIfree(&mri);
            ErrorReturn(NULL, (ERROR_BADPARM, "read_afni_header(): unrecognized byte order string %s", s));
          }

          gotten = gotten | BYTEORDER_STRING_FLAG;

        }
        else /* ignore unknown variables */
        {
        }

        if(s != NULL)
          free(s);
        if(f != NULL)
          free(f);
        if(ip != NULL)
          free(ip);

      }

    }

  }

  if((gotten & AFNI_ALL_REQUIRED) != AFNI_ALL_REQUIRED)
  {

    ErrorPrintf(ERROR_BADFILE, "missing fields in afni header file");

    if(!(gotten & ORIENT_SPECIFIC_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  ORIENT_SPECIFIC missing");
    if(!(gotten & BRICK_TYPES_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  BRICK_TYPES missing");
    if(!(gotten & DATASET_DIMENSIONS_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  DATASET_DIMENSIONS missing");
    if(!(gotten & DELTA_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  DELTA missing");
    if(!(gotten & ORIGIN_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  ORIGIN missing");
    if(!(gotten & BYTEORDER_STRING_FLAG))
      ErrorPrintf(ERROR_BADFILE, "  BYTEORDER_STRING missing");

    MRIfree(&mri);
    return(NULL);

  }

  mri->c_r = mri->x_r * (mri->xsize * (mri->width-1.0)/2.0 + origin[0]) + 
             mri->y_r * (mri->ysize * (mri->height-1.0)/2.0 + origin[1]) + 
             mri->z_r * (mri->zsize * (mri->depth-1.0)/2.0 + origin[2]);

  mri->c_a = mri->x_a * (mri->xsize * (mri->width-1.0)/2.0 + origin[0]) + 
             mri->y_a * (mri->ysize * (mri->height-1.0)/2.0 + origin[1]) + 
             mri->z_a * (mri->zsize * (mri->depth-1.0)/2.0 + origin[2]);

  mri->c_s = mri->x_s * (mri->xsize * (mri->width-1.0)/2.0 + origin[0]) + 
             mri->y_s * (mri->ysize * (mri->height-1.0)/2.0 + origin[1]) + 
             mri->z_s * (mri->zsize * (mri->depth-1.0)/2.0 + origin[2]);

  mri->ras_good_flag = 1;

  if(mri->xsize < 0)
    mri->xsize = -mri->xsize;

  if(mri->ysize < 0)
    mri->ysize = -mri->ysize;

  if(mri->zsize < 0)
    mri->zsize = -mri->zsize;

  mri->imnr0 = 1;
  mri->imnr1 = mri->depth;

  mri->ps = mri->xsize;
  mri->thick = mri->zsize;

  mri->xend = (mri->width  / 2.0) * mri->xsize;  mri->xstart = -mri->xend;
  mri->yend = (mri->height / 2.0) * mri->ysize;  mri->ystart = -mri->yend;
  mri->zend = (mri->depth  / 2.0) * mri->zsize;  mri->zstart = -mri->zend;

  xfov = mri->xend - mri->xstart;
  yfov = mri->yend - mri->ystart;
  zfov = mri->zend - mri->zstart;

  mri->fov = ( xfov > yfov ? (xfov > zfov ? xfov : zfov ) : (yfov > zfov ? yfov : zfov ) );

  return(mri);

} /* end read_afni_header() */

static int *get_afni_int(FILE *fp, int count, char *name)
{

  int *buf = NULL;
  int i;
  char line[STRLEN];
  char *c;
  char *e;
  char blank_flag;

  buf = (int *)malloc(count * sizeof(int));

  for(i = 0;i < count;)
  {

    fgets(line, STRLEN, fp);

    blank_flag = 1;
    for(c = line;*c != '\0';c++)
      if(!isspace(*c))
        blank_flag = 0;

    if(feof(fp))
    {
      free(buf);
      ErrorReturn(NULL, (ERROR_BADPARM, "get_afni_int(): hit EOF while reading %d ints for %s", count, name));
    }

    if(blank_flag)
    {
      free(buf);
      ErrorReturn(NULL, (ERROR_BADPARM, "get_afni_int(): hit a blank line while reading %d ints for %s", count, name));
    }

    if(strncmp(line, "type", 4) == 0)
    {
      free(buf);
      ErrorReturn(NULL, (ERROR_BADPARM, "get_afni_int(): hit a type line while reading %d ints for %s", count, name));
    }

    for(c = line;*c != '\0';)
    {
      for(;isspace(*c) && *c != '\0';c++);
      if(*c != '\0')
        {
        buf[i] = strtol(c, &e, 10);
        c = e;
        i++;
        }
    }
  }

  return(buf);

} /* end get_afni_int() */

static float *get_afni_float(FILE *fp, int count, char *name)
{

  float *buf = NULL;
  int i;
  char line[STRLEN];
  char *c;
  char *e;
  char blank_flag;

  buf = (float *)malloc(count * sizeof(float));

  for(i = 0;i < count;)
  {

    fgets(line, STRLEN, fp);

    blank_flag = 1;
    for(c = line;*c != '\0';c++)
      if(!isspace(*c))
        blank_flag = 0;

    if(feof(fp))
    {
      free(buf);
      ErrorReturn(NULL, (ERROR_BADPARM, "get_afni_float(): hit EOF while reading %d floats for %s", count, name));
    }

    if(blank_flag)
    {
      free(buf);
      ErrorReturn(NULL, (ERROR_BADPARM, "get_afni_float(): hit a blank line while reading %d floats for %s", count, name));
    }

    if(strncmp(line, "type", 4) == 0)
    {
      free(buf);
      ErrorReturn(NULL, (ERROR_BADPARM, "get_afni_float(): hit a type line while reading %d floats for %s", count, name));
    }

    for(c = line;*c != '\0';)
    {
      for(;isspace(*c) && *c != '\0';c++);
      if(*c != '\0')
        {
        buf[i] = (float)strtod(c, &e);
        c = e;
        i++;
        }
    }
  }

  return(buf);

} /* end get_afni_float() */

static char *get_afni_string(FILE *fp, int count, char *name)
{

  char *buf;
  int i;
  char c;

  buf = (char *)malloc(count+1);

  c = fgetc(fp);

  if(c != '\'')
  {
    free(buf);
    ErrorReturn(NULL, (ERROR_BADPARM, "get_afni_string(): afni header string %s does not start with \"'\"", name));
  }

  for(i = 0;i < count;i++)
  {

    if(feof(fp))
    {
      free(buf);
      ErrorReturn(NULL, (ERROR_BADPARM, "get_afni_string(): end of file reached at %d of %d bytes of string %s", i+1, count, name));
    }

    c = fgetc(fp);

    if(i == count - 1 && c != '~')
    {
      ErrorPrintf(ERROR_BADPARM, "warning: string %s does not end with \"~\"", name);
    }

    buf[i] = (c == '~' ? '\0' : c);

  }

  buf[count] = '\0';

  for(c = fgetc(fp) ; c != '\n' && !feof(fp) ; c = fgetc(fp));

  return(buf);

} /* end get_afni_string() */

static int afniWrite(MRI *mri, char *fname)
{

  char header_fname[STRLEN];
  FILE *fp;
  int i, j, k;
  int orient_specific[3];
  int bytes_per_voxel;
  float max, min;
  int dest_type;
  short s;
  float f;
  char *c;

  ErrorReturn(ERROR_UNSUPPORTED, (ERROR_UNSUPPORTED, "AFNI BRIK write unsupported"));

  /* ----- keep compiler quiet ----- */
  bytes_per_voxel = 0;
  dest_type = -1;

  if(mri->nframes != 1)
  {
      ErrorReturn(ERROR_UNSUPPORTED, (ERROR_UNSUPPORTED, "afniRead(): writing of anything but 1 frame unsupported (mri->nframes = %d)", mri->nframes));
  }

  orient_specific[0] = -1;
  orient_specific[1] = -1;
  orient_specific[2] = -1;

  for(i = 0;i < 6;i++)
  {
    if(mri->x_r == afni_orientations[i][0] && mri->x_a == afni_orientations[i][1] && mri->x_s == afni_orientations[i][2])
      orient_specific[0] = i;
    if(mri->y_r == afni_orientations[i][0] && mri->y_a == afni_orientations[i][1] && mri->y_s == afni_orientations[i][2])
      orient_specific[1] = i;
    if(mri->z_r == afni_orientations[i][0] && mri->z_a == afni_orientations[i][1] && mri->z_s == afni_orientations[i][2])
      orient_specific[2] = i;
  }

  if(orient_specific[0] == -1 || orient_specific[1] == -1 || orient_specific[2] == -1)
  {
    ErrorPrintf(ERROR_UNSUPPORTED, "afniWrite(): oblique volume writing to AFNI unsupported");
    ErrorPrintf(ERROR_UNSUPPORTED, "x_(r, a, s) = (%g, %g, %g)", mri->x_r, mri->x_a, mri->x_s);
    ErrorPrintf(ERROR_UNSUPPORTED, "y_(r, a, s) = (%g, %g, %g)", mri->y_r, mri->y_a, mri->y_s);
    ErrorPrintf(ERROR_UNSUPPORTED, "z_(r, a, s) = (%g, %g, %g)", mri->z_r, mri->z_a, mri->z_s);
    return(ERROR_UNSUPPORTED);
  }

  if(mri->type == MRI_INT || mri->type == MRI_LONG)
  {
    MRIlimits(mri, &min, &max);
    if(min > -32768.0 && min < 32768.0 && max > -32768.0 && max < 32768.0)
      dest_type = MRI_SHORT;
    else
      dest_type = MRI_FLOAT;
  }
  else if(mri->type == MRI_UCHAR)
    bytes_per_voxel = 1;
  else if(mri->type == MRI_SHORT)
    bytes_per_voxel = 2;
  else if(mri->type == MRI_FLOAT)
    bytes_per_voxel = 4;
  else
  {
      ErrorReturn(ERROR_UNSUPPORTED, (ERROR_UNSUPPORTED, "afniRead(): unsupported data type %d", mri->type));
  }

  strcpy(header_fname, fname);
  c = strrchr(header_fname, '.');

  if(c == NULL)
  {
      ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "afniRead(): bad file name %s", fname));
  }

  if(strcmp(c, ".BRIK") != 0)
  {
      ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "afniRead(): bad file name %s", fname));
  }

  sprintf(c, ".HEAD");

  if((fp = fopen(header_fname, "w")) == NULL)
  {
    ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "afniWrite(): can't open file %s for writing", header_fname));
  }

  fprintf(fp, "\n");

  fprintf(fp, "type = integer-attribute\n");
  fprintf(fp, "name = ORIENT_SPECIFIC\n");
  fprintf(fp, "count = 3\n");
  fprintf(fp, " 1\n");

  fprintf(fp, "\n");

  fprintf(fp, "type = integer-attribute\n");
  fprintf(fp, "name = BRICK_TYPES\n");
  fprintf(fp, "count = 1\n");
  fprintf(fp, " 1\n");

  fprintf(fp, "\n");

  fprintf(fp, "type = integer-attribute\n");
  fprintf(fp, "name = DATASET_DIMENSIONS\n");
  fprintf(fp, "count = 3\n");
  fprintf(fp, " %d %d %d\n", mri->width, mri->height, mri->depth);

  fprintf(fp, "\n");

  fprintf(fp, "type = float-attribute\n");
  fprintf(fp, "name = DELTA\n");
  fprintf(fp, "count = 3\n");
  fprintf(fp, " %g %g %g\n", mri->xsize, mri->ysize, mri->zsize);

  fprintf(fp, "\n");

  fprintf(fp, "type = float-attribute\n");
  fprintf(fp, "name = ORIGIN\n");
  fprintf(fp, "count = 3\n");
  fprintf(fp, " %g %g %g\n", -(mri->width  - 1.0) / 2.0 * mri->xsize, 
                             -(mri->height - 1.0) / 2.0 * mri->ysize, 
                             -(mri->depth  - 1.0) / 2.0 * mri->zsize);

  fprintf(fp, "\n");

  fprintf(fp, "type = \n");
  fprintf(fp, "name = BYTEORDER_STRING\n");
  fprintf(fp, "count = 10\n");
#ifdef Linux
  fprintf(fp, " LSB_FIRST~\n");
#else
  fprintf(fp, " MSB_FIRST~\n");
#endif

  fclose(fp);

  if((fp = fopen(fname, "w")) == NULL)
  {
    ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "afniWrite(): can't open file %s for writing", fname));
  }

  for(k = 0;k < mri->depth;k++)
  {
    for(j = 0;j < mri->height;j++)
    {

      if(mri->type == MRI_INT || mri->type == MRI_LONG)
      {
        for(i = 0;i < mri->width;i++)
        {
          if(dest_type == MRI_SHORT)
          {
            if(mri->type == MRI_INT)
              s = (short)MRIIvox(mri, i, j, k);
            if(mri->type == MRI_LONG)
              s = (short)MRILvox(mri, i, j, k);
            fwrite(&s, sizeof(short), 1, fp);
          }
          if(dest_type == MRI_FLOAT)
          {
            if(mri->type == MRI_INT)
              f = (float)MRIIvox(mri, i, j, k);
            if(mri->type == MRI_LONG)
              f = (float)MRILvox(mri, i, j, k);
            fwrite(&f, sizeof(float), 1, fp);
          }
        }
      }

      else
        fwrite(mri->slices[k][j], bytes_per_voxel, mri->width, fp);

    }
  }

  fclose(fp);

  printf("no afni write\n");
  return(0);

} /* end afniWrite() */

static void nflip(unsigned char *buf, int b, int n)
{

  int i, j;
  unsigned char *copy;

  copy = (unsigned char *)malloc(b);
  for(i = 0;i < n;i++)
  {
    memcpy(copy, &buf[i*b], b);
    for(j = 0;j < b;j++)
      buf[i*b+j] = copy[b-j-1];
  }
  free(copy);

} /* end nflip() */

static int bad_ras_fill(MRI *mri)
{

  if(mri->slice_direction == MRI_CORONAL)
  {
    mri->x_r = -1.0;  mri->y_r =  0.0;  mri->z_r =  0.0;
    mri->x_a =  0.0;  mri->y_a =  0.0;  mri->z_a =  1.0;
    mri->x_s =  0.0;  mri->y_s = -1.0;  mri->z_s =  0.0;
    mri->c_r = 0.0;  mri->c_a = 0.0;  mri->c_s = 0.0;
  }
  else if(mri->slice_direction == MRI_SAGITTAL)
  {
    mri->x_r =  0.0;  mri->y_r =  0.0;  mri->z_r = -1.0;
    mri->x_a =  1.0;  mri->y_a =  0.0;  mri->z_a =  0.0;
    mri->x_s =  0.0;  mri->y_s =  1.0;  mri->z_s =  0.0;
    mri->c_r = 0.0;  mri->c_a = 0.0;  mri->c_s = 0.0;
  }
  else if(mri->slice_direction == MRI_HORIZONTAL)
  {
    mri->x_r =  1.0;  mri->y_r =  0.0;  mri->z_r =  0.0;
    mri->x_a =  0.0;  mri->y_a = -1.0;  mri->z_a =  0.0;
    mri->x_s =  0.0;  mri->y_s =  0.0;  mri->z_s =  1.0;
    mri->c_r = 0.0;  mri->c_a = 0.0;  mri->c_s = 0.0;
  }
  else
  {
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "bad_ras_fill(): unknown slice direction"));
  }

  return(NO_ERROR);

} /* end bad_ras_fill() */

#ifdef VT_TO_CV

static int voxel_center_to_center_voxel(MRI *mri, float *x, float *y, float *z)
{

  int result;
  MATRIX *m, *i, *r, *mi;

  if(!mri->ras_good_flag)
    if((result = bad_ras_fill(mri)) != NO_ERROR)
      return(result);

  if((m = extract_i_to_r(mri)) == NULL)
    return(ERROR_BADPARM);

  mi = MatrixInverse(m, NULL);
  if(mi == NULL)
  {
    MatrixFree(&m);
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "voxel_center_to_center_voxel(): error inverting matrix"));
  }

  r = MatrixAlloc(4, 1, MATRIX_REAL);
  if(r == NULL)
  {
    MatrixFree(&m);
    MatrixFree(&mi);
    ErrorReturn(ERROR_NOMEMORY, (ERROR_NOMEMORY, "voxel_center_to_center_voxel(): couldn't allocate matrix"));
  }

  *MATRIX_RELT(r, 1, 1) = 0.0;
  *MATRIX_RELT(r, 2, 1) = 0.0;
  *MATRIX_RELT(r, 3, 1) = 0.0;
  *MATRIX_RELT(r, 4, 1) = 1.0;

  i = MatrixMultiply(mi, r, NULL);
  if(i == NULL)
  {
    MatrixFree(&m);
    MatrixFree(&mi);
    MatrixFree(&r);
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "voxel_center_to_center_voxel(): error in matrix multiplication"));
  }

  *x = *MATRIX_RELT(i, 1, 1);
  *y = *MATRIX_RELT(i, 2, 1);
  *z = *MATRIX_RELT(i, 3, 1);

  MatrixFree(&m);
  MatrixFree(&mi);
  MatrixFree(&i);
  MatrixFree(&r);

  return(NO_ERROR);

} /* end voxel_center_to_center_voxel() */

#endif

static int center_voxel_to_voxel_center(MRI *mri, float x, float y, float z)
{

  int result;
  MATRIX *m;

  if(!mri->ras_good_flag)
    if((result = bad_ras_fill(mri)) != NO_ERROR)
      return(result);

  if((m = extract_i_to_r(mri)) == NULL)
    return(ERROR_BADPARM);

  *MATRIX_RELT(m, 1, 4) = 0 - (*MATRIX_RELT(m, 1, 1) * x + *MATRIX_RELT(m, 1, 2) * y + *MATRIX_RELT(m, 1, 3) * z);
  *MATRIX_RELT(m, 2, 4) = 0 - (*MATRIX_RELT(m, 2, 1) * x + *MATRIX_RELT(m, 2, 2) * y + *MATRIX_RELT(m, 2, 3) * z);
  *MATRIX_RELT(m, 3, 4) = 0 - (*MATRIX_RELT(m, 3, 1) * x + *MATRIX_RELT(m, 3, 2) * y + *MATRIX_RELT(m, 3, 3) * z);

  apply_i_to_r(mri, m);

  MatrixFree(&m);

  return(NO_ERROR);

} /* end center_voxel_to_voxel_center() */

static MRI *gdfRead(char *fname, int read_volume)
{

  MRI *mri;
  FILE *fp;
  char line[STRLEN];
  char *c;
  char file_path[STRLEN];
  float ipr[2];
  float st;
  float center[3];
  char units_string[STRLEN], orientation_string[STRLEN], data_type_string[STRLEN];
  int size[2];
  int path_d, ipr_d, st_d, u_d, dt_d, o_d, s_d, c_d;
  int data_type;
  int orientation;
  char *or;
  char os_orig[STRLEN];
  float units_factor;
  char file_path_1[STRLEN], file_path_2[STRLEN];
  char gdf_path[STRLEN];
  int i, j, k;
  short *sbuf = NULL;
  float *fbuf = NULL;
  unsigned char *ucbuf = NULL;
  int n_files;
  char fname_use[STRLEN];

  if((fp = fopen(fname, "r")) == NULL)
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "gdfRead(): error opening file %s", fname));
  }

  /* --- defined flags --- */
  path_d = ipr_d = st_d = u_d = dt_d = o_d = s_d = c_d = FALSE;

  while(fgets(line, STRLEN, fp) != NULL)
  {

    /* --- strip the newline --- */
    if((c = strrchr(line, '\n')) != NULL)
      *c = '\0';

    if(strncmp(line, "IMAGE_FILE_PATH", 15) == 0)
    {
      sscanf(line, "%*s %s", file_path);
      path_d = TRUE;
    }
    else if(strncmp(line, "IP_RES", 6) == 0)
    {
      sscanf(line, "%*s %f %f", &ipr[0], &ipr[1]);
      ipr_d = TRUE;
    }
    else if(strncmp(line, "SL_THICK", 8) == 0)
    {
      sscanf(line, "%*s %f", &st);
      st_d = TRUE;
    }
    else if(strncmp(line, "UNITS", 5) == 0)
    {
      sscanf(line, "%*s %s", units_string);
      u_d = TRUE;
    }
    else if(strncmp(line, "SIZE", 4) == 0)
    {
      sscanf(line, "%*s %d %d", &size[0], &size[1]);
      s_d = TRUE;
    }
    else if(strncmp(line, "DATA_TYPE", 9) == 0)
    {
      sscanf(line, "%*s %s", data_type_string);
      dt_d = TRUE;
    }
    else if(strncmp(line, "ORIENTATION", 11) == 0)
    {
      sscanf(line, "%*s %s", orientation_string);
      strcpy(os_orig, orientation_string);
      o_d = TRUE;
    }
    else if(strncmp(line, "DISPLAY_CENTER", 14) == 0)
    {
      sscanf(line, "%*s %f %f %f", &center[0], &center[1], &center[2]);
      c_d = TRUE;
    }
    else
    {
    }

  }

  fclose(fp);

  if(!(path_d && ipr_d && st_d && s_d && o_d))
  {
    ErrorPrintf(ERROR_BADPARM, "gdfRead(): missing field(s) from %s:", fname);
    if(!path_d)
      ErrorPrintf(ERROR_BADPARM, "  IMAGE_FILE_PATH");
    if(!ipr_d)
      ErrorPrintf(ERROR_BADPARM, "  IP_RES");
    if(!st_d)
      ErrorPrintf(ERROR_BADPARM, "  SL_THICK");
    if(!s_d)
      ErrorPrintf(ERROR_BADPARM, "  SIZE");
    if(!o_d)
      ErrorPrintf(ERROR_BADPARM, "  ORIENTATION");
    return(NULL);
  }

  if(!(dt_d))
  {
    printf("missing field DATA_TYPE in file %s; assuming 'short'\n", fname);
    sprintf(data_type_string, "short");
  }

  if(!(c_d))
  {
    printf("missing field DISPLAY_CENTER in file %s; assuming center of volume is origin'\n", fname);
    center[0] = center[1] = center[2] = -1.0;
  }

  if(!(u_d))
  {
    printf("missing field UNITS in file %s; assuming 'mm'\n", fname);
    sprintf(data_type_string, "mm");
  }

  StrLower(data_type_string);
  StrLower(orientation_string);
  StrLower(units_string);

  /* --- data type --- */
  if(strcmp(data_type_string, "short") == 0)
    data_type = MRI_SHORT;
  else if(strcmp(data_type_string, "float") == 0)
    data_type = MRI_FLOAT;
  else if(strcmp(data_type_string, "unsigned char") == 0)
    data_type = MRI_UCHAR;
  else
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "gdfRead(): unknown data type '%s'", data_type_string));
  }

  /* --- orientation --- */
  or = strrchr(orientation_string, ' ');
  or = (or == NULL ? orientation_string : or+1);
  if(strncmp(or, "cor", 3) == 0)
    orientation = MRI_CORONAL;
  else if(strncmp(or, "sag", 3) == 0)
    orientation = MRI_SAGITTAL;
  else if(strncmp(or, "ax", 2) == 0 || strncmp(or, "hor", 3) == 0)
    orientation = MRI_HORIZONTAL;
  else
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "gdfRead(): can't determine orientation from string '%s'", os_orig));
  }

  if(strcmp(units_string, "mm") == 0)
    units_factor = 1.0;
  else if(strcmp(units_string, "cm") == 0)
    units_factor = 1.0;
  else if(strcmp(units_string, "m") == 0)
    units_factor = 100.0;
  else
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "gdfRead(): unknown units '%s'", units_string));
  }

  ipr[0] /= units_factor;
  ipr[1] /= units_factor;
  st /= units_factor;

  strcpy(file_path_1, file_path);
  c = strrchr(file_path_1, '*');
  if(c == NULL)
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "gdfRead(): file path %s does not contain '*'\n", file_path));
  }

  *c = '\0';
  c++;
  strcpy(file_path_2, c);

  /* ----- relative path -- go from directory with the .gdf file ----- */
  if(file_path_1[0] != '/')
  {
    if(fname[0] == '/')
      sprintf(gdf_path, "%s", fname);
    else
      sprintf(gdf_path, "./%s", fname);

    c = strrchr(gdf_path, '/');
    c[1] = '\0';

    strcat(gdf_path, file_path_1);
    strcpy(file_path_1, gdf_path);
    
  }

  n_files = 0;
  do
  {
    n_files++;
    sprintf(fname_use, "%s%d%s", file_path_1, n_files, file_path_2);
  } while(FileExists(fname_use));

  if(n_files == 1)
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "gdfRead(): can't find file %s\n", fname_use));
  }

  n_files--;

  if(read_volume)
    mri = MRIallocSequence(size[0], size[1], n_files, data_type, 1);
  else
    mri = MRIallocHeader(size[0], size[1], n_files, data_type);

  mri->xsize = ipr[0];
  mri->ysize = ipr[1];
  mri->zsize = st;

  mri->xend = mri->width  * mri->xsize / 2.0;
  mri->xstart = -mri->xend;
  mri->yend = mri->height * mri->ysize / 2.0;
  mri->ystart = -mri->yend;
  mri->zend = mri->depth  * mri->zsize / 2.0;
  mri->zstart = -mri->zend;
  
  strcpy(mri->fname, fname);

  mri->slice_direction = orientation;
  if(bad_ras_fill(mri) != NO_ERROR)
    return(NULL);

  /* ----- hack ----- */
  center[0] = (mri->width  - 1.0) / 2.0;
  center[1] = (mri->height - 1.0) / 2.0;
  center[2] = (mri->depth  - 1.0) / 2.0;
  center_voxel_to_voxel_center(mri, center[0], center[1], center[2]);

  printf("warning: gdf volume may be incorrectly oriented or centered\n");

  if(!read_volume)
    return(mri);

  if(mri->type == MRI_UCHAR)
  {
    ucbuf = (unsigned char *)malloc(mri->width);
    if(ucbuf == NULL)
    {
      MRIfree(&mri);
      ErrorReturn(NULL, (ERROR_NOMEMORY, "gdfRead(): error allocating %d bytes for read buffer", mri->width));
    }
  }
  else if(mri->type == MRI_SHORT)
  {
    sbuf = (short *)malloc(mri->width * sizeof(short));
    if(sbuf == NULL)
    {
      MRIfree(&mri);
      ErrorReturn(NULL, (ERROR_NOMEMORY, "gdfRead(): error allocating %d bytes for read buffer", mri->width * sizeof(short)));
    }
  }
  else if(mri->type == MRI_FLOAT)
  {
    fbuf = (float *)malloc(mri->width * sizeof(float));
    if(fbuf == NULL)
    {
      MRIfree(&mri);
      ErrorReturn(NULL, (ERROR_NOMEMORY, "gdfRead(): error allocating %d bytes for read buffer", mri->width * sizeof(float)));
    }
  }
  else
  {
    MRIfree(&mri);
    ErrorReturn(NULL, (ERROR_BADPARM, "gdfRead(): internal error; data type %d accepted but not supported in read", mri->type));
  }

  for(i = 1;i <= n_files;i++)
  {

    sprintf(fname_use, "%s%d%s", file_path_1, i, file_path_2);

    fp = fopen(fname_use, "r");
    if(fp == NULL)
    {
      MRIfree(&mri);
      if(mri->type == MRI_UCHAR)
        free(ucbuf);
      if(mri->type == MRI_SHORT)
        free(sbuf);
      if(mri->type == MRI_FLOAT)
        free(fbuf);
      ErrorReturn(NULL, (ERROR_BADFILE, "gdfRead(): error opening file %s", fname_use));
    }

    if(mri->type == MRI_UCHAR)
    {
      for(j = 0;j < mri->height;j++)
      {
        if(fread(ucbuf, 1, mri->width, fp) != mri->width)
        {
          free(ucbuf);
          MRIfree(&mri);
          ErrorReturn(NULL, (ERROR_BADFILE, "gdfRead(): error reading from file %s", fname_use));
        }
        for(k = 0;k < mri->width;k++)
          MRIvox(mri, k, j, i-1) = ucbuf[k];
      }
    }

    if(mri->type == MRI_SHORT)
    {
      for(j = 0;j < mri->height;j++)
      {
        if(fread(sbuf, sizeof(short), mri->width, fp) != mri->width)
        {
          free(sbuf);
          MRIfree(&mri);
          ErrorReturn(NULL, (ERROR_BADFILE, "gdfRead(): error reading from file %s", fname_use));
        }
#ifdef Linux
        for(k = 0;k < mri->width;k++)
          MRISvox(mri, k, j, i-1) = orderShortBytes(sbuf[k]);
#else
        for(k = 0;k < mri->width;k++)
          MRISvox(mri, k, j, i-1) = sbuf[k];
#endif
      }
    }

    if(mri->type == MRI_FLOAT)
    {
      for(j = 0;j < mri->height;j++)
      {
        if(fread(fbuf, sizeof(float), mri->width, fp) != mri->width)
        {
          free(fbuf);
          MRIfree(&mri);
          ErrorReturn(NULL, (ERROR_BADFILE, "gdfRead(): error reading from file %s", fname_use));
        }
#ifdef Linux
        for(k = 0;k < mri->width;k++)
          MRIFvox(mri, k, j, i-1) = orderFloatBytes(fbuf[k]);
#else
        for(k = 0;k < mri->width;k++)
          MRIFvox(mri, k, j, i-1) = fbuf[k];
#endif
      }
    }

    fclose(fp);

  }

  if(mri->type == MRI_UCHAR)
    free(ucbuf);
  if(mri->type == MRI_SHORT)
    free(sbuf);
  if(mri->type == MRI_FLOAT)
    free(fbuf);

  return(mri);

} /* end gdfRead() */

static int gdfWrite(MRI *mri, char *fname)
{

  ErrorReturn(ERROR_UNSUPPORTED, (ERROR_UNSUPPORTED, "GDF read unsupported"));

}

static int parc_fill(short label_value, short seed_x, short seed_y)
{

  if(seed_x < 0 || seed_x >= 512 || seed_y < 0 || seed_y >= 512)
    return(NO_ERROR);

  if(cma_field[seed_x][seed_y] == label_value)
    return(NO_ERROR);

  cma_field[seed_x][seed_y] = label_value;

  parc_fill(label_value, seed_x + 1, seed_y    );
  parc_fill(label_value, seed_x - 1, seed_y    );
  parc_fill(label_value, seed_x    , seed_y + 1);
  parc_fill(label_value, seed_x    , seed_y - 1);

  return(NO_ERROR);

} /* end parc_fill() */

static int register_unknown_label(char *label)
{

  int i;

  if(n_unknown_labels == MAX_UNKNOWN_LABELS)
    return(NO_ERROR);

  for(i = 0;i < n_unknown_labels;i++)
  {
    if(strcmp(unknown_labels[i], label) == 0)
      return(NO_ERROR);
  }

  strcpy(unknown_labels[n_unknown_labels], label);
  n_unknown_labels++;

  return(NO_ERROR);

} /* end register_unknown_label() */

static int clear_unknown_labels(void)
{

  n_unknown_labels = 0;

  return(NO_ERROR);

} /* end clear_unknown_labels() */

static int print_unknown_labels(char *prefix)
{

  int i;

  for(i = 0;i < n_unknown_labels;i++)
    printf("%s%s\n", prefix, unknown_labels[i]);

  return(NO_ERROR);

} /* end print_unknown_labels() */

static int read_otl_file(FILE *fp, MRI *mri, int slice, mriColorLookupTableRef color_table, int fill_flag)
{

  int n_outlines = -1;
  int n_rows, n_cols;
  char label[STRLEN], label_to_compare[STRLEN];
  int seed_x, seed_y;
  char line[STRLEN];
  int main_header_flag;
  int i, j;
  int gdf_header_flag;
  char type[STRLEN];
  char *c;
  short *points;
  int n_read;
  short label_value;
  float scale_x, scale_y;
  int source_x, source_y;
  char alt_compare[STRLEN];

  for(i = 0;i < 512;i++)
    memset(cma_field[i], 0x00, 512 * 2);

  fgets(line, STRLEN, fp);
  if(strncmp(line, "GDF FILE VERSION", 15) != 0)
  {
    ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "otl slice %s does not appear to be a GDF file", slice));
  }

  main_header_flag = FALSE;
  while(!main_header_flag)
  {
    if(feof(fp))
    {
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "premature EOF () in otl file %d", slice));
    }
    fgets(line, STRLEN, fp);
    if(strncmp(line, "START MAIN HEADER", 17) == 0)
      main_header_flag = TRUE;
  }

  n_cols = -1;
  type[0] = '\0';

  while(main_header_flag)
  {
    if(feof(fp))
    {
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "premature EOF (in main header) in otl file %d", slice));
    }
    fgets(line, STRLEN, fp);
    if(strncmp(line, "END MAIN HEADER", 15) == 0)
      main_header_flag = FALSE;
    if(strncmp(line, "ONUM ", 5) == 0)
      sscanf(line, "%*s %d", &n_outlines);
    if(strncmp(line, "COL_NUM", 7) == 0)
      sscanf(line, "%*s %d", &n_cols);
    if(strncmp(line, "TYPE", 4) == 0)
    {
      strcpy(type, &(line[5]));
      c = strrchr(type, '\n');
      if(c != NULL)
        *c = '\0';
    }
  }

  if(n_outlines == -1)
  {
    ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "bad or undefined ONUM in otl file %d", slice));
  }

  for(i = 0;i < n_outlines;i++)
  {
    if(feof(fp))
    {
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "premature EOF (ready for next outline) in otl file %d", slice));
    }

    gdf_header_flag = FALSE;

    while(!gdf_header_flag)
    {
      if(feof(fp))
      {
        ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "premature EOF (searching for gdf header) in otl file %d", slice));
      }
      fgets(line, STRLEN, fp);
      if(strncmp(line, "START GDF HEADER", 16) == 0)
        gdf_header_flag = TRUE;
    }

    n_rows = -1;
    seed_x = seed_y = -1;
    label[0] = '\0';

    while(gdf_header_flag)
    {

      if(feof(fp))
      {
        ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "premature EOF (in gdf header) in otl file %d", slice));
      }
      fgets(line, STRLEN, fp);
      if(strncmp(line, "END GDF HEADER", 14) == 0)
        gdf_header_flag = FALSE;
      if(strncmp(line, "ROW_NUM", 7) == 0)
        sscanf(line, "%*s %d", &n_rows);
      if(strncmp(line, "COL_NUM", 7) == 0)
        sscanf(line, "%*s %d", &n_cols);
      if(strncmp(line, "TYPE", 4) == 0)
      {
        strcpy(type, &(line[5]));
        c = strrchr(type, '\n');
        if(c != NULL)
          *c = '\0';
      }
      if(strncmp(line, "SEED", 4) == 0)
        sscanf(line, "%*s %d %d", &seed_x, &seed_y);
      if(strncmp(line, "LABEL", 5) == 0)
      {
        strcpy(label, &(line[6]));
        c = strrchr(label, '\n');
        if(c != NULL)
          *c = '\0';
      }

    }

    if(n_rows < 0)
    {
      ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "bad or undefined ROW_NUM in otl file %d", slice));
    }

    if(n_cols != 2)
    {
      ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "bad or undefined COL_NUM in otl file %d", slice));
    }

    if(label[0] == '\0')
    {
      ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "bad or undefined LABEL in otl file %d", slice));
    }

    if(seed_x < 0 || seed_x >= 512 || seed_y < 0 || seed_y >= 512)
    {
      ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "bad or undefined SEED in otl file %d", slice));
    }

    if(strcmp(type, "short") != 0 && type[0] != '\0')
    {
      ErrorReturn(ERROR_UNSUPPORTED, (ERROR_UNSUPPORTED, "unsupported TYPE \"%s\"in otl file %d", type, slice));
    }

    do
    {
      fgets(line, STRLEN, fp);
      if(feof(fp))
      {
        ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "premature EOF (searching for points) in otl file %d", slice));
      }
    } while(strncmp(line, "START POINTS", 12) != 0);

    points = (short *)malloc(2 * n_rows * sizeof(short));
    if(points == NULL)
    {
      ErrorReturn(ERROR_NOMEMORY, (ERROR_NOMEMORY, "error allocating memory for points in otl file %d", slice));
    }

    n_read = fread(points, 2, n_rows * 2, fp);
    if(n_read != n_rows * 2)
    {
      free(points);
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "error reading points from otl file %d", slice));
    }

    fgets(line, STRLEN, fp);
    if(strncmp(line, "END POINTS", 10) != 0)
    {
      free(points);
      ErrorReturn(ERROR_BADFILE, (ERROR_BADFILE, "error after points (\"END POINTS\" expected but not found) in otl file %d", slice));
    }

#ifdef Linux
    swab(points, points, 2 * n_rows * sizeof(short));
#endif

    strcpy(label_to_compare, label);
    for(j = 0;label_to_compare[j] != '\0';j++)
    {
      if(label_to_compare[j] == '\n')
        label_to_compare[j] = '\0';
      if(label_to_compare[j] == ' ')
        label_to_compare[j] = '-';
    }

    /* --- strip 'Left' and 'Right'; --- */
    strcpy(alt_compare, label_to_compare);
    StrLower(alt_compare);
    if(strncmp(alt_compare, "left-", 5) == 0)
      strcpy(alt_compare, &(label_to_compare[5]));
    else if(strncmp(alt_compare, "right-", 6) == 0)
      strcpy(alt_compare, &(label_to_compare[6]));

    label_value = -1;
    for(j = 0;j < color_table->mnNumEntries;j++)
      if(strcmp(color_table->maEntries[j].msLabel, label_to_compare) == 0 || 
         strcmp(color_table->maEntries[j].msLabel, alt_compare) == 0)
        label_value = j;

    if(label_value == -1)
    {
      register_unknown_label(label);
    }
    else
    {

      for(j = 0;j < n_rows;j++)
        cma_field[points[2*j]][points[2*j+1]] = label_value;

      if(fill_flag && label_value != 0)
        parc_fill(label_value, seed_x, seed_y);

    }

    free(points);

  }

  scale_x = 512 / mri->width;
  scale_y = 512 / mri->height;

  for(i = 0;i < mri->width;i++)
    for(j = 0;j < mri->height;j++)
    {
      source_x = (int)floor(scale_x * (float)i);
      source_y = (int)floor(scale_y * (float)j);
      if(source_x < 0)
        source_x = 0;
      if(source_x > 511)
        source_x = 511;
      if(source_y < 0)
        source_y = 0;
      if(source_y > 511)
        source_y = 511;
      MRISvox(mri, i, j, slice-1) = cma_field[source_x][source_y];
    }

  return(NO_ERROR);

} /* end read_otl_file() */

MRI *MRIreadOtl(char *fname, int width, int height, int slices, char *color_file_name, int read_volume_flag, int fill_flag)
{

  char stem[STRLEN];
  int i;
  MRI *mri;
  char *c;
  int one_file_exists;
  char first_name[STRLEN], last_name[STRLEN];
  FILE *fp;
  mriColorLookupTableRef color_table;

  clear_unknown_labels();

  /* ----- defaults to width and height ----- */
  if(width <= 0)
    width = 512;
  if(height <= 0)
    height = 512;

  /* ----- strip the stem of the otl file name ----- */
  strcpy(stem, fname);
  c = strrchr(stem, '.');
  if(c == NULL)
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "MRIreadOtl(): bad file name: %s", fname));
  }

  for(c--;c >= stem && isdigit(*c);c--);

  if(c < stem)
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "MRIreadOtl(): bad file name: %s", fname));
  }

  c++;

  one_file_exists = FALSE;
  for(i = 1;i <= slices;i++)
  {
    sprintf(c, "%d.otl", i);
    if(FileExists(stem))
      one_file_exists = TRUE;
    if(i == 1)
      strcpy(first_name, stem);
  }

  strcpy(last_name, stem);

  if(!one_file_exists)
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "MRIreadOtl(): couldn't find any file between %s and %s", first_name, last_name));
  }

  mri = MRIalloc(width, height, slices, MRI_SHORT);
  if(mri == NULL)
  {
    ErrorReturn(NULL, (ERROR_NOMEMORY, "MRIreadOtl(): error allocating MRI structure"));
  }

  if(CLUT_New(&color_table, color_file_name) != CLUT_tErr_NoErr)
  {
    MRIfree(&mri);
    ErrorReturn(NULL, (ERROR_BADFILE, "MRIreadOtl(): error reading color file %s", color_file_name));
  }

  one_file_exists = FALSE;
  for(i = 1;i <= slices;i++)
  {
    sprintf(c, "%d.otl", i);
    if((fp = fopen(stem, "r")) != NULL)
    {
      if(read_otl_file(fp, mri, i, color_table, fill_flag) != NO_ERROR)
      {
        MRIfree(&mri);
        return(NULL);
      }
      one_file_exists = TRUE;
    }
  }

  if(!one_file_exists)
  {
    MRIfree(&mri);
    ErrorReturn(NULL, (ERROR_BADFILE, "MRIreadOtl(): found at least one file between %s and %s but couldn't open it!", first_name, last_name));
  }

  CLUT_Delete(&color_table);

  if(n_unknown_labels > 0)
  {
    printf("unknown labels:\n");
    print_unknown_labels("  ");
  }
  clear_unknown_labels();

  return(mri);

} /* end MRIreadOtl() */

MRI *MRIreadGeRoi(char *fname, int n_slices)
{

  MRI *mri;
  int i;
  char prefix[STRLEN], postfix[STRLEN];
  int n_digits;
  FILE *fp;
  int width, height;
  char fname_use[STRLEN];
  int read_one_flag;
  int pixel_data_offset;
  int y;

  if((fp = fopen(fname, "r")) == NULL)
  {
    ErrorReturn(NULL, (ERROR_BADFILE, "MRIreadGeRoi(): error opening file %s", fname));
  }

  fseek(fp, 8, SEEK_SET);
  fread(&width, 4, 1, fp);  width = orderIntBytes(width);
  fread(&height, 4, 1, fp);  height = orderIntBytes(height);

  fclose(fp);

  for(i = strlen(fname);i >= 0 && fname[i] != '/';i--);
  i++;

  n_digits = 0;
  for(;fname[i] != '\0' && n_digits < 3;i++)
  {
    if(isdigit(fname[i]))
      n_digits++;
    else
      n_digits = 0;
  }  

  if(n_digits < 3)
  {
    ErrorReturn(NULL, (ERROR_BADPARM, "MRIreadGeRoi(): bad GE file name (couldn't find three consecutive digits in the base file name)"));
    return(NULL);
  }

  strcpy(prefix, fname);
  prefix[i-3] = '\0';

  strcpy(postfix, &(fname[i]));

  printf("%s---%s\n", prefix, postfix);

  mri = MRIalloc(width, height, n_slices, MRI_SHORT);

  if(mri == NULL)
  {
    ErrorReturn(NULL, (ERROR_NOMEMORY, "MRIreadGeRoi(): couldn't allocate MRI structure"));
  }

  MRIclear(mri);

  read_one_flag = FALSE;

  for(i = 0;i < n_slices;i++)
  {

    sprintf(fname_use, "%s%03d%s", prefix, i, postfix);
    if((fp = fopen(fname_use, "r")) != NULL)
    {

      fseek(fp, 4, SEEK_SET);
      fread(&pixel_data_offset, 4, 1, fp);  pixel_data_offset = orderIntBytes(pixel_data_offset);
      fseek(fp, pixel_data_offset, SEEK_SET);

      for(y = 0;y < mri->height;y++)
      {
        if(fread(mri->slices[i][y], 2, mri->width, fp) != mri->width)
        {
          fclose(fp);
          MRIfree(&mri);
          ErrorReturn(NULL, (ERROR_BADFILE, "MRIreadGeRoi(): error reading from file file %s", fname_use));
        }
#ifdef Linux
        swab(mri->slices[i][y], mri->slices[i][y], 2 * mri->width);
#endif
      }

      fclose(fp);
      read_one_flag = TRUE;

    }
  }

  if(!read_one_flag)
  {
    MRIfree(&mri);
    ErrorReturn(NULL, (ERROR_BADFILE, "MRIreadGeRoi(): didn't read any ROI files"));
  }

  return(mri);

} /* end MRIreadGeRoi() */

/*******************************************************************/
/*******************************************************************/
/*******************************************************************/

static int data_size[] = { 1, 4, 4, 4, 2 };

static MRI *sdtRead(char *fname, int read_volume)
{

  char header_fname[STR_LEN];
  char line[STR_LEN];
  char *colon, *dot;
  FILE *fp;
  MRI *mri;
  int ndim = -1, data_type = -1;
  int dim[4];
  float xsize = 1.0, ysize = 1.0, zsize = 1.0, dummy_size;
  int orientation = MRI_CORONAL;

  dim[0] = -1;
  dim[1] = -1;
  dim[2] = -1;
  dim[3] = -1;

  /* form the header file name */
  strcpy(header_fname, fname);
  if((dot = strrchr(header_fname, '.')))
    sprintf(dot+1, "spr");
  else
    strcat(header_fname, ".spr");

  /* open the header */
  if((fp = fopen(header_fname, "r")) == NULL)
    ErrorReturn(NULL, (ERROR_BADFILE, "sdtRead(%s): could not open header file %s\n", fname, header_fname));

  while(!feof(fp))
  {
    fgets(line, STR_LEN, fp);
    if((colon = strchr(line, ':')))
    {
      *colon = '\0';
      colon++;
      if(strcmp(line, "numDim") == 0)
      {
        sscanf(colon, "%d", &ndim);
        if(ndim < 3 || ndim > 4)
          {
          fclose(fp);
          ErrorReturn(NULL, (ERROR_UNSUPPORTED, "sdtRead(%s): only 3 or 4 dimensions supported (numDim = %d)\n", fname, ndim));
          }
      }
      else if(strcmp(line, "dim") == 0)
      {
        if(ndim == -1)
        {
          fclose(fp);
          ErrorReturn(NULL, (ERROR_BADFILE, "sdtRead(%s): 'dim' before 'numDim' in header file %s\n", fname, header_fname));
        }
        if(ndim == 3)
        {
          sscanf(colon, "%d %d %d", &dim[0], &dim[1], &dim[2]);
          dim[3] = 1;
        }
        else
        {
          sscanf(colon, "%d %d %d %d", &dim[0], &dim[1], &dim[2], &dim[3]);
          if(dim[3] != 1)
          {
            fclose(fp);
            ErrorReturn(NULL, (ERROR_UNSUPPORTED, "sdtRead(%s): nframes != 1 unsupported for sdt (dim(4) = %d)\n", fname, dim[3]));
          }
        }
      }
      else if(strcmp(line, "dataType") == 0)
      {
        while(isspace((int)*colon))
          colon++;
        if(strncmp(colon, "BYTE", 4) == 0)
          data_type = MRI_UCHAR;
        else if(strncmp(colon, "WORD", 4) == 0)
          data_type = MRI_SHORT;
        else if(strncmp(colon, "LWORD", 5) == 0)
          data_type = MRI_INT;
        else if(strncmp(colon, "REAL", 4) == 0)
          data_type = MRI_FLOAT;
        else if(strncmp(colon, "COMPLEX", 7) == 0)
        {
          fclose(fp);
          ErrorReturn(NULL, (ERROR_UNSUPPORTED, "sdtRead(%s): unsupported data type '%s'\n", fname, colon));
        }
        else
        {
          fclose(fp);
          ErrorReturn(NULL, (ERROR_BADFILE, "sdtRead(%s): unknown data type '%s'\n", fname, colon));
        }
      }
      else if(strcmp(line, "interval") == 0)
      {
        if(ndim == 3)
          sscanf(colon, "%f %f %f", &xsize, &ysize, &zsize);
        else
          sscanf(colon, "%f %f %f %f", &xsize, &ysize, &zsize, &dummy_size);
        xsize *= 10.0;
        ysize *= 10.0;
        zsize *= 10.0;
      }
      else if(strcmp(line, "sdtOrient") == 0)
      {
        while(isspace((int)*colon))
          colon++;
        if(strncmp(colon, "sag", 3) == 0)
          orientation = MRI_SAGITTAL;
        else if(strncmp(colon, "ax", 2) == 0)
          orientation = MRI_HORIZONTAL;
        else if(strncmp(colon, "cor", 3) == 0)
          orientation = MRI_CORONAL;
        else
        {
          fclose(fp);
          ErrorReturn(NULL, (ERROR_BADFILE, "sdtRead(%s): unknown orientation %s\n", fname, colon));
        }
      }
      else
      {
      }
    }
  }

  fclose(fp);

  if(data_type == -1)
    ErrorReturn(NULL, (ERROR_BADFILE, "sdtRead(%s): data type undefined\n", fname));
  if(dim[0] == -1 || dim[1] == -1 || dim[2] == -1)
    ErrorReturn(NULL, (ERROR_BADFILE, "sdtRead(%s): one or more dimensions undefined\n", fname));

  if(read_volume)
  {
    if((fp = fopen(fname, "r")) == NULL)
      ErrorReturn(NULL, (ERROR_BADFILE, "sdtRead(%s): error opening data file %s\n", fname, fname));

    mri = MRIreadRaw(fp, dim[0], dim[1], dim[2], data_type);

    if(mri == NULL)
      return(NULL);

    fclose(fp);

  }
  else
  {
    mri = MRIallocHeader(dim[0], dim[1], dim[2], data_type);
    if(mri == NULL)
      return(NULL);
  }

  mri->xsize = xsize;
  mri->ysize = ysize;
  mri->zsize = zsize;

  mri->slice_direction = orientation;

  if(orientation == MRI_CORONAL)
  {
    mri->xdir = XDIM;
    mri->ydir = YDIM;
    mri->zdir = ZDIM;
  }
  if(orientation == MRI_SAGITTAL)
  {
    mri->xdir = ZDIM;
    mri->ydir = YDIM;
    mri->zdir = XDIM;
  }
  if(orientation == MRI_HORIZONTAL)
  {
    mri->xdir = XDIM;
    mri->ydir = -ZDIM;
    mri->zdir = YDIM;

  }

  mri->thick = mri->zsize;
  mri->xend = mri->xsize * mri->width / 2.;
  mri->yend = mri->ysize * mri->height / 2.;
  mri->zend = mri->zsize * mri->depth / 2.;
  mri->xstart = -mri->xend;
  mri->ystart = -mri->yend;
  mri->zstart = -mri->zend;

  mri->imnr0 = 1;
  mri->imnr1 = dim[2];

  mri->ps = 1.0 /*0.001*/;
  mri->tr = 0 ;
  mri->te = 0 ;
  mri->ti = 0 ;

  strcpy(mri->fname, fname) ;

  return(mri);

} /* end sdtRead() */

MRI *MRIreadRaw(FILE *fp, int width, int height, int depth, int type)
{
  MRI     *mri ;
  BUFTYPE *buf ;
  int     slice, pixels ;
  int     i;

  mri = MRIalloc(width, height, depth, type) ;
  if (!mri)
    return(NULL) ;

  pixels = width*height ;
  buf = (BUFTYPE *)calloc(pixels, data_size[type]) ;

  /* every width x height pixels should be another slice */
  for (slice = 0 ; slice < depth ; slice++)
  {
    if (fread(buf, data_size[type], pixels, fp) != pixels)
      ErrorReturn(NULL,
                  (ERROR_BADFILE, "%s: could not read %dth slice (%d)",
                   Progname, slice, pixels)) ;
    if(type == 0)
      buffer_to_image(buf, mri, slice, 0) ;
    if(type == 1)
    {
      for(i = 0;i < pixels;i++)
        ((int *)buf)[i] = orderIntBytes(((int *)buf)[i]);
      int_buffer_to_image((int *)buf, mri, slice, 0);
    }
    if(type == 2)
    {
      for(i = 0;i < pixels;i++)
        ((long *)buf)[i] = orderLongBytes(((long *)buf)[i]);
      long_buffer_to_image((long *)buf, mri, slice, 0);
    }
    if(type == 3)
    {
      for(i = 0;i < pixels;i++)
        ((float *)buf)[i] = orderFloatBytes(((float *)buf)[i]);
      float_buffer_to_image((float *)buf, mri, slice, 0);
    }
    if(type == 4)
    {
      for(i = 0;i < pixels;i++)
        ((short *)buf)[i] = orderShortBytes(((short *)buf)[i]);
      short_buffer_to_image((short *)buf, mri, slice, 0);
    }
  }

  MRIinitHeader(mri) ;
  free(buf) ;
  return(mri) ;
}

static void
int_buffer_to_image(int *buf, MRI *mri, int slice, int frame)
{
  int           y, width, height ;
  int           *pslice ;
  
  width = mri->width ;
  height = mri->height ;
  for (y = 0 ; y < height ; y++)
  {
    pslice = &MRIIseq_vox(mri, 0, y, slice, frame) ;
    memcpy(pslice, buf, width*sizeof(int)) ;
    buf += width ;
  }
}

#if 0
static void
image_to_int_buffer(int *buf, MRI *mri, int slice)
{
  int y, x, width, height, depth ;

  width = mri->width ;
  height = mri->height ;
  depth = mri->depth;
  for (y=0; y < height ; y++)
  {
    if(mri->type == MRI_UCHAR)
    {
      for(x = 0;x < depth;x++)
        buf[x] = (int)MRIvox(mri, x, y, slice);
    }
    else if(mri->type == MRI_SHORT)
    {
      for(x = 0;x < depth;x++)
        buf[x] = (int)MRISvox(mri, x, y, slice);
    }
    else if(mri->type == MRI_LONG)
    {
      for(x = 0;x < depth;x++)
        buf[x] = (int)MRILvox(mri, x, y, slice);
    }
    else if(mri->type == MRI_FLOAT)
    {
      for(x = 0;x < depth;x++)
        buf[x] = (int)MRIFvox(mri, x, y, slice);
    }
    else
    {
      memcpy(buf, mri->slices[slice][y], width*sizeof(int)) ;
    }

    buf += width ;
  }
}
static void
image_to_long_buffer(long *buf, MRI *mri, int slice)
{
  int y, x, width, height, depth ;

  width = mri->width ;
  height = mri->height ;
  depth = mri->depth;
  for (y=0; y < height ; y++)
  {
    if(mri->type == MRI_UCHAR)
    {
      for(x = 0;x < depth;x++)
        buf[x] = (long)MRIvox(mri, x, y, slice);
    }
    else if(mri->type == MRI_INT)
    {
      for(x = 0;x < depth;x++)
        buf[x] = (long)MRIIvox(mri, x, y, slice);
    }
    else if(mri->type == MRI_SHORT)
    {
      for(x = 0;x < depth;x++)
        buf[x] = (long)MRISvox(mri, x, y, slice);
    }
    else if(mri->type == MRI_FLOAT)
    {
      for(x = 0;x < depth;x++)
        buf[x] = (long)MRIFvox(mri, x, y, slice);
    }
    else
    {
      memcpy(buf, mri->slices[slice][y], width*sizeof(long)) ;
    }

    buf += width ;
  }
}
static void
image_to_float_buffer(float *buf, MRI *mri, int slice)
{
  int y, x, width, height, depth ;

  width = mri->width ;
  height = mri->height ;
  depth = mri->depth;
  for (y=0; y < height ; y++)
  {
    if(mri->type == MRI_UCHAR)
    {
      for(x = 0;x < depth;x++)
        buf[x] = (float)MRIvox(mri, x, y, slice);
    }
    else if(mri->type == MRI_INT)
    {
      for(x = 0;x < depth;x++)
        buf[x] = (float)MRIIvox(mri, x, y, slice);
    }
    else if(mri->type == MRI_LONG)
    {
      for(x = 0;x < depth;x++)
        buf[x] = (float)MRILvox(mri, x, y, slice);
    }
    else if(mri->type == MRI_SHORT)
    {
      for(x = 0;x < depth;x++)
        buf[x] = (float)MRISvox(mri, x, y, slice);
    }
    else
    {
      memcpy(buf, mri->slices[slice][y], width*sizeof(float)) ;
    }

    buf += width ;
  }
}
#endif

static void
long_buffer_to_image(long *buf, MRI *mri, int slice, int frame)
{
  int           y, width, height ;
  long          *pslice ;
  
  width = mri->width ;
  height = mri->height ;
  for (y = 0 ; y < height ; y++)
  {
    pslice = &MRILseq_vox(mri, 0, y, slice, frame) ;
    memcpy(pslice, buf, width*sizeof(long)) ;
    buf += width ;
  }
}


static void
float_buffer_to_image(float *buf, MRI *mri, int slice, int frame)
{
  int           y, width, height ;
  float         *pslice ;
  
  width = mri->width ;
  height = mri->height ;
  for (y = 0 ; y < height ; y++)
  {
    pslice = &MRIFseq_vox(mri, 0, y, slice, frame) ;
    memcpy(pslice, buf, width*sizeof(float)) ;
    buf += width ;
  }
}

static void
short_buffer_to_image(short *buf, MRI *mri, int slice, int frame)
{
  int           y, width, height ;
  short         *pslice ;
  
  width = mri->width ;
  height = mri->height ;
  for (y = 0 ; y < height ; y++)
  {
    pslice = &MRISseq_vox(mri, 0, y, slice, frame) ;
    memcpy(pslice, buf, width*sizeof(short)) ;
    buf += width ;
  }
}

static void
buffer_to_image(BUFTYPE *buf, MRI *mri, int slice, int frame)
{
  int           y, width, height ;
  BUFTYPE       *pslice ;
  
  width = mri->width ;
  height = mri->height ;
  for (y = 0 ; y < height ; y++)
  {
    pslice = &MRIseq_vox(mri, 0, y, slice, frame) ;
    memcpy(pslice, buf, width*sizeof(BUFTYPE)) ;
    buf += width ;
  }
}

#define UNUSED_SPACE_SIZE 256
#define USED_SPACE_SIZE   (3*sizeof(float)+4*3*sizeof(float))

#define MGH_VERSION       1

static MRI *
mghRead(char *fname, int read_volume, int frame)
{
  MRI  *mri ;
  FILE  *fp ;
  int   start_frame, end_frame, width, height, depth, nframes, type, x, y, z,
        bpv, dof, bytes, version, ival, unused_space_size, good_ras_flag ;
  BUFTYPE *buf ;
  char   unused_buf[UNUSED_SPACE_SIZE+1] ;
  float  fval, xsize, ysize, zsize, x_r, x_a, x_s, y_r, y_a, y_s,
         z_r, z_a, z_s, c_r, c_a, c_s ;
  short  sval ;

  /* keep the compiler quiet */
  xsize = ysize = zsize = 0;
  x_r = x_a = x_s = 0;
  y_r = y_a = y_s = 0;
  z_r = z_a = z_s = 0;
  c_r = c_a = c_s = 0;

  fp = fopen(fname, "rb") ;
  if (!fp)
    ErrorReturn(NULL, (ERROR_BADPARM,"mghRead(%s, %d): could not open file",
                       fname, frame)) ;
  version = freadInt(fp) ;
  width = freadInt(fp) ;
  height = freadInt(fp) ;
  depth =  freadInt(fp) ;
  nframes = freadInt(fp) ;
  type = freadInt(fp) ;
  dof = freadInt(fp) ;

  unused_space_size = UNUSED_SPACE_SIZE-sizeof(short) ;

  good_ras_flag = freadShort(fp) ;
  if (good_ras_flag)     /* has RAS and voxel size info */
  {
    unused_space_size -= USED_SPACE_SIZE ;
    xsize = freadFloat(fp) ;
    ysize = freadFloat(fp) ;
    zsize = freadFloat(fp) ;
    
    x_r = freadFloat(fp) ; x_a = freadFloat(fp) ; x_s = freadFloat(fp) ;
    y_r = freadFloat(fp) ; y_a = freadFloat(fp) ; y_s = freadFloat(fp) ;
    
    z_r = freadFloat(fp) ; z_a = freadFloat(fp) ; z_s = freadFloat(fp) ;
    c_r = freadFloat(fp) ; c_a = freadFloat(fp) ; c_s = freadFloat(fp) ;
  }

  /* so stuff can be added to the header in the future */
  fread(unused_buf, sizeof(char), unused_space_size, fp) ;

  switch (type)
  {
  default:
  case MRI_FLOAT:  bpv = sizeof(float) ; break ;
  case MRI_UCHAR:  bpv = sizeof(char)  ; break ;
  case MRI_SHORT:  bpv = sizeof(short) ; break ;
  case MRI_INT:     bpv = sizeof(int) ; break ;
  }
  bytes = width * height * bpv ;  /* bytes per slice */
  if (frame >= 0)
  {
    start_frame = end_frame = frame ;
    fseek(fp, frame*width*height*depth*bpv, SEEK_CUR) ;
    nframes = 1 ;
  }
  else
  {  /* hack - # of frames < -1 means to only read in that
        many frames. Otherwise I would have had to change the whole
        MRIread interface and that was too much of a pain. Sorry.
     */
    if (frame < -1)  
    { nframes = frame*-1 ; } 
    start_frame = 0 ; end_frame = nframes-1 ;
  }
  if (!read_volume)
  {
    mri = MRIallocHeader(width, height, depth, type) ;
    mri->dof = dof ;
  }
  else
  {
    if (type == MRI_UCHAR)
      buf = (BUFTYPE *)calloc(bytes, sizeof(BUFTYPE)) ;
    else
      buf = NULL ;
    mri = MRIallocSequence(width, height, depth, type, nframes) ;
    mri->dof = dof ;
    for (frame = start_frame ; frame <= end_frame ; frame++)
    {
      for (z = 0 ; z < depth ; z++)
      {
        switch (type)
        {
          case MRI_INT:
          for (y = 0 ; y < height ; y++)
          {
            for (x = 0 ; x < width ; x++)
            {
              ival = freadInt(fp) ; 
              MRIIseq_vox(mri,x,y,z,frame-start_frame) = ival ;
            }
          }
          break ;
          case MRI_SHORT:
          for (y = 0 ; y < height ; y++)
          {
            for (x = 0 ; x < width ; x++)
            {
              sval = freadShort(fp) ; 
              MRISseq_vox(mri,x,y,z,frame-start_frame) = sval ;
            }
          }
          break ;
          case MRI_FLOAT:
          for (y = 0 ; y < height ; y++)
          {
            for (x = 0 ; x < width ; x++)
            {
              fval = freadFloat(fp) ; 
              MRIFseq_vox(mri,x,y,z,frame-start_frame) = fval ;
            }
          }
          break ;
        case MRI_UCHAR:
          if (fread(buf, sizeof(BUFTYPE), bytes, fp) != bytes)
            ErrorReturn(NULL,
                        (ERROR_BADFILE, "%s: could not read %dth slice (%d)",
                         Progname, z, bytes)) ;
          buffer_to_image(buf, mri, z, frame-start_frame) ;
          break ;
        default:
          ErrorReturn(NULL, 
                      (ERROR_UNSUPPORTED, "mghRead: unsupported type %d",
                       mri->type)) ;
          break ;
        }
      }
    }
    if (buf)
      free(buf) ;
  }

  fclose(fp) ;

  if (good_ras_flag)
  {
    mri->xsize =     xsize ;
    mri->ysize =     ysize ;
    mri->zsize =     zsize ;
    
    mri->x_r = x_r  ;
    mri->x_a = x_a  ;
    mri->x_s = x_s  ;
    
    mri->y_r = y_r  ;
    mri->y_a = y_a  ;
    mri->y_s = y_s  ;
    
    mri->z_r = z_r  ;
    mri->z_a = z_a  ;
    mri->z_s = z_s  ;
    
    mri->c_r = c_r  ;
    mri->c_a = c_a  ;
    mri->c_s = c_s  ;
    if (good_ras_flag > 0)
      mri->ras_good_flag = 1 ;
  }
  return(mri) ;
}

static int
mghWrite(MRI *mri, char *fname, int frame)
{
  FILE  *fp ;
  int   ival, start_frame, end_frame, x, y, z, width, height, depth, 
        unused_space_size ;
  char  buf[UNUSED_SPACE_SIZE+1] ;
  float fval ;
  short sval ;

  if (frame >= 0)
    start_frame = end_frame = frame ;
  else
  {
    start_frame = 0 ; end_frame = mri->nframes-1 ;
  }
  fp = fopen(fname, "wb") ;
  if (!fp)
    ErrorReturn(ERROR_BADPARM, 
                (ERROR_BADPARM,"mghWrite(%s, %d): could not open file",
                 fname, frame)) ;

  /* WARNING - adding or removing anything before nframes will
     cause mghAppend to fail.
  */
  width = mri->width ; height = mri->height ; depth = mri->depth ; 
  fwriteInt(MGH_VERSION, fp) ;
  fwriteInt(mri->width, fp) ;
  fwriteInt(mri->height, fp) ;
  fwriteInt(mri->depth, fp) ;
  fwriteInt(mri->nframes, fp) ;
  fwriteInt(mri->type, fp) ;
  fwriteInt(mri->dof, fp) ;

  unused_space_size = UNUSED_SPACE_SIZE - USED_SPACE_SIZE - sizeof(short) ;

  /* write RAS and voxel size info */
  fwriteShort(mri->ras_good_flag ? 1 : -1, fp) ;
  fwriteFloat(mri->xsize, fp) ;
  fwriteFloat(mri->ysize, fp) ;
  fwriteFloat(mri->zsize, fp) ;

  fwriteFloat(mri->x_r, fp) ;
  fwriteFloat(mri->x_a, fp) ;
  fwriteFloat(mri->x_s, fp) ;

  fwriteFloat(mri->y_r, fp) ;
  fwriteFloat(mri->y_a, fp) ;
  fwriteFloat(mri->y_s, fp) ;

  fwriteFloat(mri->z_r, fp) ;
  fwriteFloat(mri->z_a, fp) ;
  fwriteFloat(mri->z_s, fp) ;

  fwriteFloat(mri->c_r, fp) ;
  fwriteFloat(mri->c_a, fp) ;
  fwriteFloat(mri->c_s, fp) ;

  /* so stuff can be added to the header in the future */
  memset(buf, 0, UNUSED_SPACE_SIZE*sizeof(char)) ;
  fwrite(buf, sizeof(char), unused_space_size, fp) ;

  for (frame = start_frame ; frame <= end_frame ; frame++)
  {
    for (z = 0 ; z < depth ; z++)
    {
      for (y = 0 ; y < height ; y++)
      {
        switch (mri->type)
        {
        case MRI_SHORT:
          for (x = 0 ; x < width ; x++)
          {
            if (z == 74 && y == 16 && x == 53)
              DiagBreak() ;
            sval = MRISseq_vox(mri,x,y,z,frame) ;
            fwriteShort(sval, fp) ;
          }
          break ;
        case MRI_INT:
          for (x = 0 ; x < width ; x++)
          {
            if (z == 74 && y == 16 && x == 53)
              DiagBreak() ;
            ival = MRIIseq_vox(mri,x,y,z,frame) ;
            fwriteInt(ival, fp) ;
          }
          break ;
        case MRI_FLOAT:
          for (x = 0 ; x < width ; x++)
          {
            if (z == 74 && y == 16 && x == 53)
              DiagBreak() ;
            fval = MRIFseq_vox(mri,x,y,z,frame) ;
            fwriteFloat(fval, fp) ;
          }
          break ;
        case MRI_UCHAR:
          if (fwrite(&MRIseq_vox(mri,0,y,z,frame), sizeof(BUFTYPE), width, fp) 
              != width)
            ErrorReturn(ERROR_BADFILE, 
                        (ERROR_BADFILE, 
                         "mghWrite: could not write %d bytes to %s",
                         width, fname)) ;
          break ;
        default:
          ErrorReturn(ERROR_UNSUPPORTED, 
                      (ERROR_UNSUPPORTED, "mghWrite: unsupported type %d",
                       mri->type)) ;
          break ;
        }
      }
    }
  }

  fclose(fp) ;
  return(NO_ERROR) ;
}


MRI *
MRIreorder(MRI *mri_src, MRI *mri_dst, int xdim, int ydim, int zdim)
{
  int  width, height, depth, xs, ys, zs, xd, yd, zd, x, y, z ;
  float ras_sign;

  width = mri_src->width ; height = mri_src->height ; depth = mri_src->depth;
  if (!mri_dst)
    mri_dst = MRIclone(mri_src, NULL) ;

  /* check that the source ras coordinates are good and that each direction is used once and only once */
  if(mri_src->ras_good_flag)
    if(abs(xdim) * abs(ydim) * abs(zdim) != 6 || abs(xdim) + abs(ydim) + abs(zdim) != 6)
      mri_dst->ras_good_flag = 0;

  xd = yd = zd = 0 ;

  ras_sign = (xdim < 0 ? -1.0 : 1.0);
  switch (abs(xdim))
  {
  default:
  case XDIM: 
    if (mri_dst->width != width)
      ErrorReturn(NULL,(ERROR_BADPARM, "MRIreorder: incorrect dst width"));
/*
    if(mri_dst->ras_good_flag)
    {
      mri_dst->x_r = mri_src->x_r * ras_sign;
      mri_dst->x_a = mri_src->x_a * ras_sign;
      mri_dst->x_s = mri_src->x_s * ras_sign;
    }
*/
    break ;
  case YDIM: 
    if (mri_dst->height != width)
      ErrorReturn(NULL,(ERROR_BADPARM, "MRIreorder: incorrect dst width"));
/*
    if(mri_dst->ras_good_flag)
    {
      mri_dst->y_r = mri_src->x_r * ras_sign;
      mri_dst->y_a = mri_src->x_a * ras_sign;
      mri_dst->y_s = mri_src->x_s * ras_sign;
    }
*/
    break ;
  case ZDIM: 
    if (mri_dst->depth != width)
      ErrorReturn(NULL,(ERROR_BADPARM, "MRIreorder: incorrect dst width"));
/*
    if(mri_dst->ras_good_flag)
    {
      mri_dst->z_r = mri_src->x_r * ras_sign;
      mri_dst->z_a = mri_src->x_a * ras_sign;
      mri_dst->z_s = mri_src->x_s * ras_sign;
    }
*/
    break ;
  }
  ras_sign = (ydim < 0 ? -1.0 : 1.0);
  switch (abs(ydim))
  {
  default:
  case XDIM: 
    if (mri_dst->width != height)
      ErrorReturn(NULL,(ERROR_BADPARM, "MRIreorder: incorrect dst height"));
/*
    if(mri_dst->ras_good_flag)
    {
      mri_dst->x_r = mri_src->y_r * ras_sign;
      mri_dst->x_a = mri_src->y_a * ras_sign;
      mri_dst->x_s = mri_src->y_s * ras_sign;
    }
*/
    break ;
  case YDIM: 
    if (mri_dst->height != height)
      ErrorReturn(NULL,(ERROR_BADPARM, "MRIreorder: incorrect dst height"));
/*
    if(mri_dst->ras_good_flag)
    {
      mri_dst->y_r = mri_src->y_r * ras_sign;
      mri_dst->y_a = mri_src->y_a * ras_sign;
      mri_dst->y_s = mri_src->y_s * ras_sign;
    }
*/
    break ;
  case ZDIM: 
    if (mri_dst->depth != height)
      ErrorReturn(NULL,(ERROR_BADPARM, "MRIreorder: incorrect dst height"));
/*
    if(mri_dst->ras_good_flag)
    {
      mri_dst->z_r = mri_src->y_r * ras_sign;
      mri_dst->z_a = mri_src->y_a * ras_sign;
      mri_dst->z_s = mri_src->y_s * ras_sign;
    }
*/
    break ;
  }
  ras_sign = (zdim < 0 ? -1.0 : 1.0);
  switch (abs(zdim))
  {
  default:
  case XDIM: 
    if (mri_dst->width != depth)
      ErrorReturn(NULL,(ERROR_BADPARM, "MRIreorder: incorrect dst depth"));
/*
    if(mri_dst->ras_good_flag)
    {
      mri_dst->x_r = mri_src->z_r * ras_sign;
      mri_dst->x_a = mri_src->z_a * ras_sign;
      mri_dst->x_s = mri_src->z_s * ras_sign;
    }
*/
    break ;
  case YDIM: 
    if (mri_dst->height != depth)
      ErrorReturn(NULL,(ERROR_BADPARM, "MRIreorder: incorrect dst depth"));
/*
    if(mri_dst->ras_good_flag)
    {
      mri_dst->y_r = mri_src->z_r * ras_sign;
      mri_dst->y_a = mri_src->z_a * ras_sign;
      mri_dst->y_s = mri_src->z_s * ras_sign;
    }
*/
    break ;
  case ZDIM: 
    if (mri_dst->depth != depth)
      ErrorReturn(NULL,(ERROR_BADPARM, "MRIreorder: incorrect dst depth"));
/*
    if(mri_dst->ras_good_flag)
    {
      mri_dst->z_r = mri_src->z_r * ras_sign;
      mri_dst->z_a = mri_src->z_a * ras_sign;
      mri_dst->z_s = mri_src->z_s * ras_sign;
    }
*/
    break ;
  }

  for (zs = 0 ; zs < depth ; zs++)
  {
    if (zdim < 0)
      z = depth - zs - 1 ;
    else
      z = zs ;
    switch (abs(zdim))
    {
    case XDIM:  xd = z ; break ;
    case YDIM:  yd = z ; break ;
    default:
    case ZDIM:  zd = z ; break ;
    }
    for (ys = 0 ; ys < height ; ys++)
    {
      if (ydim < 0)
        y = height - ys - 1 ;
      else
        y = ys ;
      switch (abs(ydim))
      {
      case XDIM: xd = y ; break ;
      case YDIM: yd = y ; break ;
      default:
      case ZDIM: zd = y ; break ;
      }
      for (xs = 0 ; xs < width ; xs++)
      {
        if (xdim < 0)
          x = width - xs - 1 ;
        else
          x = xs ;
        switch (abs(xdim))
        {
          case XDIM: xd = x ; break ;
          case YDIM: yd = x ; break ;
          default:
          case ZDIM: zd = x ; break ;
        }
        switch (mri_src->type)
        {
        case MRI_SHORT:
          MRISvox(mri_dst, xd, yd, zd) = MRISvox(mri_src, xs, ys, zs) ;
          break ;
        case MRI_FLOAT:
          MRIFvox(mri_dst, xd, yd, zd) = MRIFvox(mri_src, xs, ys, zs) ;
          break ;
        case MRI_INT:
          MRIIvox(mri_dst, xd, yd, zd) = MRIIvox(mri_src, xs, ys, zs) ;
          break ;
        case MRI_UCHAR:
          MRIvox(mri_dst, xd, yd, zd) = MRIvox(mri_src, xs, ys, zs) ;
          break ;
        default:
          ErrorReturn(NULL,
                      (ERROR_UNSUPPORTED, 
                       "MRIreorder: unsupported voxel format %d",
                       mri_src->type)) ;
          break ;
        }
      }
    }
  }
  return(mri_dst) ;
}

/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
           Write the MRI header information to the file
           COR-.info in the directory specified by 'fpref'
------------------------------------------------------*/
int
MRIwriteInfo(MRI *mri, char *fpref)
{
  FILE    *fp;
  char    fname[STRLEN];

  sprintf(fname,"%s/%s",fpref, INFO_FNAME);
  fp = fopen(fname,"w");
  if (fp == NULL) 
    ErrorReturn(ERROR_NO_FILE, 
                (ERROR_NO_FILE,
                 "MRIwriteInfo(%s): could not open %s.\n", fpref, fname)) ;

  fprintf(fp, "%s %d\n", "imnr0", mri->imnr0);
  fprintf(fp, "%s %d\n", "imnr1", mri->imnr1);
  fprintf(fp, "%s %d\n", "ptype", 
          mri->slice_direction == MRI_CORONAL ? 2 :
          mri->slice_direction == MRI_HORIZONTAL ? 0 : 1) ;
  fprintf(fp, "%s %d\n", "x", mri->width);
  fprintf(fp, "%s %d\n", "y", mri->height);
  fprintf(fp, "%s %f\n", "fov", mri->fov/MM_PER_METER);
  fprintf(fp, "%s %f\n", "thick", mri->ps/MM_PER_METER);
  fprintf(fp, "%s %f\n", "psiz", mri->ps/MM_PER_METER);
  fprintf(fp, "%s %f\n", "locatn", mri->location); /* locatn */
  fprintf(fp, "%s %f\n", "strtx", mri->xstart/MM_PER_METER); /* strtx */
  fprintf(fp, "%s %f\n", "endx", mri->xend/MM_PER_METER); /* endx */
  fprintf(fp, "%s %f\n", "strty", mri->ystart/MM_PER_METER); /* strty */
  fprintf(fp, "%s %f\n", "endy", mri->yend/MM_PER_METER); /* endy */
  fprintf(fp, "%s %f\n", "strtz", mri->zstart/MM_PER_METER); /* strtz */
  fprintf(fp, "%s %f\n", "endz", mri->zend/MM_PER_METER); /* endz */
  fprintf(fp, "%s %f\n", "tr", mri->tr) ;
  fprintf(fp, "%s %f\n", "te", mri->te) ;
  fprintf(fp, "%s %f\n", "ti", mri->ti) ;
  if (mri->linear_transform)
  {
    char fname[STRLEN] ;

/* 
   this won't work for relative paths which are not the same for the
   destination directory as for the the source directory.
   */
    sprintf(fname,"%s", mri->transform_fname);
    fprintf(fp, "xform %s\n", fname) ;

#if 0
    /* doesn't work - I don't know why */
    if (output_transform_file(fname, "talairach xfm", &mri->transform) != OK)
      ErrorPrintf(ERROR_BADFILE, "MRIwriteInfo(%s): xform write failed",fpref);
#endif

  }

  fprintf(fp, "%s %d\n", "ras_good_flag", mri->ras_good_flag);
  fprintf(fp, "%s %f %f %f\n", "x_ras", mri->x_r, mri->x_a, mri->x_s);
  fprintf(fp, "%s %f %f %f\n", "y_ras", mri->y_r, mri->y_a, mri->y_s);
  fprintf(fp, "%s %f %f %f\n", "z_ras", mri->z_r, mri->z_a, mri->z_s);
  fprintf(fp, "%s %f %f %f\n", "c_ras", mri->c_r, mri->c_a, mri->c_s);

  fclose(fp);

  return(NO_ERROR) ;
}

/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
          Write an MRI header and a set of data files to
          the directory specified by 'fpref'
------------------------------------------------------*/
int
MRIappend(MRI *mri, char *fpref)
{
  int      type, frame ;
  char     fname[STRLEN] ;

  MRIunpackFileName(fpref, &frame, &type, fname) ;
  if (type == MRI_MGH_FILE)
    return(mghAppend(mri, fname, frame)) ;
  else
    ErrorReturn(ERROR_UNSUPPORTED, 
                (ERROR_UNSUPPORTED, "MRIappend(%s): file type not supported",
                fname)) ;

  return(NO_ERROR) ;
}

static int
mghAppend(MRI *mri, char *fname, int frame)
{
  FILE  *fp ;
  int   start_frame, end_frame, x, y, z, width, height, depth, nframes ;

  if (frame >= 0)
    start_frame = end_frame = frame ;
  else
  {
    start_frame = 0 ; end_frame = mri->nframes-1 ;
  }
  fp = fopen(fname, "rb") ;
  if (!fp)   /* doesn't exist */
    return(mghWrite(mri, fname, frame)) ;
  fclose(fp) ;
  fp = fopen(fname, "r+b") ;
  if (!fp)
    ErrorReturn(ERROR_BADPARM, 
                (ERROR_BADPARM,"mghAppend(%s, %d): could not open file",
                 fname, frame)) ;

  /* WARNING - this is dependent on the order of writing in mghWrite */
  width = mri->width ; height = mri->height ; depth = mri->depth ;
  fseek(fp, 4*sizeof(int), SEEK_SET) ;
  nframes = freadInt(fp) ;
  fseek(fp, 4*sizeof(int), SEEK_SET) ;
  fwriteInt(nframes+end_frame-start_frame+1, fp) ;
  fseek(fp, 0, SEEK_END) ;

  for (frame = start_frame ; frame <= end_frame ; frame++)
  {
    for (z = 0 ; z < depth ; z++)
    {
      for (y = 0 ; y < height ; y++)
      {
        switch (mri->type)
        {
        case MRI_FLOAT:
          for (x = 0 ; x < width ; x++)
          {
            fwriteFloat(MRIFseq_vox(mri,x,y,z,frame), fp) ;
          }
          break ;
        case MRI_UCHAR:
          if (fwrite(&MRIseq_vox(mri,0,y,z,frame), sizeof(BUFTYPE), width, fp) 
              != width)
            ErrorReturn(ERROR_BADFILE, 
                        (ERROR_BADFILE, 
                         "mghAppend: could not write %d bytes to %s",
                         width, fname)) ;
          break ;
        default:
          ErrorReturn(ERROR_UNSUPPORTED, 
                      (ERROR_UNSUPPORTED, "mghAppend: unsupported type %d",
                       mri->type)) ;
          break ;
        }
      }
    }
  }

  fclose(fp) ;
  return(NO_ERROR) ;
}

/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
int
MRIunpackFileName(char *inFname, int *pframe, int *ptype, char *outFname)
{
  char *number = NULL, *at = NULL, buf[STRLEN] ;
  struct stat stat_buf;

  strcpy(outFname, inFname) ;
  number = strrchr(outFname, '#') ;
  at = strrchr(outFname, '@');

  if(at)
    *at = '\0';

  if (number)   /* '#' in filename indicates frame # */
  {
    if (sscanf(number+1, "%d", pframe) < 1)
      *pframe = -1 ;
    *number = 0 ;
  }
  else
    *pframe = -1 ;

  if (at)
  {
    at = StrUpper(strcpy(buf, at+1)) ;
    if (!strcmp(at, "MNC"))
      *ptype = MRI_MINC_FILE ;
    else if (!strcmp(at, "MINC"))
      *ptype = MRI_MINC_FILE ;
    else if (!strcmp(at, "BRIK"))
      *ptype = BRIK_FILE ;
    else if (!strcmp(at, "SIEMENS"))
      *ptype = SIEMENS_FILE ;
    else if (!strcmp(at, "MGH"))
      *ptype = MRI_MGH_FILE ;
    else if (!strcmp(at, "MR"))
      *ptype = GENESIS_FILE ;
    else if (!strcmp(at, "GE"))
      *ptype = GE_LX_FILE ;
    else if (!strcmp(at, "IMG"))
      *ptype = MRI_ANALYZE_FILE ;
    else if (!strcmp(at, "COR"))
      *ptype = MRI_CORONAL_SLICE_DIRECTORY ;
    else if (!strcmp(at, "BSHORT"))
      *ptype = BSHORT_FILE;
    else if (!strcmp(at, "SDT"))
      *ptype = SDT_FILE;
    else
      ErrorExit(ERROR_UNSUPPORTED, "unknown file type %s", at);
  }
  else  /* no '@' found */
  {

    *ptype = -1;


    if(is_genesis(outFname))
      *ptype = GENESIS_FILE;
    else if(is_ge_lx(outFname))
      *ptype = GE_LX_FILE;
    else if(is_brik(outFname))
      *ptype = BRIK_FILE;
    else if(is_siemens(outFname))
      *ptype = SIEMENS_FILE;
    else if(is_analyze(outFname))
      *ptype = MRI_ANALYZE_FILE;
    else if(is_sdt(outFname))
      *ptype = SDT_FILE;
    else if(is_mgh(outFname))
      *ptype = MRI_MGH_FILE;
    else if(is_mnc(outFname))
      *ptype = MRI_MINC_FILE;
    else if(is_bshort(outFname))
      *ptype = BSHORT_FILE;
    else 
    {
      if(stat(outFname, &stat_buf) < 0)
      {
        ErrorReturn(ERROR_BADFILE, (ERROR_BAD_FILE, "can't stat file %s", outFname));
      }
      if(S_ISDIR(stat_buf.st_mode))
        *ptype = MRI_CORONAL_SLICE_DIRECTORY;
    }

    if(*ptype == -1)
      ErrorReturn(ERROR_BADPARM, (ERROR_BADPARM, "unrecognized file type for file %s", outFname));

  }

  return(NO_ERROR) ;
}

/* EOF */
