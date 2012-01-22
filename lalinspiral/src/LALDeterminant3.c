/*
*  Copyright (C) 2007 David Churches, Duncan Brown, Jolien Creighton
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with with program; see the file COPYING. If not, write to the
*  Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
*  MA  02111-1307  USA
*/

/**
\author Sathyaprakash, B. S.
\file
\ingroup LALInspiralBank_h

\brief Module to calculate the determinant of a 3-dimensional matrix \f$g_{ij}\f$.

\heading{Prototypes}


<b>LALDeterminant()</b>:
<ul>
   <li> <tt>determinant,</tt> Output, determinant of the matrix.
   </li><li> <tt>matrix,</tt> Input, the input \f$(3\times 3)\f$ matrix whose determinant is required.</li>
</ul>

\heading{Description}

This code computes the determinant of a 3-dimensional matrix.

\heading{Algorithm}
Given a matrix \f$g_{ij}\f$ its determinant
is computed using  the formula \f$g = \epsilon^{ijk} g_{i1} g_{j2} g_{k3},\f$
where \f$\epsilon\f$ is the totally anti-symmetric tensor in 3-dimensions.

\heading{Uses}
None.

\heading{Notes}
Don't ever generalise this to higher dimensions since this
would take many more operations than some of the standard routines.

*/

#include <lal/LALInspiralBank.h>

NRCSID(LALDETERMINANT3C, "$Id$");



void LALDeterminant3(LALStatus *status,
                     REAL8     *determinant,
                     REAL8     **matrix)
{

   REAL8 epsilon[3][3][3] = {{
                              { 0, 0, 0},
                              { 0, 0, 1},
                              { 0,-1, 0}},
                             {{ 0, 0,-1},
                              { 0, 0, 0},
                              { 1, 0, 0}},
                             {{ 0, 1, 0},
                              {-1, 0, 0},
                              { 0, 0, 0}}};
   INT4 Dim=3,i,j,k;
   INITSTATUS(status, "LALDeterminant3", LALDETERMINANT3C);
   ATTATCHSTATUSPTR(status);

   ASSERT (matrix,  status, LALINSPIRALBANKH_ENULL, LALINSPIRALBANKH_MSGENULL);

   *determinant = 0;
   for (i=0; i<Dim; i++) {
   for (j=0; j<Dim; j++) {
   for (k=0; k<Dim; k++) {
      *determinant+=epsilon[i][j][k]*matrix[0][i]*matrix[1][j]*matrix[2][k];
   }}}

   ASSERT (*determinant != 0, status, LALINSPIRALH_ESIZE, LALINSPIRALH_MSGESIZE);

   DETATCHSTATUSPTR(status);
   RETURN(status);
}
