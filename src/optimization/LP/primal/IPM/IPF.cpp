/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#include "El.hpp"
#include "./util.hpp"

namespace El {
namespace lp {
namespace primal {

// The following solves a linear program in "primal" conic form:
//
//   min c^T x
//   s.t. A x = b, x >= 0,
//
// as opposed to the more general "dual" conic form:
//
//   min c^T x
//   s.t. A x = b, G x + s = h, s >= 0,
//
// using a simple Infeasible Path Following (IPF) scheme. This routine
// should only be used for academic purposes, as the Mehrotra alternative
// typically requires an order of magnitude fewer iterations.

template<typename Real>
void IPF
( const Matrix<Real>& A, 
  const Matrix<Real>& b, const Matrix<Real>& c,
        Matrix<Real>& x,       Matrix<Real>& y, 
        Matrix<Real>& z,
  const IPFCtrl<Real>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("lp::primal::IPF"))    
    const Int m = A.Height();
    const Int n = A.Width();

    const Real bNrm2 = Nrm2( b );
    const Real cNrm2 = Nrm2( c );

    if( !ctrl.initialized )
        lp::primal::Initialize( A, b, c, x, y, z );

    Matrix<Real> J, d, 
                 rc, rb, rmu, 
                 dx, dy, dz;
#ifndef EL_RELEASE
    Matrix<Real> dxError, dyError, dzError;
#endif
    for( Int numIts=0; ; ++numIts )
    {
        // Ensure that x and z are in the cone
        // ===================================
        const Int xNumNonPos = NumNonPositive( x );
        const Int zNumNonPos = NumNonPositive( z );
        if( xNumNonPos > 0 || zNumNonPos > 0 )
            LogicError
            (xNumNonPos," entries of x were nonpositive and ",
             zNumNonPos," entries of z were nonpositive");

        // Check for convergence
        // =====================
        // |primal - dual| / (1 + |primal|) <= tol ?
        // -----------------------------------------
        const Real primObj = Dot(c,x);
        const Real dualObj = -Dot(b,y); 
        const Real objConv = Abs(primObj-dualObj) / (Real(1)+Abs(primObj));
        // || r_b ||_2 / (1 + || b ||_2) <= tol ?
        // --------------------------------------
        rb = b;
        Scale( Real(-1), rb );
        Gemv( NORMAL, Real(1), A, x, Real(1), rb );
        const Real rbNrm2 = Nrm2( rb );
        const Real rbConv = rbNrm2 / (Real(1)+bNrm2);
        // || r_c ||_2 / (1 + || c ||_2) <= tol ?
        // --------------------------------------
        rc = c;
        Gemv( TRANSPOSE, Real(1), A, y, Real(1), rc );
        Axpy( Real(-1), z, rc );
        const Real rcNrm2 = Nrm2( rc );
        const Real rcConv = rcNrm2 / (Real(1)+cNrm2);
        // Now check the pieces
        // --------------------
        if( objConv <= ctrl.tol && rbConv <= ctrl.tol && rcConv <= ctrl.tol )
            break;
        else if( ctrl.print )
            std::cout << " iter " << numIts << ":\n"
                      << "  |primal - dual| / (1 + |primal|) = "
                      << objConv << "\n"
                      << "  || r_b ||_2 / (1 + || b ||_2)   = "
                      << rbConv << "\n"
                      << "  || r_c ||_2 / (1 + || c ||_2)   = "
                      << rcConv << std::endl;

        // Raise an exception after an unacceptable number of iterations
        // =============================================================
        if( numIts == ctrl.maxIts )
            RuntimeError
            ("Maximum number of iterations (",ctrl.maxIts,") exceeded");

        // Compute the duality measure and r_mu = x o z - tau e
        // ====================================================
        const Real mu = Dot(x,z) / n;
        rmu = z;
        DiagonalScale( LEFT, NORMAL, x, rmu );
        Shift( rmu, -ctrl.centering*mu );

        if( ctrl.system == FULL_KKT )
        {
            // Construct the full KKT system
            // =============================
            KKT( A, x, z, J );
            KKTRHS( rc, rb, rmu, z, d );

            // Compute the proposed step from the KKT system
            // =============================================
            SymmetricSolve( LOWER, NORMAL, J, d );
            ExpandSolution( m, n, d, dx, dy, dz );
        }
        else if( ctrl.system == AUGMENTED_KKT )
        {
            // Construct the reduced KKT system
            // ================================
            AugmentedKKT( A, x, z, J );
            AugmentedKKTRHS( x, rc, rb, rmu, d );

            // Compute the proposed step from the KKT system
            // =============================================
            SymmetricSolve( LOWER, NORMAL, J, d );
            ExpandAugmentedSolution( x, z, rmu, d, dx, dy, dz );
        }
        else if( ctrl.system == NORMAL_KKT )
        {
            // Construct the reduced KKT system
            // ================================
            NormalKKT( A, x, z, J );
            NormalKKTRHS( A, x, z, rc, rb, rmu, dy );

            // Compute the proposed step from the KKT system
            // =============================================
            SymmetricSolve( LOWER, NORMAL, J, dy );
            ExpandNormalSolution( A, c, x, z, rc, rmu, dx, dy, dz );
        }

#ifndef EL_RELEASE
        // Sanity checks
        // =============
        const Real rmuNrm2 = Nrm2( rmu );
        dzError = rmu;
        for( Int i=0; i<n; ++i )
        {
            const Real xi = x.Get(i,0);
            const Real zi = z.Get(i,0);
            const Real dxi = dx.Get(i,0);
            const Real dzi = dz.Get(i,0);
            dzError.Update( i, 0, xi*dzi + zi*dxi );
        }
        const Real dzErrorNrm2 = Nrm2( dzError );

        dyError = rc;
        Gemv( TRANSPOSE, Real(1), A, dy, Real(1), dyError );
        Axpy( Real(-1), dz, dyError );
        const Real dyErrorNrm2 = Nrm2( dyError );

        dxError = rb;
        Gemv( NORMAL, Real(1), A, dx, Real(1), dxError );
        const Real dxErrorNrm2 = Nrm2( dxError );

        if( ctrl.print )
            std::cout << "  || dxError ||_2 / (1 + || r_b ||_2) = " 
                      << dxErrorNrm2/(1+rbNrm2) << "\n"
                      << "  || dyError ||_2 / (1 + || r_c ||_2) = " 
                      << dyErrorNrm2/(1+rcNrm2) << "\n"
                      << "  || dzError ||_2 / (1 + || r_mu ||_2) = " 
                      << dzErrorNrm2/(1+rmuNrm2) << std::endl;
#endif

        // Take a step in the computed direction
        // =====================================
        const Real alpha =
          IPFLineSearch
          ( A, b, c, x, y, z, dx, dy, dz, 
            ctrl.tol*(1+bNrm2), ctrl.tol*(1+cNrm2), ctrl.lineSearchCtrl );
        if( ctrl.print )
            std::cout << "  alpha = " << alpha << std::endl;
        Axpy( alpha, dx, x );
        Axpy( alpha, dy, y );
        Axpy( alpha, dz, z );
    }
}

template<typename Real>
void IPF
( const AbstractDistMatrix<Real>& APre, 
  const AbstractDistMatrix<Real>& b,    const AbstractDistMatrix<Real>& c,
        AbstractDistMatrix<Real>& xPre,       AbstractDistMatrix<Real>& yPre, 
        AbstractDistMatrix<Real>& zPre, 
  const IPFCtrl<Real>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("lp::primal::IPF"))    

    ProxyCtrl proxCtrl;
    proxCtrl.colConstrain = true;
    proxCtrl.rowConstrain = true;
    proxCtrl.colAlign = 0;
    proxCtrl.rowAlign = 0;
    auto APtr = ReadProxy<Real,MC,MR>(&APre,proxCtrl);      auto& A = *APtr;
    // NOTE: The following do not need to be read proxies when !ctrl.initialized
    auto xPtr = ReadWriteProxy<Real,MC,MR>(&xPre,proxCtrl); auto& x = *xPtr;
    auto yPtr = ReadWriteProxy<Real,MC,MR>(&yPre,proxCtrl); auto& y = *yPtr;
    auto zPtr = ReadWriteProxy<Real,MC,MR>(&zPre,proxCtrl); auto& z = *zPtr;

    const Int m = A.Height();
    const Int n = A.Width();
    const Grid& grid = A.Grid();
    const Int commRank = A.Grid().Rank();

    const Real bNrm2 = Nrm2( b );
    const Real cNrm2 = Nrm2( c );

    if( !ctrl.initialized )
        lp::primal::Initialize( A, b, c, x, y, z );

    DistMatrix<Real> J(grid), d(grid), 
                     rc(grid), rb(grid), rmu(grid),
                     dx(grid), dy(grid), dz(grid);
    dx.AlignWith( x );
    dz.AlignWith( x );
#ifndef EL_RELEASE
    DistMatrix<Real> dxError(grid), dyError(grid), dzError(grid);
    dzError.AlignWith( dz );
#endif
    for( Int numIts=0; ; ++numIts )
    {
        // Ensure that x and z are in the cone
        // ===================================
        const Int xNumNonPos = NumNonPositive( x );
        const Int zNumNonPos = NumNonPositive( z );
        if( xNumNonPos > 0 || zNumNonPos > 0 )
            LogicError
            (xNumNonPos," entries of x were nonpositive and ",
             zNumNonPos," entries of z were nonpositive");

        // Check for convergence
        // =====================
        // |primal - dual| / (1 + |primal|) <= tol ?
        // -----------------------------------------
        const Real primObj = Dot(c,x);
        const Real dualObj = -Dot(b,y);
        const Real objConv = Abs(primObj-dualObj) / (Real(1)+Abs(primObj));
        // || r_b ||_2 / (1 + || b ||_2) <= tol ?
        // --------------------------------------
        rb = b;
        Scale( Real(-1), rb );
        Gemv( NORMAL, Real(1), A, x, Real(1), rb );
        const Real rbNrm2 = Nrm2( rb );
        const Real rbConv = rbNrm2 / (Real(1)+bNrm2);
        // || r_c ||_2 / (1 + || c ||_2) <= tol ?
        // --------------------------------------
        rc = c;
        Gemv( TRANSPOSE, Real(1), A, y, Real(1), rc );
        Axpy( Real(-1), z, rc );
        const Real rcNrm2 = Nrm2( rc );
        const Real rcConv = rcNrm2 / (Real(1)+cNrm2);
        // Now check the pieces
        // --------------------
        if( objConv <= ctrl.tol && rbConv <= ctrl.tol && rcConv <= ctrl.tol )
            break;
        else if( ctrl.print && commRank == 0 )
            std::cout << " iter " << numIts << ":\n"
                      << "  |primal - dual| / (1 + |primal|) = "
                      << objConv << "\n"
                      << "  || r_b ||_2 / (1 + || b ||_2)   = "
                      << rbConv << "\n"
                      << "  || r_c ||_2 / (1 + || c ||_2)   = "
                      << rcConv << std::endl;

        // Raise an exception after an unacceptable number of iterations
        // =============================================================
        if( numIts == ctrl.maxIts )
            RuntimeError
            ("Maximum number of iterations (",ctrl.maxIts,") exceeded");

        // Compute the duality measure and r_mu = x o z - tau e
        // ====================================================
        const Real mu = Dot(x,z) / n;
        rmu = z;
        DiagonalScale( LEFT, NORMAL, x, rmu );
        Shift( rmu, -ctrl.centering*mu );

        if( ctrl.system == FULL_KKT )
        {
            // Construct the full KKT system
            // =============================
            KKT( A, x, z, J );
            KKTRHS( rc, rb, rmu, z, d );

            // Compute the proposed step from the KKT system
            // =============================================
            SymmetricSolve( LOWER, NORMAL, J, d );
            ExpandSolution( m, n, d, dx, dy, dz );
        }
        else if( ctrl.system == AUGMENTED_KKT )
        {
            // Construct the reduced KKT system
            // ================================
            AugmentedKKT( A, x, z, J );
            AugmentedKKTRHS( x, rc, rb, rmu, d );

            // Compute the proposed step from the KKT system
            // =============================================
            SymmetricSolve( LOWER, NORMAL, J, d );
            ExpandAugmentedSolution( x, z, rmu, d, dx, dy, dz );
        }
        else if( ctrl.system == NORMAL_KKT )
        {
            // Construct the reduced KKT system
            // ================================
            NormalKKT( A, x, z, J );
            NormalKKTRHS( A, x, z, rc, rb, rmu, dy );

            // Compute the proposed step from the KKT system
            // =============================================
            SymmetricSolve( LOWER, NORMAL, J, dy );
            ExpandNormalSolution( A, c, x, z, rc, rmu, dx, dy, dz );
        }

#ifndef EL_RELEASE
        // Sanity checks
        // =============
        const Real rmuNrm2 = Nrm2( rmu );
        // TODO: Find a more convenient syntax for expressing this operation
        dzError = rmu;
        if( dzError.IsLocalCol(0) )
        {
            for( Int iLoc=0; iLoc<dzError.LocalHeight(); ++iLoc )
            {
                const Real xi = x.GetLocal(iLoc,0);
                const Real zi = z.GetLocal(iLoc,0);
                const Real dxi = dx.GetLocal(iLoc,0);
                const Real dzi = dz.GetLocal(iLoc,0);
                dzError.UpdateLocal( iLoc, 0, xi*dzi + zi*dxi );
            }
        }
        const Real dzErrorNrm2 = Nrm2( dzError );

        dyError = rc;
        Gemv( TRANSPOSE, Real(1), A, dy, Real(1), dyError );
        Axpy( Real(-1), dz, dyError );
        const Real dyErrorNrm2 = Nrm2( dyError );

        dxError = rb;
        Gemv( NORMAL, Real(1), A, dx, Real(1), dxError );
        const Real dxErrorNrm2 = Nrm2( dxError );

        if( ctrl.print && commRank == 0 )
            std::cout << "  || dxError ||_2 / (1 + || r_b ||_2) = " 
                      << dxErrorNrm2/(1+rbNrm2) << "\n"
                      << "  || dyError ||_2 / (1 + || r_c ||_2) = " 
                      << dyErrorNrm2/(1+rcNrm2) << "\n"
                      << "  || dzError ||_2 / (1 + || r_mu ||_2) = " 
                      << dzErrorNrm2/(1+rmuNrm2) << std::endl;
#endif

        // Take a step in the computed direction
        // =====================================
        const Real alpha =
          IPFLineSearch
          ( A, b, c, x, y, z, dx, dy, dz, 
            ctrl.tol*(1+bNrm2), ctrl.tol*(1+cNrm2), ctrl.lineSearchCtrl );
        if( ctrl.print && commRank == 0 )
            std::cout << "  alpha = " << alpha << std::endl;
        Axpy( alpha, dx, x );
        Axpy( alpha, dy, y );
        Axpy( alpha, dz, z );
    }
}

template<typename Real>
void IPF
( const SparseMatrix<Real>& A, 
  const Matrix<Real>& b,       const Matrix<Real>& c,
        Matrix<Real>& x,             Matrix<Real>& y, 
        Matrix<Real>& z,
  const IPFCtrl<Real>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("lp::primal::IPF"))    
    LogicError("Sequential sparse-direct solvers not yet supported");
}

template<typename Real>
void IPF
( const DistSparseMatrix<Real>& A, 
  const DistMultiVec<Real>& b,     const DistMultiVec<Real>& c,
        DistMultiVec<Real>& x,           DistMultiVec<Real>& y, 
        DistMultiVec<Real>& z,
  const IPFCtrl<Real>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("lp::primal::IPF"))    

    const Int m = A.Height();
    const Int n = A.Width();
    mpi::Comm comm = A.Comm();
    const Int commRank = mpi::Rank(comm);
    const Real epsilon = lapack::MachineEpsilon<Real>();

    const Real bNrm2 = Nrm2( b );
    const Real cNrm2 = Nrm2( c );

    DistSymmInfo info;
    DistSeparatorTree sepTree;
    DistMap map, invMap;

    if( !ctrl.initialized )
    {
        // The initialization involves an augmented KKT system, and so we can
        // only reuse the factorization metadata if the this IPM is using the
        // augmented formulation
        if( ctrl.system == AUGMENTED_KKT )
        {
            lp::primal::Initialize
            ( A, b, c, x, y, z, map, invMap, sepTree, info );
        }
        else
        {
            DistMap augMap, augInvMap;
            DistSymmInfo augInfo;
            DistSeparatorTree augSepTree;
            lp::primal::Initialize
            ( A, b, c, x, y, z, augMap, augInvMap, augSepTree, augInfo );
        }
    }

    DistSparseMatrix<Real> J(comm);
    DistSymmFrontTree<Real> JFrontTree;
    DistMultiVec<Real> d(comm),
                       rc(comm), rb(comm), rmu(comm), 
                       dx(comm), dy(comm), dz(comm);
    DistNodalMultiVec<Real> dNodal;

    DistMultiVec<Real> regCand(comm), reg(comm);
    DistNodalMultiVec<Real> regCandNodal, regNodal;

#ifndef EL_RELEASE
    DistMultiVec<Real> dxError(comm), dyError(comm), dzError(comm);
#endif
    for( Int numIts=0; ; ++numIts )
    {
        // Ensure that x and z are in the cone
        // ===================================
        const Int xNumNonPos = NumNonPositive( x );
        const Int zNumNonPos = NumNonPositive( z );
        if( xNumNonPos > 0 || zNumNonPos > 0 )
            LogicError
            (xNumNonPos," entries of x were nonpositive and ",
             zNumNonPos," entries of z were nonpositive");

        // Check for convergence
        // =====================
        // |primal - dual| / (1 + |primal|) <= tol ?
        // -----------------------------------------
        const Real primObj = Dot(c,x);
        const Real dualObj = -Dot(b,y);
        const Real objConv = Abs(primObj-dualObj) / (Real(1)+Abs(primObj));
        // || r_b ||_2 / (1 + || b ||_2) <= tol ?
        // --------------------------------------
        rb = b;
        Scale( Real(-1), rb );
        Multiply( NORMAL, Real(1), A, x, Real(1), rb );
        const Real rbNrm2 = Nrm2( rb );
        const Real rbConv = rbNrm2 / (Real(1)+bNrm2);
        // || r_c ||_2 / (1 + || c ||_2) <= tol ?
        // --------------------------------------
        rc = c;
        Multiply( TRANSPOSE, Real(1), A, y, Real(1), rc );
        Axpy( Real(-1), z, rc );
        const Real rcNrm2 = Nrm2( rc );
        const Real rcConv = rcNrm2 / (Real(1)+cNrm2);
        // Now check the pieces
        // --------------------
        if( objConv <= ctrl.tol && rbConv <= ctrl.tol && rcConv <= ctrl.tol )
            break;
        else if( ctrl.print && commRank == 0 )
            std::cout << " iter " << numIts << ":\n"
                      << "  |primal - dual| / (1 + |primal|) = "
                      << objConv << "\n"
                      << "  || r_b ||_2 / (1 + || b ||_2)   = "
                      << rbConv << "\n"
                      << "  || r_c ||_2 / (1 + || c ||_2)   = "
                      << rcConv << std::endl;

        // Raise an exception after an unacceptable number of iterations
        // =============================================================
        if( numIts == ctrl.maxIts )
            RuntimeError
            ("Maximum number of iterations (",ctrl.maxIts,") exceeded");

        // Compute the duality measure and r_mu = x o z - tau e
        // ====================================================
        const Real mu = Dot(x,z) / n;
        rmu = z;
        DiagonalScale( NORMAL, x, rmu );
        Shift( rmu, -ctrl.centering*mu );

        // Compute the search direction
        // ============================
        const Real minReductionFactor = 2;
        const Int maxRefineIts = 10;
        if( ctrl.system == FULL_KKT )
        {
            // Construct the full KKT system
            // -----------------------------
            // TODO: Add default regularization
            KKT( A, x, z, J, false );
            KKTRHS( rc, rb, rmu, z, d );
            const Real pivTol = MaxNorm(J)*epsilon;
            const Real regMagPrimal = Pow(epsilon,Real(0.75));
            const Real regMagLagrange = Pow(epsilon,Real(0.5));
            const Real regMagDual = Pow(epsilon,Real(0.5));
            regCand.Resize( m+2*n, 1 );
            for( Int iLoc=0; iLoc<regCand.LocalHeight(); ++iLoc )
            {
                const Int i = regCand.FirstLocalRow() + iLoc;
                if( i < n )
                    regCand.SetLocal( iLoc, 0, regMagPrimal );
                else if( i < n+m )
                    regCand.SetLocal( iLoc, 0, -regMagLagrange );
                else 
                    regCand.SetLocal( iLoc, 0, -regMagDual );
            }
            // Do not use any a priori regularization
            Zeros( reg, m+2*n, 1 );

            // Compute the proposed step from the KKT system
            // ---------------------------------------------
            if( numIts == 0 )
            {
                NestedDissection( J.LockedDistGraph(), map, sepTree, info );
                map.FormInverse( invMap );
            }
            JFrontTree.Initialize( J, map, sepTree, info );
            regCandNodal.Pull( invMap, info, regCand );
            regNodal.Pull( invMap, info, reg );
            RegularizedLDL
            ( info, JFrontTree, pivTol, regCandNodal, regNodal, LDL_1D );
            regNodal.Push( invMap, info, reg );
            // TODO: Iterative refinement
            /*
            SolveWithIterativeRefinement
            ( J, invMap, info, JFrontTree, d, 
              minReductionFactor, maxRefineIts );
            */
            dNodal.Pull( invMap, info, d );
            Solve( info, JFrontTree, dNodal );
            dNodal.Push( invMap, info, d );
            ExpandSolution( m, n, d, dx, dy, dz );
        }
        else if( ctrl.system == AUGMENTED_KKT )
        {
            // Construct the "augmented" KKT system
            // ------------------------------------
            // TODO: Add default regularization
            AugmentedKKT( A, x, z, J, false );
            AugmentedKKTRHS( x, rc, rb, rmu, d );
            const Real pivTol = MaxNorm(J)*epsilon;
            const Real regMagPrimal = Pow(epsilon,Real(0.75));
            const Real regMagLagrange = Pow(epsilon,Real(0.5));
            regCand.Resize( n+m, 1 );
            for( Int iLoc=0; iLoc<regCand.LocalHeight(); ++iLoc )
            {
                const Int i = regCand.FirstLocalRow() + iLoc;
                if( i < n )
                    regCand.SetLocal( iLoc, 0, regMagPrimal );
                else
                    regCand.SetLocal( iLoc, 0, -regMagLagrange );
            }
            // Do not use any a priori regularization
            Zeros( reg, m+n, 1 );

            // Compute the proposed step from the KKT system
            // ---------------------------------------------
            if( ctrl.initialized && numIts == 0 )
            {
                NestedDissection( J.LockedDistGraph(), map, sepTree, info );
                map.FormInverse( invMap );
            }
            JFrontTree.Initialize( J, map, sepTree, info );
            regCandNodal.Pull( invMap, info, regCand );
            regNodal.Pull( invMap, info, reg );
            RegularizedLDL
            ( info, JFrontTree, pivTol, regCandNodal, regNodal, LDL_1D );
            regNodal.Push( invMap, info, reg );
            // TODO: Iterative refinement
            /*
            SolveWithIterativeRefinement
            ( J, invMap, info, JFrontTree, d, 
              minReductionFactor, maxRefineIts );
            */
            dNodal.Pull( invMap, info, d );
            Solve( info, JFrontTree, dNodal );
            dNodal.Push( invMap, info, d );
            ExpandAugmentedSolution( x, z, rmu, d, dx, dy, dz );
        }
        else // ctrl.system == NORMAL_KKT
        {
            // Construct the reduced KKT system, J dy = d
            // ------------------------------------------
            // NOTE: Explicit symmetry is currently required for both METIS and
            //       for the frontal tree initialization
            NormalKKT( A, x, z, J, false );
            NormalKKTRHS( A, x, z, rc, rb, rmu, dy );

            // Compute the proposed step from the KKT system
            // ---------------------------------------------
            if( numIts == 0 )
            {
                NestedDissection( J.LockedDistGraph(), map, sepTree, info );
                map.FormInverse( invMap );
            }
            JFrontTree.Initialize( J, map, sepTree, info );
            LDL( info, JFrontTree, LDL_INTRAPIV_1D ); 
            SolveWithIterativeRefinement
            ( J, invMap, info, JFrontTree, dy, 
              minReductionFactor, maxRefineIts );
            ExpandNormalSolution( A, c, x, z, rc, rmu, dx, dy, dz );
        }

#ifndef EL_RELEASE
        // Sanity checks
        // =============
        const Real rmuNrm2 = Nrm2( rmu );
        dzError = rmu;
        for( Int iLoc=0; iLoc<dzError.LocalHeight(); ++iLoc )
        {
            const Real xi = x.GetLocal(iLoc,0);
            const Real zi = z.GetLocal(iLoc,0);
            const Real dxi = dx.GetLocal(iLoc,0);
            const Real dzi = dz.GetLocal(iLoc,0);
            dzError.UpdateLocal( iLoc, 0, xi*dzi + zi*dxi );
        }
        const Real dzErrorNrm2 = Nrm2( dzError );

        dyError = rc;
        Multiply( TRANSPOSE, Real(1), A, dy, Real(1), dyError );
        Axpy( Real(-1), dz, dyError );
        const Real dyErrorNrm2 = Nrm2( dyError );

        dxError = rb;
        Multiply( NORMAL, Real(1), A, dx, Real(1), dxError );
        const Real dxErrorNrm2 = Nrm2( dxError );

        if( ctrl.print && commRank == 0 )
            std::cout << "  || dxError ||_2 / (1 + || r_b ||_2) = " 
                      << dxErrorNrm2/(1+rbNrm2) << "\n"
                      << "  || dyError ||_2 / (1 + || r_c ||_2) = " 
                      << dyErrorNrm2/(1+rcNrm2) << "\n"
                      << "  || dzError ||_2 / (1 + || r_mu ||_2) = " 
                      << dzErrorNrm2/(1+rmuNrm2) << std::endl;
#endif

        // Take a step in the computed direction
        // =====================================
        const Real alpha = 
          IPFLineSearch
          ( A, b, c, x, y, z, dx, dy, dz, 
            ctrl.tol*(1+bNrm2), ctrl.tol*(1+cNrm2), ctrl.lineSearchCtrl );
        if( ctrl.print && commRank == 0 )
            std::cout << "  alpha = " << alpha << std::endl;
        Axpy( alpha, dx, x );
        Axpy( alpha, dy, y );
        Axpy( alpha, dz, z );
    }
}

#define PROTO(Real) \
  template void IPF \
  ( const Matrix<Real>& A, \
    const Matrix<Real>& b, const Matrix<Real>& c, \
          Matrix<Real>& x,       Matrix<Real>& y, \
          Matrix<Real>& z, \
    const IPFCtrl<Real>& ctrl ); \
  template void IPF \
  ( const AbstractDistMatrix<Real>& A, \
    const AbstractDistMatrix<Real>& b, const AbstractDistMatrix<Real>& c, \
          AbstractDistMatrix<Real>& x,       AbstractDistMatrix<Real>& y, \
          AbstractDistMatrix<Real>& z, \
    const IPFCtrl<Real>& ctrl ); \
  template void IPF \
  ( const SparseMatrix<Real>& A, \
    const Matrix<Real>& b, const Matrix<Real>& c, \
          Matrix<Real>& x,       Matrix<Real>& y, \
          Matrix<Real>& z, \
    const IPFCtrl<Real>& ctrl ); \
  template void IPF \
  ( const DistSparseMatrix<Real>& A, \
    const DistMultiVec<Real>& b, const DistMultiVec<Real>& c, \
          DistMultiVec<Real>& x,       DistMultiVec<Real>& y, \
          DistMultiVec<Real>& z, \
    const IPFCtrl<Real>& ctrl );

#define EL_NO_INT_PROTO
#define EL_NO_COMPLEX_PROTO
#include "El/macros/Instantiate.h"

} // namespace primal
} // namespace lp
} // namespace El
