#if 0  /* autodoc block */

<lalVerbatim file="FindRootCV">
$Id$
</lalVerbatim>

<lalLaTeX>
\subsection{Module \texttt{FindRoot.c}}
\label{ss:FindRoot.c}

Functions for root finding.

\subsubsection*{Prototypes}
\vspace{0.1in}
\input{FindRootCP}
\idx{LALSBracketRoot()}
\idx{LALDBracketRoot()}
\idx{LALSBisectionFindRoot()}
\idx{LALDBisectionFindRoot()}

\subsubsection*{Description}

The routine \verb+LALSBracketRoot()+ expands the specified domain until a root
is contained.  The routine \verb+LALDBracketRoot()+ is the same but for a
double-precision function.

The routine \verb+LALSFindRoot()+ bisects the domain (which must contain one
root) until the root is found with the desired accuracy.  The routine
\verb+LALDFindRoot()+ is the same but for a double-precision function.

\subsubsection*{Operating Instructions}

Suppose we want to find the root of the function $y = F(x;y_0) = y_0 + x^2$.
Define the function:
\begin{verbatim}
static void F( LALStatus *status, REAL4 *y, REAL4 x, void *y0 )
{
  INITSTATUS( status, "F", "Function F()" );
  ASSERT( y0, status, 1, "Null pointer" );
  *y = *(REAL4 *)y0 + x*x;
  RETURN( status );
}
\end{verbatim}

Then use the following code to bracket and find the root $x_0=1$ where
$F(x_0;y_0=-1)=0$:
\begin{verbatim}
static LALStatus status;
SFindRootIn      input;
REAL4            y0;
REAL4            x0;

y0             = -1;
input.function = F;
input.xmin     = 0.1;
input.xmax     = 0.2;
input.xacc     = 1e-5;

/* expand domain until a root is bracketed */
LALSBracketRoot( &status, &input, &y0 );

/* bisect domain until root is found */
LALSBisectionFindRoot( &status, &x0, &input, &y0 );
\end{verbatim}

\subsubsection*{Algorithm}

This is an implementation of the root bracketing and bisection finding
routines \verb+zbrac+ and \verb+rtbis+ in Numerical Recipes~\cite{ptvf:1992}.

\subsubsection*{Uses}

\subsubsection*{Notes}
\vfill{\footnotesize\input{FindRootCV}}

</lalLaTeX>

#endif /* autodoc block */


#include <math.h>
#include <lal/LALStdlib.h>
#include <lal/LALConstants.h>
#include <lal/FindRoot.h>

NRCSID (FINDROOTC, "$Id$");

/* <lalVerbatim file="FindRootCP"> */
void
LALSBracketRoot (
    LALStatus      *status,
    SFindRootIn *inout,
    void        *params
    )
{ /* </lalVerbatim> */
  const REAL4 fac  = LAL_SQRT2;
  const INT4  imax = 64;

  INT4  i = 0;
  REAL4 y_1;
  REAL4 y_2;

  INITSTATUS (status, "LALSBracketRoot", FINDROOTC);
  ATTATCHSTATUSPTR (status);

  /* check that arguments are reasonable */
  ASSERT (inout, status, FINDROOTH_ENULL, FINDROOTH_MSGENULL);
  ASSERT (inout->function, status, FINDROOTH_ENULL, FINDROOTH_MSGENULL);
  /* params can be NULL ... */

  ASSERT (inout->xmax != inout->xmin, status,
          FINDROOTH_EIDOM, FINDROOTH_MSGEIDOM);

  /* evaluate function at endpoints */

  ((REAL4LALFunction *)inout->function)(status->statusPtr, &y_1, inout->xmin, params);
  CHECKSTATUSPTR (status);

  inout->function (status->statusPtr, &y_2, inout->xmax, params);
  CHECKSTATUSPTR (status);

  while (1)
  {
    /* break out if root has been bracketed */
    if (y_1*y_2 < 0)
    {
      break;
    }

    /* increment iteration count */
    ASSERT (i < imax, status, FINDROOTH_EMXIT, FINDROOTH_MSGEMXIT);
    ++i;

    if (fabs(y_1) < fabs(y_2))
    {
      /* expand lower limit */
      inout->xmin += fac*(inout->xmin - inout->xmax);
      inout->function (status->statusPtr, &y_1, inout->xmin, params);
      CHECKSTATUSPTR (status);
    }
    else
    {
      /* expand upper limit */
      inout->xmax += fac*(inout->xmax - inout->xmin);
      inout->function (status->statusPtr, &y_2, inout->xmax, params);
      CHECKSTATUSPTR (status);
    }

  }

  DETATCHSTATUSPTR (status);
  RETURN (status);
}


/* <lalVerbatim file="FindRootCP"> */
void
LALDBracketRoot (
    LALStatus      *status,
    DFindRootIn *inout,
    void        *params
    )
{ /* </lalVerbatim> */
  const REAL8 fac  = LAL_SQRT2;
  const INT4  imax = 64;

  INT4  i = 0;
  REAL8 y_1;
  REAL8 y_2;

  INITSTATUS (status, "LALDBracketRoot", FINDROOTC);
  ATTATCHSTATUSPTR (status);

  /* check that arguments are reasonable */
  ASSERT (inout, status, FINDROOTH_ENULL, FINDROOTH_MSGENULL);
  ASSERT (inout->function, status, FINDROOTH_ENULL, FINDROOTH_MSGENULL);
  /* params can be NULL ... */

  ASSERT (inout->xmax != inout->xmin, status,
          FINDROOTH_EIDOM, FINDROOTH_MSGEIDOM);

  /* evaluate function at endpoints */

  inout->function (status->statusPtr, &y_1, inout->xmin, params);
  CHECKSTATUSPTR (status);

  inout->function (status->statusPtr, &y_2, inout->xmax, params);
  CHECKSTATUSPTR (status);

  while (1)
  {
    /* break out if root has been bracketed */
    if (y_1*y_2 < 0)
    {
      break;
    }

    /* increment iteration count */
    ASSERT (i < imax, status, FINDROOTH_EMXIT, FINDROOTH_MSGEMXIT);
    ++i;

    if (fabs(y_1) < fabs(y_2))
    {
      /* expand lower limit */
      inout->xmin += fac*(inout->xmin - inout->xmax);
      inout->function (status->statusPtr, &y_1, inout->xmin, params);
      CHECKSTATUSPTR (status);
    }
    else
    {
      /* expand upper limit */
      inout->xmax += fac*(inout->xmax - inout->xmin);
      inout->function (status->statusPtr, &y_2, inout->xmax, params);
      CHECKSTATUSPTR (status);
    }

  }

  DETATCHSTATUSPTR (status);
  RETURN (status);
}


/* <lalVerbatim file="FindRootCP"> */
void
LALSBisectionFindRoot (
    LALStatus      *status,
    REAL4       *root,
    SFindRootIn *input,
    void        *params
    )
{ /* </lalVerbatim> */
  const INT4 imax = 40;

  INT4  i = 0;
  REAL4 y_1;
  REAL4 y_2;
  REAL4 x;
  REAL4 dx;

  INITSTATUS (status, "LALSBisectionFindRoot", FINDROOTC);
  ATTATCHSTATUSPTR (status);

  /* check that arguments are reasonable */
  ASSERT (root, status, FINDROOTH_ENULL, FINDROOTH_MSGENULL);
  ASSERT (input, status, FINDROOTH_ENULL, FINDROOTH_MSGENULL);
  ASSERT (input->function, status, FINDROOTH_ENULL, FINDROOTH_MSGENULL);
  /* params can be NULL ... */

  /* evaluate function at endpoints */

  input->function (status->statusPtr, &y_1, input->xmin, params);
  CHECKSTATUSPTR (status);

  input->function (status->statusPtr, &y_2, input->xmax, params);
  CHECKSTATUSPTR (status);

  ASSERT (y_1*y_2 < 0, status, FINDROOTH_EBRKT, FINDROOTH_MSGEBRKT);

  if (y_1 < 0)
  {
    /* start search at xmin and increase */
    x  = input->xmin;
    dx = input->xmax - input->xmin;
  }
  else
  {
    /* start search at xmax and decrease */
    x  = input->xmax;
    dx = input->xmin - input->xmax;
  }

  /* infinite loop to locate root */
  while (1)
  {
    REAL4 xmid;
    REAL4 ymid;

    /* increment iteration count */
    ASSERT (i < imax, status, FINDROOTH_EMXIT, FINDROOTH_MSGEMXIT);
    ++i;

    /* locate midpoint of domain */
    dx   /= 2;
    xmid  = x + dx;
    
    /* evaluate function at midpoint */
    input->function (status->statusPtr, &ymid, xmid, params);
    CHECKSTATUSPTR (status);

    if (ymid < 0)
    {
      /* function is in second half of domain */
      x = xmid;
    }
    else if (ymid == 0)
    {
      /* root has been found */
      *root = xmid;
      break;
    }

    if (fabs(dx) < input->xacc)
    {
      /* domain has shrunk to acceptably small size */
      *root = xmid;
      break;
    }

  }

  DETATCHSTATUSPTR (status);
  RETURN (status);
}


/* <lalVerbatim file="FindRootCP"> */
void
LALDBisectionFindRoot (
    LALStatus      *status,
    REAL8       *root,
    DFindRootIn *input,
    void        *params
    )
{ /* </lalVerbatim> */
  const INT4 imax = 80;

  INT4  i = 0;
  REAL8 y_1;
  REAL8 y_2;
  REAL8 x;
  REAL8 dx;

  INITSTATUS (status, "LALDBisectionFindRoot", FINDROOTC);
  ATTATCHSTATUSPTR (status);

  /* check that arguments are reasonable */
  ASSERT (root, status, FINDROOTH_ENULL, FINDROOTH_MSGENULL);
  ASSERT (input, status, FINDROOTH_ENULL, FINDROOTH_MSGENULL);
  ASSERT (input->function, status, FINDROOTH_ENULL, FINDROOTH_MSGENULL);
  /* params can be NULL ... */

  /* evaluate function at endpoints */

  input->function (status->statusPtr, &y_1, input->xmin, params);
  CHECKSTATUSPTR (status);

  input->function (status->statusPtr, &y_2, input->xmax, params);
  CHECKSTATUSPTR (status);

  ASSERT (y_1*y_2 < 0, status, FINDROOTH_EBRKT, FINDROOTH_MSGEBRKT);

  if (y_1 < 0)
  {
    /* start search at xmin and increase */
    x  = input->xmin;
    dx = input->xmax - input->xmin;
  }
  else
  {
    /* start search at xmax and decrease */
    x  = input->xmax;
    dx = input->xmin - input->xmax;
  }

  /* infinite loop to locate root */
  while (1)
  {
    REAL8 xmid;
    REAL8 ymid;

    /* increment iteration count */
    ASSERT (i < imax, status, FINDROOTH_EMXIT, FINDROOTH_MSGEMXIT);
    ++i;

    /* locate midpoint of domain */
    dx   /= 2;
    xmid  = x + dx;
    
    /* evaluate function at midpoint */
    input->function (status->statusPtr, &ymid, xmid, params);
    CHECKSTATUSPTR (status);

    if (ymid < 0)
    {
      /* function is in second half of domain */
      x = xmid;
    }
    else if (ymid == 0)
    {
      /* root has been found */
      *root = xmid;
      break;
    }

    if (fabs(dx) < input->xacc)
    {
      /* domain has shrunk to acceptably small size */
      *root = xmid;
      break;
    }

  }

  DETATCHSTATUSPTR (status);
  RETURN (status);
}

