/********************************** <lalVerbatim file="FlatMeshCV">
Author: Creighton, T. D.
$Id$
**************************************************** </lalVerbatim> */

/********************************************************** <lalLaTeX>

\subsection{Module \texttt{FlatMesh.c}}
\label{ss:FlatMesh.c}

Places a mesh of templates on an $n$-dimensional rectilinear parameter
space.

\subsubsection*{Prototypes}
\vspace{0.1in}
\input{FlatMeshCP}
\idx{LALCreateFlatMesh()}
\idx{LALRectIntersect()}

\subsubsection*{Description}

\verb@LALFlatMesh()@ lays out a mesh on an $n$-dimensional parameter
space according to the method discussed in \verb@FlatMesh.h@.  It
first creates a unit-cube lattice in $\mathsf{y}^a$ over a rectilinear
region large enough to cover the search area completely, and then
calls the routine \verb@params->intersection()@ to restrict the list
to those mesh points that lie inside the search region.  (If this
function pointer is \verb@NULL@, then no restriction is done.)  The
list of mesh point locations is returned in \verb@**mesh@; see
\verb@FlatMesh.h@ for a description of the fields in \verb@*params@.

\verb@LALRectIntersect()@ is a simple routine that restricts a
parameter mesh \verb@*mesh@ to a rectilinear region defined by the
first two vectors $\mathsf{x}^a_{(1)}$, $\mathsf{x}^a_{(2)}$ in the
sequence \verb@*controlPoints@ (other vectors in the sequence are
ignored): the region is
$[x^1_{(1)},x^1_{(2)}]\otimes\cdots\otimes[x^n_{(1)},x^n_{(2)}]$.  In
general the values of \verb@mesh->length@ and the pointer
\verb@mesh->data@ will be changed when the dataset is reduced.

\subsubsection*{Algorithm}

The algorithm in \verb@LALFlatMesh()@ initially lays a mesh over a
region much larger than is ultimately required.  First, in the
$\mathsf{x}^a$ coordinate system, the minimum and maximum parameter
values \verb@params->xMin@ and \verb@params->xMax@ are used to define
a rectilinear region $[x^1_\mathrm{min},x^1_\mathrm{max}]\otimes\cdots
\otimes[x^n_\mathrm{min},x^n_\mathrm{max}]$ that is a superset of the
desired search region.  Upon transformation to the $\mathsf{y}^a$
coordinate system, this superset is now a parallelogram; the algorithm
then defines a super-superset
$[y^1_\mathrm{min},y^1_\mathrm{max}]\otimes\cdots
\otimes[y^n_\mathrm{min},y^n_\mathrm{max}]$ that completely encloses
the parallelogram.  A unit-cube mesh is placed on this super-superset,
transformed back to $\mathsf{x}^a$ coordinates, and then passed to
\verb@params->intersect@ to restrict it to the region of interest.

Obviously if the desired region in $\mathsf{x}^a$ coordinates is
highly elongated along a non-principal axis, and if the transformation
from $\mathsf{x}^a$ to $\mathsf{y}^a$ involves both rotation and
elongation of principal axes, then the final intersection may
eliminate all but a tiny fraction of the mesh points generated
initially.  However, laying out mesh points is rarely expected to be
the dominant computational cost in any analysis, so some inefficiency
can be tolerated.  Furthermore, since the definition of signal
parameters $\mathsf{x}^a$ is somewhat arbitrary, one can cleverly
choose an initial coordinate system that is aligned with the preferred
axes of the desired search area, or with the preferred axes of the
mismatch metric, whichever will improve performance the most.

\verb@LALRectIntersect()@ performs the dataset reduction ``in place'',
within the memory block allocated to \verb@mesh->data@, and then uses
\verb@LALRealloc()@ to reduce the memory storage accordingly.  In most
cases this will mean allocating a new block, copying the reduced
dataset over, and then freeing the old block.  However, when debugging
is turned off, \verb@LALRealloc()@ reverts to the lower-level memory
management function \verb@realloc()@; this routine can often simply
deallocate the excess memory without having to touch the reduced
dataset at the start of the block.

\subsubsection*{Uses}
\begin{verbatim}
lalDebugLevel
LALInfo()                   LALPrintError()
LALRealloc()
LALMalloc()                 LALFree()
LALSCreateVectorSequence()  LALSDestroyVectorSequence()
\end{verbatim}

\subsubsection*{Notes}

\vfill{\footnotesize\input{FlatMeshCV}}

******************************************************* </lalLaTeX> */

#include <math.h>
#include "LALStdlib.h"
#include "AVFactories.h"
#include "SeqFactories.h"
#include "FlatMesh.h"

NRCSID(FLATMESHC,"$Id$");

/* Local function prototypes. */
static int
Superset( REAL4 *mesh, REAL4 *matrix, REAL4 *yMin, UINT4 *nMax,
	  UINT4 dim );

static void
Transform( REAL4 *vectorOut, REAL4 *vectorIn, REAL4 *matrix,
	   UINT4 dim );

/* <lalVerbatim file="FlatMeshCP"> */
void
LALCreateFlatMesh( LALStatus           *stat,
		   REAL4VectorSequence **mesh,
		   FlatMeshParamStruc  *params )
{ /* </lalVerbatim> */
  INT4 code;   /* subroutine return code */
  UINT4 dim;   /* dimension of parameter space */
  UINT4 *nMax; /* max. no. of mesh points in each y direction */
  REAL4 *yMin; /* minimum value of each y-coordinate */

  INITSTATUS( stat, "LALCreateFlatMesh", FLATMESHC );
  ATTATCHSTATUSPTR( stat );

  /* Check that all parameters exist. */
  ASSERT( mesh, stat, FLATMESHH_ENUL, FLATMESHH_MSGENUL );
  ASSERT( params, stat, FLATMESHH_ENUL, FLATMESHH_MSGENUL );
  ASSERT( params->matrix, stat, FLATMESHH_ENUL, FLATMESHH_MSGENUL );
  ASSERT( params->matrix->data, stat, FLATMESHH_ENUL,
	  FLATMESHH_MSGENUL );
  ASSERT( params->matrixInv, stat, FLATMESHH_ENUL,
	  FLATMESHH_MSGENUL );
  ASSERT( params->matrixInv->data, stat, FLATMESHH_ENUL,
	  FLATMESHH_MSGENUL );
  ASSERT( params->xMin, stat, FLATMESHH_ENUL, FLATMESHH_MSGENUL );
  ASSERT( params->xMin->data, stat, FLATMESHH_ENUL,
	  FLATMESHH_MSGENUL );
  ASSERT( params->xMax, stat, FLATMESHH_ENUL, FLATMESHH_MSGENUL );
  ASSERT( params->xMax->data, stat, FLATMESHH_ENUL, FLATMESHH_MSGENUL );
  /* params->controlPoints and its fields are only used by the
     function params->intersection(), which should check these fields
     itself. */

  /* Check that output does not already exist. */
  ASSERT( !(*mesh), stat, FLATMESHH_EOUT, FLATMESHH_MSGEOUT );

  /* Make sure that dimensions all agree. */
  dim = params->matrix->length;
  ASSERT( params->matrix->vectorLength==dim, stat, FLATMESHH_EDIM,
	  FLATMESHH_MSGEDIM );
  ASSERT( params->matrixInv->length==dim, stat, FLATMESHH_EDIM,
	  FLATMESHH_MSGEDIM );
  ASSERT( params->matrixInv->vectorLength==dim, stat, FLATMESHH_EDIM,
	  FLATMESHH_MSGEDIM );
  ASSERT( params->xMin->length==dim, stat, FLATMESHH_EDIM,
	  FLATMESHH_MSGEDIM );
  ASSERT( params->xMax->length==dim, stat, FLATMESHH_EDIM,
	  FLATMESHH_MSGEDIM );

  /* Allocate local memory. */
  nMax = (UINT4 *)LALMalloc( dim*sizeof(UINT4) );
  if ( !nMax ) {
    ABORT( stat, FLATMESHH_EMEM, FLATMESHH_MSGEMEM );
  }
  yMin = (REAL4 *)LALMalloc( dim*sizeof(REAL4) );
  if ( !yMin ) {
    LALFree( nMax );
    ABORT( stat, FLATMESHH_EMEM, FLATMESHH_MSGEMEM );
  }

  /* Transform the corners of the covering rectangle to find the
     minimum value of y and number of mesh points required in each y
     direction. */
  {
    UINT4 i, j;                    /* indecies */
    UINT4 nVertex = pow( 2, dim ); /* number of vertecies */
    REAL4 *x, *y, *yMax;           /* local temporary memory */

    /* Allocate additional local memory. */
    x = (REAL4 *)LALMalloc( dim*sizeof(REAL4) );
    if ( !x ) {
      LALFree( nMax );
      LALFree( yMin );
      ABORT( stat, FLATMESHH_EMEM, FLATMESHH_MSGEMEM );
    }
    y = (REAL4 *)LALMalloc( dim*sizeof(REAL4) );
    if ( !y ) {
      LALFree( nMax );
      LALFree( yMin );
      LALFree( x );
      ABORT( stat, FLATMESHH_EMEM, FLATMESHH_MSGEMEM );
    }
    yMax = (REAL4 *)LALMalloc( dim*sizeof(REAL4) );
    if ( !yMax ) {
      LALFree( nMax );
      LALFree( yMin );
      LALFree( x );
      LALFree( y );
      ABORT( stat, FLATMESHH_EMEM, FLATMESHH_MSGEMEM );
    }

    /* Place the first vertex in y-space. */
    Transform( yMin, params->xMin->data, params->matrixInv->data,
	       dim );
    memcpy( yMax, yMin, dim*sizeof(REAL4) );

    /* Compute remaining vertecies in y-space. */
    for ( i = 1; i < nVertex; i++ ) {
      for ( j = 0; j < dim; j++ )
	if ( i/(INT4)( pow( 2, j ) ) % 2 )
	  x[j] = params->xMax->data[j];
	else
	  x[j] = params->xMin->data[j];
      Transform( y, x, params->matrixInv->data, dim );
      for ( j = 0; j < dim; j++ )
	if ( y[j] < yMin[j] )
	  yMin[j] = y[j];
	else if ( y[j] > yMax[j] )
	  yMax[j] = y[j];
    }

    /* Compute nMax from yMax and yMin. */
    for ( j = 0; j < dim; j++ )
      nMax[j] = (UINT4)( yMax[j] - yMin[j] ) + 1;

    /* Free additional local memory. */
    LALFree( x );
    LALFree( y );
    LALFree( yMax );
  }

  /* Allocate the mesh covering the superset. */
  {
    INT4 j = dim;              /* an index */
    CreateVectorSequenceIn in; /* creation parameter structure */

    in.vectorLength = dim;
    in.length = 1;
    while ( j-- )
      in.length *= nMax[j];
#ifndef NDEBUG
    if ( lalDebugLevel&LALINFO ) {
      LALInfo( stat, "Placing search points on parameter superset" );
      LALPrintError( "\t%u search points to be placed\n", in.length );
    }
#endif
    LALSCreateVectorSequence( stat->statusPtr, mesh, &in );
    BEGINFAIL( stat ) {
      LALFree( yMin );
      LALFree( nMax );
    } ENDFAIL( stat );
  }

  /* Assign a mesh covering the superset, and free local memory. */
  code = Superset( (*mesh)->data, params->matrix->data, yMin,
		   nMax, dim );
  LALFree( yMin );
  LALFree( nMax );
  if ( code ) {
    TRY( LALSDestroyVectorSequence( stat->statusPtr, mesh ), stat );
    ABORT( stat, FLATMESHH_EMEM, FLATMESHH_MSGEMEM );
  }

  LALInfo( stat, "Placement complete" );

  /* Restrict the mesh to the actual search area (if specified). */
  if ( params->intersection ) {
    params->intersection( stat->statusPtr, *mesh,
			  params->controlPoints );
    BEGINFAIL( stat )
      TRY( LALSDestroyVectorSequence( stat->statusPtr, mesh ), stat );
    ENDFAIL (stat );
  }

  /* Done. */
  DETATCHSTATUSPTR( stat );
  RETURN( stat );
}


static void
Transform( REAL4 *vectorOut, REAL4 *vectorIn, REAL4 *matrix,
	   UINT4 dim )
{
  INT4 i = dim;
  matrix += dim*dim;
  while ( i-- ) {
    INT4 j = dim;
    vectorOut[i] = 0;
    while ( j-- )
      vectorOut[i] += *(--matrix)*vectorIn[j];
  }
  return;
}


static int
Superset( REAL4 *mesh, REAL4 *matrix, REAL4 *yMin, UINT4 *nMax,
	  UINT4 dim )
     /* This routine sweeps through all mesh points on a unit-cube
        lattice covering the dim-dimensional rectilinear volume with
        corners at yMin and yMin+nMax.  For each y coordinate, it
        cormputes the corresponding x coordinate.  It returns 0 when
        done, or 1 if local memory allocation failed. */
{
  UINT4 i;      /* dimension index */
  UINT4 *n;     /* current vector position relative to yMin */
  REAL4 *x, *y; /* current position in x and y coordinates */

  /* Allocate local memory. */
  n = (INT4 *) LALMalloc( dim*sizeof( INT4 ) );
  if ( !n )
    return 1;
  x = (REAL4 *) LALMalloc( dim*sizeof( REAL4 ) );
  if ( !x ) {
    LALFree( n );
    return 1;
  }
  y = (REAL4 *) LALMalloc( dim*sizeof( REAL4 ) );
  if ( !y ) {
    LALFree( n );
    LALFree( x );
    return 1;
  }
  for ( i = 0; i < dim; i++ )
    n[i] = nMax[i];


  /* Step through all integer dim-tuples from nMax to (1,...,1). */
  while (1) {

    /* Find the x-position of the current mesh point. */
    for ( i = 0; i < dim; i++ )
      y[i] = yMin[i] + (REAL4)( n[i] - 1 );
    Transform( x, y, matrix, dim );
    for ( i = 0; i < dim; i++ )
      *(mesh++) = x[i];

    /* Find the next mesh point. */
    i = 0;
    while ( i < dim )
      if ( --n[i] )
	i = dim;
      else if ( i == dim - 1 ) {
	LALFree( n );
	LALFree( x );
	LALFree( y );
	return 0;
      } else {
	n[i] = nMax[i];
	i++;
      }

  }
  /* This loop never ends, but eventually will exit from within. */
}


/* <lalVerbatim file="FlatMeshCP"> */
void
LALRectIntersect( LALStatus           *stat,
		  REAL4VectorSequence *mesh,
		  REAL4VectorSequence *controlPoints )
{ /* </lalVerbatim> */
  UINT4 j, k, l;               /* indecies */
  UINT4 dim;                   /* dimension of parameter space */
  REAL4 *x1, *x2, *xIn, *xOut; /* local vectors */
  /* x1 and x2 point to opposite corners of the rectangular region.
     xIn points to the current mesh point being checked, while xOut
     points to the memory location where it will be saved if it lies
     inside the region.  Since the restriction is being done in-place,
     both xIn and xOut will point to the same contiguous block of
     memory. */

  INITSTATUS( stat, "LALRectIntersect", FLATMESHC );

  /* Check that all parameters exist. */
  ASSERT( mesh, stat, FLATMESHH_ENUL, FLATMESHH_MSGENUL );
  ASSERT( mesh->data, stat, FLATMESHH_ENUL, FLATMESHH_MSGENUL );
  ASSERT( controlPoints, stat, FLATMESHH_ENUL, FLATMESHH_MSGENUL );
  ASSERT( controlPoints->data, stat, FLATMESHH_ENUL,
	  FLATMESHH_MSGENUL );

  /* Check that dimensions are consistent, and that there are enough
     control points (at least two). */
  dim = mesh->vectorLength;
  ASSERT( controlPoints->vectorLength==dim, stat, FLATMESHH_EDIM,
	  FLATMESHH_MSGEDIM );
  ASSERT( controlPoints->length>=2, stat, FLATMESHH_ELEN,
	  FLATMESHH_MSGELEN );

  /* Assign the two corner points. */
  x1 = controlPoints->data;
  x2 = x1 + dim;

  /* For each point in the mesh, check whether all of its coordinates
     xIn[j] lie on or between x1[j] and x2[j].  If so, copy it to
     xOut; otherwise, leave it in place (where it will eventually be
     overwritted or freed). */
  l = 0;
  xOut = mesh->data;
  for ( k=0, xIn=mesh->data; k < mesh->length; k++, xIn+=dim ) {
    BOOLEAN inside = 1;
    for ( j = 0; j < dim; j++ )
      inside &= ( ( xIn[j] - x1[j] )*( xIn[j] - x2[j]) <= 0 );
    if ( inside ) {
      memcpy( xOut, xIn, dim*sizeof(REAL4) );
      l++;
      xOut += dim;
    }
  }

  /* Change mesh->length and reallocate mesh->data down to the reduced
     size.  Note that this will usually change the memory location
     pointed to by mesh->data.  (However, if lalDebugLevel is zero, or
     LALMalloc.c is compiled with debugging off, then LALRealloc()
     reverts to the C realloc() function, which will typically change
     the size of the block allocated to mesh->data without having to
     change the block location.) */
  mesh->length = l;
  mesh->data = (REAL4 *)LALRealloc( mesh->data, l*dim*sizeof(REAL4) );
  if ( !mesh->data ) {
    ABORT( stat, FLATMESHH_EMEM, FLATMESHH_MSGEMEM );
  }

#ifndef NDEBUG
  if ( lalDebugLevel&LALINFO ) {
    LALInfo( stat, "Search points restricted to rectangualr area" );
    LALPrintError( "\t%u search points left after restriction\n", l );
  }
#endif

  /* That is all! */
  RETURN( stat );
}
