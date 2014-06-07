/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#pragma once
#ifndef EL_LQ_APPLYQ_HPP
#define EL_LQ_APPLYQ_HPP

#include EL_APPLYPACKEDREFLECTORS_INC

namespace El {
namespace lq {

template<typename F>
void ApplyQ
( LeftOrRight side, Orientation orientation, 
  const Matrix<F>& A, const Matrix<F>& t, 
  const Matrix<Base<F>>& d, Matrix<F>& B )
{
    DEBUG_ONLY(CallStackEntry cse("lq::ApplyQ"))
    const bool normal = (orientation==NORMAL);
    const bool onLeft = (side==LEFT);

    const bool applyDFirst = normal!=onLeft;
    if( applyDFirst )
    {
        if( onLeft )
        {
            auto BTop = View( B, 0, 0, d.Height(), B.Width() );
            DiagonalScale( side, orientation, d, BTop );
        }
        else
        {
            auto BLeft = View( B, 0, 0, B.Height(), d.Height() );
            DiagonalScale( side, orientation, d, BLeft );
        }
    }

    const ForwardOrBackward direction = ( normal==onLeft ? FORWARD : BACKWARD );
    const Conjugation conjugation = ( normal ? CONJUGATED : UNCONJUGATED );
    ApplyPackedReflectors
    ( side, UPPER, HORIZONTAL, direction, conjugation, 0, A, t, B );

    if( !applyDFirst )
    {
        if( onLeft )
        {
            auto BTop = View( B, 0, 0, d.Height(), B.Width() );
            DiagonalScale( side, orientation, d, BTop );
        }
        else
        {
            auto BLeft = View( B, 0, 0, B.Height(), d.Height() );
            DiagonalScale( side, orientation, d, BLeft );
        }
    }
}

template<typename F,Dist Ut,Dist Vt,Dist Ud,Dist Vd>
void ApplyQ
( LeftOrRight side, Orientation orientation, 
  const DistMatrix<F>& A, const DistMatrix<F,Ut,Vt>& t, 
  const DistMatrix<Base<F>,Ud,Vd>& d, DistMatrix<F>& B )
{
    DEBUG_ONLY(CallStackEntry cse("lq::ApplyQ"))
    const bool normal = (orientation==NORMAL);
    const bool onLeft = (side==LEFT);

    const bool applyDFirst = normal!=onLeft;
    if( applyDFirst )
    {
        if( onLeft )
        {
            auto BTop = View( B, 0, 0, d.Height(), B.Width() );
            DiagonalScale( side, orientation, d, BTop );
        }
        else
        {
            auto BLeft = View( B, 0, 0, B.Height(), d.Height() );
            DiagonalScale( side, orientation, d, BLeft );
        }
    }

    const ForwardOrBackward direction = ( normal==onLeft ? FORWARD : BACKWARD );
    const Conjugation conjugation = ( normal ? CONJUGATED : UNCONJUGATED );

    DistMatrix<F,MD,STAR> tDiag(A.Grid());
    tDiag.SetRoot( A.DiagonalRoot() );
    tDiag.AlignCols( A.DiagonalAlign() );
    tDiag = t;
    ApplyPackedReflectors
    ( side, UPPER, HORIZONTAL, direction, conjugation, 0, A, tDiag, B );

    if( !applyDFirst )
    {
        if( onLeft )
        {
            auto BTop = View( B, 0, 0, d.Height(), B.Width() );
            DiagonalScale( side, orientation, d, BTop );
        }
        else
        {
            auto BLeft = View( B, 0, 0, B.Height(), d.Height() );
            DiagonalScale( side, orientation, d, BLeft );
        }
    }
}

} // namespace lq
} // namespace El

#endif // ifndef EL_LQ_APPLY_HPP