
/*  Author:  Travis Oliphant
 *  Copyright:  2003
 *
 *  Ported to GraphicsMagick by Christian Klein
 *
 *  See LICENSE for explanation of terms
 */

#include <Python.h>
#include <setjmp.h>
#include <Numeric/arrayobject.h>
#include <magick/api.h>

/* Compatibility 
*/


#ifndef ThrowBinaryException
#define ThrowBinaryException(severity,tag,context) \
{ \
  if (image != (Image *) NULL) \
    (void) ThrowException(&image->exception, severity, tag, context); \
  return(MagickFalse); \
}
#endif


/* End
 */


static PyObject *PyMagickError;
static jmp_buf error_jmp;
static int ptype;
static size_t _qsize;

#if !defined(DegreesToRadians)
#define DegreesToRadians(x) ((x)*3.14159265358979323846/180.0)
#endif

#define False 0
#define True 1
#define DATA(arr) (((PyArrayObject *)(arr))->data)
#define ELSIZE(arr) (((PyArrayObject *)(arr))->descr->elsize)
#define TYPE(arr) (((PyArrayObject *)(arr))->descr->type_num)
#define RANK(arr) (((PyArrayObject *)(arr))->nd)
#define DIMS(arr) (((PyArrayObject *)(arr))->dimensions)
#define DIM(arr,n) ((((PyArrayObject *)(arr))->dimensions)[(n)])
#define ASARR(arr) ((PyArrayObject *)(arr))
#define ASIM(im) ((PyMImageObject *)(im))
#define ASDI(di) ((PyDrawInfoObject *)(di))

#define STRIDES(arr) (((PyArrayObject *)(arr))->strides)
#define STRIDE(arr,n) ((((PyArrayObject *)(arr))->strides)[(n)])

#define ERRMSG(s) {PyErr_SetString(PyMagickError, (s)); goto fail;}
#define ERRMSG4(st,ind) PyErr_Format(PyMagickError, \
                        "Undefined %d in %s", ind, st ); \
                        return NULL
#define STR2PYSTR(str) \
            ((str) ? PyString_FromString((str)) : PyString_FromString(""))
#define ENUM2STR(str,val,attr) \
            if (((val) >= 0) && ((val) < (long) NumberOf((str))-1)) \
                return STR2PYSTR((str)[(val)]); \
            ERRMSG4((attr), (val))

static ExceptionInfo exception;
#define PyMagickErr(exc)       ((exc).severity != UndefinedException)
#define ERR(exc) {              \
    if ((exc).severity < ErrorException) {      \
        fprintf(stderr, "Exception %d: %.512s%s%.512s%s",   \
            (exc).severity,             \
            ((exc).reason ? (exc).reason : "ERROR"),    \
            ((exc).description ? " (" : ""),        \
            ((exc).description ? (exc).description : ""),   \
            ((exc).description ? ")" : ""));            \
        SetExceptionInfo(&(exc),UndefinedException);        \
    }                                   \
    else {                              \
        PyErr_Format( PyMagickError,                \
              "Exception %d: %.512s%s%.512s%s",     \
              (exc).severity,               \
              ((exc).reason ? (exc).reason : "ERROR"),  \
              ((exc).description ? " (" : ""),      \
              ((exc).description ? (exc).description : ""), \
              ((exc).description ? ")" : ""));      \
        SetExceptionInfo(&(exc),UndefinedException);        \
        goto fail; \
    }              \
}
#define CLEAR_ERR  if (PyErr_Occurred()) PyErr_Clear()
#define CHECK_ERR  if PyMagickErr(exception) ERR(exception)
#define CHECK_ERR_IM(im)  if PyMagickErr((im)->exception) ERR((im)->exception)

#define ThrowImage2Exception(severity,tag,context)  \
{ \
  (void) ThrowException(exception, severity,tag, context);\
  return((Image *) NULL); \
}


#define NumberOf(array)  (sizeof((array))/sizeof(*(array)))

staticforward PyTypeObject MImage_Type;
staticforward PyTypeObject DrawInfo_Type;

#define PyMImage_Check(v)      ((v)->ob_type == &MImage_Type)
#define PyDrawInfo_Check(v)      ((v)->ob_type == &DrawInfo_Type)

#define DRAWALLOCSIZE 10000

typedef struct {
    PyObject_HEAD
    Image *ims;      /* Can be a single image or a linked list of images */
} PyMImageObject;

typedef struct {
    PyObject_HEAD
    DrawInfo *info;
    char *prim;
    long alloc;
    long len;
} PyDrawInfoObject;


/*
  Static declarations from PerlMagick  + Additions
*/
static char
  *AlignTypes[] =
  {
    "Undefined", "Left", "Center", "Right", (char *) NULL
  }, /*
  *BooleanTypes[] =
  {
    "False", "True", (char *) NULL
    }, */
  *ChannelTypes[] =
  {
    "Undefined", "Red", "Cyan", "Green", "Magenta", "Blue", "Yellow",
    "Opacity", "Black", "Matte", (char *) NULL
  },
  *ClassTypes[] =
  {
    "Undefined", "DirectClass", "PseudoClass", (char *) NULL
  },
  *ClipPathUnitss[] =
  {
      "UserSpace", "UserSpaceOnUse", "ObjectBoundingBox", (char *) NULL
  },
  *ColorspaceTypes[] =
  {
    "Undefined", "RGB", "Gray", "Transparent", "OHTA", "XYZ", "YCbCr",
    "YCC", "YIQ", "YPbPr", "YUV", "CMYK", "sRGB", (char *) NULL
  }, /*
  *ComplianceTypes[] = 
  {
    "Undefined", "No", "SVG", "X11", "XPM", "All", (char *) NULL
    },    */
  *CompositeTypes[] =
  {
    "Undefined", "Over", "In", "Out", "Atop", "Xor", "Plus", "Minus",
    "Add", "Subtract", "Difference", "Multiply", "Bumpmap", "Copy",
    "CopyRed", "CopyGreen", "CopyBlue", "CopyOpacity", "Clear", "Dissolve",
    "Displace", "Modulate", "Threshold", "No", "Darken", "Lighten",
    "Hue", "Saturate", "Colorize", "Luminize", "Screen", "Overlay",
    "ReplaceMatte", (char *) NULL
  },
  *CompressionTypes[] =
  {
    "Undefined", "None", "BZip", "Fax", "Group4", "JPEG", "LosslessJPEG",
    "LZW", "RLE", "Zip", (char *) NULL
  },
  *DisposeTypes[] =
  {
    "Undefined", "None", "Background", "Previous", (char *) NULL
  },
  *DecorationTypes[] = 
  {
      "No", "Underline", "Overline", "LineThrough", (char *)NULL
  },
  *EndianTypes[] =
  {
    "Undefined", "LSB", "MSB", (char *) NULL
  },
  *FillRules[] = 
  {
    "Undefined", "EvenOdd", "NonZero", (char *)NULL
  },
  *FilterTypess[] =
  {
    "Undefined", "Point", "Box", "Triangle", "Hermite", "Hanning",
    "Hamming", "Blackman", "Gaussian", "Quadratic", "Cubic", "Catrom",
    "Mitchell", "Lanczos", "Bessel", "Sinc", (char *) NULL
  }, /*
  *GradientTypes[] = 
  {
    "Undefined", "Linear", "Radial", (char *)NULL
    }, */
  *GravityTypes[] =
  {
    "Forget", "NorthWest", "North", "NorthEast", "West", "Center",
    "East", "SouthWest", "South", "SouthEast", "Static", (char *) NULL
  },
  *ImageTypes[] =
  {
    "Undefined", "Bilevel", "Grayscale", "GrayscaleMatte", "Palette",
    "PaletteMatte", "TrueColor", "TrueColorMatte", "ColorSeparation",
    "ColorSeparationMatte", "Optimize", (char *) NULL
  },
  *IntentTypes[] =
  {
    "Undefined", "Saturation", "Perceptual", "Absolute", "Relative",
    (char *) NULL
  },
  *InterlaceTypes[] =
  {
    "Undefined", "None", "Line", "Plane", "Partition", (char *) NULL
  }, /*
  *LogEventTypes[] =
  {
    "No", "Configure", "Annotate", "Render", "Locale", "Coder",
    "X11", "Cache", "Blob", "All", (char *) NULL
    }, */
  *LineCapTypes[] = 
  { 
    "Undefined", "Butt", "Round", "Square", (char *)NULL
   },
  *LineJoinTypes[] = 
  { 
    "Undefined", "Miter", "Round", "Bevel", (char *)NULL
   }, /*
  *MethodTypes[] =
  {
    "Point", "Replace", "Floodfill", "FillToBorder", "Reset", (char *) NULL
    },
  *ModeTypes[] =
  {
    "Undefined", "Frame", "Unframe", "Concatenate", (char *) NULL
    }, */
  *NoiseTypes[] =
  {
    "Uniform", "Gaussian", "Multiplicative", "Impulse", "Laplacian",
    "Poisson", (char *) NULL
  },
  *PreviewTypes[] =
  {
    "Undefined", "Rotate", "Shear", "Roll", "Hue", "Saturation",
    "Brightness", "Gamma", "Spiff", "Dull", "Grayscale", "Quantize",
    "Despeckle", "ReduceNoise", "AddNoise", "Sharpen", "Blur",
    "Threshold", "EdgeDetect", "Spread", "Solarize", "Shade", "Raise",
    "Segment", "Swirl", "Implode", "Wave", "OilPaint", "Charcoal",
    "JPEG", (char *) NULL
  }, /*
  *PrimitiveTypes[] =
  {
    "Undefined", "point", "line", "rectangle", "roundRectangle", "arc",
    "ellipse", "circle", "polyline", "polygon", "bezier", "path", "color",
    "matte", "text", "image", (char *) NULL
    }, */
  *ResolutionTypes[] =
  {
    "Undefined", "PixelsPerInch", "PixelsPerCentimeter", (char *) NULL
  }, /*
  *SpreadTypes[] = 
  {
    "Undefined", "PadSpread", "Reflect", "Repeat", (char *)NULL
    }, */
  *StretchTypes[] =
  {
    "Normal", "UltraCondensed", "ExtraCondensed", "Condensed",
    "SemiCondensed", "SemiExpanded", "Expanded", "ExtraExpanded",
    "UltraExpanded", "Any", (char *) NULL
  },
  *StyleTypes[] =
  {
    "Normal", "Italic", "Oblique", "Any", (char *) NULL
  },
  *VirtualPixelMethods[] =
  {
    "Undefined", "", "Constant", "Edge", "Mirror", "Tile",
    (char *) NULL
  };

#define strEQ(str1, str2) (!strcmp((str1),(str2)))

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   s t r E Q c a s e                                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  Method strEQcase compares two strings and returns 0 if they are the
%  same or if the second string runs out first.  The comparison is case
%  insensitive.
%
%  The format of the strEQcase routine is:
%
%      int strEQcase(const char *p,const char *q)
%
%  A description of each parameter follows:
%
%    o status: Method strEQcase returns zero if strings p and q are the
%      same or if the second string runs out first.
%
%    o p: a character string.
%
%    o q: a character string.
%
%
*/

#   define isUPPER(c)   ((c) >= 'A' && (c) <= 'Z')
#   define toLOWER(c)   (isUPPER(c) ? (c) + ('a' - 'A') : (c))

static int strEQcase(const char *p,const char *q)
{
  char
    c;

  register int
    i;

  for (i=0 ; (c=(*q)) != 0; i++)
  {
    if ((isUPPER(c) ? toLOWER(c) : c) != (isUPPER(*p) ? toLOWER(*p) : *p))
      return(0);
    p++;
    q++;
  }
  return(i);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   L o o k u p S t r                                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  Method LookupStr searches through a list of strings matching it to string
%  and return its index in the list, or -1 for not found .
%
%  The format of the LookupStr routine is:
%
%      int LookupStr(char **list,const char *string)
 %
%  A description of each parameter follows:
%
%    o status: Method LookupStr returns the index of string in the list
%      otherwise -1.
%
%    o list: a list of strings.
%
%    o string: a character string.
%
%
*/
static int LookupStr(char **list,const char *string)
{
  int
    longest,
    offset;

  register char
    **p;

  offset=(-1);
  longest=0;
  for (p=list; *p; p++)
    if (strEQcase(string,*p) > longest)
      {
        offset=p-list;
        longest=strEQcase(string,*p);
      }
  return(offset);
}

/* Forward declarations */

static int mimage_setattr(PyMImageObject *, char *, PyObject *);

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   P y M a g i c k E r r o r H a n d l e r                                   %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  Method PyMagickErrorHandler replaces ImageMagick's fatal error handler.  
%  This stores the message in a Python variable,and longjmp's to return the 
%  error.  Note that this doesn't exit but returns control to Python; 
%
%  The format of the PyMagickErrorHandler routine is:
%
%      PyMagickErrorHandler(const ExceptionType severity,const char *reason,
%        const char *qualifier)
%
%  A description of each parameter follows:
%
%    o severity: The severity of the exception.
%
%    o reason: The reason of the exception.
%
%    o description: The exception description.
%
%
*/
static void PyMagickErrorHandler(const ExceptionType severity,
                 const char *reason,
                 const char *description)
{
    char
        text[MaxTextExtent];

    FormatString(text,"Exception %d: %.512s%s%.512s%s",severity,
         reason ? reason : "ERROR",
         description ? " (" : "", 
                 description ? description : "",
         description ? ")" : "");
    PyErr_SetString(PyMagickError, text);
    longjmp(error_jmp, (int) severity);
}


static StorageType
arraytype_to_storagetype(int type_num)
{
    switch(type_num) {
    case PyArray_CHAR:
    case PyArray_UBYTE:
        return CharPixel;
    case PyArray_USHORT:
        return ShortPixel;
    case PyArray_UINT:
    case PyArray_LONG:
        return IntegerPixel;
    case PyArray_FLOAT:
        return FloatPixel;
    case PyArray_DOUBLE:
        return DoublePixel;        
    }
    return CharPixel;
}


static int
ConstitutePaletteColormap(Image *image, const char *colorspace,
                          StorageType ctype, const void *cmap,
                          const unsigned long colors)
{
    register long x,i;
    size_t length, clen;
    PixelPacket *q;

    assert(image != (Image *) NULL);
    assert(image->signature == MagickSignature);
    if (colors > MaxColormapSize) {
        ThrowBinaryException(OptionError,"UnableToConstitutePalette",
                             "Selected Palette too large.");
    }
    image->storage_class=PseudoClass;
    image->colors=colors;
    length=image->colors*sizeof(PixelPacket);
    if (image->colormap != (PixelPacket *) NULL) 
        MagickRealloc((void **) &image->colormap, length);
    else 
        image->colormap=(PixelPacket *) MagickMalloc(length);
    if (image->colormap == (PixelPacket *) NULL)
        return(False);

    /* Pre-initialize to Opaque */
    for (x=0; x < (long) image->colors; x++) {
        image->colormap[x].opacity = OpaqueOpacity;
    }

    clen = strlen(colorspace);
    image->colorspace=RGBColorspace;
    for (i=0; i < (long) clen; i++) {
        switch (colorspace[i])
            {
            case 'a':
            case 'A':
                image->matte=True;
                break;
            case 'c':
            case 'C':
            case 'm':
            case 'M':
            case 'y':
            case 'Y':
            case 'k':
            case 'K':
                image->colorspace=CMYKColorspace;
                break;
            default:
                break;
            }        
    }

    switch (ctype) 
        {
        case CharPixel:
            {
                register unsigned char *p;
                
                p = (unsigned char *) cmap;
                q = image->colormap;
                for (x=0; x < (long) image->colors; x++) {
                    for (i=0; i< (long) clen; i++) {
                        switch(colorspace[i]) 
                            {
                            case 'r':
                            case 'R':
                            case 'c':
                            case 'C':
                                q->red = ScaleCharToQuantum(*p++);
                                break;
                            case 'g':
                            case 'G':
                            case 'm':
                            case 'M':
                                q->green = ScaleCharToQuantum(*p++);
                                break;
                            case 'b':
                            case 'B':
                            case 'y':
                            case 'Y':
                                q->blue = ScaleCharToQuantum(*p++);
                                break;
                            case 'a':
                            case 'A':
                            case 'k':
                            case 'K':
                                q->opacity = ScaleCharToQuantum(*p++);
                                break;
                            default:
                                MagickFree(image->colormap);
                                return(False);
                            }
                    }
                    q++;            
                }
                break;
            }
        case ShortPixel:
            {
                register unsigned short *p;
                
                p = (unsigned short *) cmap;
                q = image->colormap;
                for (x=0; x < (long) image->colors; x++) {
                    for (i=0; i< (long) clen; i++) {
                        switch(colorspace[i]) 
                            {
                            case 'r':
                            case 'R':
                            case 'c':
                            case 'C':
                                q->red = ScaleShortToQuantum(*p++);
                                break;
                            case 'g':
                            case 'G':
                            case 'm':
                            case 'M':
                                q->green = ScaleShortToQuantum(*p++);
                                break;
                            case 'b':
                            case 'B':
                            case 'y':
                            case 'Y':
                                q->blue = ScaleShortToQuantum(*p++);
                                break;
                            case 'a':
                            case 'A':
                            case 'k':
                            case 'K':
                                q->opacity = ScaleShortToQuantum(*p++);
                                break;
                            default:
                                MagickFree(image->colormap);
                                return(False);
                            }
                    }
                    q++;            
                }
                break;
            }
        case IntegerPixel:
            {
                register unsigned int *p;
                
                p = (unsigned int *) cmap;
                q = image->colormap;
                for (x=0; x < (long) image->colors; x++) {
                    for (i=0; i< (long) clen; i++) {
                        switch(colorspace[i]) 
                            {
                            case 'r':
                            case 'R':
                            case 'c':
                            case 'C':
                                q->red = ScaleLongToQuantum(*p++);
                                break;
                            case 'g':
                            case 'G':
                            case 'm':
                            case 'M':
                                q->green = ScaleLongToQuantum(*p++);
                                break;
                            case 'b':
                            case 'B':
                            case 'y':
                            case 'Y':
                                q->blue = ScaleLongToQuantum(*p++);
                                break;
                            case 'a':
                            case 'A':
                            case 'k':
                            case 'K':
                                q->opacity = ScaleLongToQuantum(*p++);
                                break;
                            default:
                                MagickFree(image->colormap);
                                return(False);
                            }
                    }
                    q++;            
                }
                break;
            }
        case LongPixel:
            {
                register unsigned long *p;
                
                p = (unsigned long *) cmap;
                q = image->colormap;
                for (x=0; x < (long) image->colors; x++) {
                    for (i=0; i< (long) clen; i++) {
                        switch(colorspace[i]) 
                            {
                            case 'r':
                            case 'R':
                            case 'c':
                            case 'C':
                                q->red = ScaleLongToQuantum(*p++);
                                break;
                            case 'g':
                            case 'G':
                            case 'm':
                            case 'M':
                                q->green = ScaleLongToQuantum(*p++);
                                break;
                            case 'b':
                            case 'B':
                            case 'y':
                            case 'Y':
                                q->blue = ScaleLongToQuantum(*p++);
                                break;
                            case 'a':
                            case 'A':
                            case 'k':
                            case 'K':
                                q->opacity = ScaleLongToQuantum(*p++);
                                break;
                            default:
                                MagickFree(image->colormap);
                                return(False);
                            }
                    }
                    q++;            
                }
                break;
            }
        case FloatPixel:
            {
                register float *p;
                
                p = (float *) cmap;
                q = image->colormap;
                for (x=0; x < (long) image->colors; x++) {
                    for (i=0; i< (long) clen; i++) {
                        switch(colorspace[i]) 
                            {
                            case 'r':
                            case 'R':
                            case 'c':
                            case 'C':
                                q->red = (Quantum) ((float) MaxRGB*(*p++));
                                break;
                            case 'g':
                            case 'G':
                            case 'm':
                            case 'M':
                                q->green = (Quantum) ((float) MaxRGB*(*p++));
                                break;
                            case 'b':
                            case 'B':
                            case 'y':
                            case 'Y':
                                q->blue =  (Quantum) ((float) MaxRGB*(*p++));
                                break;
                            case 'a':
                            case 'A':
                            case 'k':
                            case 'K':
                                q->opacity = (Quantum) ((float) MaxRGB*(*p++));
                                break;
                            default:
                                MagickFree(image->colormap);
                                return(False);
                            }
                    }
                    q++;
                }
                break;
            }
        case DoublePixel:
            {
                register double *p;
                
                p = (double *) cmap;
                q = image->colormap;
                for (x=0; x < (long) image->colors; x++) {
                    for (i=0; i< (long) clen; i++) {
                        switch(colorspace[i]) 
                            {
                            case 'r':
                            case 'R':
                            case 'c':
                            case 'C':
                                q->red = (Quantum) ((double) MaxRGB*(*p++));
                                break;
                            case 'g':
                            case 'G':
                            case 'm':
                            case 'M':
                                q->green = (Quantum) ((double) MaxRGB*(*p++));
                                break;
                            case 'b':
                            case 'B':
                            case 'y':
                            case 'Y':
                                q->blue =  (Quantum) ((double) MaxRGB*(*p++));
                                break;
                            case 'a':
                            case 'A':
                            case 'k':
                            case 'K':
                                q->opacity = (Quantum) ((double) MaxRGB*(*p++));
                                break;
                            default:
                                MagickFree(image->colormap);
                                return(False);
                            }
                    }
                    q++;
                }
                break;
            }
        default:
            MagickFree(image->colormap);
            return(False);
        }
    return(True); 
}


#define ScaleCharToRange(val,N) ((Quantum) ((val) * (N) / 256UL))
#define ScaleShortToRange(val,N) ((Quantum) ((val) * (N) / 65536UL))
#define ScaleIntToRange(val,N) ((Quantum) (((val) != 429467295UL) ? ((val) * (N) / 4294967295UL) : N-1 ))
#define ScaleLongToRange(val,N) ((Quantum) (((val) != 429467295UL) ? ((val) * (N) / 4294967295UL) : N-1))

static Image*
ConstitutePaletteImage(const unsigned long width, 
                       const unsigned long height,
                       const StorageType type,
                       const void *pixels,
                       const char *colorspace,
                       const StorageType ctype,
                       const void *cmap,
                       const unsigned long colors,
                       ExceptionInfo *exception)
{
    Image
        *image;
    long
        y, N;
    PixelPacket
        *q;
    register IndexPacket
        *indexes;
    
    register long
        x;

  /*
    Allocate image structure.
  */
  assert(pixels != (void *) NULL);
  assert(exception != (ExceptionInfo *) NULL);
  assert(exception->signature == MagickSignature);
  SetExceptionInfo(exception,UndefinedException);
  image=AllocateImage((ImageInfo *) NULL);
  if (image == (Image *) NULL)
    return((Image *) NULL);
  if ((width == 0) || (height == 0))
    ThrowImage2Exception(OptionError,"UnableToConstituteImage",
            "NonzeroWidthAndHeightRequired");
  image->columns=width;
  image->rows=height;

  if (!ConstitutePaletteColormap(image,colorspace,ctype,cmap,colors))
    ThrowImage2Exception(ResourceLimitError,"MemoryAllocationFailed",
             "UnableToConstituteImage"); 

  N = image->colors;
  /* What to do if value in pixels surpasses size of colormap?

     Scale the pixel value range (as defined by the type)
        to the colormap size range.  
  */
  switch (type)
      {
      case CharPixel:
          {
              register unsigned char
                  *p;
              
              p=(unsigned char *) pixels;
              for (y=0; y < (long) image->rows; y++)
                  {
                      q=SetImagePixels(image,0,y,image->columns,1);
                      if (q == (PixelPacket *) NULL)
                          break;
                      indexes=GetIndexes(image);
                      for (x=0; x < (long) image->columns; x++)
                          { 
                              indexes[x]=ScaleCharToRange(*p++,N);
                              q->red=image->colormap[indexes[x]].red;
                              q->green=image->colormap[indexes[x]].green;
                              q->blue=image->colormap[indexes[x]].blue;
                              q->opacity=image->colormap[indexes[x]].opacity;
                              q++;
                          }
                      if (!SyncImagePixels(image))
                          break;
                  }
              break;
          }
      case ShortPixel:
          {
              register unsigned short
                  *p;
              
              p=(unsigned short *) pixels;
              for (y=0; y < (long) image->rows; y++)
                  {
                      q=SetImagePixels(image,0,y,image->columns,1);
                      if (q == (PixelPacket *) NULL)
                          break;
                      indexes=GetIndexes(image);
                      for (x=0; x < (long) image->columns; x++)
                          {
                              indexes[x]=ScaleShortToRange(*p++,N);
                              q->red=image->colormap[indexes[x]].red;
                              q->green=image->colormap[indexes[x]].green;
                              q->blue=image->colormap[indexes[x]].blue;
                              q->opacity=image->colormap[indexes[x]].opacity;
                              q++;
                          }
                      if (!SyncImagePixels(image))
                          break;
                  }
              break;
          }
      case IntegerPixel:
          {
              register unsigned int
                  *p;
          
              p=(unsigned int *) pixels;
              for (y=0; y < (long) image->rows; y++)
                  {
                      q=SetImagePixels(image,0,y,image->columns,1);
                      if (q == (PixelPacket *) NULL)
                          break;
                      indexes=GetIndexes(image);
                      for (x=0; x < (long) image->columns; x++)
                          {
                              indexes[x]=ScaleIntToRange(*p,N); p++;
                              q->red=image->colormap[indexes[x]].red;
                              q->green=image->colormap[indexes[x]].green;
                              q->blue=image->colormap[indexes[x]].blue;
                              q->opacity=image->colormap[indexes[x]].opacity;
                              q++;
                          }
                      if (!SyncImagePixels(image))
                          break;
                  }
              break;
          }
      case LongPixel:
          {
              register unsigned long
                  *p;
              
              p=(unsigned long *) pixels;
              for (y=0; y < (long) image->rows; y++)
                  {
                      q=SetImagePixels(image,0,y,image->columns,1);
                      if (q == (PixelPacket *) NULL)
                          break;
                      indexes=GetIndexes(image);
                      for (x=0; x < (long) image->columns; x++)
                          {
                              indexes[x]=ScaleLongToRange(*p,N); p++;
                              q->red=image->colormap[indexes[x]].red;
                              q->green=image->colormap[indexes[x]].green;
                              q->blue=image->colormap[indexes[x]].blue;
                              q->opacity=image->colormap[indexes[x]].opacity;
                              q++;
                          }
                      if (!SyncImagePixels(image))
                          break;
                  }
              break;
          }
      case FloatPixel:
          {
              register float
                  *p;
              
              p=(float *) pixels;
              for (y=0; y < (long) image->rows; y++)
                  {
                      q=SetImagePixels(image,0,y,image->columns,1);
                      if (q == (PixelPacket *) NULL)
                          break;
                      indexes=GetIndexes(image);
                      for (x=0; x < (long) image->columns; x++)
                          {
                              indexes[x]=(Quantum) ((float) (N-1)*(*p++));
                              q->red=image->colormap[indexes[x]].red;
                              q->green=image->colormap[indexes[x]].green;
                              q->blue=image->colormap[indexes[x]].blue;
                              q->opacity=image->colormap[indexes[x]].opacity;
                              q++;
                          }
                      if (!SyncImagePixels(image))
                          break;
                  }
              break;
          }
      case DoublePixel:
          {
              register double
                  *p;
              
              p=(double *) pixels;
              for (y=0; y < (long) image->rows; y++)
                  {
                      q=SetImagePixels(image,0,y,image->columns,1);
                      if (q == (PixelPacket *) NULL)
                          break;
                      indexes=GetIndexes(image);
                      for (x=0; x < (long) image->columns; x++)
                          {
                              indexes[x]=(Quantum) ((double) (N-1)*(*p++));
                              q->red=image->colormap[indexes[x]].red;
                              q->green=image->colormap[indexes[x]].green;
                              q->blue=image->colormap[indexes[x]].blue;
                              q->opacity=image->colormap[indexes[x]].opacity;
                              q++;
                          }
                      if (!SyncImagePixels(image))
                          break;
                  }
              break;
          }
      default:
          {
              DestroyImage(image);
              ThrowImage2Exception(OptionError,"UnrecognizedPixelMap", colorspace)
          }
      }
  return(image);
}  


static Image*
convert_bitmap(PyArrayObject *arrobj)
{
    PyArrayObject *bitobj;
    unsigned char zeros[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    int N, k, elsize;
    unsigned char *bitptr;
    char *arrptr;
    Image *image;

    bitobj = (PyArrayObject *)PyArray_FromDims(2,arrobj->dimensions,
                                               PyArray_UBYTE);
    if (bitobj == NULL) return NULL;
    N = PyArray_SIZE(arrobj);
    arrptr = (char *)DATA(arrobj);
    elsize = ELSIZE(arrobj);
    bitptr = (unsigned char *)DATA(bitobj);
    for (k=0; k<N; k++) {
        *bitptr++ = MaxRGB*(memcmp(arrptr,&zeros,elsize) != 0);
        arrptr += elsize;
    }
    
    image = ConstituteImage(DIM(bitobj,1),DIM(bitobj,0),
                            "I", CharPixel, DATA(bitobj), &exception);
    if PyMagickErr(exception) return NULL;
    if (image != NULL) 
        SetImageType(image, BilevelType);
    return image;    
}

static Image*
convert_grayscale(PyArrayObject *arrobj) 
{
    Image *image;
    StorageType stype;

    stype = arraytype_to_storagetype(TYPE(arrobj));
    image = ConstituteImage(DIM(arrobj,1), DIM(arrobj,0),
                "I", stype, DATA(arrobj),&exception);
    if PyMagickErr(exception) return NULL;
    return image;
}

static Image*
convert_palette(PyArrayObject *arrobj, PyArrayObject *pal, ImageInfo *info) 
{

    Image *image;
    StorageType stype;
    StorageType paltype;

    char *str,
        *str1 = "RGB",
        *str2 = "CMYK",
        *str3 = "RGBA";


    if (DIM(pal,1) == 3) str=str1;
    else if(info->colorspace == CMYKColorspace) str=str2;
    else str=str3;

    stype = arraytype_to_storagetype(TYPE(arrobj));
    paltype = arraytype_to_storagetype(TYPE(pal));

    image = ConstitutePaletteImage(DIM(arrobj,0), DIM(arrobj,1),
                                   stype, DATA(arrobj),
                                   str, paltype, DATA(pal),
                                   DIM(pal,0), &exception);

    if PyMagickErr(exception) return NULL;
    return image;
}


static Image*
convert_colorspace(PyArrayObject *arrobj, ImageInfo *info)
{
    Image *image;
    StorageType stype;
    char *str,
        *str1 = "RGB",
        *str2 = "CMYK",
        *str3 = "RGBA";


    if (DIM(arrobj,2) == 3) str=str1;
    else if(info->colorspace == CMYKColorspace) str=str2;
    else str=str3;
    stype = arraytype_to_storagetype(TYPE(arrobj));
    image = ConstituteImage(DIM(arrobj,1), DIM(arrobj,0),
                            str, stype, DATA(arrobj),
                            &exception);
    if PyMagickErr(exception) return NULL;
    return image;
}


static Image*
convert_grayscale_sequence(PyArrayObject *arrobj, ImageInfo *info)
{
    int N, k, step;
    Image *images, *image=NULL;
    StorageType stype;
    char *arrptr;

    N = DIM(arrobj,0);  /* The number of frames to convert */
    images = NewImageList();    
    stype = arraytype_to_storagetype(TYPE(arrobj));
    arrptr = DATA(arrobj);
    step = STRIDE(arrobj,0);
    
    for (k=0; k<N; k++) {
        image = ConstituteImage(DIM(arrobj,2), DIM(arrobj,1),
                                "I", stype, arrptr, &exception);
        AppendImageToList(&images, image);
        if PyMagickErr(exception) goto fail;
        arrptr += step;
    }
    return images;

 fail:
    if (images) DestroyImageList(images);
    return NULL;
}

static Image*
convert_palette_sequence (PyArrayObject *arrobj, PyArrayObject *pal, 
                          ImageInfo *info) 
{
    int N, k, step;
    Image *images, *image=NULL;
    StorageType stype, paltype;
    char *arrptr;
    char *str,
        *str1 = "RGB",
        *str2 = "CMYK",
        *str3 = "RGBA";


    if (DIM(pal,1) == 3) str=str1;
    else if(info->colorspace == CMYKColorspace) str=str2;
    else str=str3;

    stype = arraytype_to_storagetype(TYPE(arrobj));
    paltype = arraytype_to_storagetype(TYPE(pal));

    N = DIM(arrobj,0);  /* The number of frames to convert */
    images = NewImageList();    
    arrptr = DATA(arrobj);
    step = STRIDE(arrobj,0);
    
    for (k=0; k<N; k++) {
        image = ConstitutePaletteImage(DIM(arrobj,1), DIM(arrobj,2),
                                       stype, arrptr,
                                       str, paltype, DATA(pal),
                                       DIM(pal,0), &exception);
        AppendImageToList(&images, image);
        if PyMagickErr(exception) goto fail;
        arrptr += step;
    }
    return images;

 fail:
    if (images) DestroyImageList(images);
    return NULL;
}


static Image*
convert_colorspace_sequence(PyArrayObject *arrobj, ImageInfo *info)
{
    int N, k, step;
    Image *images, *image=NULL;
    StorageType stype;
    char *arrptr;
    char *str,
        *str1 = "RGB",
        *str2 = "CMYK",
        *str3 = "RGBA";

    N = DIM(arrobj,0);       /* The number of frames to convert */
    images = NewImageList();    

    if (DIM(arrobj,3) == 3) str=str1;
    else if(info->colorspace == CMYKColorspace) str=str2;
    else str=str3;

    stype = arraytype_to_storagetype(TYPE(arrobj));
    arrptr = DATA(arrobj);
    step = STRIDE(arrobj,0);
    
    for (k=0; k<N; k++) {
        image = ConstituteImage(DIM(arrobj,2), DIM(arrobj,1),
                                str, stype, arrptr, &exception);
        AppendImageToList(&images, image);
        if PyMagickErr(exception) goto fail;
        arrptr += step;
    }
    return images;

 fail:
    if (images) DestroyImageList(images);
    return NULL;
}



static void 
normalize_DOUBLE(PyArrayObject *arrobj)
{
    double maxval;
    double minval;
    double alpha, beta, diff;
    double *arrptr;
    register int k;
    long N;
    
    N = PyArray_SIZE(arrobj);
    /* Find maximum and minimum */
    maxval = *(double *)(arrobj->data);
    minval = *(double *)(arrobj->data);
    arrptr = (double *)(arrobj->data);
    arrptr++;
    for (k=1; k < N; k++) {
        if (*arrptr > maxval)
            maxval = *arrptr;
        if (*arrptr < minval)
            minval = *arrptr;
        arrptr++;
    }

    /* Return if already normalized */
    if ((maxval <= 1) && (minval >= 0)) return;

    /* Compute transformation alpha and beta */
    /*  newval = oldval * alpha + beta */
    diff = maxval - minval;
    if (diff == 0.0) {
        alpha = 0.0; beta = 0.0;
    }
    else {
        alpha = 1.0 / diff;
        beta = -minval / diff;
    }
    
    /* Normalize array */
    arrptr = (double *)(arrobj->data);
    for (k=0; k<N; k++) {
        *arrptr = alpha * (*arrptr) + beta;
        arrptr++;
    }
    return;
}


static void 
normalize_FLOAT(PyArrayObject *arrobj)
{
    float maxval;
    float minval;
    float alpha, beta, diff;
    float *arrptr;
    register int k;
    long N;
    
    N = PyArray_SIZE(arrobj);
    /* Find maximum and minimum */
    maxval = *(float *)(arrobj->data);
    minval = *(float *)(arrobj->data);
    arrptr = (float *)(arrobj->data);
    arrptr++;
    for (k=1; k < N; k++) {
        if (*arrptr > maxval)
            maxval = *arrptr;
        if (*arrptr < minval)
            minval = *arrptr;
        arrptr++;
    }

    /* Return if already normalized */
    if ((maxval <= 1) && (minval >= 0)) return;

    /* Compute transformation alpha and beta */
    /*  newval = oldval * alpha + beta */
    diff = maxval - minval;
    if (diff == 0.0) {
        alpha = 0.0; beta = 0.0;
    }
    else {
        alpha = 1.0 / diff;
        beta = -minval / diff;
    }
    
    /* Normalize array */
    arrptr = (float *)(arrobj->data);
    for (k=0; k<N; k++) {
        *arrptr = alpha * (*arrptr) + beta;
        arrptr++;
    }
    return;
}


static Image* 
Convert_From_Array(PyObject *in, ImageInfo *info)
{
    PyObject *obj, *pal;
    PyArrayObject *arrobj=NULL, *palobj=NULL, *newobj=NULL;
    int nd, N;
    int type_num;
    Image *image=NULL;
    
    if (PyTuple_Check(in)) { /* Palette image */
        if ((N=PyTuple_Size(in)) != 2) ERRMSG("If object is a tuple, then it must be of length 2 (array, palette)");
        obj = PyTuple_GET_ITEM(in, 0);
        pal = PyTuple_GET_ITEM(in, 1);
        palobj = (PyArrayObject *)PyArray_ContiguousFromObject(pal,
                                                               PyArray_NOTYPE,
                                                               2,2);
        if (palobj == NULL) goto fail;
        if ((RANK(palobj) != 2) || (DIM(palobj,1) < 3) || \
            (DIM(palobj,1) > 4)) ERRMSG("Palette array must be 2-dimensional Mx3 or Mx4 array");
        type_num = TYPE(palobj);
        if (!( (type_num==PyArray_CHAR)   || (type_num==PyArray_UBYTE) || \
               (type_num==PyArray_USHORT) || (type_num==PyArray_UINT)  || \
               (type_num==PyArray_LONG)   || (type_num==PyArray_FLOAT) || \
               (type_num==PyArray_DOUBLE) ))
            ERRMSG("Invalid type for palette array."); 

        /* Allow Floating point palette maps */
        if ((type_num==PyArray_FLOAT) || (type_num==PyArray_DOUBLE)) {
            if ((newobj = (PyArrayObject *)PyArray_Copy(palobj))==NULL)
                ERRMSG("Could not copy array for normalization.");
            if (type_num == PyArray_FLOAT)
                normalize_FLOAT(newobj);
            else
                normalize_DOUBLE(newobj);
            Py_DECREF(palobj);
            palobj = newobj;
        }
    }
    
    else obj = in;
        
    arrobj = (PyArrayObject *)PyArray_ContiguousFromObject(obj, \
                                                           PyArray_NOTYPE,\
                                                           0,0);
    if (arrobj == NULL) ERRMSG("Cannot convert object to array.");
    nd = RANK(arrobj);
    
    if ((nd < 2) || (nd > 4)) ERRMSG("Array must have 2, 3, or 4 dimensions");

    type_num = TYPE(arrobj);
    if (!( (type_num==PyArray_CHAR) || (type_num==PyArray_UBYTE) || \
           (type_num==PyArray_USHORT) || (type_num==PyArray_UINT) || \
           (type_num==PyArray_LONG) || (type_num==PyArray_FLOAT) || \
           (type_num==PyArray_DOUBLE)))
        ERRMSG("Only unsigned integers, floats or doubles accepted");

    /* normalize float or double (if not a bitmap output) */
    if (((type_num==PyArray_FLOAT) || (type_num==PyArray_DOUBLE)) \
        && (info->monochrome != 1)) {
    if ((newobj = (PyArrayObject *)PyArray_Copy(arrobj))==NULL)
            ERRMSG("Could not copy array for normalization.");
        if (type_num == PyArray_FLOAT)
            normalize_FLOAT(newobj);
        else
            normalize_DOUBLE(newobj);
        Py_DECREF(arrobj);
        arrobj = newobj;
    }

    /* Test for different cases */ 
   
    if (nd == 2) {
        if (info->monochrome) image = convert_bitmap(arrobj);
        else if (palobj == NULL) image = convert_grayscale(arrobj);
        else image = convert_palette(arrobj, palobj, info);
    }
    else if (nd == 3) {
        if (info->colorspace==GRAYColorspace) 
            image = convert_grayscale_sequence(arrobj, info);
        else if (palobj != NULL) 
            image = convert_palette_sequence(arrobj, palobj, info);
        else if ((DIM(arrobj,2) < 3) || (DIM(arrobj,2) > 4))
            image = convert_grayscale_sequence(arrobj, info);
        else image = convert_colorspace(arrobj, info);
    }
    else {  /* nd == 4 */
        if ((DIM(arrobj,3) < 3) || (DIM(arrobj,3) > 4))
            ERRMSG("Last dimension of array must be 3 or 4.");
        image = convert_colorspace_sequence(arrobj, info);
    }
    CHECK_ERR;
    Py_DECREF(arrobj);
    Py_XDECREF(palobj);
    return image;

 fail:
    Py_XDECREF(arrobj);
    Py_XDECREF(palobj);
    return NULL;
}

/* Converts an object to an image.

   The object can be:
         another MImage Object
         a string (representing a filename)
         a stream-object to read from  --- stream closed after done reading
         a Numeric array 2-D to 4-D
           2-D --- implies grayscale image
           3-D --- implies RGB image if (MxNx3)
                           RGBA image if (MxNx4)
                           CMYK image if (MxNx4) and colorspace='cmyk'
                   sequence of grayscale otherwise       
           4-D --- sequence of RGB images if (KxMxNx3)
                   sequence of RGBA images if (KxMxNx4)
                   sequence of CMYK images if (KxMxNx4) and colorspace='cmyk'
         a tuple
           first element is an array
           2-D ---  palette image
           3-D ---  sequence of palette images (array should be sequence also)
           second element is the palette array Mx3 -- RGB 
                                               Mx4 -- RGBA
                                               Mx4 -- CMYK if colorspace='cmyk'

           Notes:  If monochrome=1 passed in info, then bitmap image. 

*/

/* The info term is for passing in any keywords that were passed
    from Python describing the new image.
*/
static Image *
_convert_object(PyObject *in, ImageInfo *info)
{
    Image *image = NULL;
    ImageInfo *image_info = NULL;

       /* Clone an image */
    if PyMImage_Check(in) {
        image = CloneImageList(((PyMImageObject *)in)->ims,&exception);
        CHECK_ERR;
    }
         /* create image from a file. */
    else if PyString_Check(in) { 
        image_info = CloneImageInfo((ImageInfo *)info);
    
        (void ) strcpy(image_info->filename,
                       PyString_AS_STRING((PyObject *)in));
        image = ReadImage(image_info, &exception);
    if (image_info) DestroyImageInfo(image_info);
        CHECK_ERR;
    }
    else if PyFile_Check(in) {
        image_info = CloneImageInfo((ImageInfo *)info);        
        image_info->file = PyFile_AsFile(in);
        image = ReadImage(image_info, &exception);
    if (image_info) DestroyImageInfo(image_info);
        CHECK_ERR;
    }
    else { /* try to create image from an array */
        image = Convert_From_Array(in, info);
        if (image == NULL) goto fail;
    }
    return image;

fail:
    return NULL;
}


static PyObject*
mimage_from_object(PyObject *obj)
{
    PyMImageObject *new=NULL;
    ImageInfo *info=NULL;
    Image *im;

    if (PyMImage_Check(obj)) {
    Py_INCREF(obj);
    return obj;
    }
    
    info = CloneImageInfo((ImageInfo *)NULL);
    im =_convert_object(obj, info);
    if (im==NULL) goto fail;
    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = im;
    if (info) DestroyImageInfo(info);
    return (PyObject *)new;

 fail:
    if (info) DestroyImageInfo(info);
    return NULL;
}

/* take a Python object representing a color for optional keyword 
    The Python object can be: 

                A string ( '#FFddee', or a name 'aquablue')
                A tuple of integers representing RGB
*/

#define ERRMSG2(str) {\
     PyErr_Format(PyMagickError, str, keyname);\
     goto fail; \
}

static int
set_color_from_obj(PixelPacket *packet, PyObject *obj, char *keyname)
{
    int len;
    char *cstr;
    PyObject *itobj=NULL;

    if (PyString_Check(obj)) {
        cstr = PyString_AS_STRING(obj);
        len = PyString_GET_SIZE(obj);
        (void) QueryColorDatabase(cstr, packet, &exception);
        CHECK_ERR;
        return(True);
    }
    else if (PySequence_Check(obj)) {
        len = PySequence_Length(obj);
        if (len < 3 || len > 4) ERRMSG2("Wrong number of colors in %s");
        itobj = PySequence_GetItem(obj,0);
        if (itobj == NULL) goto fail;
        packet->red = (Quantum) PyInt_AsLong(itobj);
        itobj = PySequence_GetItem(obj,1);
        if (itobj == NULL) goto fail;
        packet->green = (Quantum) PyInt_AsLong(itobj);
        itobj = PySequence_GetItem(obj,2);
        if (itobj == NULL) goto fail;
        packet->blue = (Quantum) PyInt_AsLong(itobj);
        packet->opacity = 0;
        if (len == 4) {
            itobj = PySequence_GetItem(obj,3);
            if (itobj == NULL) goto fail;
            packet->opacity = (Quantum) PyInt_AsLong(itobj);
        }
    }
    else ERRMSG2("Unupported color object in %s");    
    return 1;

 fail:
    Py_XDECREF(itobj);
    return 0;
    
}

#undef ERRMSG2


/* Take a dictionary of kewords and update the imageinfo structure with it 
 */

#define ERRMSG2 {\
        PyErr_Format(PyMagickError, "Unrecognized attribute: %s", errtext);\
        goto fail;\
}
#define ERRMSG3(val,skey) {\
        PyErr_Format(PyMagickError, "Unrecognized attribute value: %s in %s",\
                     (val),(skey));\
        goto fail;\
}

static int
update_info_from_kwds(ImageInfo *info, PyObject* kwds)
{
    PyObject *key, *value;
    Py_ssize_t pos = 0;
    int ind;
    long tmplong;
    double tmpdouble;
    char *skey;
    char errtext[MaxTextExtent];
    char *tmpstr;

    if (!(info)) return 0;
    
    while (PyDict_Next(kwds, &pos, &key, &value)) {
        if ((!PyString_Check(key)) || (PyString_Size(key) < 1))
            ERRMSG("Internal error: keyword not valid string");
        skey = PyString_AS_STRING(key);
        tmpstr = PyString_AsString(value);  /* May not be a string */
        switch (*skey) {
        case 'A':
        case 'a':
            if (strEQ(skey,"adjoin"))
                info->adjoin = \
                    (unsigned int)PyObject_IsTrue(value);
            else if (strEQ(skey,"antialias"))
                info->antialias = \
                    (unsigned int)PyObject_IsTrue(value);
            else ERRMSG2;
            break;
        case 'B':
        case 'b':
            if (strEQ(skey,"background")) {
                if (!set_color_from_obj(&(info->background_color),
                                        value,skey))
                    goto fail;
            }
            else if (strEQ(skey,"bordercolor")) {
                if (!set_color_from_obj(&(info->border_color),
                                        value, skey))
                    goto fail;
            }
            else ERRMSG2;
            break;
        case 'C':
        case 'c':
            if (strEQ(skey,"colorspace")) {
                if (!tmpstr) goto fail;
                if ((ind = LookupStr(ColorspaceTypes, tmpstr)) < 0)
                    ERRMSG3(tmpstr,skey);
                info->colorspace = (ColorspaceType) ind;
            }
            else if (strEQ(skey, "compression")) {
                if (!tmpstr) goto fail;
                if ((ind = LookupStr(CompressionTypes, tmpstr)) < 0)
                    ERRMSG3(tmpstr,skey);
                info->colorspace = (CompressionType) ind;
            }
            else ERRMSG2;
            break;
        case 'D':
        case 'd':
            /* delay not in ImageInfo */
            if (strEQ(skey,"density")) {
                if (!tmpstr) goto fail;
                info->density = AllocateString(tmpstr);
            }
            else if (strEQ(skey,"depth")) {
                tmplong = PyInt_AsLong(value);
                if (PyErr_Occurred()) goto fail;
#if defined(QuantumLeap)
                if ((tmplong != 8) && (tmplong != 16)) {
                    PyErr_SetString(PyMagickError,
                                    "depth keyword must be 8 or 16");
                    goto fail;
                }
#else
                if ((tmplong != 8)) {
                    PyErr_SetString(PyMagickError,
                                    "depth keyword must be 8");
                    goto fail;
                }
#endif
                info->depth = tmplong;
            }
            /* dispose not in ImageInfo */
            else if (strEQ(skey,"dither")) {
                info->dither = (unsigned int) PyObject_IsTrue(value);
            }
            else ERRMSG2;
            break;
        case 'E':
        case 'e':
            /* extract not in ImageInfo */
            if (strEQ(skey, "endian")) {
                if (!tmpstr) goto fail;
                if ((ind = LookupStr(EndianTypes, tmpstr)) < 0)
                    ERRMSG3(tmpstr,skey);
                info->endian = (EndianType) ind;                
            }
            else ERRMSG2;
            break;
        case 'F':
        case 'f':
            /* file and filename not dealt with here */
            /* fill not in image.h */
            if (strEQ(skey, "font")) {
                if (!tmpstr) goto fail;
                info->font = AllocateString(tmpstr);
            }
            else if (strEQ(skey, "fuzz")) {
                tmpdouble = PyFloat_AsDouble(value);
                if (PyErr_Occurred()) goto fail;
                info->fuzz = tmpdouble;   /* double not int as web-docs say */
            }
            else ERRMSG2;
            break;
        case 'I':
        case 'i':
            if (strEQ(skey, "interlace")) {
                if (!tmpstr) goto fail;
                if ((ind = LookupStr(InterlaceTypes, tmpstr)) < 0)
                    ERRMSG3(tmpstr,skey);
                info->interlace = (InterlaceType) ind;
            }
            /* iterations not in image.h */
            else ERRMSG2;
            break;
            /* linewidth not in image.h */
        case 'M':
        case 'm':
            if (strEQ(skey, "magick")) { 
                if (!tmpstr) goto fail;
                strncpy(info->magick,tmpstr,MaxTextExtent);
            }
            else if (strEQ(skey, "matte_color")) {
                if (!set_color_from_obj(&(info->matte_color),
                                        value,skey))
                    goto fail;
            }
            else if (strEQ(skey, "monochrome")) {
                info->monochrome = \
                    (unsigned int) PyObject_IsTrue(value);
            }
            else ERRMSG2;
            break;
            /* number_scenes not in image.h */
        case 'P':
        case 'p':
            if (strEQ(skey, "page")) {
                if (!tmpstr) goto fail;
                info->page = AllocateString(tmpstr);
            }
            else if (strEQ(skey, "ping")) {
                info->ping = \
                    (unsigned int) PyObject_IsTrue(value);
            }
            else if (strEQ(skey, "pointsize")) {
                tmpdouble = PyFloat_AsDouble(value);
                if (PyErr_Occurred()) goto fail;
                info->pointsize = tmpdouble;
            }
            else if (strEQ(skey, "preview_type")) {
                if (!tmpstr) goto fail;
                if ((ind = LookupStr(PreviewTypes, tmpstr)) < 0)
                    ERRMSG3(tmpstr,skey);
                info->preview_type = (PreviewType) ind;
            }
            else ERRMSG2;
            break;
        case 'Q':
        case 'q':
            if (strEQ(skey, "quality")) {
                tmplong = PyInt_AsLong(value);
                if (PyErr_Occurred()) goto fail;
                info->quality = tmplong;
            }
            else ERRMSG2;
            break;
        case 'S':
        case 's':
            if (strEQ(skey, "server_name")) {
                if (!tmpstr) goto fail;
                info->server_name = AllocateString(tmpstr);
            }
            else if (strEQ(skey, "size")) {
                if (!tmpstr) goto fail;
                info->size = AllocateString(tmpstr);
            }
            /* stroke not in ImageInfo */
            /* scene not in ImageInfo */
            else ERRMSG2;
            break;
        case 'T':
        case 't':
            if (strEQ(skey, "texture")) {
                if (!tmpstr) goto fail;
                info->texture = AllocateString(tmpstr);
            }
            else ERRMSG2;
            break;
        case 'U':
        case 'u':
            if (strEQ(skey, "units")) {
                if (!tmpstr) goto fail;
                if ((ind = LookupStr(ResolutionTypes, tmpstr)) < 0)
                    ERRMSG3(tmpstr,skey);
                info->units = (ResolutionType) ind;
            }
            else ERRMSG2;
            break;
        case 'V':
        case 'v':
            if (strEQ(skey, "verbose")) {
                info->verbose = \
                    (unsigned int) PyObject_IsTrue(value);
            }
            else if (strEQ(skey, "view")) {
                info->view = AllocateString(tmpstr);
            }
            else ERRMSG2;
            break;
        default:
            ERRMSG2;
            break;
        }
    }
    if (PyErr_Occurred()) PyErr_Clear();
    return 1;

 fail:
    return 0;
        
}

#undef ERRMSG2


static char doc_image[] = "image(obj1, {obj2, ...}) create an Image Magick image.  obj can be a file name, a file stream, another image object, an array object, or a 2-tuple of an array object and a second object (either a string or a palette array) describing how to interpret the array data.  Multiple objects can be combined into an image sequence using repeating arguments.";
static PyObject *
magick_new_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyMImageObject *obj=NULL;
    ImageInfo *info=NULL;
    Image *images=NULL, *im=NULL;
    int N, k;

    if(((N=PySequence_Length(args)) < 0) || (!PyTuple_Check(args)) || \
       (kwds && !PyDict_Check(kwds)))
    ERRMSG("Invalid argument to internal function.");

    obj = PyObject_New(PyMImageObject, &MImage_Type);
    if (obj == NULL) goto fail;
    obj->ims = NULL;
   
    if (!(info = CloneImageInfo((ImageInfo *)NULL))) ERRMSG("Resource error.");
    if ((N > 0) && (kwds != NULL))
        if (!update_info_from_kwds(info, kwds))
            goto fail;
    k = 0;
    while (N--) {
        im = _convert_object(PyTuple_GET_ITEM(args,k++),info);
        if (im == NULL) goto fail;
        AppendImageToList(&images, im);                          
    }
    DestroyImageInfo(info);
    obj->ims = images;
    return (PyObject *)obj;

fail:
    Py_XDECREF(obj);
    if (info) DestroyImageInfo(info);
    if (images) DestroyImageList(images);
    return NULL;
}

static void
mimage_dealloc(PyObject *self)
{
    PyMImageObject *obj;
    
    obj = ASIM(self);
    if (obj && obj->ims)
        DestroyImageList(obj->ims);
    PyObject_Del(self);
}

static char doc_write_image[] = "img.write(<filename>) \n"\
" Write an image to the given file.  Keywords are used to update image_info\n"
"   before writing.  If no filename given then use filename attribute";
static PyObject *
write_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    int N;
    PyObject *in;
    PyMImageObject *imobj;
    ImageInfo *info=NULL;

    if(((N=PySequence_Length(args)) < 0) || (!PyTuple_Check(args)) || \
       (kwds && !PyDict_Check(kwds)))
    ERRMSG("Invalid argument to internal function.");

    if (N > 1) ERRMSG("Too many input arguments");

    info = CloneImageInfo(NULL);
    if ((kwds) && !update_info_from_kwds(info, kwds))
        goto fail;

    in = PyTuple_GET_ITEM(args,0);
    imobj = (PyMImageObject *)self;
    
    if (!(imobj->ims)) ERRMSG("No image to write");

    if (N==1) {
        if (PyString_Check(in)) {
            (void) strncpy(imobj->ims->filename, 
                           PyString_AS_STRING(in), MaxTextExtent-1);
        }
        else ERRMSG("Input argument must be filename or file stream");
    }
    else if (imobj->ims->filename == NULL) ERRMSG("Image has no filename.");

    if (!WriteImage(info, imobj->ims))
        ERR(imobj->ims->exception);

    DestroyImageInfo(info);
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    if (info) DestroyImageInfo(info);
    return NULL;

}

static char doc_display_image[] = "display an image to the screen.";
static PyObject *
display_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyMImageObject *imobj = (PyMImageObject *)self;
    ImageInfo *info=NULL;

    info = CloneImageInfo(NULL);
    if (!PyArg_ParseTuple(args,""))
        return NULL;
    if ((kwds) && !update_info_from_kwds(info, kwds))
        goto fail;
    if (imobj->ims)
        (void) (DisplayImages(info, imobj->ims));
    else ERRMSG("No image to display.");

    CHECK_ERR_IM(imobj->ims);
    DestroyImageInfo(info);
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    if (info) DestroyImageInfo(info);
    return NULL;    
}

static char doc_animate_image[] = "animate an image to the screen.";
static PyObject *
animate_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyMImageObject *imobj = (PyMImageObject *)self;
    ImageInfo *info=NULL;

    info = CloneImageInfo(NULL);
    if (!PyArg_ParseTuple(args,""))
        return NULL;
    if ((kwds) && !update_info_from_kwds(info, kwds))
        goto fail;
    if (imobj->ims) 
        (void )AnimateImages(info, imobj->ims);
    else ERRMSG("No image to animate.");
    CHECK_ERR_IM(imobj->ims);
    DestroyImageInfo(info);
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    if (info) DestroyImageInfo(info);
    return NULL;    
}

static char doc_copy_image[] = "copy an image to another image";
static PyObject *
copy_image(PyObject *self)
{
    PyMImageObject *obj=NULL;
    
    obj = PyObject_New(PyMImageObject, &MImage_Type);
    if (obj == NULL) return NULL;
    obj->ims = NULL;

    obj->ims = CloneImageList(ASIM(self)->ims, &exception);
    CHECK_ERR;

    return (PyObject *)obj;
    
 fail:
    Py_XDECREF(obj);
    return NULL;    
    
}


static char doc_quantize_image[] = \
"img.quantize(colors(256), dither(1), colorspace='rgb', measerr=0, depth=0)\n\n"\
" Quantize an image to the given number of colors using dithering if \n"\
"   specified.  \n\n"\
" depth      controls the tree depth to use while quantizing.  Values of \n"\
"            0 and 1 cause automatic tree depth determination.  Values from\n"\
"            2 to 8 forces the tree depth. The ideal tree depth depends on the\n"\
"            characteristics of the input image, and may be determined through\n"\
"            experimentation.\n"\
" colorspace Specify the colorspace to quantize in.\n"\
" measerr    Set to non-zero to calculate quantization erros when quantizing.";
static PyObject *
quantize_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyMImageObject *imobj = (PyMImageObject *)self;
    int colors=MaxRGB, dither=1;
    QuantizeInfo *qinfo=NULL;
    char *tmpstr, *vstr;

    if (!PyArg_ParseTuple(args, "|ii",&colors, &dither))
        return NULL;

    if ((colors <=0 ) || (colors > MaxRGB+1)) 
        ERRMSG("Number of colors must be >0 and <=MaxRGB+1");

    qinfo = CloneQuantizeInfo(NULL);
    if (qinfo == NULL) return NULL;
    qinfo->number_colors = colors;
    qinfo->dither = dither;    
    if (kwds != NULL) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        int ind;
        while (PyDict_Next(kwds, &pos, &key, &value)) {
            tmpstr = PyString_AsString(key);
            if (tmpstr == NULL) ERRMSG("Invalid keyword");
            if (strEQ(tmpstr,"colorspace")) {
                vstr = PyString_AsString(value);
                if (vstr==NULL) ERRMSG("Colorspace must be valid string");
                if ((ind = LookupStr(ColorspaceTypes, vstr)) < 0)
                    ERRMSG("Invalid colorspace.");
                qinfo->colorspace = (ColorspaceType) ind;
            }
            else if (strEQ(tmpstr, "measerr")) {
                ind = PyInt_AsLong(value);
                if ((ind==-1) && PyErr_Occurred()) return NULL;
                qinfo->measure_error = ind;
            }
            else if (strEQ(tmpstr, "depth")) {
                ind = PyInt_AsLong(value);
                if ((ind < 0) || (ind > 8)) 
                    ERRMSG("Tree depth must be in range [0,8]");
                qinfo->tree_depth = ind;                        
            }
        }
    }

    if (imobj->ims) 
        if (!QuantizeImages(qinfo, imobj->ims))
            CHECK_ERR_IM(imobj->ims);
    DestroyQuantizeInfo(qinfo);
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    if (qinfo) DestroyQuantizeInfo(qinfo);
    return NULL;    
}


static char doc_compresscolormap_image[] = "img.compresscolormap()\n\n"\
" Compress an image colormap by removing any duplicate or unused color entries.\n";
static PyObject *
compresscolormap_image(PyObject *self)
{
    Image *mag;

    for (mag=ASIM(self)->ims; mag; mag=mag->next)         
        CompressImageColormap(mag);
    
    Py_INCREF(Py_None);
    return Py_None;
}


static PyObject *
get_colormap(Image *im)
{
    int paldims[2];
    int k;
    PyObject *pal;
    char *ptr;
    PixelPacket *cptr;
        
    paldims[0] = im->colors;
    paldims[1] = 3;
    if (im->colorspace == CMYKColorspace) paldims[1] = 4;
    if (im->matte) paldims[1] = 4;
    pal = PyArray_FromDims(2,paldims,ptype);
    if (pal == NULL) {
        return NULL;
    }

    /* Fill up color map */
    ptr = DATA(pal);
    cptr = im->colormap;
    for (k=0; k < im->colors; k++) {
        memcpy((void *)ptr,(void *)&(cptr->red), _qsize);
        ptr += _qsize;
        memcpy((void *)ptr,(void *)&(cptr->green), _qsize);
        ptr += _qsize;
        memcpy((void *)ptr,(void *)&(cptr->blue), _qsize);
        ptr += _qsize;
        if (paldims[1] == 4) {
            memcpy((void *)ptr, (void *)&(cptr->opacity), _qsize);
            ptr += _qsize;
        }
        cptr++;
    }

    return pal;
}


static PyArrayObject*
convert_from_palette(Image *im, int type)
{
    PyArrayObject *arr;
    register PixelPacket *p;
    int arrdims[2];
    int ret;
    int rawtransfer = 1;
    StorageType stype;
    long x, y;
    char *ptr;
    unsigned char *q;
    IndexPacket *indexes;
    
    /* Only use ImageMagick's scaling magick for float and double */
    if ((type == PyArray_FLOAT) || (type == PyArray_DOUBLE)) rawtransfer = 0;
    else type = PyArray_UBYTE;  /* palette images only if colormap is less than 256 */

    arrdims[0] = im->rows;
    arrdims[1] = im->columns;
    arr = (PyArrayObject *)PyArray_FromDims(2,arrdims,type);
    if (arr==NULL) return NULL;

    /* Fill up array */
    ptr = DATA(arr);
    if (!rawtransfer) {
    stype = arraytype_to_storagetype(type);
    ret = DispatchImage(im, 0, 0, arrdims[1], arrdims[0], "I", stype, 
                (void *)ptr, &exception);
    CHECK_ERR;
    return arr;
    }
    /* Else grab pixels from image, determine which color in the colormap they correspond
       to and place that index number in the output array */

    q = (unsigned char *)ptr;
    for (y=0; y < (long) im->rows; y++) {
    p = GetImagePixels(im,0,y,im->columns,1);
    if (p==NULL) break;
        indexes = GetIndexes(im);
    for (x=0; x< (long) im->columns; x++) {
        *q++ = (unsigned char)(indexes[x]);
        p++;
    }
        CHECK_ERR_IM(im);
    }
    return arr;
    
 fail:
    Py_DECREF(arr);
    return NULL;
}

static PyArrayObject*
convert_from_palette_sequence(Image *im, int type, int len)
{
    PyArrayObject *arr;
    register PixelPacket *p;
    IndexPacket *indexes;
    int arrdims[3];
    int ret;
    int rawtransfer = 1;
    StorageType stype;
    long x, y;
    register int k;
    char *ptr;
    unsigned char *q;
    

    /* Only use ImageMagick's scaling magick for float and double */
    if ((type == PyArray_FLOAT) || (type == PyArray_DOUBLE)) rawtransfer = 0;
    else type = PyArray_UBYTE;  /* palette images only if colormap is less than 256 */

    arrdims[0] = len;
    arrdims[1] = im->rows;
    arrdims[2] = im->columns;
    arr = (PyArrayObject *)PyArray_FromDims(3,arrdims,type);
    if (arr==NULL) return NULL;


    /* Fill up array */
    ptr = DATA(arr);
    if (!rawtransfer) {
    for (k=0; k< len; k++) {
        stype = arraytype_to_storagetype(type);
        ret = DispatchImage(im, 0, 0, arrdims[2], arrdims[1], "I", stype, 
                (void *)ptr, &exception);
        CHECK_ERR;
        ptr += STRIDE(arr,0);
    }
    return arr;
    }

    /* Else grab pixels from image, determine which color in the colormap they 
          correspond to and place that index number in the output array */
    q = (unsigned char *)ptr;
    for (k=0; k < len; k++) {
        for (y=0; y < (long) im->rows; y++) {
            p = GetImagePixels(im,0,y,im->columns,1);
            if (p==NULL) break;
            indexes = GetIndexes(im);
            for (x=0; x< (long) im->columns; x++) {
                *q++ = (unsigned char)(indexes[x]);
                p++;
            }
        }
        CHECK_ERR_IM(im);
    }
    return arr;
    
 fail:
    Py_DECREF(arr);
    return NULL;
}


static PyArrayObject*
convert_from_direct(Image *im, int type)
{
    PyArrayObject *arr;
    register const PixelPacket *p;
    int arrdims[3];
    int ret, lastdim;
    int rawtransfer = 1;
    StorageType stype;
    long x, y;
    char *ptr, *str;
    char *q;
    
    /* Only use ImageMagick's scaling magick for float and double */
    if ((type == PyArray_FLOAT) || (type == PyArray_DOUBLE)) rawtransfer = 0;
    else type = ptype;

    if (im->colorspace==CMYKColorspace) str="CMYK";
    else if (im->matte) str = "RGBA";
    else str = "RGB";
    lastdim = strlen(str);

    arrdims[0] = im->rows;
    arrdims[1] = im->columns;
    arrdims[2] = lastdim;
    arr = (PyArrayObject *)PyArray_FromDims(3,arrdims,type);
    if (arr==NULL) return NULL;

    /* Fill up array */
    ptr = DATA(arr);
    if (!rawtransfer) {
    stype = arraytype_to_storagetype(type);
    ret = DispatchImage(im, 0, 0, arrdims[1], arrdims[0], str, stype,
                (void *)ptr, &exception);
    CHECK_ERR;
    return arr;
    }

    /* Else grab pixels from image and copy them to the output */
    q = ptr;
    for (y=0; y < (long) im->rows; y++) {
    p = AcquireImagePixels(im,0,y,im->columns,1,&exception);
    if (p==NULL) break;
    for (x=0; x< (long) im->columns; x++) {
        memcpy(q, &(p->red), _qsize);
        q += _qsize;
        memcpy(q, &(p->green), _qsize);
        q += _qsize;
        memcpy(q, &(p->blue), _qsize);
        q += _qsize;
        if (lastdim == 4) {
        memcpy(q, &(p->opacity), _qsize);
        q += _qsize;
        }
        p++;
    }
    }
    CHECK_ERR;
    return arr;
    
 fail:
    Py_DECREF(arr);
    return NULL;
}



static PyArrayObject*
convert_from_direct_sequence(Image *im, int type, int len)
{
    PyArrayObject *arr;
    register const PixelPacket *p;
    int arrdims[4];
    int ret, lastdim;
    int rawtransfer = 1;
    StorageType stype;
    long x, y;
    register int k;
    char *ptr, *str;
    char *q;
    
    /* Only use ImageMagick's scaling magick for float and double */
    if ((type == PyArray_FLOAT) || (type == PyArray_DOUBLE)) rawtransfer = 0;
    else type = ptype;

    lastdim = 4;
    if (im->colorspace==CMYKColorspace) str="CMYK";
    else if (im->matte) str = "RGBA";
    else {str = "RGB"; lastdim = 3;}
    
    arrdims[0] = len;
    arrdims[1] = im->rows;
    arrdims[2] = im->columns;
    arrdims[3] = lastdim;
    arr = (PyArrayObject *)PyArray_FromDims(4,arrdims,type);
    if (arr==NULL) return NULL;

    /* Fill up array */
    ptr = DATA(arr);
    if (!rawtransfer) {
    stype = arraytype_to_storagetype(type);
    for (k=0; k < len; k++) {
        if (!im) break;
        ret = DispatchImage(im, 0, 0, arrdims[2], arrdims[1], str, stype,
                (void *)ptr, &exception);
        CHECK_ERR;
        ptr += STRIDE(arr,0);
        im = im->next;
    }
    return arr;
    }

    /* Else grab pixels from image and copy them to the output */
    q = ptr;
    for (k=0; k < len; k++) {
    if (!im) break;
    for (y=0; y < (long) im->rows; y++) {
        p = AcquireImagePixels(im,0,y,im->columns,1,&exception);
        if (p==NULL) break;
        for (x=0; x< (long) im->columns; x++) {
        memcpy(q, &(p->red), _qsize);
        q += _qsize;
        memcpy(q, &(p->green), _qsize);
        q += _qsize;
        memcpy(q, &(p->blue), _qsize);
        q += _qsize;
        if (lastdim == 4) {
            memcpy(q, &(p->opacity), _qsize);
            q += _qsize;
        }
        p++;
        }
    }
    CHECK_ERR;
    im = im->next;
    }
    return arr;
    
 fail:
    Py_DECREF(arr);
    return NULL;
}


static char doc_toarray_image[] = "return an array representation for an image.  Use colormap to get palette (if any)";
static PyObject *
toarray_image(PyObject *self, PyObject *args)
{
    PyMImageObject *obj=NULL;
    PyArrayObject *arr=NULL;
    PyArray_Descr *descr;
    int N, otype;
    Image *images;
    char atype='b';

    if (!PyArg_ParseTuple(args, "|c", &atype)) return NULL;
    descr = PyArray_DescrFromType(atype);
    if (descr == NULL) return NULL;
    otype = descr->type_num;

    if (!((otype == PyArray_UBYTE) || (otype == PyArray_CHAR) || \
    (otype == PyArray_USHORT) || (otype == PyArray_UINT) || \
    (otype == PyArray_LONG) || (otype == PyArray_FLOAT) || \
    (otype == PyArray_DOUBLE))) {
    PyErr_SetString(PyExc_TypeError, "Invalid type for return array.");
    return NULL;
    }

    obj = ASIM(self);
    images = obj->ims;
    N = GetImageListLength(images);
    if (N==0) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    if ((images->storage_class==PseudoClass) && (images->colors <= 256)) {
    if (N==1) arr = convert_from_palette(images, otype);
    else arr = convert_from_palette_sequence(images, otype, N);
    if (arr==NULL) return NULL;
    return (PyObject *)arr;
    }
    if (N==1) arr = convert_from_direct(images, otype);
    else arr = convert_from_direct_sequence(images, otype, N);
    if (arr==NULL) return NULL;
    return (PyObject *)arr;
}


static char doc_contrast_image[] = "img.contrast({sharpen}) \n\n"\
" Enhance the intensity differences between lighter and darker elements\n"\
"   of the image.  Set sharpen to a value other than 0 (default) to increase the\n"\
"   image contrast otherwise the contrast is reuced.";   
static PyObject *
contrast_image(PyObject *self, PyObject *args)
{
    Image *mag;
    int sharpen=0;

    if (!PyArg_ParseTuple(args, "|i",&sharpen))
        return NULL;
    if (sharpen < 0) ERRMSG("sharpen must be > 0.");
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        (void) ContrastImage(mag, (const unsigned int) sharpen);
        CHECK_ERR_IM(mag)
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}


static char doc_equalize_image[] = "img.equalize() \n\n"\
" Apply a histogram equalization to the image.";
static PyObject *
equalize_image(PyObject *self)
{
    Image *mag;

    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!EqualizeImage(mag))
            CHECK_ERR_IM(mag)
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}


static char doc_normalize_image[] = "img.normalize() \n\n"\
" Enhances the contrast of a color image by adjusting the pixels color to\n"\
"   to span the entire range of colors available.";
static PyObject *
normalize_image(PyObject *self)
{
    Image *mag;

    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!NormalizeImage(mag))
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}


static char doc_gamma_image[] = "img.gamma(R,{G,B}) \n\n"\
" Specifify individual gamma levles for the Red, Green, and Blue channels.\n"\
"   If G and/or B are not given, they are assumed to be the same as R.\n\n"\
" The same image viewed on different devices will have perceptual differences \n"\
"   in the way the image's intensities are represented on the screen.  This\n"\
"   changes the way an image is displayed.  Typical values range from 0.8 to 2.3\n"\
"   A value of 0 will reduce the influence of a particular channel.";
static PyObject *
gamma_image(PyObject *self, PyObject *args)
{
    Image *mag;
    double red, green, blue;
    char message[MaxTextExtent];
    int numargs;

    if (!PyArg_ParseTuple(args, "d|dd",&red, &green, &blue))
        return NULL;
    numargs = PyTuple_Size(args);
    if (numargs < 3) blue = red;
    if (numargs < 2) green = red;
    FormatString(message, "%g,%g,%g", red, green, blue);
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!GammaImage(mag,message))
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}

static char doc_level_image[] = "img.level(black(0.0),mid(1.0),white(MaxRGB)) \n\n"\
" Adjusts the levels of an image by scaling the colors falling between \n"\
"   specified white and black points to the full available quantum range.\n"\
"   The black point specifies the darkest color in the image (as an integer \n"\
"       Colors darker than the black point are set to zero. \n"\
"   Mid point specifies a gamma correction to apply to the image. \n"\
"   White point specifies the lightest color in the image. \n"\
"       Colors brighter than the white point are set to the maximum quantum \n"\
"       value.\n\n"\
"   If black and white are < 1 they are interpreted as percentages of MaxRGB.";
static PyObject *
level_image(PyObject *self, PyObject *args)
{
    Image *mag;
    double black=0.0, mid=1.0, white=(double)MaxRGB;
    char message[MaxTextExtent];

    if (!PyArg_ParseTuple(args, "|ddd",&black, &mid, &white))
        return NULL;
    if (black < 1) black *= MaxRGB;
    if (white < 1) white *= MaxRGB;
    if ((black < 0) || (black > MaxRGB) || \
        (white < 0) || (white > MaxRGB) || \
        (mid < 0) || (mid > 10)) {
        PyErr_Format(PyMagickError,"white and black must be in range 0"\
                     "to %d and gamma must be 0 to 10.", (int) MaxRGB);
        return NULL;
    }
    FormatString(message, "%g,%g,%g", black, white, mid);
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!LevelImage(mag,message))
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}


static char doc_levelchannel_image[] = \
"img.levelchannel(channel,black(0.0),mid(1.0),white(MaxRGB)) \n\n"\
" Adjusts the levels of an image channel by scaling the colors falling between"\
"\n"\
"   specified white and black points to the full available quantum range.\n"\
"   The black point specifies the darkest color in the image.\n"\
"       Colors darker than the black point are set to zero. \n"\
"   Mid point specifies a gamma correction to apply to the image. \n"\
"   White point specifies the lightest color in the image. \n"\
"       Colors brighter than the white point are set to the maximum quantum \n"\
"       value.\n\n"\
"   If black and white are < 1 they are interpreted as percentages of MaxRGB.";
static PyObject *
levelchannel_image(PyObject *self, PyObject *args)
{
    Image *mag;
    char *channel;
    double black=0.0, mid=1.0, white=(double) MaxRGB;
    int chan;

    if (!PyArg_ParseTuple(args, "s|ddd",&channel,&black, &white, &mid))
        return NULL;
    if ((chan=LookupStr(ChannelTypes, channel))<0)
        ERRMSG3(channel, "channel");

    if (black < 1) black *= MaxRGB;
    if (white < 1) white *= MaxRGB;
    if ((black < 0) || (black > MaxRGB) || \
        (white < 0) || (white > MaxRGB) || \
        (mid < 0) || (mid > 10)) {
        PyErr_Format(PyMagickError,"white and black must be in range 0"\
                     "to %d and gamma must be 0 to 10.", (int) MaxRGB);
        return NULL;
    }
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!LevelImageChannel(mag, (ChannelType) chan,
                               black, white, mid))
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}


static char doc_modulate_image[] = \
"img.modulate(brightness,{saturation,hue}) \n\n"\
" Control the percent change in the brightness, saturation, and hue of the\n"\
"   image (100 means no change)";
static PyObject *
modulate_image(PyObject *self, PyObject *args)
{
    Image *mag;
    double brightness, saturation=100.0, hue=100.0;
    char message[MaxTextExtent];

    if (!PyArg_ParseTuple(args, "d|dd",&brightness, &saturation, &hue))
        return NULL;
    FormatString(message, "%g,%g,%g", brightness, saturation, hue);
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!ModulateImage(mag,message))
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}

static char doc_negate_image[] = "img.negate({grayscale}) \n\n"\
" Negates the colors in the image.  \n"\
"   If grayscale (default false) is true then only the grayscale values in\n"\
"   the image are negated."; 
static PyObject *
negate_image(PyObject *self, PyObject *args)
{
    PyObject *obj=NULL;
    Image *mag;
    unsigned int grayscale;

    if (!PyArg_ParseTuple(args, "|O",&obj)) return NULL;
    if (obj==NULL) grayscale = 0;
    else grayscale = PyObject_IsTrue(obj);
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!NegateImage(mag,grayscale))
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}

#if 0
ThresholdImageChannel
#endif
static char doc_threshold_image[] = "img.threshold(R,{G,B,O}) \n\n"\
" Create an image based on a threshold value. \n\n"\
"   All values should be between 0 and MaxRGB.  If < 1 then values are\n"\
"   interpreted as percentages of maximum.\n"\
"   If G & B & O not given then threshold is based on pixel intensity and a \n"\
"      two-color image is produced.";
static PyObject *
threshold_image(PyObject *self, PyObject *args)
{
    Image *mag;
    double red, green, blue, opacity;
    char message[MaxTextExtent];
    int numargs;

    if (!PyArg_ParseTuple(args, "d|ddd",&red, &green, &blue, &opacity))
        return NULL;
    numargs = PyTuple_Size(args);
    if (numargs < 4) opacity = red;
    if (numargs < 3) blue = red;
    if (numargs < 2) green = red;
    if (red < 1) red *= MaxRGB;
    if (green < 1) green *= MaxRGB;
    if (blue < 1) blue *= MaxRGB;
    if (opacity < 1) opacity *= MaxRGB;
    if ((red < 0) || (red > MaxRGB) || \
        (green < 0) || (green > MaxRGB) || \
        (blue < 0) || (blue > MaxRGB) || \
    (opacity < 0) || (opacity > MaxRGB)) {
        PyErr_Format(PyMagickError,"values must be in range 0 to %d",
             (int) MaxRGB);
        return NULL;
    }
    if (numargs==1) {
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!ThresholdImage(mag,red)) {
        CHECK_ERR_IM(mag);
        }
    }
    }
    else {
    FormatString(message, "%g,%g,%g,%g", red, green, blue, opacity);
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!ChannelThresholdImage(mag,message))
        CHECK_ERR_IM(mag);
    }
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}


static char doc_solarize_image[] = "img.solarize(thresh(50.0))\n\n"\
" Apply a special effect similar to the effect achieved in a photo\n"\
"   darkroom by selectively exposing areas of photo sensitive paper\n"\
"   to light.  \n"\
"   thresh  ranges from 0 to MaxRGB and is a measure of the extent of the\n"\
"           solarization.  If thresh is < 1 then it is interpreted as a \n"\
"           fraction of MaxRGB.";
static PyObject *
solarize_image(PyObject *self, PyObject *obj)
{
    Image *mag;
    double thresh=50.0;
    int N;
 
    N = PySequence_Length(obj);
    if (N > 0) {
        thresh = PyFloat_AsDouble(obj);
        if ((thresh==-1) && PyErr_Occurred()) return NULL;
        if (thresh < 1) thresh*=MaxRGB;
    }
    if ((thresh < 0) || (thresh > MaxRGB)) {
        PyErr_Format(PyMagickError,"thresh must be in range 0 to %d",
                     (int) MaxRGB);
        return NULL;
    }
    
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
    SolarizeImage(mag, thresh);
        CHECK_ERR_IM(mag);
    }
    if (PyErr_Occurred()) PyErr_Clear();
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}



static char doc_raise_image[] = "img.raise(width(6), height(6), raise(1)})\n\n"\
" Create a simulated three-dimensional button-like effect in the current\n"\
"   image by lightening and darkening the edges of the image.\n"\
"   Width and height parameters define the width of the vertical and\n"\
"   horizontal edge of the effect.  A value of raise other than zero\n"\
"   produces a 3-D raise effect, a value of raise that is zero produces\n"\
"   a lowered effect (default = 1)";
static PyObject *
raise_image(PyObject *self, PyObject *args)
{
    Image *mag;
    int width=6, height=6, raise=1;
    RectangleInfo raise_info;
    
    if (!PyArg_ParseTuple(args, "|iii",&width, &height, &raise))
        return NULL;

    if ((width < 0) || (height < 0)) 
        ERRMSG("Width and height must be >= 0");
    
    raise_info.width = width;
    raise_info.height = height;
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
    if (!RaiseImage(mag, &raise_info, raise))
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}

static PyObject *clear_draw(PyObject *);

static char doc_draw_image[] = "img.draw(primitives)\n\n"\
" Draw on an image, using primitives \n"\
"   Primitives is a string or an object with a .__primitives__ attribute\n"\
"   that is a string or a .__primitives__() method that produces a string\n"\
"   The string will be passed directly to ImageMagick to be used\n"\
"   in drawing on the image.  primitives can also be a drawing info object.\n"\
"   Primitives can either draw or set parameters for basic drawing.\n"\
"   It is like a graphics language.\n";
static PyObject *
draw_image(PyObject *self, PyObject *obj)
{
    Image *mag;
    PyObject *meth=NULL;
    PyObject *res=NULL;
    DrawInfo *draw_info=NULL, *current=NULL;
    char *primitives=NULL;
    char 
    errm[]="argument must be a string or an object with a\n"\
    "__primitives__ attribute that is a string or a \n"\
    "__primitives__() method that produces a string.\n"\
    "   or a special draw_info object.";
    int dcobj=0;
    
    if (PyDrawInfo_Check(obj)) {
    primitives = ASDI(obj)->prim;
    current = ASDI(obj)->info;
    dcobj=1;
    }
    else if (PyString_Check(obj)) primitives = PyString_AS_STRING(obj);
    else {
        meth = PyObject_GetAttrString(obj, "__primitives__");
        if (meth == NULL) 
            ERRMSG(errm);
        if (PyString_Check(meth)) primitives = PyString_AS_STRING(meth);
        else {
            if (PyCallable_Check(meth)) {
                res = PyObject_CallObject(meth, (PyObject *)NULL);
                if (res == NULL) ERRMSG(errm);
                if (PyString_Check(res)) 
                    primitives = PyString_AS_STRING(res);
                else
                    ERRMSG(errm);
            }
            else 
                ERRMSG(errm);
        }
    }
    Py_XDECREF(meth);
    Py_XDECREF(res);
    if (primitives == NULL) goto done;
        
    draw_info = CloneDrawInfo(NULL, current);
    if (!CloneString(&(draw_info->primitive), primitives))
        ERRMSG("Could not copy primitives to drawing context.");
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
    DrawImage(mag, draw_info);
        CHECK_ERR_IM(mag);
    }
    DestroyDrawInfo(draw_info);
    if (dcobj) {
    return clear_draw(obj);
    }
 done:
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    Py_XDECREF(meth);
    Py_XDECREF(res);
    if (draw_info) DestroyDrawInfo(draw_info);
    return NULL; 
}


static char doc_clip_path_image[] = "img.clip_path(primitives)\n\n"\
" Set the clipping mask for an image.  Clip_masks define the set of pixels over which\n"\
"   some operations occur\n"\
"   Primitives is a string or an object with a .__primitives__ attribute\n"\
"   that is a string or a .__primitives__() method that produces a string\n"\
"   The string will be passed directly to ImageMagick to be used\n"\
"   in drawing on the image.  primitives can also be a drawing info object.\n"\
"   Primitives can either draw or set parameters for basic drawing.\n"\
"   It is like a graphics language.\n";
static PyObject *
clip_path_image(PyObject *self, PyObject *obj)
{
    Image *mag;
    PyObject *meth=NULL;
    PyObject *res=NULL;
    DrawInfo *draw_info=NULL, *current=NULL;
    char *primitives=NULL;
    char 
    errm[]="argument must be a string or an object with a\n"\
    "__primitives__ attribute that is a string or a \n"\
    "__primitives__() method that produces a string.\n"\
    "   or a special draw_info object.";
    int dcobj=0;
    
    if (PyDrawInfo_Check(obj)) {
    primitives = ASDI(obj)->prim;
    current = ASDI(obj)->info;
    dcobj=1;
    }
    else if (PyString_Check(obj)) primitives = PyString_AS_STRING(obj);
    else {
        meth = PyObject_GetAttrString(obj, "__primitives__");
        if (meth == NULL) 
            ERRMSG(errm);
        if (PyString_Check(meth)) primitives = PyString_AS_STRING(meth);
        else {
            if (PyCallable_Check(meth)) {
                res = PyObject_CallObject(meth, (PyObject *)NULL);
                if (res == NULL) ERRMSG(errm);
                if (PyString_Check(res)) 
                    primitives = PyString_AS_STRING(res);
                else
                    ERRMSG(errm);
            }
            else 
                ERRMSG(errm);
        }
    }
    if (primitives == NULL) 
        ERRMSG("Nothing to draw.");
    draw_info = CloneDrawInfo(NULL, current);
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        (void) SetImageAttribute(mag, "[_internal_clip]", primitives);
        if (!DrawClipPath(mag, draw_info, "_internal_clip"))
            CHECK_ERR_IM(mag);
        
    }
    DestroyDrawInfo(draw_info);
    Py_XDECREF(meth);
    Py_XDECREF(res);
    if (dcobj) {
    return clear_draw(obj);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    Py_XDECREF(meth);
    Py_XDECREF(res);
    if (draw_info) DestroyDrawInfo(draw_info);
    return NULL; 
}

static char doc_annotate_image[] = "img.annotate(dc, x, y, text)\n\n"\
" Annotate an image with text at offset x, y.\n"\
"   dc must be a DrawInfo Object (drawing context) \n"\
"   text can contain the following special codes with given meanings\n"\
"    %b   file size in bytes.\n"\
"    %c   comment.\n"\
"    %d   directory in which the image resides.\n"\
"    %e   extension of the image file.\n"\
"    %f   original filename of the image.\n"\
"    %h   height of image.\n"\
"    %i   filename of the image.\n"\
"    %k   number of unique colors.\n"\
"    %l   image label.\n"\
"    %m   image file format.\n"\
"    %n   number of images in a image sequence.\n"\
"    %o   output image filename.\n"\
"    %p   page number of the image.\n"\
"    %q   image depth (8 or 16).\n"\
"    %p   page number of the image.\n"\
"    %q   image depth (8 or 16).\n"\
"    %s   image scene number.\n"\
"    %t   image filename without any extension.\n"\
"    %u   a unique temporary filename.\n"\
"    %w   image width.\n"\
"    %x   x resolution of the image.\n"\
"    %y   y resolution of the image.";
static PyObject *
annotate_image(PyObject *self, PyObject *args)
{
    Image *mag;
    DrawInfo *clone_info=NULL;
    PyObject *dc;
    char *text;
    long x, y;
    char geomstr[MaxTextExtent];

    if (!PyArg_ParseTuple(args,"O!lls", &DrawInfo_Type, &dc, &x, &y, &text)) 
      return NULL;
    clone_info = CloneDrawInfo((ImageInfo *)NULL, ASDI(dc)->info);
    if (clone_info==NULL) ERRMSG("Problem copying drawing context.");
    if (!CloneString(&(clone_info->text), text))
        ERRMSG("Could not copy text to drawing context.");
    FormatString(geomstr, "+%ld+%ld", x, y);
    if (clone_info->geometry != NULL) 
        MagickFree(clone_info->geometry);
    if (!CloneString(&(clone_info->geometry), geomstr))
        ERRMSG("Memory error");
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!AnnotateImage(mag, clone_info))
            CHECK_ERR_IM(mag);
    }
    DestroyDrawInfo(clone_info);
    Py_INCREF(Py_None);
    return Py_None;
    
 fail:
    if (clone_info) DestroyDrawInfo(clone_info);
    return NULL;
}

/* 
   TypeMetric structure:
   PointInfo {double x, y}  pixels_per_em;
   double ascent,descent, width, height, max_advance;
   SegmentInfo {double x1,y1,x2,y2} bounds;
   double underline_position;
   double underline thickness
 */
static char doc_gettypemetrics_image[] = "img.get_type_metrics(dc, text)\n\n"\
" Get height and width of text rendered to image using current dc.\n"\
"   Dc is a DrawInfo object and is unaltered by the command.\n"\
"   Returned values are in a tuple as follows: all values are floats\n\n"\
"   (pixels_per_em (x,y),\n"\
"    ascent, descent, width, height, max_advance,\n"\
"    bounds (x1,y1,x2,y2),\n"\
"    underline_position, underline_thickness)";
static PyObject *
gettypemetrics_image(PyObject *self, PyObject *args)
{
    Image *mag;
    DrawInfo *clone_info=NULL;
    PyObject *dc;
    char *text;
    TypeMetric metrics;


    if (!PyArg_ParseTuple(args,"O!s", &DrawInfo_Type, &dc, &text)) 
      return NULL;
    clone_info = CloneDrawInfo((ImageInfo *)NULL, ASDI(dc)->info);
    if (clone_info==NULL) ERRMSG("Problem copying drawing context.");
    if (!CloneString(&(clone_info->text), text))
        ERRMSG("Could not copy text to drawing context.");
    mag = ASIM(self)->ims;
    if (mag == NULL) ERRMSG("Cannot draw on null image.");
    if (!GetTypeMetrics(mag, clone_info, &metrics))
        ERRMSG("Error in calculation.");
    DestroyDrawInfo(clone_info);
    return Py_BuildValue("(dd)ddddd(dddd)dd", metrics.pixels_per_em.x,
                         metrics.pixels_per_em.y, metrics.ascent,
                         metrics.descent, metrics.width, metrics.height,
                         metrics.max_advance, metrics.bounds.x1,
                         metrics.bounds.y1, metrics.bounds.x2, 
                         metrics.bounds.y2, metrics.underline_position,
                         metrics.underline_thickness);
    
 fail:
    if (clone_info) DestroyDrawInfo(clone_info);
    return NULL;
}




static char doc_colorfloodfill_image[] = \
"img.colorfloodfill(target, fill, x, y, toborder(0), bordercolor=, fuzz=)\n\n"\
" Change the color value of any pixel that matches target and is\n"\
"   an immediate neighbor to x,y.  If the toborder is non-zero (default 0)\n"\
"   then the color value is changed for any neighbor pixel that does \n"\
"   not match the bordercolor member of img.\n\n"\
" By default target must match a particular pixel color exactly.  However,\n"\
"   in many cases two colors may differ by a small amount.  The fuzz,\n"\
"   member of image defines how much tolerance is acceptable to consider\n"\
"   two colors as the same. \n\n"\
" Fill can be any color object (tuple or string) or it can be an image to\n"\
"   fill with an image.\n\n"\
" Any keywords given are interpreted as attributes of img to set prior\n"\
"   to performing the flood fill.\n";
static PyObject *
colorfloodfill_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *tcolor, *fcolor;
    PyObject *imobj = NULL;
    Image *mag;
    long xoffset, yoffset;
    int toborder=0;
    int imagefill=0;
    PixelPacket target, fill;
    DrawInfo *draw_info=NULL;
    PaintMethod method;
    
    if (!PyArg_ParseTuple(args, "OOll|i", &tcolor, &fcolor, &xoffset, 
                          &yoffset, &toborder)) return NULL;

    if (toborder == 0) method = FloodfillMethod;
    else method = FillToBorderMethod;

    if (kwds != NULL) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        
        while (PyDict_Next(kwds, &pos, &key, &value)) {
            if (mimage_setattr(ASIM(self), PyString_AS_STRING(key), 
                               value) == -1) goto fail;
        }
    }    
    draw_info = CloneDrawInfo(NULL, NULL);
    if (draw_info == NULL) return NULL;
    if (!set_color_from_obj(&target, tcolor, "colorfloodfill (target)"))
        goto fail;
    if (!set_color_from_obj(&fill, fcolor, "")) {
        if (PyErr_Occurred()) PyErr_Clear();
        if ((imobj = mimage_from_object(fcolor))==NULL) 
            ERRMSG("Fill must be a color or an image.");
        imagefill = 1;
    }

    if (imagefill) {
        if (ASIM(imobj)->ims)
            draw_info->fill_pattern = CloneImage(ASIM(imobj)->ims,0,0,True,&exception);
        else 
            draw_info->fill_pattern = NULL;
        CHECK_ERR;
    }
    else {
        draw_info->fill_pattern = NULL;
        draw_info->fill = fill;
    }

    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
    if (!ColorFloodfillImage(mag, draw_info, target, xoffset, 
                                 yoffset, method))
            CHECK_ERR_IM(mag);
    }
    DestroyDrawInfo(draw_info);
    Py_XDECREF(imobj);
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    if (draw_info) DestroyDrawInfo(draw_info);
    Py_XDECREF(imobj);
    return NULL; 
}

static char doc_mattefloodfill_image[] = \
"img.mattefloodfill(target, opacity, x, y, toborder(0), bordercolor=, fuzz=)\n\n"\
" Change the transparency value of any pixel that matches target and is\n"\
"   an immediate neighbor to x,y.  If the toborder is non-zero (default 0)\n"\
"   then the color value is changed for any neighbor pixel that does \n"\
"   not match the bordercolor member of img.\n\n"\
" By default target must match a particular transparency exactly.  However,\n"\
"   in many cases two colors may differ by a small amount.  The fuzz,\n"\
"   member of image defines how much tolerance is acceptable to consider\n"\
"   two colors as the same. \n\n"\
" Opacity is the level of transparency (0 is fully opaque and MaxRGB is fully\n"\
"   transparent.\n\n"\
" Any keywords given are interpreted as attributes of img to set prior\n"\
"   to performing the fill.\n";
static PyObject *
mattefloodfill_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *tcolor;
    Image *mag;
    long xoffset, yoffset;
    int toborder=0;
    int opacity;
    PixelPacket target;
    PaintMethod method;
    
    if (!PyArg_ParseTuple(args, "Oill|i", &tcolor, &opacity, &xoffset, 
                          &yoffset, &toborder)) return NULL;

    if ((opacity < 0) || (opacity > MaxRGB)) 
        ERRMSG("opacity must be <= MaxRGB and >= 0");

    if (toborder == 0) method = FloodfillMethod;
    else method = FillToBorderMethod;

    if (kwds != NULL) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwds, &pos, &key, &value)) {
            if (mimage_setattr(ASIM(self), PyString_AS_STRING(key), 
                               value) == -1) goto fail;
        }
    }    
    if (!set_color_from_obj(&target, tcolor, "mattefloodfill (target)"))
        goto fail;

    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
    if (!MatteFloodfillImage(mag, target, opacity, xoffset, 
                                 yoffset, method))
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}


static char doc_opaque_image[] = \
"img.opaque(target, fill, fuzz=)\n\n"\
" Change any pixel that matches target with the color defined by fill.\n\n"\
" By default target must match a particular transparency exactly.  However,\n"\
"   in many cases two colors may differ by a small amount.  The fuzz,\n"\
"   member of image defines how much tolerance is acceptable to consider\n"\
"   two colors as the same. \n\n"\
" Any keywords given are interpreted as attributes of img to set prior\n"\
"   to performing the change.\n";
static PyObject *
opaque_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *tcolor, *fcolor;
    Image *mag;
    PixelPacket target, fill;
    
    if (!PyArg_ParseTuple(args, "OO", &tcolor, &fcolor)) return NULL;

    if (kwds != NULL) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwds, &pos, &key, &value)) {
            if (mimage_setattr(ASIM(self), PyString_AS_STRING(key), 
                               value) == -1) goto fail;
        }
    }    
    if (!set_color_from_obj(&target, tcolor, "opaque (target)"))
        goto fail;
    if (!set_color_from_obj(&fill, fcolor, "opaque (fill)"))
        goto fail;

    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
    if (!OpaqueImage(mag, target, fill))
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}


static char doc_transparent_image[] = \
"img.transparent(target, opacity, fuzz=)\n\n"\
" Change the opacity of any pixel that matches target to the given opacity.\n\n"\
" By default target must match a particular transparency exactly.  However,\n"\
"   in many cases two colors may differ by a small amount.  The fuzz,\n"\
"   member of image defines how much tolerance is acceptable to consider\n"\
"   two colors as the same. \n\n"\
" Any keywords given are interpreted as attributes of img to set prior\n"\
"   to performing the change.\n";
static PyObject *
transparent_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *tcolor;
    Image *mag;
    int opacity;
    PixelPacket target;
    
    if (!PyArg_ParseTuple(args, "Oi", &tcolor, &opacity)) return NULL;

    if ((opacity < 0) || (opacity > MaxRGB)) 
        ERRMSG("opacity must be <= MaxRGB and >= 0");

    if (kwds != NULL) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwds, &pos, &key, &value)) {
            if (mimage_setattr(ASIM(self), PyString_AS_STRING(key), 
                               value) == -1) goto fail;
        }
    }    
    if (!set_color_from_obj(&target, tcolor, "transparent (target)"))
        goto fail;

    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
    if (!TransparentImage(mag, target, opacity))
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}

static int
get_affine_matrix(AffineMatrix *matrix, PyObject *affobj) 
{
    int len, cols;
    double *affptr;
    PyObject *affarr;

    /* Get affine transform elements */
    affarr = PyArray_ContiguousFromObject(affobj,PyArray_DOUBLE,1,2);
    if (affarr == NULL) return False;
    affptr = (double *)DATA(affarr);
    len = DIM(affarr,0);
    if (RANK(affarr)==1) { /* sequence sx, rx, ry, sy, tx, ty */
        if ((len < 4) || (len > 6)) 
            ERRMSG("Affine sequence must be at least 4 elements and "\
                   "no more than 6.");
        matrix->sx = *(affptr);
        matrix->rx = *(affptr+1);
        matrix->ry = *(affptr+2);
        matrix->sy = *(affptr+3);
        matrix->tx = (len > 4) ? *(affptr + 4) : 0.0;
        matrix->ty = (len > 5) ? *(affptr + 5) : 0.0;
    }
    else {   /* matrix input */
        cols = DIM(affarr,1);
        if ((len < 2) || (len > 3) || (cols < 2) || (cols > 3)) 
            ERRMSG("Affine matrix must be 2x2, 2x3, 3x2, or 3x3.");
        matrix->sx = *(affptr);
        matrix->rx = *(affptr+1);
        matrix->ry = *(affptr+cols);
        matrix->sy = *(affptr+cols+1);
        matrix->tx = (len == 3) ? *(affptr+cols*2) : 0.0;
        matrix->ty = (len == 3) ? *(affptr+cols*2+1): 0.0;
    }
    Py_DECREF(affarr);
    return(True);
    
 fail:
    Py_XDECREF(affarr);
    return(False);
}


static char doc_drawaffine_image[] = \
"img.drawaffine(source, affine)\n\n"\
" Composite the source over the current image as dictated by the affine \n"\
"   transformation.\n"\
"   affine  can be a 2x2, 3x3, 2x3, 3x2 matrix or a length 4-6 sequence.\n";
static PyObject *
drawaffine_image(PyObject *self, PyObject *args)
{
    PyObject *source, *affobj;
    PyObject *imobj = NULL;
    Image *mag, *sim=NULL;
    AffineMatrix matrix;

    if (!PyArg_ParseTuple(args, "OO", &source, &affobj)) return NULL;

    if (!get_affine_matrix(&matrix, affobj)) return NULL;

    if ((imobj = mimage_from_object(source))==NULL) return NULL;
    
    for (mag=ASIM(self)->ims; mag; mag=mag->next, sim=sim->next) {
        if (sim==NULL) sim = ASIM(imobj)->ims;
    if (!DrawAffineImage(mag, sim, &matrix))
            CHECK_ERR_IM(mag);
    }
    Py_XDECREF(imobj);
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    Py_XDECREF(imobj);
    return NULL; 
}


static char doc_composite_image[] = \
"img.composite(source, xoff(0), yoff(0), method('over'))\n\n"\
" Composite the source over the current image using method operator.\n"\
"   Offset the composited image by xoff columns and yoff rows.";
static PyObject *
composite_image(PyObject *self, PyObject *args)
{
    PyObject *source;
    char *method=NULL;
    PyObject *imobj = NULL;
    Image *mag, *sim=NULL;
    long xoff=0, yoff=0;
    int ind;

    if (!PyArg_ParseTuple(args, "O|lls", &source, &xoff, &yoff, &method)) 
        return NULL;

    if (method==NULL) method = "over";
    if ((ind = LookupStr(CompositeTypes, method)) < 0)
        ERRMSG3(method,"composite");
    
    if ((imobj = mimage_from_object(source))==NULL) return NULL;
    
    for (mag=ASIM(self)->ims; mag; mag=mag->next, sim=sim->next) {
        if (sim==NULL) sim = ASIM(imobj)->ims;
    if (!CompositeImage(mag, (CompositeOperator) ind, sim, xoff, yoff))
            CHECK_ERR_IM(mag);
    }
    Py_XDECREF(imobj);
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    Py_XDECREF(imobj);
    return NULL; 
}



static char doc_clip_image[] = "Clip and image according to any clip_path";
static PyObject *
clip_image(PyObject *self)
{
    Image *im;

    im = ASIM(self)->ims;
    if (!ClipImage(im))
        CHECK_ERR_IM(im);
    
    Py_INCREF(Py_None);
    return Py_None;
    
 fail:
    return NULL;    
}

static char doc_set_image[] = \
"img.set(color(img.background), opacity(0))\n\n"\
" Set the color of each pixel in the image to color with given opacity\n\n"\
" img.set()  erases the entire image with the img.background;\n";
static PyObject *
set_image(PyObject *self, PyObject *args)
{
    Image *mag;
    PyObject *tcolor=NULL;
    int opacity=OpaqueOpacity;
    int nocolor=0;
    PixelPacket target;
    
    if (!PyArg_ParseTuple(args, "|Oi", &tcolor, &opacity)) return NULL;

    if ((opacity < 0) || (opacity > MaxRGB)) 
        ERRMSG("opacity must be <= MaxRGB and >= 0");

    if (tcolor==NULL)
        nocolor = 1;
    else
        if (!set_color_from_obj(&target, tcolor, "set (target)"))
            goto fail;

    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!nocolor)
            mag->background_color = target;
        SetImage(mag, opacity);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}


static char doc_describe_image[] = \
"img.describe(verbose(0), file(stdout))\n\n"\
" Describe an image by printing attributes to the file.";
static PyObject *
describe_image(PyObject *self, PyObject *args)
{
    PyObject *fileobj=NULL;
    int verbose = 0;
    FILE *fid;
    
    if (!PyArg_ParseTuple(args, "|iO", &verbose, &fileobj)) return NULL;

    if (fileobj==NULL) fid = stdout;
    else fid = PyFile_AsFile(fileobj);
    if (fid == NULL) ERRMSG("File must be a valid file object");
    DescribeImage(ASIM(self)->ims, fid, verbose);
    Py_INCREF(Py_None);
    return Py_None;
    
 fail:
    return NULL;

}


static char doc_setopacity_image[] = \
"img.setopacity(opacity)\n\n"\
" Attenuates the opacity channel of an image. If the image pixels are\n"\
"   opaque, they are set to the specified opacity level. Otherwise, the\n"\
"   pixel opacity values are blended with the supplied transparency value";

static PyObject *
setopacity_image(PyObject *self, PyObject *args)
{
    Image *mag;
    int opacity=0;
    
    if (!PyArg_ParseTuple(args, "i", &opacity)) return NULL;

    if ((opacity < 0) || (opacity > MaxRGB)) 
        ERRMSG("opacity must be <= MaxRGB and >= 0");

    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        SetImageOpacity(mag, opacity);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}


static char doc_diff_image[] = \
"img.diff(ref)\n\n"\
" Measure the difference between colors at each pixel location of two images.\n"\
"   Return 0 if the colors match exactly.\n"\
"   Otherwise, an error measure is computed and placed in the attributes.\n"\
"   img.error  (mean error for any single pixel)\n"\
"   img.mean_error  (normalized mean quantization error -- range 0 to 1)\n"\
"   img.max_error (normalized maximum quantization error)\n\n"\
" Uses only the first images in an image sequence.";
static PyObject *
diff_image(PyObject *self, PyObject *args)
{
    PyObject *source, *val;
    PyObject *imobj = NULL;
     

    if (!PyArg_ParseTuple(args, "O", &source)) return NULL;

    if ((imobj = mimage_from_object(source))==NULL) return NULL;
    
    if ((ASIM(self)->ims==NULL) || (ASIM(imobj)->ims)==NULL)
        ERRMSG("Images must have length >=1");

    val = Py_BuildValue("i", IsImagesEqual(ASIM(self)->ims,ASIM(imobj)->ims));
    Py_DECREF(imobj);
    return val;

 fail:
    Py_XDECREF(imobj);
    return NULL; 
}

static char doc_map_image[] = \
"img.map(ref, dither(1))\n\n"\
" Replace the colors of img with the closest color from ref.\n"\
"   If dither is non-zero, dither the quantized image.\n\n"\
" If img is an image list then map each image in the list, using\n"\
"   ref in a cyclic fashion.";
static PyObject *
map_image(PyObject *self, PyObject *args)
{
    Image *mag, *sim=NULL;
    PyObject *ref;
    PyObject *imobj = NULL;
    int dither=1;
    
    if (!PyArg_ParseTuple(args, "O|i", &ref, &dither)) return NULL;

    if ((imobj = mimage_from_object(ref))==NULL) return NULL;
    
    for (mag=ASIM(self)->ims; mag; mag=mag->next, sim=sim->next) {
        if (sim==NULL) sim = ASIM(imobj)->ims;
    if (!MapImage(mag, sim, dither))
            CHECK_ERR_IM(mag);
    }
    Py_DECREF(imobj);
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    Py_XDECREF(imobj);
    return NULL; 
}


static char doc_channel_image[] = "img.channel(chan)\n\n"\
" Extract a channel ('red', 'green', 'blue', etc) from the image.";
static PyObject *
channel_image(PyObject *self, PyObject *args)
{
    Image *mag;
    char *chan;
    int ind;
    
    if (!PyArg_ParseTuple(args, "s",&chan)) return NULL;
    
    if ((ind = LookupStr(ChannelTypes, chan)) < 0)
        ERRMSG3(chan,"channel");
    
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!ChannelImage(mag,(ChannelType) ind))
            CHECK_ERR_IM(mag);
    }            
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}

static char doc_cyclecolor_image[] = \
"img.cyclecolor(amount)\n\n"\
" Cycle the colormap of the image by the given amount.";
static PyObject *
cyclecolor_image(PyObject *self, PyObject *args)
{
    Image *mag;
    int amount;
    
    if (!PyArg_ParseTuple(args, "i", &amount)) return NULL;

    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        CycleColormapImage(mag, amount);
    }
    Py_INCREF(Py_None);
    return Py_None;

}

static char doc_ordereddither_image[] = \
"img.ordered_dither()\n\n"\
" Uses the ordered dithering technique of reducing color images to \n"\
"   monochrome using positional information to retain as much information\n"\
"   as possible.";
static PyObject *
ordereddither_image(PyObject *self)
{
    Image *mag;
    
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!OrderedDitherImage(mag)) 
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL;
}



static char doc_plasma_image[] = \
"img.plasma(region,atten,depth)\n\n"\
" Initialize an image with plasma fractal values.\n"\
" region is (x1,y1,x2,y2) to describe the region to initialize.";
static PyObject *
plasma_image(PyObject *self, PyObject *args)
{
    Image *mag;
    long atten, depth;
    SegmentInfo seg;
    
    if (!PyArg_ParseTuple(args, "(dddd)ll", &(seg.x1), &(seg.y1), &(seg.x2),
                          &(seg.y2), &atten, &depth)) return NULL;

    if ((atten < 0) || (depth < 0)) 
        ERRMSG("atten and depth must be >= 0");
    srand((unsigned int) time(NULL));
    for (mag=ASIM(self)->ims; mag; mag=mag->next) {
        if (!PlasmaImage(mag, &seg, atten, depth))
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL; 
}


static char doc_segment_image[] = \
"img.segment(colorspace('rgb'), verbose(0), cluster(1.0), smooth(1.5))\n\n"\
" Segment an image using fuzzy C-means techniqueto identify units of\n"\
"   the histograms of the color components that are homogeneous.\n\n"\
" colorpsace indicates the colorspace.\n"\
" verbose    if non-zero print detailed information about the classes.\n"\
" cluster    the minimum number of pixels contained in a hexahedra before\n"\
"            it can be considered valid (expressed as a percentage)\n"\
" smooth     eliminates noise in the second derivative of the histogram\n"\
"            as this value is increased, the second derivative is smoother\n";
static PyObject *
segment_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyMImageObject *imobj = (PyMImageObject *)self;
    Image *mag;
    char *cspace=NULL;
    int verbose=0;
    double cluster=1.0, smooth=1.5;
    int ind;

    if (!PyArg_ParseTuple(args, "|sidd", &cspace, &verbose, &cluster,
                          &smooth)) return NULL;

    if (cspace==NULL) cspace="rgb";
    if ((ind = LookupStr(ColorspaceTypes, cspace)) < 0)
        ERRMSG3(cspace,"segment");

    for (mag=imobj->ims; mag; mag=mag->next) {
        if (!SegmentImage(mag, (ColorspaceType) ind, verbose,
                          cluster, smooth))
            CHECK_ERR_IM(mag);
    }
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL;    
}


static char doc_index_image[] = \
"img.index(x,y<,z_or_index, index>)\n\n"\
" Gets/sets the index of the pixel at x,y,z\n"\
" If z_or_color is an integer it is the img in the list to work with and\n"\
"   the fourth argument is the index.\n\n"\
" Thus, IF YOU WANT THE THIRD ARGUMENT TO BE THE INDEX AND DEFAULT TO z=0\n"\
"   YOU MUST PASS IT IN AS AN OBJECT THAT IS NOT AN INTEGER but that Python\n"\
"   can convert to an integer (float, string, Numeric array, etc.)\n\n"\
" If index is present then index is changed and the old index is returned.\n"\
"   This only works for PseudoClass images.";
static PyObject *
index_image(PyObject *self, PyObject *args)
{
    Image *mag;
    PyObject *z_or_index=NULL;
    PixelPacket old_color, *pixel;
    long old_index, index=-1;
    long x,y,z=0;
    int N, k;
    int get=1;
    IndexPacket *indexes;


    if (!PyArg_ParseTuple(args, "ll|Ol", &x, &y, &z_or_index, &index))
        return NULL;
    
    if (z_or_index!=NULL) {
        if (PyInt_Check(z_or_index)) {
            z = PyInt_AS_LONG(z_or_index);
            if (index!=-1) get=0;
        }
        else {
            if (index!=-1)
                ERRMSG("Invalid arguments: if z_or_index is not an integer\n"\
                       "  then index should not be given.");
            index=PyInt_AsLong(z_or_index); /* will convert if needed */
            if (PyErr_Occurred()) return NULL;
            get=0;
        }
    }

    N = GetImageListLength(ASIM(self)->ims);
    if ((z < 0) || (z >= N))  
        ERRMSG("Given value of z is too large for image list.");
    k = 0;
    for (mag=ASIM(self)->ims; k < z; mag=mag->next) k++;
    
    if (mag->storage_class != PseudoClass)
        ERRMSG("getting and setting indexes only works for PseudoClass"\
               " images.");
    
    if (get) {                   /* get the pixel */        
        old_color = *AcquireImagePixels(mag, x, y, 1, 1, &exception);
        CHECK_ERR;
        
        indexes = GetIndexes(mag);
        old_index = (long) (*indexes);
    }
    else {
        if ((index < 0) || (index >= mag->colors))
            ERRMSG("Index is not in colormap.");
        pixel = GetImagePixels(mag, x, y, 1, 1);
        if (!pixel) 
            CHECK_ERR_IM(mag);
        indexes = GetIndexes(mag);
        old_index = (long) (*indexes);
        *indexes = index;        
        if (!SyncImagePixels(mag)) 
            ERRMSG("image pixels could not be synced");
    }
    return Py_BuildValue("l", old_index);
        
 fail:
    return NULL; 
}



static char doc_pixelcolor_image[] = \
"img.pixel(x,y<,z_or_color, color>)\n\n"\
" Gets/sets the color of the pixel at x,y,z\n"\
" If z_or_color is an integer it is the img in the list to work with and\n"\
"   the fourth argument is the color otherwise it is intepreted as the\n"\
"   color and z=0 is assumed.\n"\
" If color is present then color is changed and the old_color is returned.";
static PyObject *
pixelcolor_image(PyObject *self, PyObject *args)
{
    Image *mag;
    PyObject *z_or_color=NULL;
    PyObject *ncolor=NULL;
    PixelPacket old_color = {0}, new_color, *pixel;
    long x,y,z=0;
    int N, k;
    int get=1;

    if (!PyArg_ParseTuple(args, "ll|OO", &x, &y, &z_or_color, &ncolor))
        return NULL;
    
    if (z_or_color!=NULL) {
        if (PyInt_Check(z_or_color)) {
            z = PyInt_AS_LONG(z_or_color);
            if (ncolor!=NULL) get=0;
        }
        else {
            if (ncolor!=NULL) 
                ERRMSG("Invalid arguments: if z_or_color is not an integer\n"\
                       "  then color should not be given.");
            ncolor=z_or_color;
            get = 0;
        }
    }

    N = GetImageListLength(ASIM(self)->ims);
    if ((z < 0) || (z >= N)) 
        ERRMSG("Image list to small for given value of z.");

    k = 0;
    for (mag=ASIM(self)->ims; k < z; mag=mag->next) k++;
    
    if (get) {     /* get the pixel */
        IndexPacket *indexes;
        
        old_color = *AcquireImagePixels(mag, x, y, 1, 1, &exception);
        CHECK_ERR;
        
        /* PseudoClass */
        if (mag->storage_class == PseudoClass) {
            indexes = GetIndexes(mag);
            old_color = mag->colormap[*indexes];
        }
        if (!mag->matte)
            old_color.opacity = OpaqueOpacity;
    }
    else {
        if (!set_color_from_obj(&new_color, ncolor, "pixel")) return NULL;
        if (mag->storage_class == PseudoClass) {
            SyncImage(mag);
            MagickFree(mag->colormap);
            mag->storage_class = DirectClass;
        }
        pixel = GetImagePixels(mag, x, y, 1, 1);
        if (pixel) {
            old_color = *pixel;
            if (!mag->matte) old_color.opacity = OpaqueOpacity;
        }
        *pixel = new_color;
        if (!SyncImagePixels(mag))
            ERRMSG("image pixels could not be synced");
    }
    return Py_BuildValue("llll", old_color.red, old_color.green,
                         old_color.blue, old_color.opacity);
        
 fail:
    return NULL; 
}


static char doc_getpixels_image[] = \
"img.getpixels(x,y,cols,rows<,z,imgs>)\n\n"\
" Gets a rectangle of raw pixel data from image.\n"\
" If not given z defaults to zero and imgs defaults to 1.";
static PyObject *
getpixels_image(PyObject *self, PyObject *args)
{
    Image *im, *image;
    PixelPacket *pixels;
    PixelPacket pix;
    long x,y, cols, rows, z=0, imgs=1;
    long size, n;
    long num;
    PyObject *arrobj=NULL;
    int nd, dims[4], ld;
    Quantum* arrptr;
    
    im = ASIM(self)->ims;
    if (!PyArg_ParseTuple(args, "llll|ll", &x, &y, &cols, &rows, &z,
                          &imgs)) return NULL;
    
    if ((x+cols > im->columns) || (y+rows > im->rows) || \
        (cols < 0) || (rows < 0)) {
        PyErr_Format(PyMagickError,"goemetry (%lux%lu%+ld%+ld) exceeds image bounds",
                     cols, rows, x, y);
        return NULL;
    }
    num = GetImageListLength(im);
    if ((z < 0) || (z+imgs > num) || (imgs < 0)) {
        PyErr_Format(PyMagickError,"z = %ld and imgs=%ld not valid", num, imgs);
        return NULL;
    }
    
    /* Create Array to hold pixels in */
    if (imgs > 1) { 
        dims[0] = imgs;
        dims[1] = rows;
        dims[2] = cols;
        if (im->matte) dims[3] = 4;
        else dims[3] = 3;
        nd = 4;
    }
    else {
        nd = 3; dims[0] = rows; dims[1] = cols;
        if (im->matte) dims[2] = 4;
        else dims[2] = 3;
        dims[3] = 1;
    }    
    arrobj = PyArray_FromDims(nd,dims,ptype);
    if (arrobj == NULL) return NULL;
    ld = dims[3];
    arrptr = (Quantum *) DATA(arrobj);
    size = rows*cols;
    for (image = im; image; image=image->next) {
        pixels = (PixelPacket *)AcquireImagePixels(image, x, y, cols, rows, 
                                                   &exception);
        CHECK_ERR;
        if (!pixels) ERRMSG("Could not acquire pixels.");
        /* Copy over pixels into output array */
        for (n=0; n < size; n++) {
            pix = pixels[n];
            *arrptr++ = pix.red;
            *arrptr++ = pix.green;
            *arrptr++ = pix.blue;
            if (ld == 4) *arrptr++ = pix.opacity;
        }
    } 
    return arrobj;    
    
 fail:
    Py_XDECREF(arrobj);
    return NULL; 
}


static char doc_setpixels_image[] = \
"img.setpixels(data<,x,y,z>)\n\n"\
" Set raw data into the image at offset x,y, z\n"\
" Default to 0,0,0.  The array shape determines how many pixels to set";
static PyObject*
setpixels_image(PyObject *self, PyObject *args)
{
    Image *im, *image;
    PixelPacket *pixels;
    PixelPacket *pix;
    long x=0,y=0, cols, rows, z=0, imgs=1;
    long size, n;
    long num;
    PyObject *obj, *arrobj=NULL, *tmp=NULL;
    int nd, ld;
    Quantum* arrptr;
    
    im = ASIM(self)->ims;
    if (!PyArg_ParseTuple(args, "O|lll", &obj, &x, &y, &z))
        return NULL;
    
    num = GetImageListLength(im);
    if (num > 1) nd = 4;
    else nd = 3;
    arrobj = PyArray_ContiguousFromObject(obj, PyArray_NOTYPE,nd,nd);
    if (arrobj == NULL) return NULL;
    tmp = PyArray_Cast((PyArrayObject *)arrobj, ptype);
    Py_DECREF(arrobj);
    arrobj = tmp;
    if (arrobj==NULL) return NULL;

    cols = DIM(arrobj,nd-2);
    rows = DIM(arrobj,nd-3);
    imgs = (nd==3) ? 1 : DIM(arrobj,0);

    if ((x+cols > im->columns) || (y+rows > im->rows) || \
        (cols < 0) || (rows < 0)) {
        PyErr_Format(PyMagickError,"goemetry (%lux%lu%+ld%+ld) exceeds image bounds",
                     cols, rows, x, y);
        return NULL;
    }
    if ((z < 0) || (z+imgs > num) || (imgs <= 0)) {
        PyErr_Format(PyMagickError,"z = %ld and imgs=%ld not valid", num, imgs);
        return NULL;
    }
    
    ld = DIM(arrobj,nd-1);
    arrptr = (Quantum *) DATA(arrobj);
    size = rows*cols;
    for (image = im; image; image=image->next) {
        if (ld == 4) SetImageType(image, TrueColorMatteType);
        else SetImageType(image, TrueColorType);
        pixels = SetImagePixels(image, x, y, cols, rows);
        if (!pixels) ERRMSG("Could not acquire pixels.");
        /* Copy over pixels from array */
        for (n=0; n < size; n++) {
            pix = pixels++;
            pix->red = *arrptr++;
            pix->green = *arrptr++;
            pix->blue = *arrptr++;
            if (ld == 4) pix->opacity = *arrptr++;
        }
        if (!SyncImagePixels(image)) ERRMSG("Could not sync image pixels.");
    }
    Py_DECREF(arrobj);
    Py_INCREF(Py_None);
    return Py_None;
    
 fail:
    Py_XDECREF(arrobj);
    return NULL; 
}


static char doc_getindexes_image[] = \
"img.getindexes(x,y,cols,rows<,z,imgs>)\n\n"\
" Gets a rectangle of raw pixel data from image.\n"\
" If not given z defaults to zero and imgs defaults to 1.";
static PyObject *
getindexes_image(PyObject *self, PyObject *args)
{
    Image *im, *image;
    IndexPacket *indexes;
    PixelPacket *pixels;
    long x,y, cols, rows, z=0, imgs=1;
    long size, n;
    long num;
    PyObject *arrobj=NULL;
    int nd, dims[3];
    Quantum* arrptr;
    
    im = ASIM(self)->ims;
    if (im->storage_class != PseudoClass)
        ERRMSG("getindexes only works with PseudoClass arrays.");

    if (!PyArg_ParseTuple(args, "llll|ll", &x, &y, &cols, &rows, &z,
                          &imgs)) return NULL;
    
    if ((x+cols > im->columns) || (y+rows > im->rows) || \
        (cols < 0) || (rows < 0)) {
        PyErr_Format(PyMagickError,"goemetry (%lux%lu%+ld%+ld) exceeds image bounds",
                     cols, rows, x, y);
        return NULL;
    }
    num = GetImageListLength(im);
    if ((z < 0) || (z+imgs > num) || (imgs < 0)) {
        PyErr_Format(PyMagickError,"z = %ld and imgs=%ld not valid", num, imgs);
        return NULL;
    }
    
    /* Create Array to hold pixels in */
    if (imgs > 1) { 
        dims[0] = imgs;
        dims[1] = rows;
        dims[2] = cols;
        nd = 3;
    }
    else {
        nd = 2; dims[0] = rows; dims[1] = cols;
        dims[2] = 1;
    }
    arrobj = PyArray_FromDims(nd,dims,ptype);
    if (arrobj == NULL) return NULL;
    arrptr = (Quantum *) DATA(arrobj);
    size = rows*cols;
    for (image = im; image; image=image->next) {
        pixels = (PixelPacket *)AcquireImagePixels(image, x, y, cols, rows, 
                                                   &exception);
        CHECK_ERR;
        if (!pixels) ERRMSG("Could not acquire pixels.");
        indexes = GetIndexes(image);
        /* Copy over pixels index values into output array */
        for (n=0; n < size; n++) {
            *arrptr++ = indexes[n];
        }
    } 
    return arrobj;
    
 fail:
    Py_XDECREF(arrobj);
    return NULL; 
}


static char doc_setindexes_image[] = \
"img.setpixels(data<,x,y,z>)\n\n"\
" Set raw data into the image at offset x,y,z\n"\
" Default to 0,0,0.  The array shape determines how many pixels to set";
static PyObject*
setindexes_image(PyObject *self, PyObject *args)
{
    Image *im, *image;
    IndexPacket *indexes;
    PixelPacket *pixels;
    long x=0,y=0, cols, rows, z=0, imgs=1;
    long size, n;
    long num;
    PyObject *obj, *arrobj=NULL, *tmp=NULL;
    int nd;
    Quantum* arrptr;
    
    im = ASIM(self)->ims;
    if (im->storage_class != PseudoClass)
        ERRMSG("getindexes only works with PseudoClass arrays.");

    if (!PyArg_ParseTuple(args, "O|lll", &obj, &x, &y, &z))
        return NULL;
    
    num = GetImageListLength(im);
    if (num > 1) nd = 3;
    else nd = 2;
    arrobj = PyArray_ContiguousFromObject(obj, PyArray_NOTYPE,nd,nd);
    if (arrobj == NULL) return NULL;
    tmp = PyArray_Cast((PyArrayObject *)arrobj, ptype);
    Py_DECREF(arrobj);
    arrobj = tmp;
    if (arrobj==NULL) return NULL;

    cols = DIM(arrobj,nd-1);
    rows = DIM(arrobj,nd-2);
    imgs = (nd==2) ? 1 : DIM(arrobj,0);

    if ((x+cols > im->columns) || (y+rows > im->rows) || \
        (cols < 0) || (rows < 0)) {
        PyErr_Format(PyMagickError,"goemetry (%lux%lu%+ld%+ld) exceeds image bounds",
                     cols, rows, x, y);
        return NULL;
    }
    if ((z < 0) || (z+imgs > num) || (imgs <= 0)) {
        PyErr_Format(PyMagickError,"z = %ld and imgs=%ld not valid", num, imgs);
        return NULL;
    }
    
    arrptr = (Quantum *) DATA(arrobj);
    size = rows*cols;
    for (image = im; image; image=image->next) {
        pixels = SetImagePixels(image, x, y, cols, rows);
        if (!pixels) ERRMSG("Could not acquire pixels.");
        /* Copy over pixels from array */
        indexes = GetIndexes(image);
        if (!indexes) ERRMSG("Could not get indexes.")
        for (n=0; n < size; n++) {
            indexes[n] = *arrptr++;
        }
        if (!SyncImagePixels(image)) ERRMSG("Could not sync image pixels.");
    }
    Py_DECREF(arrobj);
    Py_INCREF(Py_None);
    return Py_None;
    
 fail:
    Py_XDECREF(arrobj);
    return NULL; 
}




static PyMethodDef image_methods[] = {
    {"write",  (PyCFunction)write_image, METH_VARARGS|METH_KEYWORDS, 
     doc_write_image},
    {"display",  (PyCFunction)display_image, METH_VARARGS|METH_KEYWORDS, 
     doc_display_image},
    {"animate",  (PyCFunction)animate_image, METH_VARARGS|METH_KEYWORDS, 
     doc_animate_image},
    {"quantize",  (PyCFunction)quantize_image, METH_VARARGS|METH_KEYWORDS, 
     doc_quantize_image},
    {"segment",  (PyCFunction)segment_image, METH_VARARGS|METH_KEYWORDS, 
     doc_segment_image},
    {"compresscolormap", (PyCFunction)compresscolormap_image, METH_NOARGS, 
     doc_compresscolormap_image},
    {"copy", (PyCFunction)copy_image, METH_NOARGS, doc_copy_image},
    {"ordered_dither", (PyCFunction)ordereddither_image, METH_NOARGS, 
     doc_ordereddither_image},
    {"set", (PyCFunction)set_image, METH_VARARGS, doc_set_image},    
    {"describe", (PyCFunction)describe_image, METH_VARARGS, doc_describe_image},  
    {"diff", (PyCFunction)diff_image, METH_VARARGS, doc_diff_image},    
    {"map", (PyCFunction)map_image, METH_VARARGS, doc_map_image},    
    {"channel", (PyCFunction)channel_image, METH_VARARGS, doc_channel_image},    
    {"cyclecolor", (PyCFunction)cyclecolor_image, METH_VARARGS, 
     doc_cyclecolor_image},
    {"setopacity", (PyCFunction)setopacity_image, METH_VARARGS, 
     doc_setopacity_image},
    {"plasma", (PyCFunction)plasma_image, METH_VARARGS, 
     doc_plasma_image},
    
/* Why was "clip" commented out? */
    {"clip", (PyCFunction)clip_image, METH_NOARGS, doc_clip_image},

    {"toarray", (PyCFunction)toarray_image, METH_VARARGS, doc_toarray_image},
    {"contrast", (PyCFunction)contrast_image, METH_VARARGS, doc_contrast_image},
    {"equalize", (PyCFunction)equalize_image, METH_NOARGS, doc_equalize_image},
    {"gamma", (PyCFunction)gamma_image, METH_VARARGS, doc_gamma_image},
    {"level", (PyCFunction)level_image, METH_VARARGS, doc_level_image},
    {"levelchannel", (PyCFunction)levelchannel_image, METH_VARARGS, 
     doc_levelchannel_image},
    {"modulate", (PyCFunction)modulate_image, METH_VARARGS, doc_modulate_image},
    {"negate", (PyCFunction)negate_image, METH_VARARGS, doc_negate_image},
    {"normalize", (PyCFunction)normalize_image, METH_NOARGS, 
     doc_normalize_image},
    {"threshold", (PyCFunction)threshold_image, METH_VARARGS, 
     doc_threshold_image},
    {"solarize", (PyCFunction)solarize_image, METH_O, doc_solarize_image},
    {"raise_", (PyCFunction)raise_image, METH_VARARGS, doc_raise_image},
    {"draw", (PyCFunction)draw_image, METH_O,
     doc_draw_image},
    {"clip_path", (PyCFunction)clip_path_image, METH_O,
     doc_clip_path_image}, 
    {"annotate", (PyCFunction)annotate_image, METH_VARARGS,
     doc_annotate_image},
    {"get_type_metrics", (PyCFunction)gettypemetrics_image, METH_VARARGS,
     doc_gettypemetrics_image},
    {"colorfloodfill",  (PyCFunction)colorfloodfill_image, 
     METH_VARARGS|METH_KEYWORDS,  doc_colorfloodfill_image},
    {"mattefloodfill",  (PyCFunction)mattefloodfill_image, 
     METH_VARARGS|METH_KEYWORDS,  doc_mattefloodfill_image},
    {"opaque",  (PyCFunction)opaque_image, 
     METH_VARARGS|METH_KEYWORDS,  doc_opaque_image},
    {"transparent",  (PyCFunction)transparent_image, 
     METH_VARARGS|METH_KEYWORDS,  doc_transparent_image},
    {"drawaffine", (PyCFunction)drawaffine_image, METH_VARARGS, 
     doc_drawaffine_image},
    {"composite", (PyCFunction)composite_image, METH_VARARGS, 
     doc_composite_image},
    {"index", (PyCFunction)index_image, METH_VARARGS,
     doc_index_image},
    {"pixel", (PyCFunction)pixelcolor_image, METH_VARARGS, 
     doc_pixelcolor_image},
    {"getpixels", (PyCFunction)getpixels_image, METH_VARARGS, 
     doc_getpixels_image},
    {"setpixels", (PyCFunction)setpixels_image, METH_VARARGS, 
     doc_setpixels_image},
    {"getindexes", (PyCFunction)getindexes_image, METH_VARARGS, 
     doc_getindexes_image},
    {"setindexes", (PyCFunction)setindexes_image, METH_VARARGS, 
     doc_setindexes_image},
    {NULL, NULL, 0, NULL}    /* sentinel */
};


static PyObject *
mimageattr_get(Image *im, char *name)
{
    PyObject *obj;
    int j;
    long num;

    switch(*name) {
    case 'b':
      if (strcmp(name, "background")==0) {
          if ((obj=PyTuple_New(4))==NULL) return NULL;
          PyTuple_SET_ITEM(obj,0,PyInt_FromLong(im->background_color.red));
          PyTuple_SET_ITEM(obj,1,PyInt_FromLong(im->background_color.green));
          PyTuple_SET_ITEM(obj,2,PyInt_FromLong(im->background_color.blue));
          PyTuple_SET_ITEM(obj,3,PyInt_FromLong(im->background_color.opacity));
          return obj;
        }
        else if (strcmp(name, "bordercolor")==0) {
            if ((obj=PyTuple_New(4))==NULL) return NULL;
            PyTuple_SET_ITEM(obj,0,PyInt_FromLong(im->border_color.red));
            PyTuple_SET_ITEM(obj,1,PyInt_FromLong(im->border_color.green));
            PyTuple_SET_ITEM(obj,2,PyInt_FromLong(im->border_color.blue));
            PyTuple_SET_ITEM(obj,3,PyInt_FromLong(im->border_color.opacity));
            return obj;
        }
        else if (strcmp(name, "blue_primary")==0) {
            if ((obj=PyTuple_New(2))==NULL) return NULL;
            PyTuple_SET_ITEM(obj,0,
                             PyFloat_FromDouble(im->chromaticity.\
                                                blue_primary.x));
            PyTuple_SET_ITEM(obj,1,
                             PyFloat_FromDouble(im->chromaticity.\
                                            blue_primary.y));
            return obj;
        }
        break;
    case 'c':
        if (strcmp(name, "class_") == 0) {
            j = im->storage_class;
            if ((j >=0) && (j < (long) NumberOf(ClassTypes)-1))
                return PyString_FromString(ClassTypes[j]);
            PyErr_Format(PyMagickError,"Undefined ClassType (%d) ", j);
            return NULL;            
        }
    else if (strcmp(name, "clip_mask")==0) {
            if (im->clip_mask == (Image *)NULL)
                ClipImage(im);
            obj = (PyObject *)PyObject_New(PyMImageObject, &MImage_Type);
            if (obj == NULL) return NULL;
            ((PyMImageObject *)obj)->ims = \
                im->clip_mask ? CloneImage(im->clip_mask,0,0,True,&exception) : NULL;
            CHECK_ERR;
            return obj;
        }
        else if (strcmp(name, "colormap") == 0) {
            return get_colormap(im);
        }
        else if (strcmp(name, "colors") == 0) {
            num = GetNumberColors(im,(FILE *)NULL,&(im->exception));
            if PyMagickErr(im->exception) ERR(im->exception);
            return PyInt_FromLong(num);
        }
        else if (strcmp(name, "colorspace") == 0) {
            j = im->colorspace;
            if ((j >=0) && (j < (long) NumberOf(ColorspaceTypes)-1))
                return PyString_FromString(ColorspaceTypes[j]);
            PyErr_Format(PyMagickError,"Undefined colorspace (%d) ", j);
            return NULL;
        }
        else if (strcmp(name, "columns") == 0) {
            return PyInt_FromLong(im->columns);
        }
        else if (strcmp(name, "compression") == 0) {
            j = im->colorspace;
            if ((j >=0) && (j < (long) NumberOf(CompressionTypes)-1))
                return PyString_FromString(CompressionTypes[j]);
            PyErr_Format(PyMagickError,"Undefined CompressionType (%d) ", j);
            return NULL;            
        }
        else if (strcmp(name, "compose") == 0) {
            j = im->compose;
            if ((j >=0) && (j < (long) NumberOf(CompositeTypes)-1))
                return PyString_FromString(CompositeTypes[j]);
            return NULL;
        }
        break;
    case 'd':
        if (strcmp(name, "delay")==0) {
            return PyInt_FromLong(im->delay);
        }
        else if (strcmp(name, "depth") == 0) {
            num = GetImageDepth(im, &(im->exception));
            if PyMagickErr(im->exception) ERR(im->exception);
            return PyInt_FromLong(num);                
        }
        else if (strcmp(name, "density") == 0) {
            if ((obj=PyTuple_New(2))==NULL) return NULL;
            PyTuple_SET_ITEM(obj,0,PyFloat_FromDouble(im->x_resolution));
            PyTuple_SET_ITEM(obj,1,PyFloat_FromDouble(im->y_resolution));
            return obj;
        }
        else if (strcmp(name, "directory") == 0) {
            if (im->directory)
                return PyString_FromString(im->directory);
            else
                return PyString_FromString("");
        }
        break;
    case 'e':
        if (strcmp(name, "endian")==0) {
            j = im->endian;
            if ((j >=0) && (j < (long) NumberOf(EndianTypes)-1))
                return PyString_FromString(EndianTypes[j]);
            PyErr_Format(PyMagickError,"Undefined EndianType (%d) ", j);
            return NULL;            
        }
        else if (strcmp(name, "error") == 0) {
            return PyFloat_FromDouble(im->error.mean_error_per_pixel);
        }
        break;
    case 'f':
        if (strcmp(name, "filename")==0) {
            if (im->filename)
                return PyString_FromString(im->filename);
            else
                return PyString_FromString("");
        }
        else if (strcmp(name, "filesize") == 0) {
            return PyInt_FromLong(GetBlobSize(im));
        }
        else if (strcmp(name, "filter") == 0) {
            j = im->filter;
            if ((j >=0) && (j < (long) NumberOf(FilterTypess)-1))
                return PyString_FromString(FilterTypess[j]);
            PyErr_Format(PyMagickError,"Undefined FilterType (%d) ", j);
            return NULL;            
        }
        else if (strcmp(name, "format") == 0) {
            const MagickInfo *minfo;
            if (*(im->magick)) {
                minfo = GetMagickInfo(im->magick, &exception);
                CHECK_ERR;
                return minfo ? PyString_FromString(minfo->name) \
                    : PyString_FromString("");
            }
            return NULL;
        }
        else if (strcmp(name, "fuzz") == 0) {
            return PyFloat_FromDouble(im->fuzz);
        }
        break;
    case 'g':
        if (strcmp(name, "green_primary")==0) {
            if ((obj=PyTuple_New(2))==NULL) return NULL;
            PyTuple_SET_ITEM(obj,0,
                             PyFloat_FromDouble(im->chromaticity.\
                                                green_primary.x));
            PyTuple_SET_ITEM(obj,1,
                             PyFloat_FromDouble(im->chromaticity.\
                                            green_primary.y));
            return obj;            
        }
        else if (strcmp(name, "gamma") == 0) {
            return PyFloat_FromDouble(im->gamma);
        }
        else if (strcmp(name, "geometry") == 0) {
            if (im->geometry)
                return PyString_FromString(im->geometry);
            else
                return PyString_FromString("");
        }
        else if (strcmp(name, "gravity") == 0) {
            j = im->gravity;
            if ((j >=0) && (j < (long) NumberOf(GravityTypes)-1))
                return PyString_FromString(GravityTypes[j]);
            PyErr_Format(PyMagickError,"Undefined GravityType (%d) ", j);
            return NULL;            
        }
        break;
    case 'h':
        if (strcmp(name, "height") == 0) {
            return PyInt_FromLong(im->rows);
        }
        break;
#if 0
/*
  ImageMagick indicates ProfileInfo as deprecated,
  GraphicsMagick's struct _Image does not have the profiles anymore
*/
    case 'i':
        if (strcmp(name, "icm") == 0) {
            return PyString_FromStringAndSize(im->color_profile.info, 
                                              im->color_profile.length);
        }
        if (strcmp(name, "interlace")==0) {
            j = im->interlace;
            if ((j >=0) && (j < (long) NumberOf(InterlaceTypes)-1))
                return PyString_FromString(InterlaceTypes[j]);
            PyErr_Format(PyMagickError,"Undefined InterlaceType (%d) ", j);
            return NULL;
        }
        /* else if (strcmp(name, "index") == 0) { 
        } 
        else if (strcmp(name, "id") == 0) {
            num = SetMagickRegistry(ImageRegistryType, im, 0,
                                    &exception);
            CHECK_ERR;
            return PyInt_FromLong(num);
        }
        */
        else if (strcmp(name, "iptc") == 0) {
            return PyString_FromStringAndSize(im->iptc_profile.info,
                                              im->iptc_profile.length);
        }
        else if (strcmp(name, "iterations") == 0) {
            return PyInt_FromLong(im->iterations);
        }
        else if (strcmp(name, "ismono")==0) {
            j=IsMonochromeImage(im, &exception);
            CHECK_ERR;
            return PyInt_FromLong(j);                       
        }
        if (strcmp(name, "isopaque")==0) {
            j=IsOpaqueImage(im, &exception);
            CHECK_ERR;
            return PyInt_FromLong(j);
        }
        else if (strcmp(name, "isgray") == 0) {
            j=IsGrayImage(im, &exception);
            CHECK_ERR;
            return PyInt_FromLong(j);
        }
        else if (strcmp(name, "ispalette")==0) {
            j=IsPaletteImage(im, &exception);
            CHECK_ERR;
            return PyInt_FromLong(j);            
        }
        break;
#endif
    case 'l':
        if (strcmp(name, "loop")==0) {
            return PyInt_FromLong(im->iterations);            
        }        
        else if (strcmp(name, "label") == 0) {
            const ImageAttribute *attribute;
            
            attribute = GetImageAttribute(im,"label");
            if (attribute != (ImageAttribute *)NULL)
                return PyString_FromString(attribute->value);
            else
                return PyString_FromString("");
        }
    case 'm':
        if (strcmp(name, "magick")==0) {
            if (im->magick) 
                return PyString_FromString(im->magick);
            else
                return PyString_FromString("");
                   
        }
        else if (strcmp(name, "max_error")==0) {
            return PyFloat_FromDouble(im->error.\
                                      normalized_maximum_error);
            
        }
        else if (strcmp(name, "mean_error") == 0) {
            return PyFloat_FromDouble(im->error.\
                                      normalized_mean_error);
        }
        else if (strcmp(name, "matte")==0) {
            return PyInt_FromLong(im->matte);
        }
        else if (strcmp(name, "mattecolor") == 0) {
            if ((obj=PyTuple_New(4))==NULL) return NULL;
            PyTuple_SET_ITEM(obj,0,PyInt_FromLong(im->matte_color.red));
            PyTuple_SET_ITEM(obj,1,PyInt_FromLong(im->matte_color.green));
            PyTuple_SET_ITEM(obj,2,PyInt_FromLong(im->matte_color.blue));
            PyTuple_SET_ITEM(obj,3,PyInt_FromLong(im->matte_color.opacity));
            return obj;
        }
        else if (strcmp(name, "montage")==0) {
            if (im->montage) 
                return PyString_FromString(im->montage);
            else
                return PyString_FromString("");
        }
    case 'p':
        if (strcmp(name, "page")==0) {
            char
                geometry[MaxTextExtent];
            
            FormatString(geometry,"%ux%u%+d%+d",(unsigned int) im->page.width,
                      (unsigned int) im->page.height,
              (int) im->page.x, (int) im->page.y);
            return PyString_FromString(geometry);                
        }
        /* else if (strcmp(name, "pixel")==0) {
        }
        */
        break;
    case 'r':
        if (strcmp(name, "red_primary")==0) {
            if ((obj=PyTuple_New(2))==NULL) return NULL;
            PyTuple_SET_ITEM(obj,0,
                             PyFloat_FromDouble(im->chromaticity.\
                                                red_primary.x));
            PyTuple_SET_ITEM(obj,1,
                             PyFloat_FromDouble(im->chromaticity.\
                                                red_primary.y));
            return obj;
        }
        else if (strcmp(name, "render")==0) {
            j = im->rendering_intent;
            if ((j >=0) && (j < (long) NumberOf(IntentTypes)-1))
                return PyString_FromString(IntentTypes[j]);
            PyErr_Format(PyMagickError,"Undefined InterlaceType (%d) ", j);
            return NULL;           
        }
        else if (strcmp(name, "rows")==0) {
            return PyInt_FromLong(im->rows);
        }
        break;
    case 's':
        if (strcmp(name, "scene")==0) {
            return PyInt_FromLong(im->scene);
        }
    if (strcmp(name, "shape")==0) {
            if ((obj=PyTuple_New(2))==NULL) return NULL;
            PyTuple_SET_ITEM(obj,0,PyInt_FromLong(im->rows));
            PyTuple_SET_ITEM(obj,1,PyInt_FromLong(im->columns));
            return obj;     
    }
        else if (strcmp(name, "signature")==0) {
            const ImageAttribute *attribute;
            
            attribute = GetImageAttribute(im,"signature");
            if (attribute != (ImageAttribute *)NULL)
                return PyString_FromString(attribute->value);
            else
                return PyString_FromString("");            
        }
        else if (strcmp(name, "storage_class") == 0) {
            j = im->storage_class;
            if ((j >=0) && (j < (long) NumberOf(ClassTypes)-1))
                return PyString_FromString(ClassTypes[j]);
            PyErr_Format(PyMagickError,"Undefined ClassType (%d) ", j);
            return NULL;            
        }
        break;
    case 't':
        if (strcmp(name, "taint")==0) {
            return PyInt_FromLong((long) IsTaintImage(im));
        }
        else if (strcmp(name, "type")==0) {
            j = GetImageType(im, &exception);
            CHECK_ERR;
            if ((j >=0) && (j < (long) NumberOf(ImageTypes)-1))
                return PyString_FromString(ImageTypes[j]);
            PyErr_Format(PyMagickError,"Undefined ImageType (%d) ", j);
            return NULL;
        }
        break;
    case 'u':
        if (strcmp(name, "units")==0) {
            j = im->units;
            if ((j >=0) && (j < (long) NumberOf(ResolutionTypes)-1))
                return PyString_FromString(ResolutionTypes[j]);
            PyErr_Format(PyMagickError,"Undefined Units (%d) ", j);
            return NULL;
        }
        break;
    case 'v':
        if (strcmp(name, "virtual_pixel")==0) {
            j = GetImageVirtualPixelMethod(im);
            if ((j >=0) && (j < (long) NumberOf(VirtualPixelMethods)-1))
                return PyString_FromString(VirtualPixelMethods[j]);
            PyErr_Format(PyMagickError,"Undefined virtual-pixel method (%d) ", j);
            return NULL;
        }
        break;
    case 'w':
        if (strcmp(name, "white_point")==0) {
            if ((obj=PyTuple_New(2))==NULL) return NULL;
            PyTuple_SET_ITEM(obj,0,
                             PyFloat_FromDouble(im->chromaticity.\
                                                white_point.x));
            PyTuple_SET_ITEM(obj,1,
                             PyFloat_FromDouble(im->chromaticity.\
                                            white_point.y));
            return obj;            
        }
        else if (strcmp(name, "width")==0) {
            return PyInt_FromLong(im->columns);
        }
        break;
    case 'x':
        if (strcmp(name, "x_res")==0) {
            return PyFloat_FromDouble(im->x_resolution);
        }
        break;
    case 'y':
        if (strcmp(name, "y_res")==0) {
            return PyFloat_FromDouble(im->y_resolution);
        }
        break;
    default:
        goto fail2;
    }

 fail2:
    PyErr_SetString(PyExc_AttributeError, name);
    return NULL; 
    
 fail:
    return NULL;

}

static PyObject *
mimage_getattr(PyMImageObject *obj, char *name)
{
    PyObject *methobj;
    methobj = Py_FindMethod(image_methods, (PyObject *)obj, name);
    if (methobj != NULL) return methobj;
    /* no method found.  Let's look for attributes. */
    if (PyErr_Occurred()) PyErr_Clear();  
                   /* Get rid of any error from FindMethod */

    if (!obj->ims) {
        PyErr_SetString(PyMagickError, "Null image.");
        return NULL;
    }
    return mimageattr_get(obj->ims, name);  /* Look up Image attribute in image structure */
}



/******************************************
 * 
 * 
 *        Set Attribute 
 *
 *
 ******************************************/

static int
mimage_setattr(PyMImageObject *obj, char *name, PyObject *v)
{
    PyMImageObject *img=NULL;
    Image *im, *image;
    double x,y;
    int j;
    long num;
    char *tmpstr;
    PixelPacket p;

    if (v == NULL) ERRMSG("Cannot delete MImage attributes.");

    im = obj->ims;
    if (!im) ERRMSG("Null image.");

    switch(*name) {
    case 'b':
        if (strcmp(name, "background")==0) {
            if (!set_color_from_obj(&p,v,name))
                goto fail;
            for (image=im; image; image=image->next) {
                image->background_color = p;
            }
            return 0;
        }
        else if (strcmp(name, "bordercolor")==0) {
            if (!set_color_from_obj(&p,v,name))
                goto fail;
            for (image=im; image; image=image->next) {
                image->border_color = p;
            }
      return 0;
      }
        else if (strcmp(name, "blue_primary")==0) {
            y = x = PyFloat_AsDouble(v);
            if ((x==-1) && PyErr_Occurred()) { 
                PyErr_Clear();
                if (!PyArg_ParseTuple(v,"dd",&x,&y)) return -1;
            }
            for (image=im; image; image=image->next) {
                image->chromaticity.blue_primary.x = x;
                image->chromaticity.blue_primary.y = y;
            }
            return 0;
        }
        break;
    case 'c':
        if (strcmp(name, "clip_mask")==0) {
        img=(PyMImageObject *)mimage_from_object(v);
        if (img==NULL) return -1;
        for ( image=im; image ; image=image->next) {
        SetImageClipMask(image, img->ims);
                CHECK_ERR_IM(image);
            }
        Py_DECREF(img);
        return 0;       
        }
    else if (strcmp(name, "colormap") == 0) {
        PyObject *arr, *tmp;
        char *str;
        int nd, lastdim, ctype;
        char *cmap;
        int colors, k;
        
        arr = PyArray_ContiguousFromObject(v,PyArray_LONG,2,3);
        if (arr==NULL) return -1; 
        tmp = PyArray_Cast((PyArrayObject *)arr,ptype); 
        Py_DECREF(arr); 
        if (tmp==NULL) return -1; 
        nd = RANK(tmp);
        /* At some point, maybe use QuantizeImage(s) to set the number
            of colors in the image
        */
        colors = DIM(tmp,nd-2);
        if (colors > MaxColormapSize)
        ERRMSG("Too many colors requested in colormap.");

        lastdim = DIM(tmp,nd-1);
        if ((lastdim < 3) || (lastdim > 4))
        ERRMSG("Colormap must have last dimension of size 3 or 4.");
        str = "RGB";
        if ((lastdim == 4) && (im->matte)) str="RGBA";
        if ((lastdim == 4) && !(im->matte)) str="CMYK";
        ctype = arraytype_to_storagetype(ptype);
        cmap = DATA(tmp);
        k = 0;
        for( image=im; image; image=image->next) {
        if (image->storage_class == DirectClass) continue;
        if (!ConstitutePaletteColormap(image, str, ctype, 
                           cmap, colors)) {
            Py_DECREF(tmp); return -1;
        }
        if (nd == 3) {
            cmap += STRIDE(tmp,0);
            if (k >= DIM(tmp,0)) {
            k = 0;
            cmap = DATA(tmp);
            }
            k++;
        }
        }
        Py_DECREF(tmp); 
        return 0;
    } 
        else if (strcmp(name, "colorspace") == 0) {
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            if ((j = LookupStr(ColorspaceTypes, tmpstr)) < 0)
                ERRMSG3(tmpstr,name);
            for (image=im ; image; image=image->next) {
                RGBTransformImage(image, (ColorspaceType) j);
        CHECK_ERR_IM(image);
        }
            return 0;
        }
        else if (strcmp(name, "compression") == 0) {
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            if ((j = LookupStr(CompressionTypes, tmpstr)) < 0)
                ERRMSG3(tmpstr,name);
            for (image=im ; image; image=image->next)
                image->compression = (CompressionType) j;
            return 0;
        }
        else if (strcmp(name, "compose") == 0) {
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            if ((j = LookupStr(CompositeTypes, tmpstr)) < 0)
                ERRMSG3(tmpstr,name);
            for (image=im ; image; image=image->next)
                image->compose = (CompositeOperator) j;
            return 0;
        }
        break;
    case 'd':
        if (strcmp(name, "delay")==0) {
            num = PyInt_AsLong(v);
            if ((num == -1) && (PyErr_Occurred())) return -1;
            for (image=im; image; image=image->next)
                image->delay = num;
            return 0;
        }
        else if (strcmp(name, "depth") == 0) {
            num = PyInt_AsLong(v);
            if ((num == -1) && (PyErr_Occurred())) return -1;
            for (image=im; image; image=image->next)
                SetImageDepth(image, num);
            return 0;            
        }
        else if (strcmp(name, "density") == 0) {
            y = x = PyFloat_AsDouble(v);
            if ((x==-1) && PyErr_Occurred()) { 
                PyErr_Clear();
                if (!PyArg_ParseTuple(v,"dd",&x,&y)) return -1;
            }
            for (image=im; image; image=image->next) {
                image->x_resolution = x;
                image->y_resolution = y;
            }
            return 0;
        }
    else if (strcmp(name, "dispose") == 0) {
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            if ((j = LookupStr(DisposeTypes, tmpstr)) < 0)
                ERRMSG3(tmpstr,name);
            for (image=im ; image; image=image->next)
                image->dispose = (DisposeType) j;
            return 0;
    }
        break;
    case 'e':
        if (strcmp(name, "endian")==0) {
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            if ((j = LookupStr(EndianTypes, tmpstr)) < 0)
                ERRMSG3(tmpstr,name);
            for (image=im ; image; image=image->next)
                image->endian = (EndianType) j;
            return 0;
        }
        break;
    case 'f':
        if (strcmp(name, "filename")==0) {
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            for (image=im; image; image=image->next) {
                (void) strncpy(image->filename, tmpstr, MaxTextExtent-1);
            }
            return 0;
        }
        else if (strcmp(name, "fuzz") == 0) {
            x = PyFloat_AsDouble(v);
            if ((x==-1) && PyErr_Occurred()) return -1;
            for (image=im; image; image=image->next) 
                image->fuzz = x;
            return 0;
        }
        break;
    case 'g':
        if (strcmp(name, "green_primary")==0) {
          y = x = PyFloat_AsDouble(v);
          if ((x==-1) && PyErr_Occurred()) { 
              PyErr_Clear();
              if (!PyArg_ParseTuple(v,"dd",&x,&y)) return -1;
          }
          for (image=im; image; image=image->next) {
              image->chromaticity.green_primary.x = x;
              image->chromaticity.green_primary.y = y;
          }
      return 0;
        }
        else if (strcmp(name, "gamma") == 0) {
            x = PyFloat_AsDouble(v);
            if ((x==-1) && PyErr_Occurred()) return -1;
            for (image=im; image; image=image->next) 
                image->gamma = x;
            return 0;
        }
        else if (strcmp(name, "gravity") == 0) {
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            if ((j = LookupStr(GravityTypes, tmpstr)) < 0)
                ERRMSG3(tmpstr,name);
            for (image=im ; image; image=image->next)
                image->gravity = (GravityType) j;
            return 0;
    }
        break;
    case 'i':
        if (strcmp(name, "interlace")==0) {
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            if ((j = LookupStr(InterlaceTypes, tmpstr)) < 0)
                ERRMSG3(tmpstr,name);
            for (image=im ; image; image=image->next)
                image->interlace = (InterlaceType) j;
            return 0;
        }
        /* else if (strcmp(name, "index") == 0) { 
        }
        */
        else if (strcmp(name, "iterations") == 0) {
        iterations:
            num = PyInt_AsLong(v);
            if ((num==-1) && PyErr_Occurred()) return -1;
            for (image=im; image; image=image->next) 
                image->iterations=num;
            return 0;
        }
        break;
    case 'l':
        if (strcmp(name, "loop")==0) {
            goto iterations;
        }        
        /*        else if (strcmp(name, "label") == 0) {
            
        }*/
        break;
    case 'm':
        /* if (strcmp(name, "magick")==0) {
           } */
        if (strcmp(name, "matte")==0) {
            j = PyObject_IsTrue(v);
            if (PyErr_Occurred()) return -1;
            for (image=im; image; image=image->next)
                image->matte = j;
            return 0;            
        }
        else if (strcmp(name, "mattecolor") == 0) {
      if (!set_color_from_obj(&p,v,name))
          goto fail;
          for (image=im; image; image=image->next) {
              image->matte_color = p;
          }
      return 0;  
        }
        break;
    case 'o':
        if (strcmp(name, "opacity")==0) {
            num = PyInt_AsLong(v);
            for (image=im; image; image=image->next)
                (void) SetImageOpacity(image, num);
            return 0;
        }
        break;
    #if 0
    imageobject.c: warning: implicit declaration of function 'ParsePageGeometry'
    What happened to the function?
    From ImageMagick source code:
    
    %  ParsePageGeometry() returns a region as defined by the geometry string with
    %  respect to the image dimensions.
    %
    %  The format of the ParsePageGeometry method is:
    %
    %      MagickStatusType ParsePageGeometry(const Image *image,
    %        const char *geometry,RectangeInfo *region_info,
    %        ExceptionInfo *exception)
    %
    %  A description of each parameter follows:
    %
    %    o geometry:  The geometry (e.g. 100x100+10+10).
    %
    %    o region_info: the region as defined by the geometry string with
    %      respect to the image and its gravity.
    %
    %    o exception: return any errors or warnings in this structure.
    %
    */
    MagickExport MagickStatusType ParsePageGeometry(const Image *image,
      const char *geometry,RectangleInfo *region_info,ExceptionInfo *exception)
    
    case 'p':
        if (strcmp(name, "page")==0) {
            char *geometry;
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            geometry = GetPageGeometry(tmpstr);
            for (image=im; image; image=image->next)
                (void) ParsePageGeometry(image, geometry, &image->page);
            MagickFree(geometry);
            return 0;
        }
        /* else if (strcmp(name, "pixel")==0) {
        }
        */
        break;
    #endif
    case 'r':
        if (strcmp(name, "red_primary")==0) {
            y = x = PyFloat_AsDouble(v);
            if ((x==-1) && PyErr_Occurred()) { 
                PyErr_Clear();
                if (!PyArg_ParseTuple(v,"dd",&x,&y)) return -1;
            }
            for (image=im; image; image=image->next) {
                image->chromaticity.red_primary.x = x;
                image->chromaticity.red_primary.y = y;
            }
            return 0;
        }
        if (strcmp(name, "render")==0) {
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            if ((j = LookupStr(IntentTypes, tmpstr)) < 0)
                ERRMSG3(tmpstr,name);
            for (image=im ; image; image=image->next)
                image->rendering_intent = (RenderingIntent) j;
            return 0;
        }
        break;
    case 's':
        if (strcmp(name, "scene")==0) {
            num = PyInt_AsLong(v);
            if ((num==-1) && PyErr_Occurred()) return -1;
            for (image=im; image; image=image->next) 
                image->scene=num;
            return 0;
        }
        break;
    case 't':
        if (strcmp(name, "texture")==0) {
            Image *sim=NULL;
        img=(PyMImageObject *)mimage_from_object(v);
        if (img==NULL) return -1;
        for ( image=im; image ; image=image->next, sim=sim->next) {
                if (sim == NULL) sim = img->ims;
        TextureImage(image, sim);
                CHECK_ERR_IM(image);
            }
        Py_DECREF(img);
        return 0;
        }
    else if (strcmp(name, "type")==0) {
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            if ((j = LookupStr(ImageTypes, tmpstr)) < 0)
                ERRMSG3(tmpstr,name);
            for (image=im ; image; image=image->next)
                SetImageType(image, (ImageType) j);
            return 0;
        }
        break;
    case 'u':
        if (strcmp(name, "units")==0) {
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            if ((j = LookupStr(ResolutionTypes, tmpstr)) < 0)
                ERRMSG3(tmpstr,name);
            for (image=im ; image; image=image->next)
                image->units = (ResolutionType) j;
            return 0;
        }
        break;
    case 'v':
        if (strcmp(name, "virtual_pixel")==0) {
            tmpstr = PyString_AsString(v);
            if (tmpstr==NULL) return -1;
            if ((j = LookupStr(VirtualPixelMethods, tmpstr)) < 0)
                ERRMSG3(tmpstr,name);
            for (image=im ; image; image=image->next)
                SetImageVirtualPixelMethod(image, (VirtualPixelMethod) j);
            return 0;
        }
        break;
    case 'w':
        if (strcmp(name, "white_point")==0) {
            y = x = PyFloat_AsDouble(v);
            if ((x==-1) && PyErr_Occurred()) { 
                PyErr_Clear();
                if (!PyArg_ParseTuple(v,"dd",&x,&y)) return -1;
            }
            for (image=im; image; image=image->next) {
                image->chromaticity.white_point.x = x;
                image->chromaticity.white_point.y = y;
            }
            return 0;
        }
        break;
    case 'x':
        if (strcmp(name, "x_res")==0) {
            x = PyFloat_AsDouble(v);
            if ((x==-1) && PyErr_Occurred()) return -1;
            for (image=im; image; image=image->next) 
                image->x_resolution = x;
            return 0;            
        }
        break;
    case 'y':
        if (strcmp(name, "y_res")==0) {
            y = PyFloat_AsDouble(v);
            if ((x==-1) && PyErr_Occurred()) return -1;
            for (image=im; image; image=image->next) 
                image->y_resolution = y;
            return 0;
        }
        break;
    default:
        goto fail2;
    }

 fail2:
    PyErr_SetString(PyExc_AttributeError, name);
    return -1; 
    
 fail:
    Py_XDECREF(img);
    return -1;
}

static PyObject *
mimage_repr(PyMImageObject *self)
{
    int N;
   
    if (!self->ims)
        return PyString_FromString("Null Magick Image");
    N = GetImageListLength(self->ims);
    if (N < 2)
        return PyString_FromFormat("Magick Image: %ldx%ld", 
                                   (long) self->ims->rows, 
                   (long) self->ims->columns);
    else
        return PyString_FromFormat("%d Magick Images: first image size = %ldx%ld", N, (long) self->ims->rows, (long) self->ims->columns);
    
}

/* ********************************************************************
 *
 *
 *
 *
 *              MImage as a Sequence
 * 
 *
 *
 *
 * ********************************************************************
 */


static Py_ssize_t
mimage_length(PyMImageObject *self)
{
    return GetImageListLength(self->ims);
}

static PyObject *
mimage_concat(PyMImageObject *a, PyObject *bb)
{
    PyMImageObject *new=NULL;
    Image *ca=NULL, *cb=NULL;
    
    if (!PyMImage_Check(bb)) {
        PyErr_Format(PyExc_TypeError, 
                     "can only concatenate MImage (not \"%.200s\") to MImage",
                     bb->ob_type->tp_name);
        return NULL;
    }
    
#define b ((PyMImageObject *)bb)
    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    ca = CloneImageList(a->ims, &exception);
    cb = CloneImageList(b->ims, &exception);
    CHECK_ERR;
    
    AppendImageToList(&ca,cb);
    new->ims = ca;
    return (PyObject *)new;    
#undef b

 fail: 
    Py_XDECREF(new);
    if (ca) DestroyImageList(ca);
    if (cb) DestroyImageList(cb);
    return NULL;    
}

static PyObject *
mimage_inplace_concat(PyMImageObject *self, PyObject *bb)
{
    Image *cb=NULL;
    
    if (!PyMImage_Check(bb)) {
        PyErr_Format(PyExc_TypeError, 
                     "can only concatenate MImage (not \"%.200s\") to MImage",
                     bb->ob_type->tp_name);
        return NULL;
    }
    
#define b ((PyMImageObject *)bb)
    cb = CloneImageList(b->ims, &exception);
    CHECK_ERR;
    
    AppendImageToList(&(self->ims),cb);
    Py_INCREF(self);
    return (PyObject *)self;
#undef b

 fail: 
    if (cb) DestroyImageList(cb);
    return NULL;
}

static PyObject *
mimage_repeat(PyMImageObject *a, int n)
{
    int i; 
    PyMImageObject *new=NULL;
    Image *ca=NULL, *cnew=NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    for (i=0; i<n; i++) {
        ca = CloneImageList(a->ims, &exception);
        CHECK_ERR;
        AppendImageToList(&cnew,ca);
    }
    new->ims = cnew;
    return (PyObject *)new;    

 fail: 
    Py_XDECREF(new);
    if (ca) DestroyImageList(ca);
    if (cnew) DestroyImageList(cnew);
    return NULL;
}

static PyObject *
mimage_inplace_repeat(PyMImageObject *a, int n)
{
    Image *ca=NULL;
    int i;

    for (i=0; i<n; i++) {
        ca = CloneImageList(a->ims, &exception);
        CHECK_ERR;
        AppendImageToList(&(a->ims),ca);
    }
    Py_INCREF(a);
    return (PyObject *)a;

 fail: 
    if (ca) DestroyImageList(ca);
    return NULL;
}


static PyObject *
mimage_slice(PyMImageObject *a, int ilow, int ihigh)
{
    int k, N;
    Image *img, *cpy=NULL;
    Image *new=NULL;
    PyMImageObject *obj=NULL;

    N = GetImageListLength(a->ims);
    if (ilow < 0) ilow = 0;
    else if (ilow > N) ilow = N;
    if (ihigh < ilow) ihigh = ilow;
    else if (ihigh > N) ihigh = N;
    
    obj = PyObject_New(PyMImageObject, &MImage_Type);    
    if (obj == NULL) goto fail;

    img = a->ims;
    for (k=0; img != NULL; img=img->next)
        if (k++ >= ilow)
            break;
    k--;
    while( (k < ihigh) && (img != NULL)) {     
        cpy = CloneImage(img,0,0,1,&exception); /* Get current Image */
        CHECK_ERR;
        AppendImageToList(&new, cpy);
        img=img->next;
        k++;
    }    
    obj->ims = new;
    return (PyObject *)obj;

 fail:
    Py_XDECREF(obj);
    if (new) DestroyImageList(new);
    return NULL;    
}

static PyObject *
mimage_item(PyMImageObject *a, int i)
{
    int N;

    N = GetImageListLength(a->ims);
    if ((i < 0) || (i >= N)) {
        PyErr_SetString(PyExc_IndexError, "Invalid index.");
        return NULL;
    }
    return mimage_slice(a, i, i+1);
}


static int
mimage_ass_slice(PyMImageObject *a, int ilow, int ihigh, PyObject *v) 
{
    Image *img, *cpy=NULL;
    Image *a1, *a2, *b1=NULL, *b2, *c1=NULL, *start=NULL;
    ImageInfo *info=NULL;
    PyObject *tmp=NULL;
    
    int N, k, P, split = 0;
    
    if ((v!=NULL) && (!PySequence_Check(v))) ERRMSG("Must use sequence object when assigning to slice");
    
    P = PySequence_Length(v);
    N = GetImageListLength(a->ims);
    if (ilow < 0) ilow = 0;
    else if (ilow > N) ilow = N;
    if (ihigh < ilow) ihigh = ilow;
    else if (ihigh > N) ihigh = N;

    if (ilow == ihigh) {
        /* Do nothing */
        return 0;
    }
    
    if (v == NULL) {
        img = a->ims;
        for (k=0; img != NULL; img=img->next)
            if (k++ >= ilow)
                break;
        k--;
        if (img) start = img->previous;   /* Last entry to keey */
        /* img points to start of slice to delete */
        while( (k < ihigh) && (img != NULL)) {
            cpy = GetNextImageInList(img);
            DestroyImage(img);
            img = cpy;
            k++;
        }    
        if (start) start->next = img;    /* Reconnect */
        if (img) img->previous = start;        
        return 0;
    }

    /* In short, first we split the image list into three lists:
       1) from the beginning to ilow-1   (a1, a2)
       2) from ilow to ihigh-1           (b1, b2)
       3) from ihigh to the end          (c1, c2)

       The new image sequence is 1) + converted sequence + 3) 
    */

    a1 = a->ims;
    img = a1;
    for (k=0; img!=NULL; img=img->next)
        if (k++ >= ilow-1)
            break;
    /* now img points to a2 */
    a2 = img;
    for (k=ilow; img!= NULL; img=img->next)
        if (k++ >= ihigh-1)
            break;
    b2 = img;
    b1 = SplitImageList(a2);
    c1 = SplitImageList(b2);
    split = 1;
    info = CloneImageInfo(NULL);
    /* Now append to the end of the List */
    for (k=0; k<P; k++) {
        tmp = PySequence_GetItem(v,k);
        img = _convert_object(tmp, info);
        if (img == NULL) ERRMSG("Error in converting to an Image Object");        
        AppendImageToList(&a1, img);
        Py_DECREF(tmp);
    }

    DestroyImageInfo(info);
    AppendImageToList(&a1, c1); /* Reattach final piece */
    DestroyImageList(b1);  /* Get rid of spliced out element */
    return 0;
    
 fail:
    Py_XDECREF(tmp);
    if (split) {  /* Reattach spliced-out image sequence */
        AppendImageToList(&a1,b1);
        AppendImageToList(&a1,c1);
    }
    if (info) DestroyImageInfo(info);
    return -1;

}


static int
mimage_ass_item(PyMImageObject *a, int i, PyObject *v) 
{
    return mimage_ass_slice(a, i, i+1, v);
}

/*
static PyObject *
mimage_subscript(PyMImageObject *imo, PyObject *op)
{
    
}

static int 
mimage_ass_sub(PyMImageObject *imo, PyObject *index, PyObject *op)
{
}


*/

static PySequenceMethods mimage_as_sequence = {
    (lenfunc)mimage_length,     /* sq_length */
    (binaryfunc)mimage_concat,          /* sq_concat */
    (ssizeargfunc)mimage_repeat,          /* sq_repeat */
    (ssizeargfunc)mimage_item,        /* sq_item*/
    (ssizessizeargfunc)mimage_slice,    /* sq_slice*/
    (ssizeobjargproc)mimage_ass_item, /* sq_ass_item*/
    (ssizessizeobjargproc)mimage_ass_slice, /* sq_ass_slice*/
    0, /*(objobjproc)mimage_contains,*/ /* sq_contains */
    (binaryfunc)mimage_inplace_concat,  /* sq_inplace_concat */
    (ssizeargfunc)mimage_inplace_repeat,  /* sq_inplace_repeat */
};

/*
static PyMappingMethods mimage_as_mapping = {
    (inquiry)mimage_length,             
    (binaryfunc)mimage_subscript        
    (objobjargproc)mimage_ass_sub       
};
*/


/**********************************************************************
 *
 *   M I m a g e O b j e c t
 *
 */


static PyTypeObject MImage_Type = {
    PyObject_HEAD_INIT(NULL)
    0,                                
    "MImage",                          /* tp_name */
    sizeof(PyMImageObject),        /* tp_basicsize */
    0,                                /* tp_itemsize */
    (destructor)mimage_dealloc,             /* tp_dealloc */
    0,          /* tp_print*/
    (getattrfunc)mimage_getattr,          /* tp_getattr*/
    (setattrfunc)mimage_setattr,          /* tp_setattr*/
    0,          /* tp_compare*/
    (reprfunc)mimage_repr,          /* tp_repr*/
    0,          /* tp_as_number*/
    &mimage_as_sequence,          /* tp_as_sequence*/
    0,          /* tp_as_mapping*/
    0,          /* tp_hash */
    0,          /* tp_call */
    0,          /* tp_str */
    0,          /* tp_getattro */
    0,          /* tp_setattro */
    0,          /* tp_as_buffer */
    0,          /* tp_flags */
    0,          /* tp_doc */
    0,          /* tp_traverse */
    0,          /* tp_clear */
    0,          /* tp_richcompare */
    0,          /* tp_weaklistoffset */
    0,          /* tp_iter */
    0,          /* tp_iternext */
    0,          /* tp_iter */
    0,          /* tp_iternext */
    0,          /* tp_methods */
    0,          /* tp_members */
    0,          /* tp_getset */
    0,          /* tp_base */
    0,          /* tp_dict */
    0,          /* tp_descr_get */
    0,          /* tp_descr_set */
    0,          /* tp_dictoffset */
    0,          /* tp_init */
    0,          /* tp_alloc */
    0,          /* tp_new */
    0,          /* tp_free */
    0,          /* tp_bases */
    0,          /* tp_mro */
    0,          /* tp_defined */
};



/**********************************************************************
 *
 *   D r a w I n f o O b j e c t
 *
 */

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif
static int
draw_prim_cat(PyObject *draw, char *src, int M)
{
    size_t toexpand;
    long len;
    size_t N;
    PyDrawInfoObject *di;

    di = ASDI(draw);
    
    if (M <= 0)
        N = strlen(src);
    else
        N = M;
    
    len = di->len;
    if ((len + N+1) > di->alloc)
    {
        toexpand = MAX(N+1, DRAWALLOCSIZE);
        di->alloc += toexpand;
        
        di->prim = MagickRealloc(di->prim, (size_t) di->alloc);
        
        if (!di->prim) {
            di->len = 0;
            di->alloc = 0;
            PyErr_SetString(PyMagickError, "Memory allocation error."\
                            " Drawing commands may be lost.");
            return False;
        }
    }
    
    
    (void )strncpy(di->prim+len, src, N); 
    (void )strncpy(di->prim+len+N, "\n", 2); /* add null termination */
    di->len += N+1;
    return True;
}

static char doc_addany_draw[] = "dc.addany(string)\n\n"\
" Append '\n' + string to the primitives in dc.";
static PyObject *
addany_draw(PyObject *self, PyObject *obj)
{
    char *str;
    int len;
    
    if ((str = PyString_AsString(obj))==NULL) return NULL;
    len = PyString_Size(obj);
    if (len > 0) {
        if (!draw_prim_cat(self, str, len)) return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static char doc_getall_draw[] = "dc.getall()\n\n"\
" Return string of stored primitives.";
static PyObject *
getall_draw(PyObject *self)
{
    PyDrawInfoObject *di;

    di = ASDI(self);
    if ((di->len==0) || (di->alloc)==0)
        return PyString_FromString("");
    return PyString_FromStringAndSize(di->prim, di->len);
}

static char doc_clear_draw[] = "dc.clear()\n\n"\
" Clear stored primitives.";
static PyObject *
clear_draw(PyObject *self)
{
    PyDrawInfoObject *di;

    di = ASDI(self);
    if (di->prim)
        MagickFree(di->prim);
    di->alloc = 0;
    di->len = 0;
    Py_INCREF(Py_None);
    return Py_None;
}

static char doc_arc_draw[] = \
"dc.arc(startX, startY, endX, endY, startDeg, endDeg)\n\n"\
" Add an arc to the drawing.";
static PyObject *
arc_draw(PyObject *self, PyObject *args)
{
    double sX, sY, eX, eY;
    double sD, eD;
    char msg[MaxTextExtent];

    if (!PyArg_ParseTuple(args, "dddddd", &sX, &sY, &eX, &eY,
                          &sD, &eD)) return NULL;
    
    snprintf(msg, MaxTextExtent, 
             "arc %g,%g %g,%g %g,%g", sX, sY, eX, eY, sD, eD);
    if (!draw_prim_cat(self, msg, -1)) return NULL;

    Py_INCREF(Py_None);
    return Py_None;    
    
}

#define MaxTextFloat 30
static char doc_bezier_draw[] = \
"dc.bezier(points)\n\n"\
" Add a bezier curve to the drawing";
static PyObject *
bezier_draw(PyObject *self, PyObject *args)
{
    PyObject *points;
    PyObject *arr=NULL;
    PyObject *iobj=NULL;
    PyObject *allobj=NULL;
    char single[MaxTextFloat];
    char *msg=NULL;
    double *arrptr;
    int N, k, len;
    
    if (!PyArg_ParseTuple(args, "O", &points)) return NULL;
    
    arr = PyArray_ContiguousFromObject(points,PyArray_DOUBLE,1,2);
    if (arr==NULL) return NULL;    
    N = PyArray_SIZE(ASARR(arr));
    if (N%2 == 1)
        ERRMSG("Need an even number of points.");

    arrptr = (double *)DATA(arr);
    snprintf(single, MaxTextFloat, "Bezier %g", *arrptr++);
    allobj = PyString_FromString(single);
    if (allobj==NULL) goto fail;
    for (k=1; k < N; k++) {
        snprintf(single, MaxTextFloat, ",%g", *arrptr++);
        iobj = PyString_FromString(single);
        if (iobj == NULL) goto fail;
        PyString_ConcatAndDel(&allobj, iobj);
        if (allobj==NULL) goto fail;
    }
    msg = PyString_AS_STRING(allobj);
    len = PyString_GET_SIZE(allobj);
    if (!draw_prim_cat(self, msg, len)) goto fail;
    
    Py_DECREF(allobj);
    Py_DECREF(arr);
    Py_INCREF(Py_None);
    return Py_None;    
    
 fail:
    Py_XDECREF(allobj);
    Py_XDECREF(arr);
    return NULL;
}


static char doc_circle_draw[] = \
"dc.circle(originX, originY, radius)\n\n"\
" Add a circle to the drawing.";
static PyObject *
circle_draw(PyObject *self, PyObject *args)
{
    double oX, oY;
    double rad;
    char msg[MaxTextExtent];

    if (!PyArg_ParseTuple(args, "ddd", &oX, &oY, &rad))
        return NULL;

    snprintf(msg, MaxTextExtent, 
             "circle %g,%g %g,%g", oX, oY, oX+rad, oY);
    if (!draw_prim_cat(self, msg, -1))
        return NULL;
    Py_INCREF(Py_None);
    return Py_None;        
}

static char doc_ellipse_draw[] = \
"dc.ellipse(originX, originY, width, height<, arcStart, arcEnd>)\n\n"\
" Add an ellipse to the drawing";
static PyObject *
ellipse_draw(PyObject *self, PyObject *args)
{
    double oX, oY, w, h;
    double aS = 0, aE = 360;
    char msg[MaxTextExtent];

    if (!PyArg_ParseTuple(args, "dddd|dd", &oX, &oY, &w, &h,
                          &aS, &aE)) return NULL;
    
    snprintf(msg, MaxTextExtent, 
             "ellipse %g,%g %g,%g %g,%g", oX, oY, w, h, aS, aE);
    if (!draw_prim_cat(self, msg, -1)) return NULL;

    Py_INCREF(Py_None);
    return Py_None;    
    
}

static char doc_line_draw[] = \
"dc.line(startX, startY, endX, endY)\n\n"\
" Add a line to the drawing.";
static PyObject *
line_draw(PyObject *self, PyObject *args)
{
    double sX, sY, eX, eY;
    char msg[MaxTextExtent];

    if (!PyArg_ParseTuple(args, "dddd", &sX, &sY, &eX, &eY))
        return NULL;
    
    snprintf(msg, MaxTextExtent, 
             "line %g,%g %g,%g", sX, sY, eX, eY);
    if (!draw_prim_cat(self, msg, -1)) return NULL;

    Py_INCREF(Py_None);
    return Py_None;    
    
}

static char doc_path_draw[] = \
"dc.path(commands)\n\n"\
" Add a string of SVG-compatible path arguments to the path.";
static PyObject *
path_draw(PyObject *self, PyObject *obj)
{
    char *str;
    char msg[MaxTextExtent];

    if ((str=PyString_AsString(obj))==NULL) return NULL;
    
    snprintf(msg, MaxTextExtent, 
             "path '%s'", str);
    if (!draw_prim_cat(self, msg, -1)) return NULL;

    Py_INCREF(Py_None);
    return Py_None;    
    
}

static char doc_point_draw[] = \
"dc.point(X, Y)\n\n"\
" Add a point to the drawing.";
static PyObject *
point_draw(PyObject *self, PyObject *args)
{
    double sX, sY;
    char msg[MaxTextExtent];

    if (!PyArg_ParseTuple(args, "dd", &sX, &sY))
        return NULL;
    
    snprintf(msg, MaxTextExtent, 
             "point %g,%g", sX, sY);
    if (!draw_prim_cat(self, msg, -1)) return NULL;

    Py_INCREF(Py_None);
    return Py_None;
}


static char doc_polygon_draw[] = \
"dc.polygon(points)\n\n"\
" Add a polygon.";
static PyObject *
polygon_draw(PyObject *self, PyObject *args)
{
    PyObject *points;
    PyObject *arr=NULL;
    PyObject *iobj=NULL;
    PyObject *allobj=NULL;
    char single[MaxTextFloat];
    char *msg=NULL;
    double *arrptr;
    int N, k, len;
    
    if (!PyArg_ParseTuple(args, "O", &points)) return NULL;
    
    arr = PyArray_ContiguousFromObject(points,PyArray_DOUBLE,1,2);
    if (arr==NULL) return NULL;    
    N = PyArray_SIZE(ASARR(arr));
    if (N%2 == 1)
        ERRMSG("Need an even number of points.");

    arrptr = (double *)DATA(arr);
    snprintf(single, MaxTextFloat, "polygon %g", *arrptr++);
    allobj = PyString_FromString(single);
    if (allobj==NULL) goto fail;
    for (k=1; k < N; k++) {
        snprintf(single, MaxTextFloat, ",%g", *arrptr++);
        iobj = PyString_FromString(single);
        if (iobj == NULL) goto fail;
        PyString_ConcatAndDel(&allobj, iobj);
        if (allobj==NULL) goto fail;
    }
    msg = PyString_AS_STRING(allobj);
    len = PyString_GET_SIZE(allobj);
    if (!draw_prim_cat(self, msg, len)) goto fail;
    
    Py_DECREF(allobj);
    Py_DECREF(arr);
    Py_INCREF(Py_None);
    return Py_None;    
    
 fail:
    Py_XDECREF(allobj);
    Py_XDECREF(arr);
    return NULL;
}

static char doc_polyline_draw[] = \
"dc.polyline(points)\n\n"\
" Add a polyline.";
static PyObject *
polyline_draw(PyObject *self, PyObject *args)
{
    PyObject *points;
    PyObject *arr=NULL;
    PyObject *iobj=NULL;
    PyObject *allobj=NULL;
    char single[MaxTextFloat];
    char *msg=NULL;
    double *arrptr;
    int N, k, len;
    
    if (!PyArg_ParseTuple(args, "O", &points)) return NULL;
    
    arr = PyArray_ContiguousFromObject(points,PyArray_DOUBLE,1,2);
    if (arr==NULL) return NULL;    
    N = PyArray_SIZE(ASARR(arr));
    if (N%2 == 1)
        ERRMSG("Need an even number of points.");

    arrptr = (double *)DATA(arr);
    snprintf(single, MaxTextFloat, "polyline %g", *arrptr++);
    allobj = PyString_FromString(single);
    if (allobj==NULL) goto fail;
    for (k=1; k < N; k++) {
        snprintf(single, MaxTextFloat, ",%g", *arrptr++);
        iobj = PyString_FromString(single);
        if (iobj == NULL) goto fail;
        PyString_ConcatAndDel(&allobj, iobj);
        if (allobj==NULL) goto fail;
    }
    msg = PyString_AS_STRING(allobj);
    len = PyString_GET_SIZE(allobj);
    if (!draw_prim_cat(self, msg, len)) goto fail;
    
    Py_DECREF(allobj);
    Py_DECREF(arr);
    Py_INCREF(Py_None);
    return Py_None;    
    
 fail:
    Py_XDECREF(allobj);
    Py_XDECREF(arr);
    return NULL;
}


static char doc_rect_draw[] = \
"dc.rect(upperLeftX, upperLeftY, lowerRightX, lowerRightY)\n\n"\
" Add a rectangle to the drawing.";
static PyObject *
rect_draw(PyObject *self, PyObject *args)
{
    double oX, oY, w, h;
    char msg[MaxTextExtent];

    if (!PyArg_ParseTuple(args, "dddd", &oX, &oY, &w, &h))
        return NULL;
    
    snprintf(msg, MaxTextExtent, 
             "rectangle %g,%g %g,%g", oX, oY, w, h);
    if (!draw_prim_cat(self, msg, -1)) return NULL;

    Py_INCREF(Py_None);
    return Py_None;        
}

static char doc_roundrect_draw[] = \
"dc.roundrect(originX, originY, width, height<, \n"\
"                   cornerWidth, cornerHeight)\n\n"\
" Add a round rectangle to the drawing";
static PyObject *
roundrect_draw(PyObject *self, PyObject *args)
{
    double oX, oY, w, h;
    double cW = -1, cH = -1;
    char msg[MaxTextExtent];
    int N;

    if (!PyArg_ParseTuple(args, "dddd|dd", &oX, &oY, &w, &h,
                          &cW, &cH)) return NULL;

    N = PyTuple_Size(args);
    if (N < 6) cH = 0.05*h;
    if (N < 5) cW = 0.05*w;
    
    snprintf(msg, MaxTextExtent, 
             "roundrectangle %g,%g %g,%g %g,%g", oX, oY, w, h, cW, cH);
    if (!draw_prim_cat(self, msg, -1)) return NULL;

    Py_INCREF(Py_None);
    return Py_None;        
}

/* XXX: Add full font support
   TODO: size, weight...
 */

static char doc_set_font[] = \
"dc.set_font(font)\n\n"\
" Set font to be used for drawing text.";

static PyObject *
set_font(PyObject *self, PyObject *args)
{
    char *font;
    char msg[MaxTextExtent];


    if (!PyArg_ParseTuple(args, "s", &font))
        return NULL;

    snprintf(msg, MaxTextExtent, 
             "font '%s'", font);

    if (!draw_prim_cat(self, msg, -1))
        return NULL;

    Py_INCREF(Py_None);
    return Py_None;
}


static char doc_text_draw[] = \
"dc.text(X, Y, text)\n\n"\
" Add text to the drawing.";
static PyObject *
text_draw(PyObject *self, PyObject *args)
{
    double oX, oY;
    char *txt;
    char msg[MaxTextExtent];

    if (!PyArg_ParseTuple(args, "dds", &oX, &oY, &txt))
        return NULL;
    
    snprintf(msg, MaxTextExtent, 
             "text %g,%g '%s'", oX, oY, txt);

    if (!draw_prim_cat(self, msg, -1))
        return NULL;

    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef drawinfo_methods[] = {
  {"addany", (PyCFunction)addany_draw, METH_O, doc_addany_draw},
  {"getall", (PyCFunction)getall_draw, METH_NOARGS, doc_getall_draw},
  {"clear", (PyCFunction)clear_draw, METH_NOARGS, doc_clear_draw},
  {"arc", (PyCFunction)arc_draw, METH_VARARGS, doc_arc_draw},
  {"bezier", (PyCFunction)bezier_draw, METH_VARARGS, doc_bezier_draw},
  {"circle", (PyCFunction)circle_draw, METH_VARARGS, doc_circle_draw},
  {"ellipse", (PyCFunction)ellipse_draw, METH_VARARGS, doc_ellipse_draw},
  {"line", (PyCFunction)line_draw, METH_VARARGS, doc_line_draw},
  {"path", (PyCFunction)path_draw, METH_O, doc_path_draw},
  {"point", (PyCFunction)point_draw, METH_VARARGS, doc_point_draw},
  {"polyline", (PyCFunction)polyline_draw, METH_VARARGS, doc_polyline_draw},
  {"polygon", (PyCFunction)polygon_draw, METH_VARARGS, doc_polygon_draw},
  {"rect", (PyCFunction)rect_draw, METH_VARARGS, doc_rect_draw},
  {"roundrect", (PyCFunction)roundrect_draw, METH_VARARGS, doc_roundrect_draw},
  {"set_font", (PyCFunction)set_font, METH_VARARGS, doc_set_font},
  {"text", (PyCFunction)text_draw, METH_VARARGS, doc_text_draw},

  {NULL, NULL, 0, NULL}    /* sentinel */
};


static void
drawinfo_dealloc(PyObject *self)
{
    PyDrawInfoObject *obj;
    
    obj = ASDI(self);
    if (obj) {
    if (obj->info)
        DestroyDrawInfo(obj->info);
    if (obj->prim)
        MagickFree(obj->prim);
    }
    PyObject_Del(self);
}

static PyObject *
drawattr_get(DrawInfo *dinfo, char *attr)
{
    PyObject *obj=NULL;

    switch(*attr) {
    case 'a':
    if (strcmp(attr, "affine")==0) {
            int dims[2] = {3,3};
            double *arrptr;
            PyObject *arrobj;

            arrobj = PyArray_FromDims(2,dims,PyArray_DOUBLE);
            if (arrobj == NULL) return NULL;
            arrptr = (double *)DATA(arrobj);
            *(arrptr) = dinfo->affine.sx;
            *(arrptr+1) = dinfo->affine.rx;
            *(arrptr+2) = 0.0;
            *(arrptr+3) = dinfo->affine.ry;
            *(arrptr+4) = dinfo->affine.sy;
            *(arrptr+5) = 0.0;
            *(arrptr+6) = dinfo->affine.tx;
            *(arrptr+7) = dinfo->affine.ty;
            *(arrptr+8) = 1.0;
            return arrobj;
    }
    else if (strcmp(attr, "align")==0) {
            ENUM2STR(AlignTypes, dinfo->align, attr);
    }
    break;
    case 'b':
    if (strcmp(attr, "border") == 0) {
            return Py_BuildValue("llll",dinfo->border_color.red,
                                 dinfo->border_color.green,
                                 dinfo->border_color.blue,
                                 dinfo->border_color.opacity);
    }
    else if (strcmp(attr, "bounds") == 0) {
            return Py_BuildValue("dddd", dinfo->bounds.x1, dinfo->bounds.y2,
                                 dinfo->bounds.x2, dinfo->bounds.y2);
    }
    break;
    case 'c':
    if (strcmp(attr, "compose")==0) {
            ENUM2STR(CompositeTypes, dinfo->compose, attr);
    }
    else if (strcmp(attr, "clip_units")==0) {
            ENUM2STR(ClipPathUnitss, dinfo->clip_units, attr);
    }
        else if (strcmp(attr, "clip_path")==0) {
            return STR2PYSTR(dinfo->clip_path);
        }
    break;
    case 'd':   
    if (strcmp(attr, "dash_offset")==0) {
            return PyFloat_FromDouble(dinfo->dash_offset);
    }
    else if (strcmp(attr, "dash_pattern")==0) {
        PyObject *arr;
        int N, k;
            double *dashptr, *arrptr;
            int dims[1];

            dashptr=dinfo->dash_pattern;
            if (dashptr == NULL) goto noout;
            for (k=0; *dashptr != 0; k++)
                dashptr++;
            N = k+1;
            dims[0] = N;
            arr = PyArray_FromDims(1,dims,PyArray_DOUBLE);
            if (arr==NULL) return NULL;
            dashptr = dinfo->dash_pattern;
            arrptr = (double *)DATA(arr);
            for (k=0; k < N; k++)
                *arrptr++ = *dashptr++;
            return arr;
    }
    else if (strcmp(attr, "decorate")==0) {
            ENUM2STR(DecorationTypes, dinfo->decorate, attr);
    }
    else if (strcmp(attr, "density")==0) {
            return STR2PYSTR(dinfo->density);
    }
    break;
    case 'e':
    if (strcmp(attr, "encoding")==0) {
            return STR2PYSTR(dinfo->encoding);
    }
    break;
    case 'f':
    if (strcmp(attr, "fill")==0) {
            return Py_BuildValue("llll", dinfo->fill.red,
                                 dinfo->fill.green,
                                 dinfo->fill.blue, 
                                 dinfo->fill.opacity);
    }
    else if (strcmp(attr, "fill_rule")==0) {
            ENUM2STR(FillRules, dinfo->fill_rule, attr);
    }
    else if (strcmp(attr, "font")==0) {
            return STR2PYSTR(dinfo->font);
    }
    else if (strcmp(attr, "font_family")==0) {
            return STR2PYSTR(dinfo->family);
    }
    else if (strcmp(attr, "font_stretch")==0) {
            ENUM2STR(StretchTypes, dinfo->stretch, attr);
    }
    else if (strcmp(attr, "font_style")==0) {
            ENUM2STR(StyleTypes, dinfo->style, attr);
    }
    else if (strcmp(attr, "font_weight")==0) {
            return PyInt_FromLong(dinfo->weight);
    }
    else if (strcmp(attr, "fill_pattern")==0) {
            obj = (PyObject *)PyObject_New(PyMImageObject, &MImage_Type);
            if (obj == NULL) return NULL;
            ASIM(obj)->ims = dinfo->fill_pattern ? CloneImage(dinfo->fill_pattern,0,0,True,&exception) : NULL;
            CHECK_ERR;
            return obj;
    }
    break;
    case 'g':
        if (strcmp(attr, "geometry")==0) {
            return STR2PYSTR(dinfo->geometry);
        }
    else if (strcmp(attr, "gravity")==0) {
            ENUM2STR(GravityTypes, dinfo->gravity, attr);
    }
    break;
    case 'l':
    if (strcmp(attr, "linecap")==0) {
            ENUM2STR(LineCapTypes, dinfo->linecap, attr);
    }
    else if (strcmp(attr, "linejoin")==0) {
            ENUM2STR(LineJoinTypes, dinfo->linejoin, attr);
    }
    break;
    case 'm':
    if (strcmp(attr, "miterlimit")==0) {
            return PyInt_FromLong(dinfo->miterlimit);
    }
    case 'p':
    if (strcmp(attr, "pointsize")==0) {
            return PyFloat_FromDouble(dinfo->pointsize);
    }
    break;
    case 'o':
    if (strcmp(attr, "opacity")==0) {
            return PyInt_FromLong(dinfo->opacity);
    }
    break;
    case 's':
    if (strcmp(attr, "stroke")==0) {
            return Py_BuildValue("llll", dinfo->stroke.red, dinfo->stroke.green,
                                 dinfo->stroke.blue, dinfo->stroke.opacity);
    }
    else if (strcmp(attr, "stroke_antialias")==0) {
            return PyInt_FromLong(dinfo->stroke_antialias != 0);
    }
    else if (strcmp(attr, "stroke_pattern")==0) {
            obj = (PyObject *)PyObject_New(PyMImageObject, &MImage_Type);
            if (obj == NULL) return NULL;
            ASIM(obj)->ims = dinfo->stroke_pattern ? CloneImage(dinfo->stroke_pattern,0,0,True,&exception) : NULL;
            CHECK_ERR;
            return obj;
    }
    else if (strcmp(attr, "stroke_width")==0) {
            return PyFloat_FromDouble(dinfo->stroke_width);
    }
    break;
    case 't':
    if (strcmp(attr, "text")==0) {
            return STR2PYSTR(dinfo->text);
    }
    else if (strcmp(attr, "text_antialias")==0) {
            return PyInt_FromLong(dinfo->text_antialias != 0);
    }
    else if (strcmp(attr, "tile")==0) {
            obj = (PyObject *)PyObject_New(PyMImageObject, &MImage_Type);
            if (obj == NULL) return NULL;
            ASIM(obj)->ims = dinfo->tile ? CloneImage(dinfo->tile,0,0,True,&exception) : NULL;
            CHECK_ERR;
            return obj;
    }
    break;       
    case 'u':
    if (strcmp(attr, "undercolor")==0) {
            return Py_BuildValue("llll", dinfo->undercolor.red, 
                                 dinfo->undercolor.green,
                                 dinfo->undercolor.blue, 
                                 dinfo->undercolor.opacity);
    }
    break;
    default:
    PyErr_Format(PyMagickError, "invalid attribute %s", attr);
    return NULL;
    }
    PyErr_Format(PyMagickError, "invalid attribute %s", attr);
    return NULL;

 noout:
    Py_INCREF(Py_None);
    return Py_None;

 fail:
    return NULL;
}


static PyObject *
drawinfo_getattr(PyDrawInfoObject *self, char *attr)
{
    PyObject *methobj;
    methobj = Py_FindMethod(drawinfo_methods, (PyObject *)self, attr);
    if (methobj != NULL) return methobj;
    /* no method found.  Let's look for attributes. */
    CLEAR_ERR;
    /* Get rid of any error from FindMethod */

    if (!(self->info)) {
        PyErr_SetString(PyMagickError, "Null info.");
        return NULL;
    }
    return drawattr_get(self->info, attr); 
}

/*
 * D r a w I n f o    S e t A t t r i b u t e
 *
 */
static int
drawinfo_setattr(PyDrawInfoObject *self, char *attr, PyObject *val)
{
    DrawInfo *info;
    double x;
    int j;
    long num;
    char *tmpstr;
    PyObject *arr;
    int N;

    if (val == NULL) 
    ERRMSG("Cannot delete DrawInfo attributes.");
    
    info = self->info;
    if (!info) ERRMSG("Null info.");
    tmpstr = PyString_AsString(val);

    switch(*attr) {
    case 'a':
    if (strcmp(attr, "affine")==0) {
        CLEAR_ERR;
        AffineMatrix affine;
        if (!get_affine_matrix(&affine, val)) return -1;
        info->affine = affine;
        return 0;
    }
    else if (strcmp(attr, "align")==0) {
        if (tmpstr == NULL) return -1;
        if ((j = LookupStr(AlignTypes,tmpstr)) <  0)
        ERRMSG3(tmpstr, attr);
        info->align = (AlignType) j;
        return 0;
    }
    else goto noattr;
    break;
    case 'b':
    if (strcmp(attr, "border") == 0) {
        CLEAR_ERR;
        if (!set_color_from_obj(&info->border_color, val, attr)) 
        return -1;
    }
    else if (strcmp(attr, "bounds") == 0) {
        SegmentInfo bounds;
        CLEAR_ERR;
        if (!PyArg_ParseTuple(val, "dddd", &bounds.x1, &bounds.y1,
                  &bounds.x2, &bounds.y2))
        return -1;
        info->bounds = bounds;
    }
    else goto noattr;
    break;
    case 'c':
    if (strcmp(attr, "compose")==0) {
        if (tmpstr==NULL) return -1;
        if ((j = LookupStr(CompositeTypes, tmpstr)) < 0)
                ERRMSG3(tmpstr,attr);
        info->compose = (CompositeOperator) j;
    }
    else if (strcmp(attr, "clip_units")==0) {
        if (tmpstr==NULL) return -1;
        if ((j = LookupStr(ClipPathUnitss, tmpstr)) < 0)
                ERRMSG3(tmpstr,attr);
        info->clip_units = (ClipPathUnits) j;
    }
        else if (strcmp(attr, "clip_path")==0) {
          if (tmpstr == NULL) return -1;
          info->clip_path = AllocateString(tmpstr);
        }
    else goto noattr;
    break;
    case 'd':   
    if (strcmp(attr, "dash_offset")==0) {
        CLEAR_ERR;
        x = PyFloat_AsDouble(val);
        if ((x==-1) && PyErr_Occurred()) return -1;
        info->dash_offset = x;
    }
    else if (strcmp(attr, "dash_pattern")==0) {
        CLEAR_ERR;
        arr = PyArray_ContiguousFromObject(val,PyArray_DOUBLE,1,1);
        if (arr==NULL) return -1;
        N = DIM(arr,0);
        info->dash_pattern = (double *)MagickMalloc(sizeof(double)*N);
        if (info->dash_pattern == NULL) {
        Py_DECREF(arr);
        return -1;
        }
        memcpy(info->dash_pattern, DATA(arr), (N-1)*sizeof(double));
            memset(info->dash_pattern + N-1,0,sizeof(double));
            Py_DECREF(arr);
    }
    else if (strcmp(attr, "decorate")==0) {
        if (tmpstr == NULL) return -1;
        if ((j = LookupStr(DecorationTypes,tmpstr)) <  0)
        ERRMSG3(tmpstr, attr);
        info->decorate = (DecorationType) j;
    }
    else if (strcmp(attr, "density")==0) {
        if (tmpstr == NULL) return -1;
        info->density = AllocateString(tmpstr);
    }
    else goto noattr;
    break;
    case 'e':
    if (strcmp(attr, "encoding")==0) {
        if (tmpstr == NULL) return -1;
        info->encoding = AllocateString(tmpstr);
    }
    else goto noattr;
    break;
    case 'f':
    if (strcmp(attr, "fill")==0) {
        CLEAR_ERR;
        if (!set_color_from_obj(&info->fill, val, attr)) return -1;
    }
    else if (strcmp(attr, "fill_rule")==0) {
        if (tmpstr == NULL) return -1;
        if ((j = LookupStr(FillRules,tmpstr)) <  0)
        ERRMSG3(tmpstr, attr);
        info->fill_rule = (FillRule) j;
        return 0;       
    }
    else if (strcmp(attr, "font")==0) {
        if (tmpstr == NULL) return -1;
        info->font = AllocateString(tmpstr);
        return 0;
    }
    else if (strcmp(attr, "font_family")==0) {
        if (tmpstr == NULL) return -1;
        info->family = AllocateString(tmpstr);
        return 0;
    }
    else if (strcmp(attr, "font_stretch")==0) {
        if (tmpstr == NULL) return -1;
        if ((j = LookupStr(StretchTypes,tmpstr)) <  0)
        ERRMSG3(tmpstr, attr);
        info->stretch = (StretchType) j;
        return 0;       
    }
    else if (strcmp(attr, "font_style")==0) {
        if (tmpstr == NULL) return -1;
        if ((j = LookupStr(StyleTypes,tmpstr)) <  0)
        ERRMSG3(tmpstr, attr);
        info->style = (StyleType) j;
        return 0;
    }
    else if (strcmp(attr, "font_weight")==0) {
        unsigned long w;
            CLEAR_ERR;
            w = PyInt_AsLong(val);
            if (PyErr_Occurred()) return -1;
            info->weight = w;
        return 0;
    }
    else if (strcmp(attr, "fill_pattern")==0) {
        PyObject *img;
        CLEAR_ERR;
        if ((img=mimage_from_object(val))==NULL) return -1;
        if (img && (ASIM(img)->ims))
        info->fill_pattern = CloneImage(ASIM(img)->ims, 0, 0, True, &exception);
        Py_DECREF(img);
        CHECK_ERR;
        return 0;       
    }
    else goto noattr;
    break;
    case 'g':
        if (strcmp(attr, "geometry")==0) {
        if (tmpstr == NULL) return -1;
        info->geometry = AllocateString(tmpstr);
        return 0;            
        }
        else if (strcmp(attr, "gravity")==0) {
        if (tmpstr == NULL) return -1;
        if ((j = LookupStr(GravityTypes,tmpstr)) <  0)
        ERRMSG3(tmpstr, attr);
        info->gravity = (GravityType) j;
        return 0;
    }
    else goto noattr;
    break;
    case 'l':
    if (strcmp(attr, "linecap")==0) {
        if (tmpstr == NULL) return -1;
        if ((j = LookupStr(LineCapTypes,tmpstr)) <  0)
        ERRMSG3(tmpstr, attr);
        info->linecap = (LineCap) j;
        return 0;       
    }
    else if (strcmp(attr, "linejoin")==0) {
        if (tmpstr == NULL) return -1;
        if ((j = LookupStr(LineJoinTypes,tmpstr)) <  0)
        ERRMSG3(tmpstr, attr);
        info->linejoin = (LineJoin) j;
        return 0;       
    }
    else goto noattr;
    break;
    case 'm':
    if (strcmp(attr, "miterlimit")==0) {
        CLEAR_ERR;
        num = PyInt_AsLong(val);
        if ((num==-1) && (PyErr_Occurred())) return -1;
        info->miterlimit = num;
    }
    else goto noattr;
    break;
    case 'p':
    if (strcmp(attr, "pointsize")==0) {
        CLEAR_ERR;
        x = PyFloat_AsDouble(val);
        if ((x==-1) && PyErr_Occurred()) return -1;
        info->pointsize = x;
    }
    else goto noattr;
    break;
    case 'o':
    if (strcmp(attr, "opacity")==0) {
        unsigned int op;
        CLEAR_ERR;
        op = (unsigned int) PyInt_AsLong(val);
        if (PyErr_Occurred()) return -1;
        info->opacity = op;
    }
    else goto noattr;
    break;
    case 'r':
    if (strcmp(attr, "rotation")==0) {
        AffineMatrix affine, current;
        CLEAR_ERR;
        x = PyFloat_AsDouble(val);
        if ((x==-1) && PyErr_Occurred()) return -1;
        if (x != 0) {
        affine.sx=1.0;
        affine.rx=0.0;
        affine.ry=0.0;
        affine.sy=1.0;
        affine.tx=0.0;
        affine.ty=0.0;

        current = info->affine;
        affine.sx=cos(DegreesToRadians(fmod(x,360.0)));
        affine.rx=sin(DegreesToRadians(fmod(x,360.0)));
        affine.ry=(-sin(DegreesToRadians(fmod(x,360.0))));
        affine.sy=cos(DegreesToRadians(fmod(x,360.0)));
        
        info->affine.sx=current.sx*affine.sx+current.ry*affine.rx;
        info->affine.rx=current.rx*affine.sx+current.sy*affine.rx;
        info->affine.ry=current.sx*affine.ry+current.ry*affine.sy;
        info->affine.sy=current.rx*affine.ry+current.sy*affine.sy;
        info->affine.tx=current.sx*affine.tx+current.ry*affine.ty+current.tx;
        }
        return 0;
    }
    else goto noattr;
    break;
    case 's':
    if (strcmp(attr, "stroke")==0) {
        CLEAR_ERR;
        if (!set_color_from_obj(&info->stroke, val, attr)) return -1;
    }
    else if (strcmp(attr, "stroke_antialias")==0) {
        CLEAR_ERR;
        info->stroke_antialias = PyObject_IsTrue(val);
    }
    else if (strcmp(attr, "stroke_pattern")==0) {
        PyObject *img;
        CLEAR_ERR;
        if ((img=mimage_from_object(val))==NULL) return -1;
        if (img && (ASIM(img)->ims))
        info->stroke_pattern = CloneImage(ASIM(img)->ims, 0, 0, True, &exception);
        Py_DECREF(img);
        CHECK_ERR;
        return 0;
    }
    else if (strcmp(attr, "stroke_width")==0) {
        CLEAR_ERR;
        x = PyFloat_AsDouble(val);
        if ((x==-1) && (PyErr_Occurred())) return -1;
        info->stroke_width = x;
    }
    else goto noattr;
    break;
    case 't':
    if (strcmp(attr, "text")==0) {
        if (tmpstr==NULL) return -1;
        info->text = AllocateString(tmpstr);
    }
    else if (strcmp(attr, "text_antialias")==0) {
        CLEAR_ERR;
        info->text_antialias = PyObject_IsTrue(val);
    }
    else if (strcmp(attr, "tile")==0) {
        PyObject *img;
        CLEAR_ERR;
        if ((img=mimage_from_object(val))==NULL) return -1;
        if (img && (ASIM(img)->ims))
        info->tile = CloneImage(ASIM(img)->ims, 0, 0, True, &exception);
        Py_DECREF(img);
        CHECK_ERR;
        return 0;
    }
    else goto noattr;
    break;       
    case 'u':
    if (strcmp(attr, "undercolor")==0) {
        CLEAR_ERR;
        if (!set_color_from_obj(&info->undercolor, val, attr)) return -1;
    }
    else goto noattr;
    break;
    default:
    goto noattr;
    }
    CLEAR_ERR;
    return 0;

 noattr:
    PyErr_Format(PyMagickError, "invalid attribute %s", attr);
    return -1;

 fail:
    return -1;
}

static PyTypeObject DrawInfo_Type = {
    PyObject_HEAD_INIT(NULL)
    0,                                
    "DrawInfo",                          /* tp_name */
    sizeof(PyDrawInfoObject),            /* tp_basicsize */
    0,                                /* tp_itemsize */ 
    (destructor)drawinfo_dealloc,          /* tp_dealloc */
    0,          /* tp_print*/
    (getattrfunc)drawinfo_getattr,          /* tp_getattr*/
    (setattrfunc)drawinfo_setattr,          /*tp_setattr */
    0,          /* tp_compare*/
    0,          /* tp_repr*/
    0,          /* tp_as_number*/
    0,          /* tp_as_sequence*/
    0,          /* tp_as_mapping*/
    0,          /* tp_hash */
    0,          /* tp_call */
    0,          /* tp_str */
    0,          /* tp_getattro */
    0,          /* tp_setattro */
    0,          /* tp_as_buffer */
    0,          /* tp_flags */
    "A place to store default draw_info terms and to "\
    "build up primitive drawing instructions",    /* tp_doc */
    0,          /* tp_traverse */
    0,          /* tp_clear */
    0,          /* tp_richcompare */
    0,          /* tp_weaklistoffset */
    0,          /* tp_iter */
    0,          /* tp_iternext */
    0,          /* tp_iter */
    0,          /* tp_iternext */
    0,          /* tp_methods */
    0,          /* tp_members */
    0,          /* tp_getset */
    0,          /* tp_base */
    0,          /* tp_dict */
    0,          /* tp_descr_get */
    0,          /* tp_descr_set */
    0,          /* tp_dictoffset */
    0,          /* tp_init */
    0,          /* tp_alloc */
    0,          /* tp_new */
    0,          /* tp_free */
    0,          /* tp_bases */
    0,          /* tp_mro */
    0,          /* tp_defined */
};

static PyObject *
magick_new_draw(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyDrawInfoObject *draw=NULL;
    char memstr[] = "Could not allocate memory";

    if (!PyArg_ParseTuple(args,""))
        return NULL;

    draw = PyObject_New(PyDrawInfoObject, &DrawInfo_Type);
    if (draw == NULL)
        return NULL;
        
    draw->info = CloneDrawInfo(NULL, NULL);

    if (draw->info == NULL)
        ERRMSG(memstr);

    draw->prim = NULL;
    draw->alloc = 0;
    draw->len = 0;
    
    if (kwds != NULL) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwds, &pos, &key, &value)) {
            if (drawinfo_setattr(draw, PyString_AS_STRING(key), 
                 value) == -1) goto fail;
        }
    }    

    return (PyObject *)draw;

 fail:
    Py_XDECREF(draw);
    return NULL;
}



static char doc_magnify_image[] = "out = magnify(img)\n\nMagnify an image to twice its size.";
static PyObject *
magnify_image(PyObject *self, PyObject *obj)
{
    PyObject *imobj=NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail; 
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, MagnifyImage(mag, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL;   
}


static char doc_minify_image[] = "out = minify(img)\n\nMinify an image to half its size.";
static PyObject *
minify_image(PyObject *self, PyObject *obj)
{
    PyObject *imobj = NULL;
    Image *mag;
    PyMImageObject *new = NULL;
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail; 
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, MinifyImage(mag, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL;    
}

static int 
process_one(PyObject *obj, unsigned long current, long *val)
{
    double conv;

    /* Convert row, column object to long value */
    /* If obj is an integer, use it directly */
    /* if obj is a float >0, treat it as a
       scale factor to multiply by current */

    if (PyInt_Check(obj)) {
        *val = PyInt_AS_LONG(obj);
        return True;       
    }
    conv = PyFloat_AsDouble(obj);
    if (PyErr_Occurred()) {
        PyErr_SetString(PyMagickError, \
                        "Invalid type for row or column.");
        return False;
    }
    if (conv < 0) 
        *val = -1;
    else
        *val = current * conv;
    return True;
}

static int
get_rows_cols(Image *im, PyObject *robj, PyObject *cobj, long *rows, long *cols)
{    
    if ((robj==NULL) || (cobj==NULL)) ERRMSG("Objects null.");
    
    if (!process_one(robj, im->rows, rows)) goto fail;
    if (!process_one(cobj, im->columns, cols)) goto fail;
    
    if ((*cols)*(*rows) == 0) ERRMSG("Shape of zero is invald.");
    if ((*cols < 0) && (*rows < 0)) {
        *cols = im->columns;
        *rows = im->rows;
    }
    if (*cols < 0) /* Keep scale factor */
        *cols = im->columns * ((double) (*rows) / (double) (im->rows)) + 0.5;
    if (*rows < 0) /* Keep scale factor */
        *rows = im->rows * ((double) (*cols) / (double) (im->columns)) + 0.5;
    return True;
                            
 fail:
    return False;
    
}

static char doc_resize_image[] = "out = resize(img, (rows, columns) {,blur, filter}) \n\n"\
"  Resize an image to an arbitrary shape using a blur factor (>1 is blurry, \n"\
"   <1 is sharp) and a filter: 'Lanczos' (default), 'Bessel', 'Catrom', \n"\
"   'Hanning', 'Mitchell', 'Sinc', 'Blackman', 'Cubic', 'Hermite', \n"\
"   'Point', 'Triangle', 'Box', 'Gaussian', 'Quadratic'\n\n"\
"  If rows or columsn is <0 then keep aspect ratio.  If they are not integers\n"\
"    then treat as factors to multiply by the current size.";
static PyObject *
resize_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    char *str = NULL;
    PyObject *rows_obj, *cols_obj;
    long rows, cols;
    double blur = 0.9;
    int ind;
        
    if (!PyArg_ParseTuple(args, "O(OO)|ds",&obj, &rows_obj, &cols_obj, 
             &blur, &str)) return NULL;
    if (str == NULL) str = "Lanczos";
    if ((ind = LookupStr(FilterTypess, str))< 0) {
    PyErr_Format(PyMagickError, "Unrecognized Filter Type: %s", str);
    return NULL;
    }
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    if (!get_rows_cols(ASIM(imobj)->ims, rows_obj, cols_obj, &rows, &cols)) 
        goto fail;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail; 
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, ResizeImage(mag, cols, rows, 
                        (FilterTypes)ind, blur, 
                        &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);    
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL;    
}


static char doc_sample_image[] = "out = sample(img, (rows, columns)) \n\n"\
"  Resample an image to an arbitrary shape using pixel sampling.\n"\
"    No additional colors are introduced into the image.\n"\
"  If rows or columns < 0, then keep aspect ratio.";
static PyObject *
sample_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    long rows, cols;
        
    if (!PyArg_ParseTuple(args, "O(ll)",&obj, &rows, &cols)) return NULL;
    if (rows*cols == 0) ERRMSG("Negative shape not allowed");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    if ((rows < 0) && (cols < 0)) /* silently return image copy */
        return imobj;
    mag = ASIM(imobj)->ims;
    if (cols < 0) /* Keep scale factor */
        cols = mag->columns * ((double) (rows) / (double) (mag->rows)) + 0.5;
    if (rows < 0) /* Keep scale factor */
        rows = mag->rows * ((double) (cols) / (double) (mag->columns)) + 0.5;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail; 
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, SampleImage(mag, cols, rows, 
                         &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj); 
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL;    
}


static char doc_scale_image[] = "out = scale(img, (rows, columns)) \n\n"\
"  Scale an image to an arbitrary shape.\n"\
"  If rows or columns < 0, then keep aspect ratio.";
static PyObject *
scale_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    long rows, cols;
        
    if (!PyArg_ParseTuple(args, "O(ll)",&obj, &rows, &cols)) return NULL;
    if (rows*cols == 0) ERRMSG("Negative shape not allowed");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    if ((rows < 0) && (cols < 0)) /* do nothing */
        return imobj;
    mag = ASIM(imobj)->ims;
    if (cols < 0) /* Keep scale factor */
        cols = mag->columns * ((double) (rows) / (double) (mag->rows)) + 0.5;
    if (rows < 0) /* Keep scale factor */
        rows = mag->rows * ((double) (cols) / (double) (mag->columns)) + 0.5;
    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail; 
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, ScaleImage(mag, cols, rows, 
                        &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj); 
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL;    
}

static char doc_thumbnail_image[] = "out = thumbnail(img, (rows, columns)) \n\n"\
"  Scale an image -- particularly to create a thumbnail.\n\n"\
"  If rows or columsn is <0 then keep aspect ratio.  If they are not integers\n"\
"    then treat as factors to multiply by the current size.";
static PyObject *
thumbnail_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    PyObject *rows_obj, *cols_obj;
    long rows, cols;
        
    if (!PyArg_ParseTuple(args, "O(OO)",&obj, &rows_obj, &cols_obj)) return NULL;
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    if (!get_rows_cols(ASIM(imobj)->ims, rows_obj, cols_obj, &rows, &cols)) 
        goto fail;
    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, ThumbnailImage(mag, cols, rows, 
                            &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj); 
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_chop_image[] = "out = chop(img, (left,columns,upper,rows)) \n\n"\
"  Chop an image:  remove rows and columns from the image. \n"\
"                  left     the leftmost column to remove \n"\
"                  columns  how many columns to remove \n"\
"                  upper    the topmost row to remove\n"\
"                  rows     how many rows to remove\n\n"\
"         The remaining rows and columns are shifted to fill in the gaps.";
static PyObject *
chop_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    long left, upper, columns, rows;
    RectangleInfo rect;

    if (!PyArg_ParseTuple(args, "O(llll)",&obj, &left, &columns,
              &upper, &rows)) return NULL;
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    rect.width = columns;
    rect.height = rows;
    rect.x = left;
    rect.y = upper;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, ChopImage(mag, &rect, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_coalesce_images[] = "out = coalesce(images) \n\n"\
"Composite an image list of different sizes and offsets into a single\n"\
"  image sequence.";
static PyObject *
coalesce_images(PyObject *self, PyObject *obj)
{
    PyObject *imobj = NULL;
    PyMImageObject *new=NULL;

    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = CoalesceImages(ASIM(imobj)->ims, &exception);
    CHECK_ERR;
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_deconstruct_images[] = "out = deconstruct(images) \n\n"\
"Compare each image with the next in a sequence and return the maximum\n"\
"  bounding region of any pixel differences encountered";
static PyObject *
deconstruct_images(PyObject *self, PyObject *obj)
{
    PyObject *imobj = NULL;
    PyMImageObject *new=NULL;

    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = DeconstructImages(ASIM(imobj)->ims, &exception);
    CHECK_ERR;
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_crop_image[] = "out = crop(img, (left,columns,upper,rows)) \n\n"\
"  Crop an image:  keep only specified rows and columns in the image. \n"\
"                  left     the leftmost column to keep \n"\
"                  columns  how many columns to keep \n"\
"                  upper    the topmost row to keep\n"\
"                  rows     how many rows to keep";
static PyObject *
crop_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    long left, upper, columns, rows;
    RectangleInfo rect;

    if (!PyArg_ParseTuple(args, "O(llll)",&obj, &left, &columns,
              &upper, &rows)) return NULL;
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    rect.width = columns;
    rect.height = rows;
    rect.x = left;
    rect.y = upper;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, CropImage(mag, &rect, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_flatten_images[] = "out = flatten(images) \n\n"\
"Merge an image list into a single image.\n";
static PyObject *
flatten_images(PyObject *self, PyObject *obj)
{
    PyObject *imobj = NULL;
    PyMImageObject *new=NULL;

    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = FlattenImages(ASIM(imobj)->ims, &exception);
    CHECK_ERR;
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_flip_image[] = "out = flip(image) \n\n"\
"Create a vertical mirror image.";
static PyObject *
flip_image(PyObject *self, PyObject *obj)
{
    PyObject *imobj = NULL;
    PyMImageObject *new=NULL;

    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = FlipImage(ASIM(imobj)->ims, &exception);
    CHECK_ERR;
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_flop_image[] = "out = flop(image) \n\n"\
"Create a vertical mirror image.";
static PyObject *
flop_image(PyObject *self, PyObject *obj)
{
    PyObject *imobj = NULL;
    PyMImageObject *new=NULL;

    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = FlopImage(ASIM(imobj)->ims, &exception);
    CHECK_ERR;
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_mosaic_images[] = "out = mosaic(images) \n\n"\
"Inlays an image sequence to form a single coherent picture.";
static PyObject *
mosaic_images(PyObject *self, PyObject *obj)
{
    PyObject *imobj = NULL;
    PyMImageObject *new=NULL;

    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = MosaicImages(ASIM(imobj)->ims, &exception);
    CHECK_ERR;
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_roll_image[] = "out = roll(img, (columns,rows)) \n\n"\
"  Roll an image:  columns  the number of columns to roll\n"\
"                  rows     the number of rows to roll.\n";
static PyObject *
roll_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    long columns, rows;

    if (!PyArg_ParseTuple(args, "O(ll)",&obj, &columns, &rows))
        return NULL;
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, RollImage(mag, columns, rows, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_shave_image[] = "out = shave(img, (columns,rows)) \n\n"\
" Shave an image by deleting rows from both the top and bottom and\n"\
"    columns from the left and right.";
static PyObject *
shave_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    long columns, rows;
    RectangleInfo info;

    if (!PyArg_ParseTuple(args, "O(ll)",&obj, &columns, &rows)) 
        return NULL;
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    
    info.width = columns;
    info.height = rows;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, ShaveImage(mag, &info,  &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_affine_image[] = "out = affine(img, affine, background=) \n\n"\
" Transform an image as dictated by the affine sequence. \n"\
"   The affine sequence is (sx, rx, ry, sy{, tx, ty}) with tx and ty 0 \n"\
"   if not given.\n\n"\
" Background is filled with background attribute of img.  Any keywords are\n"\
"   interpreted as attributes to set in img prior to transformation.\n"\
" The affine matrix is which is post-multiplied by [x,y,1] is \n\n"\
"     sx rx 0\n"\
"     ry sy 0\n"\
"     tx ty 1\n\n"\
" The affine object can also be a 2x2, 2x3, 3x2, or 3x3 matrix with tx and ty\n"\
"   assumed to be zero if not given and any constant elements ignored";
static PyObject *
affine_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *imobj = NULL, *obj = NULL;
    PyObject *affobj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    AffineMatrix matrix;

    if (!PyArg_ParseTuple(args, "OO",&obj, &affobj))
        return NULL;
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    if (kwds != NULL) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwds, &pos, &key, &value)) {
            if (mimage_setattr(ASIM(imobj), PyString_AS_STRING(key), 
                               value) == -1) goto fail;
        }
    }

    if (!get_affine_matrix(&matrix, affobj)) goto fail;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;


    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, 
                          AffineTransformImage(mag, &matrix, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_rotate_image[] = "out = rotate(img, angle, background=) \n\n"\
" Rotate image by the given angle.  If angle is positive, then \n"\
"   clockwise rotation.  If angle is negative, then counter-clockwise \n"\
"   rotation.  Background is filled with background attribute of img.\n"\
"   Any keywords present are interpreted as attributes to set in img prior.\n"\
"   to rotation.";
static PyObject *
rotate_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double deg;

    if (!PyArg_ParseTuple(args, "Od",&obj, &deg))
        return NULL;
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    if (kwds != NULL) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwds, &pos, &key, &value)) {
            if (mimage_setattr(ASIM(imobj), PyString_AS_STRING(key), 
                               value) == -1) goto fail;
        }
    }
    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, RotateImage(mag, deg, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}



static char doc_shear_image[] = \
"out = shear(img, x_shear, y_shear, background=) \n\n"\
" Shear image by the given angles.  x_shear is measured relative to the\n"\
"   Y axis.  y_shear is measured relative to the x axis.  Background is \n"\
"   filled with background attribute of img.  Any keywords given are\n"\
"   used to set corresponding attributes of img prior to shear.";
static PyObject *
shear_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    PyObject *tcolor=NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double shx, shy;

    if (!PyArg_ParseTuple(args, "Odd|O",&obj, &shx, &shy, &tcolor))
        return NULL;
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    if (kwds != NULL) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwds, &pos, &key, &value)) {
            if (mimage_setattr(ASIM(imobj), PyString_AS_STRING(key), 
                               value) == -1) goto fail;
        }
    }

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, ShearImage(mag, shx, shy, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}



static char doc_adaptive_image[] = \
"out = lat(img, width(3), height(3) offset(0)) \n\n"\
" Perform local adative thresholding using width x height around pixel.\n"\
"   output is a thresholded image where threshold is calculated locally\n"\
"   from mean of the surrounding pixels + given offset.\n"\
"   If offset < 1 it represents a percentage.";
static PyObject *
adaptive_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    long width=3, height=3;
    double offset=0.0;

    if (!PyArg_ParseTuple(args, "O|lld",&obj, &width, &height, &offset))
        return NULL;
    if (!((width > 0)  && (height > 0))) 
    ERRMSG("width and height must be > 0");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    if (offset < 1) offset *= MaxRGB;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, 
              AdaptiveThresholdImage(mag, width, height, 
                         offset, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_addnoise_image[] = "out = addnoise(img, type)\n\n"\
" Add random noise to the image of type: 'uniform', 'gaussian', \n"\
"   'multiplicative', 'impulse', 'laplacian', 'poisson'.";
static PyObject *
addnoise_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    char *ntype=NULL;
    int numtype;

    if (!PyArg_ParseTuple(args, "O|s",&obj, &ntype))
        return NULL;

    if (ntype==NULL) ntype="Gaussian";

    if ((numtype=LookupStr(NoiseTypes, ntype)) < 0) 
    ERRMSG3(ntype,"addnoise");
        
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, AddNoiseImage(mag, (NoiseType) numtype,
                           &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_blur_image[] = "out = blur(img, sig{, rad})\n\n"\
" Blur the image with a Gaussian kernel with standard deviation sig and\n"\
"   radius, rad.  Both sigma and rad are in pixel units.  If rad not given then\n"\
"   it will be selected based on sigma.";
static PyObject *
blur_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double rad=0.0, sig;

    if (!PyArg_ParseTuple(args, "Od|d",&obj, &sig, &rad))
        return NULL;
    
    if ((sig <= 0.0) || (rad < 0)) ERRMSG("Sigma and radius must be non-negative");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, BlurImage(mag, rad, sig, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}



static char doc_despeckle_image[] = "out = despeckle(img)\n\n"\
" Despeckle an image while preserving edges.";
static PyObject *
despeckle_image(PyObject *self, PyObject *obj)
{
    PyObject *imobj=NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail; 
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, DespeckleImage(mag, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL;   
}


static char doc_edge_image[] = "out = edge(img{, rad})\n\n"\
" Find edges in an image.  rad defines the radius of the convolution filter.\n"\
"   If rad is not specified a suitable value is chosen.";
static PyObject *
edge_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double rad=0.0;

    if (!PyArg_ParseTuple(args, "O|d",&obj, &rad))
        return NULL;
    
    if ((rad < 0)) ERRMSG("Radius must be non-negative");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, EdgeImage(mag, rad, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_emboss_image[] = "out = emboss(img, sig{, rad})\n\n"\
" Returns an image with a three-dimensional effect.\n"\
"   Convolution with a Gaussian kernel of the radius, rad and standard deviation\n"\
"   sig is done.  If rad is not given, a suitable value is chosen.";
static PyObject *
emboss_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double rad=0.0, sig;

    if (!PyArg_ParseTuple(args, "Od|d",&obj, &sig, &rad))
        return NULL;
    
    if ((sig <= 0.0) || (rad < 0)) ERRMSG("Sigma and radius must be non-negative");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, EmbossImage(mag, rad, sig, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_enhance_image[] = "out = enhance(img)\n\n"\
" Apply a digital filter that improves the quality of a noisy image.";
static PyObject *
enhance_image(PyObject *self, PyObject *obj)
{
    PyObject *imobj=NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail; 
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, EnhanceImage(mag, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL;   
}

static char doc_medianfilter_image[] = "out = medianfilter(img, rad(0.0))\n\n"\
" Replace each pixel by the median in a set of neighboring pixels defined\n"\
"   rad.";
static PyObject *
medianfilter_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double rad=0.0;

    if (!PyArg_ParseTuple(args, "O|d",&obj, &rad))
        return NULL;
    
    if ((rad <= 0.0)) ERRMSG("Radius must be non-negative");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, MedianFilterImage(mag, rad, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_motionblur_image[] = "out = motionblur(img, sig, ang{, rad})\n\n"\
" Blur the image with a Gaussian kernel with standard deviation sig and\n"\
"   radius, rad.  Both sigma and rad are in pixel units.  If rad not given then\n"\
"   it will be selected based on sigma.  Angle gives the angle of the blurring\n"\
"   motion in degrees from verticle.";
static PyObject *
motionblur_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double rad=0.0, sig, ang;

    if (!PyArg_ParseTuple(args, "Odd|d",&obj, &sig, &ang, &rad))
        return NULL;
    
    if ((sig <= 0.0) || (rad < 0)) ERRMSG("Sigma and radius must be non-negative");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, MotionBlurImage(mag, rad, sig, ang, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_reducenoise_image[] = "out = reducenoise(img{, rad})\n\n"\
" Smooths the contours of an image while still preserving edge information.";
static PyObject *
reducenoise_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double rad=0.0;

    if (!PyArg_ParseTuple(args, "O|d",&obj, &rad))
        return NULL;
    
    if ((rad < 0.0)) ERRMSG("Radius must be non-negative");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, ReduceNoiseImage(mag, rad, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_shade_image[] = \
"out = shad(img, azimuth(30.0), elevation(30.0), gray(0))\n\n"\
" Shines a distant light on an image to create a 3-D effect.  \n"\
"   Position of the light is controlled with azimuth and elevation.\n"\
"   Azimuth is measured in degrees off the x axis\n"\
"   Elevation is measured in pixels above the Z axis\n"\
"   If gray is nonzero (default is zero) then return grayscale result.";
static PyObject *
shade_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double azimuth=30.0, elevation=30.0;
    int gray = 0;

    if (!PyArg_ParseTuple(args, "O|ddi",&obj, &azimuth, &elevation, &gray))
        return NULL;
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, ShadeImage(mag, gray, azimuth, 
                        elevation, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_sharpen_image[] = "out = sharpen(img, sig(1), rad(0))\n\n"\
" Sharpen a blurry image.  A Gaussian operator is used with standard\n"\
"   deviation, sig and radius, rad.  If rad is not given it will be\n"\
"   selected using sig.";
static PyObject *
sharpen_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double rad=0.0, sig=1.0;

    if (!PyArg_ParseTuple(args, "O|dd",&obj, &sig, &rad))
        return NULL;
    
    if ((sig <= 0.0) || (rad < 0)) ERRMSG("Sigma and radius must be non-negative");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, SharpenImage(mag, rad, sig, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_spread_image[] = "out = spread(img, rad(3))\n\n"\
" Create a special effect by randomly displacing each pixel in a block\n"\
"   defined by the rad parameter.";
static PyObject *
spread_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    int rad=3;

    if (!PyArg_ParseTuple(args, "Oi",&obj, &rad))
        return NULL;
    
    if ((rad <= 0)) ERRMSG("Radius must be non-negative");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, SpreadImage(mag, rad, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_unsharpmask_image[] = \
"out = unsharpmask(img, sig{, rad, amount, thresh})\n\n"\
" Sharpen an image using Unsharp Masking.\n\n"\
"   A Gaussian kernel with standard deviation, sig, and radius, rad, is used.\n"\
"           If rad is not given a suitable value is chosen.\n"\
"   amount  is the fraction of the difference between the original and the\n"\
"           blur that is added back into the original (default 1.0).\n"\
"   thresh  is the threshold as a fraction of maxRGB needed to apply\n"\
"           the difference amount (default 0.05)";
static PyObject *
unsharpmask_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double rad=0.0, sig;
    double amount, thresh;

    if (!PyArg_ParseTuple(args, "Od|ddd",&obj, &sig, &rad, &amount, &thresh))
        return NULL;
    
    if ((sig <= 0.0) || (rad < 0)) 
      ERRMSG("Sigma and radius must be non-negative");
    if ((thresh < 0) || (thresh > 1.0)) 
      ERRMSG("Threshold should be between 0.0 and 1.0");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, UnsharpMaskImage(mag, rad, sig, 
                              amount, thresh, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL;
}


static char doc_charcoal_image[] = "out = charcoal(img, sig(1.0), rad(0.0))\n\n"\
" Create an edge-highlighted image.";
static PyObject *
charcoal_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double rad=0.0, sig=1.0;

    if (!PyArg_ParseTuple(args, "O|dd",&obj, &sig, &rad))
        return NULL;
    
    if (PyTuple_Size(args) < 3) rad = 3.0*sig;
    if ((sig <= 0.0) || (rad <= 0.0)) ERRMSG("Sigma and radius must be non-negative");
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, CharcoalImage(mag, rad, sig, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_colorize_image[] = "out = colorize(img, color, {R, G, B})\n\n"\
" Blends the color with each pixel in the image. A fraction blend is\n"\
"   specified with R, G, and B.  Control the application of different color\n"\
"   components by specifying a different fraction for each component\n"\
"   (e.g. 0.9, 1.0, 0.1) is 90% red, 100% green, and 10% blue (default 0.25)";
static PyObject *
colorize_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    PyObject *tcolor = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    PixelPacket target;
    double red=0.25, green, blue;
    int N;
    char opacity[MaxTextExtent];

    if (!PyArg_ParseTuple(args, "OO|ddd",&obj, &tcolor, &red, &green, &blue))
        return NULL;
    
    N = PyTuple_Size(args);
    if (N < 5) blue = red;
    if (N < 4) green = red;
    if ((red < 0.0) || (red > 1.0) || \
        (green < 0.0) || (green > 1.0) || \
        (blue < 0.0) || (blue > 1.0))
        ERRMSG("Red, green, and blue blend values must be"\
               " between 0.0 and 1.0");
    red *= 100; blue *= 100; green *= 100;
    if (!set_color_from_obj(&target,tcolor,"color")) goto fail;
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    FormatString(opacity, "%g/%g/%g", red, green, blue);

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, ColorizeImage(mag, opacity, target,
                                                   &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_convolve_image[] = "out = convolve(img, kernel)\n\n"\
" Convolve the image with the arbitrary kernel.\n"\
"   Kernel must be an NxN array where N is odd.";
static PyObject *
convolve_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    PyObject *kernel = NULL;
    PyObject *arrkrn = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    int order;

    if (!PyArg_ParseTuple(args, "OO", &obj, &kernel))
        return NULL;
    
    arrkrn = PyArray_ContiguousFromObject(kernel, PyArray_DOUBLE, 2, 2);
    if (arrkrn == NULL) return NULL;
    order = DIM(arrkrn,0);
    if ((order != DIM(arrkrn,1)) || (order%2 != 1))
        ERRMSG("kernel must be NxN array of doubles with N odd");
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, 
                          ConvolveImage(mag, order, (double *)DATA(arrkrn),
                                        &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    Py_DECREF(arrkrn);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    Py_XDECREF(arrkrn);
    return NULL; 
}


static char doc_implode_image[] = "out = implode(img, amount(0.50))\n\n"\
" Implode image pixels by the specified factor.";
static PyObject *
implode_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double amount=0.50;

    if (!PyArg_ParseTuple(args, "O|d",&obj, &amount))
        return NULL;
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, ImplodeImage(mag, amount,  &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_morph_images[] = "out = morph(img, frames) \n\n"\
" Morph images contained in obj into each other using frames\n"\
"   in-between images.\n"\
"   obj must be an image sequence of at least two.";
static PyObject *
morph_images(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    PyMImageObject *new=NULL;
    long frames;

    if (!PyArg_ParseTuple(args, "Ol", &obj, &frames))
        return NULL;
    if (frames < 0) ERRMSG("number of frames must be > 0");

    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = MorphImages(ASIM(imobj)->ims, frames, &exception);
    CHECK_ERR;
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_oilpaint_image[] = "out = oilpaint(img, rad(3.0))\n\n"\
" Apply a special effect filter that simulates an oil painting.\n"\
"   Each pixel is replaced by the most frequent color occurring in\n"\
"   a circular region defined by rad.";
static PyObject *
oilpaint_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double rad=3.0;

    if (!PyArg_ParseTuple(args, "O|d",&obj, &rad))
        return NULL;
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, OilPaintImage(mag, rad,  &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_stegano_image[] = "out = stegano(img, mark)\n\n"\
" Hide a digital watermark within the image.\n"\
"   If mark is an image list, only uses the first image.\n";
static PyObject *
stegano_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    PyObject *mark = NULL;
    PyObject *imark = NULL;
    Image *mag;
    PyMImageObject *new=NULL;

    if (!PyArg_ParseTuple(args, "OO",&obj, &mark))
        return NULL;
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    if ((imark = mimage_from_object(mark))==NULL) goto fail;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, SteganoImage(mag, ASIM(imark)->ims,
                                                  &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    Py_DECREF(imark);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    Py_XDECREF(imark);
    return NULL; 
}


static char doc_stereo_image[] = "out = stereo(imga,imgb)\n\n"\
" Combines two images and produces a single image that is the\n"\
"   composite of a left and right image of a stereo pair.\n"\
"   Special red-green stereo glasses are required to view this effect\n"\
"   Both imga and imgb must have the same length.\n";
static PyObject *
stereo_image(PyObject *self, PyObject *args)
{
    PyObject *imga = NULL;
    PyObject *obja = NULL;
    PyObject *objb = NULL;
    PyObject *imgb = NULL;
    Image *ima, *imb;
    PyMImageObject *new=NULL;
    int Na, Nb;

    if (!PyArg_ParseTuple(args, "OO",&obja, &objb))
        return NULL;
    
    if ((imga = mimage_from_object(obja))==NULL) return NULL;
    if ((imgb = mimage_from_object(objb))==NULL) goto fail;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    imb = ASIM(imgb)->ims;
    ima = ASIM(imga)->ims;
    Na = GetImageListLength(ima);
    Nb = GetImageListLength(imb);
    if (Na != Nb) ERRMSG("Both image sequences must have the same length");
    for ( ; ima && imb; ima=ima->next, imb=imb->next) {
    AppendImageToList(&new->ims, StereoImage(ima, imb, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imga);
    Py_DECREF(imgb);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imga);
    Py_XDECREF(imgb);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_swirl_image[] = "out = swirl(img, deg)\n\n"\
" Swirl the pixels about the center of the image.\n"\
"   The parameter deg indicates the angle of the arc through which each\n"\
"   pixel is moved.  You get a more dramatic effect as the degrees move\n"\
"   from 1 to 360.";
static PyObject *
swirl_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double deg;

    if (!PyArg_ParseTuple(args, "Od",&obj, &deg))
        return NULL;
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, SwirlImage(mag, deg,  &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_wave_image[] = "out = wave(img, amp(25.0), length(15.0))\n\n"\
" Create a 'ripple' effect in the image by shifting the pixels\n"\
"   vertically along a sine wave whose amplitude and wavelength is\n"\
"   is specified by the given parameters.";
static PyObject *
wave_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    double amp=25.0, length=15.0;

    if (!PyArg_ParseTuple(args, "O|dd",&obj, &amp, &length))
        return NULL;
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, WaveImage(mag, amp, length, &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_border_image[] = \
"out = border(img, width, height, bordercolor=)\n\n"\
" Surround the image with a border of the color defined by the\n"\
"   bordercolor attribute of the img.  The width and height of the border\n"\
"   are given as parameters.  If any keyword arguments are present\n"\
"   then set the corresponding attribute of img before adding the\n"\
"   border.";
static PyObject *
border_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    int width, height;
    RectangleInfo border_info;

    if (!PyArg_ParseTuple(args, "Oii",&obj, &width, &height))
        return NULL;
      
    if ((width < 0) || (height < 0)) 
        ERRMSG("Width and height must be >= 0");
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    
    if (kwds != NULL) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwds, &pos, &key, &value)) {
            if (mimage_setattr(ASIM(imobj), PyString_AS_STRING(key), 
                               value) == -1) goto fail;
        }
    }
    border_info.width = width;
    border_info.height = height;
    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
    AppendImageToList(&new->ims, BorderImage(mag, &border_info, 
                                                 &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_frame_image[] = \
"out = frame(img, width(25), height(25), inner(6), outer(6), mattecolor=)\n\n"\
" Add a simulated three-dimensional border around the image.  The color\n"\
"   of the border is defined by the mattecolor attribute of the image.\n"\
"   The width and height parameters specify the number of pixels in the\n"\
"   vertical and horizontal sides of the frame.  The inner and outer\n"\
"   parameters indicate the width of the inner and outer shadows of the\n"\
"   frame.  Any keywords are interpreted as attributes to set in img \n"\
"   before framing.";
static PyObject *
frame_image(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    Image *mag;
    PyMImageObject *new=NULL;
    int width=25, height=25;
    int inner=6, outer=6;
    FrameInfo frame_info;

    if (!PyArg_ParseTuple(args, "O|iiii",&obj, &width, &height, &inner, 
                          &outer)) return NULL;
      
    if ((width < 0) || (height < 0)) 
        ERRMSG("Width and height must be >= 0");

    if ((inner < 0) || (outer < 0)) 
        ERRMSG("Inner and outer must be >= 0");
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;
    
    if (kwds != NULL) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwds, &pos, &key, &value)) {
            if (mimage_setattr(ASIM(imobj), PyString_AS_STRING(key), 
                               value) == -1) goto fail;
        }
    }
    frame_info.inner_bevel = inner;
    frame_info.outer_bevel = outer;
    frame_info.x = width;
    frame_info.y = height;    
    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = NewImageList();
    for (mag=ASIM(imobj)->ims; mag; mag=mag->next) {
        frame_info.width = 2*width + mag->columns;
        frame_info.height = 2*height + mag->rows;
    AppendImageToList(&new->ims, FrameImage(mag, &frame_info, 
                                                 &exception));
        CHECK_ERR;
    }
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}

static char doc_append_image[] = "out = append(img, stack(0))\n\n"\
" Appends an image list to each other to make one image\n"\
"   If stack is true, then append top-to-bottom.  Otherwsie left-to-right.\n";
static PyObject *
append_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    PyMImageObject *new=NULL;
    int stack=0;

    if (!PyArg_ParseTuple(args, "O|i",&obj, &stack))
        return NULL;
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = AppendImages(ASIM(imobj)->ims, stack, &exception);
    CHECK_ERR;
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


static char doc_average_image[] = "out = average(img)\n\n"\
" Averages a set of images together.  Each image in the set must have the\n"\
"   width and height.";
static PyObject *
average_image(PyObject *self, PyObject *args)
{
    PyObject *imobj = NULL;
    PyObject *obj = NULL;
    PyMImageObject *new=NULL;

    if (!PyArg_ParseTuple(args, "O",&obj))
        return NULL;
    
    if ((imobj = mimage_from_object(obj))==NULL) return NULL;

    new = PyObject_New(PyMImageObject, &MImage_Type);
    if (new == NULL) goto fail;
    new->ims = AverageImages(ASIM(imobj)->ims, &exception);
    CHECK_ERR;
    Py_DECREF(imobj);
    return (PyObject *)new;

 fail:
    Py_XDECREF(imobj);
    Py_XDECREF(new);
    return NULL; 
}


#if 0
/* Is this function needed?
   It has to be implemented differently with GraphicsMagick:
   GetColorInfoArray or GetColorList which both work on a pattern and not on a FILE
*/
static char doc_listcolors[] = \
"listcolors(file(stdout))\n\n"\
" List the named colors to the file.";
static PyObject *
listcolors(PyObject *self, PyObject *args)
{
    PyObject *fileobj=NULL;
    FILE *fid;
    
    if (!PyArg_ParseTuple(args, "|O", &fileobj)) return NULL;
    
    if (fileobj==NULL) fid = stdout;
    else fid = PyFile_AsFile(fileobj);
    if (fid == NULL) ERRMSG("File must be a valid file object");
    if (!ListColorInfo(fid, &exception))
        CHECK_ERR;
    Py_INCREF(Py_None);
    return Py_None;
    
 fail:
    return NULL;

}
#endif


static char doc_name2color[] = \
"name2color(name)\n\n"\
" Returns the red, green, blue, and opacity intensities for a given color name.";
static PyObject *
name2color(PyObject *self, PyObject *args)
{
    char *name;
    PixelPacket color;
    
    if (!PyArg_ParseTuple(args, "s", &name)) return NULL;

    if (!QueryColorDatabase(name, &color, &exception))
        CHECK_ERR;
    return Py_BuildValue("llll",color.red,color.green,color.blue,color.opacity);
        
 fail:
    return NULL;

}


static char doc_color2name[] = \
"color2name(color, standard('xpm'))\n\n"\
" Returns a named color for the given color intensity.  If an exact match\n"\
"   is not found, a hex value is returned instead.  Adhere to given standard \n"\
"   (X11, XPM, SVG, All)";
static PyObject *
color2name(PyObject *self, PyObject *args)
{
    PyObject *tcolor;
    char name[MaxTextExtent];
    char *standard=NULL;
    PixelPacket color;
    char imdata[] = {0};
    Image *im=NULL;
    ComplianceType comp;
    
    if (!PyArg_ParseTuple(args, "O|s", &tcolor, &standard)) return NULL;
    if (standard == NULL) 
        comp = XPMCompliance;
    else if (strEQcase(standard,"xpm"))
        comp = XPMCompliance;
    else if (strEQcase(standard, "x11")) 
        comp = X11Compliance;
    else if (strEQcase(standard, "svg"))
        comp = SVGCompliance;
    else 
        ERRMSG("Undefined compliance");
    if (!set_color_from_obj(&color, tcolor, "color2name")) return NULL;
    im = ConstituteImage(1,1,"I",CharPixel, imdata, &exception);
    CHECK_ERR;
    if (!QueryColorname(im, &color, comp, name, &exception))
        CHECK_ERR;
    DestroyImage(im);
    return Py_BuildValue("s",name);
        
 fail:
    if (im) DestroyImage(im);
    return NULL;

}


/* Think about changing all METH_VARARGS to add KEYWORDS which set
   attributes of image before application of method */

static PyMethodDef magick_methods[] = {
    {"image", (PyCFunction)magick_new_image, METH_VARARGS|METH_KEYWORDS,
     doc_image},
    {"newdc", (PyCFunction)magick_new_draw, METH_VARARGS|METH_KEYWORDS, 
     "Create a new drawing context with IM-loaded defaults."},
    {"average", (PyCFunction)average_image, METH_VARARGS, doc_average_image},
    {"append", (PyCFunction)append_image, METH_VARARGS, doc_append_image},
    {"magnify", (PyCFunction)magnify_image, METH_O, doc_magnify_image},
    {"minify", (PyCFunction)minify_image, METH_O, doc_minify_image},
    {"resize", (PyCFunction)resize_image, METH_VARARGS, doc_resize_image},
    {"sample", (PyCFunction)sample_image, METH_VARARGS, doc_sample_image},
    {"scale", (PyCFunction)scale_image, METH_VARARGS, doc_scale_image},
    {"thumbnail", (PyCFunction)thumbnail_image, METH_VARARGS, doc_thumbnail_image},
    {"chop", (PyCFunction)chop_image, METH_VARARGS, doc_chop_image},
    {"crop", (PyCFunction)crop_image, METH_VARARGS, doc_crop_image},
    {"coalesce", (PyCFunction)coalesce_images, METH_O, doc_coalesce_images},
    {"deconstruct", (PyCFunction)deconstruct_images, METH_O, doc_deconstruct_images},
    {"flatten", (PyCFunction)flatten_images, METH_O, doc_flatten_images},
    {"flip", (PyCFunction)flip_image, METH_O, doc_flip_image},
    {"flop", (PyCFunction)flop_image, METH_O, doc_flop_image},
    {"mosaic", (PyCFunction)mosaic_images, METH_O, doc_mosaic_images},
    {"roll", (PyCFunction)roll_image, METH_VARARGS, doc_roll_image},
    {"shave", (PyCFunction)shave_image, METH_VARARGS, doc_shave_image},
    {"affine", (PyCFunction)affine_image, METH_VARARGS|METH_KEYWORDS, 
     doc_affine_image},
    {"rotate", (PyCFunction)rotate_image, METH_VARARGS|METH_KEYWORDS, 
     doc_rotate_image},
    {"shear", (PyCFunction)shear_image, METH_VARARGS|METH_KEYWORDS,
     doc_shear_image},
    {"lat", (PyCFunction)adaptive_image, METH_VARARGS, doc_adaptive_image},
    {"addnoise", (PyCFunction)addnoise_image, METH_VARARGS, doc_addnoise_image},
    {"blur", (PyCFunction)blur_image, METH_VARARGS, doc_blur_image},
    {"despeckle", (PyCFunction)despeckle_image, METH_O, doc_despeckle_image},
    {"edge", (PyCFunction)edge_image, METH_VARARGS, doc_edge_image},
    {"emboss", (PyCFunction)emboss_image, METH_VARARGS, doc_emboss_image},
    {"enhance", (PyCFunction)enhance_image, METH_O, doc_enhance_image},
    {"medianfilter", (PyCFunction)medianfilter_image, METH_VARARGS, 
     doc_medianfilter_image},
    {"motionblur", (PyCFunction)motionblur_image, METH_VARARGS, 
     doc_motionblur_image},
    {"reducenoise", (PyCFunction)reducenoise_image, METH_VARARGS, 
     doc_reducenoise_image},
    {"shade", (PyCFunction)shade_image, METH_VARARGS, doc_shade_image},
    {"sharpen", (PyCFunction)sharpen_image, METH_VARARGS, doc_sharpen_image},
    {"spread", (PyCFunction)spread_image, METH_VARARGS, doc_spread_image},
    {"unsharpmask", (PyCFunction)unsharpmask_image, METH_VARARGS, 
     doc_unsharpmask_image},
    {"charcoal", (PyCFunction)charcoal_image, METH_VARARGS, doc_charcoal_image},
    {"colorize", (PyCFunction)colorize_image, METH_VARARGS, doc_colorize_image},
    {"convolve", (PyCFunction)convolve_image, METH_VARARGS, doc_convolve_image},
    {"implode", (PyCFunction)implode_image, METH_VARARGS, doc_implode_image},
    {"morph", (PyCFunction)morph_images, METH_VARARGS, doc_morph_images},
    {"oilpaint", (PyCFunction)oilpaint_image, METH_VARARGS, doc_oilpaint_image},
    {"stegano", (PyCFunction)stegano_image, METH_VARARGS, doc_stegano_image},
    {"stereo", (PyCFunction)stereo_image, METH_VARARGS, doc_stereo_image},
    {"swirl", (PyCFunction)swirl_image, METH_VARARGS, doc_swirl_image},    
    {"wave", (PyCFunction)wave_image, METH_VARARGS, doc_wave_image},
    {"border", (PyCFunction)border_image, METH_VARARGS|METH_KEYWORDS,
     doc_border_image},
    {"frame", (PyCFunction)frame_image, METH_VARARGS|METH_KEYWORDS,
     doc_frame_image},
#if 0
    {"listcolors", (PyCFunction)listcolors, METH_VARARGS, doc_listcolors},
#endif
    {"name2color", (PyCFunction)name2color, METH_VARARGS, doc_name2color},
    {"color2name", (PyCFunction)color2name, METH_VARARGS, doc_color2name},
    {NULL, NULL, 0, NULL}
};


DL_EXPORT(void)
initmagick(void) 
{
    PyObject *m, *d, *aint;
    char str[2] = {0, 0};
    PyArray_Descr *descr;
    Quantum mRGB=MaxRGB;

    MImage_Type.ob_type = &PyType_Type;
    import_array()
        
    InitializeMagick("MImage");

    GetExceptionInfo(&exception);
    SetWarningHandler(PyMagickErrorHandler);
    SetErrorHandler(PyMagickErrorHandler);
    SetFatalErrorHandler(PyMagickErrorHandler);

    _qsize = sizeof(Quantum);
    if (_qsize == 1) ptype = PyArray_UBYTE;
    else if (_qsize == 2) ptype = PyArray_USHORT;
    else if (_qsize == 4) ptype = PyArray_UINT;
    else {
        fprintf(stderr, "Unknown Quantum Depth in ImageMagick Library.");
    }
    
    m = Py_InitModule("magick", magick_methods);
    d = PyModule_GetDict(m);
    PyMagickError = PyErr_NewException("magick.error",NULL,NULL);
    PyDict_SetItemString (d, "error", PyMagickError);
    aint = PyInt_FromLong(_qsize);
    PyDict_SetItemString (d, "quantum", aint);
    Py_DECREF(aint);
    aint = PyArray_FromDims(0,NULL,ptype);
    ASARR(aint)->flags |= SAVESPACE;
    memcpy(ASARR(aint)->data,&mRGB,sizeof(Quantum));
    PyDict_SetItemString (d, "MaxRGB", aint);
    Py_DECREF(aint);
    aint = PyInt_FromLong(MaxRGB);
    PyDict_SetItemString(d, "iMaxRGB", aint);
    Py_DECREF(aint);
    descr = PyArray_DescrFromType(ptype);
    if (descr != NULL) {
        str[0] = descr->type;
        aint = PyString_FromString(str);
        PyDict_SetItemString (d, "qtype", aint);
        Py_DECREF(aint);
    }
    PyDict_SetItemString(d, "mimagetype", (PyObject *)&MImage_Type);

    if (PyErr_Occurred()) {
        Py_FatalError ("Cannot initialize module _magick");
    }
}


/* MontageImage */
