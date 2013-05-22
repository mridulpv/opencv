/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Copyright (C) 2013, OpenCV Foundation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "precomp.hpp"
#include "opencv2/calib3d/calib3d_c.h"

/************************************************************************************\
       Some backward compatibility stuff, to be moved to legacy or compat module
\************************************************************************************/

using cv::Ptr;

////////////////// Levenberg-Marquardt engine (the old variant) ////////////////////////

CvLevMarq::CvLevMarq()
{
    mask = prevParam = param = J = err = JtJ = JtJN = JtErr = JtJV = JtJW = Ptr<CvMat>();
    lambdaLg10 = 0; state = DONE;
    criteria = cvTermCriteria(0,0,0);
    iters = 0;
    completeSymmFlag = false;
}

CvLevMarq::CvLevMarq( int nparams, int nerrs, CvTermCriteria criteria0, bool _completeSymmFlag )
{
    mask = prevParam = param = J = err = JtJ = JtJN = JtErr = JtJV = JtJW = Ptr<CvMat>();
    init(nparams, nerrs, criteria0, _completeSymmFlag);
}

void CvLevMarq::clear()
{
    mask.release();
    prevParam.release();
    param.release();
    J.release();
    err.release();
    JtJ.release();
    JtJN.release();
    JtErr.release();
    JtJV.release();
    JtJW.release();
}

CvLevMarq::~CvLevMarq()
{
    clear();
}

void CvLevMarq::init( int nparams, int nerrs, CvTermCriteria criteria0, bool _completeSymmFlag )
{
    if( !param || param->rows != nparams || nerrs != (err ? err->rows : 0) )
        clear();
    mask = cvCreateMat( nparams, 1, CV_8U );
    cvSet(mask, cvScalarAll(1));
    prevParam = cvCreateMat( nparams, 1, CV_64F );
    param = cvCreateMat( nparams, 1, CV_64F );
    JtJ = cvCreateMat( nparams, nparams, CV_64F );
    JtJN = cvCreateMat( nparams, nparams, CV_64F );
    JtJV = cvCreateMat( nparams, nparams, CV_64F );
    JtJW = cvCreateMat( nparams, 1, CV_64F );
    JtErr = cvCreateMat( nparams, 1, CV_64F );
    if( nerrs > 0 )
    {
        J = cvCreateMat( nerrs, nparams, CV_64F );
        err = cvCreateMat( nerrs, 1, CV_64F );
    }
    prevErrNorm = DBL_MAX;
    lambdaLg10 = -3;
    criteria = criteria0;
    if( criteria.type & CV_TERMCRIT_ITER )
        criteria.max_iter = MIN(MAX(criteria.max_iter,1),1000);
    else
        criteria.max_iter = 30;
    if( criteria.type & CV_TERMCRIT_EPS )
        criteria.epsilon = MAX(criteria.epsilon, 0);
    else
        criteria.epsilon = DBL_EPSILON;
    state = STARTED;
    iters = 0;
    completeSymmFlag = _completeSymmFlag;
}

bool CvLevMarq::update( const CvMat*& _param, CvMat*& matJ, CvMat*& _err )
{
    double change;

    matJ = _err = 0;

    assert( !err.empty() );
    if( state == DONE )
    {
        _param = param;
        return false;
    }

    if( state == STARTED )
    {
        _param = param;
        cvZero( J );
        cvZero( err );
        matJ = J;
        _err = err;
        state = CALC_J;
        return true;
    }

    if( state == CALC_J )
    {
        cvMulTransposed( J, JtJ, 1 );
        cvGEMM( J, err, 1, 0, 0, JtErr, CV_GEMM_A_T );
        cvCopy( param, prevParam );
        step();
        if( iters == 0 )
            prevErrNorm = cvNorm(err, 0, CV_L2);
        _param = param;
        cvZero( err );
        _err = err;
        state = CHECK_ERR;
        return true;
    }

    assert( state == CHECK_ERR );
    errNorm = cvNorm( err, 0, CV_L2 );
    if( errNorm > prevErrNorm )
    {
        if( ++lambdaLg10 <= 16 )
        {
            step();
            _param = param;
            cvZero( err );
            _err = err;
            state = CHECK_ERR;
            return true;
        }
    }

    lambdaLg10 = MAX(lambdaLg10-1, -16);
    if( ++iters >= criteria.max_iter ||
        (change = cvNorm(param, prevParam, CV_RELATIVE_L2)) < criteria.epsilon )
    {
        _param = param;
        state = DONE;
        return true;
    }

    prevErrNorm = errNorm;
    _param = param;
    cvZero(J);
    matJ = J;
    _err = err;
    state = CALC_J;
    return true;
}


bool CvLevMarq::updateAlt( const CvMat*& _param, CvMat*& _JtJ, CvMat*& _JtErr, double*& _errNorm )
{
    double change;

    CV_Assert( err.empty() );
    if( state == DONE )
    {
        _param = param;
        return false;
    }

    if( state == STARTED )
    {
        _param = param;
        cvZero( JtJ );
        cvZero( JtErr );
        errNorm = 0;
        _JtJ = JtJ;
        _JtErr = JtErr;
        _errNorm = &errNorm;
        state = CALC_J;
        return true;
    }

    if( state == CALC_J )
    {
        cvCopy( param, prevParam );
        step();
        _param = param;
        prevErrNorm = errNorm;
        errNorm = 0;
        _errNorm = &errNorm;
        state = CHECK_ERR;
        return true;
    }

    assert( state == CHECK_ERR );
    if( errNorm > prevErrNorm )
    {
        if( ++lambdaLg10 <= 16 )
        {
            step();
            _param = param;
            errNorm = 0;
            _errNorm = &errNorm;
            state = CHECK_ERR;
            return true;
        }
    }

    lambdaLg10 = MAX(lambdaLg10-1, -16);
    if( ++iters >= criteria.max_iter ||
        (change = cvNorm(param, prevParam, CV_RELATIVE_L2)) < criteria.epsilon )
    {
        _param = param;
        state = DONE;
        return false;
    }

    prevErrNorm = errNorm;
    cvZero( JtJ );
    cvZero( JtErr );
    _param = param;
    _JtJ = JtJ;
    _JtErr = JtErr;
    state = CALC_J;
    return true;
}

void CvLevMarq::step()
{
    const double LOG10 = log(10.);
    double lambda = exp(lambdaLg10*LOG10);
    int i, j, nparams = param->rows;

    for( i = 0; i < nparams; i++ )
        if( mask->data.ptr[i] == 0 )
        {
            double *row = JtJ->data.db + i*nparams, *col = JtJ->data.db + i;
            for( j = 0; j < nparams; j++ )
                row[j] = col[j*nparams] = 0;
            JtErr->data.db[i] = 0;
        }

    if( !err )
        cvCompleteSymm( JtJ, completeSymmFlag );
#if 1
    cvCopy( JtJ, JtJN );
    for( i = 0; i < nparams; i++ )
        JtJN->data.db[(nparams+1)*i] *= 1. + lambda;
#else
    cvSetIdentity(JtJN, cvRealScalar(lambda));
    cvAdd( JtJ, JtJN, JtJN );
#endif
    cvSVD( JtJN, JtJW, 0, JtJV, CV_SVD_MODIFY_A + CV_SVD_U_T + CV_SVD_V_T );
    cvSVBkSb( JtJW, JtJV, JtJV, JtErr, param, CV_SVD_U_T + CV_SVD_V_T );
    for( i = 0; i < nparams; i++ )
        param->data.db[i] = prevParam->data.db[i] - (mask->data.ptr[i] ? param->data.db[i] : 0);
}


CV_IMPL int cvRANSACUpdateNumIters( double p, double ep, int modelPoints, int maxIters )
{
    return cv::RANSACUpdateNumIters(p, ep, modelPoints, maxIters);
}


CV_IMPL int cvFindHomography( const CvMat* _src, const CvMat* _dst, CvMat* __H, int method,
                              double ransacReprojThreshold, CvMat* _mask )
{
    cv::Mat src = cv::cvarrToMat(_src), dst = cv::cvarrToMat(_dst);

    if( src.channels() == 1 && (src.rows == 2 || src.rows == 3) && src.cols > 3 )
        cv::transpose(src, src);
    if( dst.channels() == 1 && (dst.rows == 2 || dst.rows == 3) && dst.cols > 3 )
        cv::transpose(dst, dst);

    const cv::Mat H = cv::cvarrToMat(__H), mask = cv::cvarrToMat(_mask);
    cv::Mat H0 = cv::findHomography(src, dst, method, ransacReprojThreshold,
                                    _mask ? cv::_OutputArray(mask) : cv::_OutputArray());

    if( H0.empty() )
    {
        cv::Mat Hz = cv::cvarrToMat(__H);
        Hz.setTo(cv::Scalar::all(0));
        return 0;
    }
    H0.convertTo(H, H.type());
    return 1;
}


CV_IMPL int cvFindFundamentalMat( const CvMat* points1, const CvMat* points2,
                                  CvMat* fmatrix, int method,
                                  double param1, double param2, CvMat* _mask )
{
    cv::Mat m1 = cv::cvarrToMat(points1), m2 = cv::cvarrToMat(points2);

    if( m1.channels() == 1 && (m1.rows == 2 || m1.rows == 3) && m1.cols > 3 )
        cv::transpose(m1, m1);
    if( m2.channels() == 1 && (m2.rows == 2 || m2.rows == 3) && m2.cols > 3 )
        cv::transpose(m2, m2);

    const cv::Mat FM = cv::cvarrToMat(fmatrix), mask = cv::cvarrToMat(_mask);
    cv::Mat FM0 = cv::findFundamentalMat(m1, m2, method, param1, param2,
                                         _mask ? cv::_OutputArray(mask) : cv::_OutputArray());

    if( FM0.empty() )
    {
        cv::Mat FM0z = cv::cvarrToMat(fmatrix);
        FM0z.setTo(cv::Scalar::all(0));
        return 0;
    }

    CV_Assert( FM0.cols == 3 && FM0.rows % 3 == 0 && FM.cols == 3 && FM.rows % 3 == 0 && FM.channels() == 1 );
    cv::Mat FM1 = FM.rowRange(0, MIN(FM0.rows, FM.rows));
    FM0.rowRange(0, FM1.rows).convertTo(FM1, FM1.type());
    return FM1.rows / 3;
}


CV_IMPL void cvComputeCorrespondEpilines( const CvMat* points, int pointImageID,
                                          const CvMat* fmatrix, CvMat* _lines )
{
    cv::Mat pt = cv::cvarrToMat(points), fm = cv::cvarrToMat(fmatrix);
    cv::Mat lines = cv::cvarrToMat(_lines);
    const cv::Mat lines0 = lines;

    if( pt.channels() == 1 && (pt.rows == 2 || pt.rows == 3) && pt.cols > 3 )
        cv::transpose(pt, pt);

    cv::computeCorrespondEpilines(pt, pointImageID, fm, lines);

    bool tflag = lines0.channels() == 1 && lines0.rows == 3 && lines0.cols > 3;
    lines = lines.reshape(lines0.channels(), (tflag ? lines0.cols : lines0.rows));

    if( tflag )
    {
        CV_Assert( lines.rows == lines0.cols && lines.cols == lines0.rows );
        if( lines0.type() == lines.type() )
            transpose( lines, lines0 );
        else
        {
            transpose( lines, lines );
            lines.convertTo( lines0, lines0.type() );
        }
    }
    else
    {
        CV_Assert( lines.size() == lines0.size() );
        if( lines.data != lines0.data )
            lines.convertTo(lines0, lines0.type());
    }
}


CV_IMPL void cvConvertPointsHomogeneous( const CvMat* _src, CvMat* _dst )
{
    cv::Mat src = cv::cvarrToMat(_src), dst = cv::cvarrToMat(_dst);
    const cv::Mat dst0 = dst;

    int d0 = src.channels() > 1 ? src.channels() : MIN(src.cols, src.rows);

    if( src.channels() == 1 && src.cols > d0 )
        cv::transpose(src, src);

    int d1 = dst.channels() > 1 ? dst.channels() : MIN(dst.cols, dst.rows);

    if( d0 == d1 )
        src.copyTo(dst);
    else if( d0 < d1 )
        cv::convertPointsToHomogeneous(src, dst);
    else
        cv::convertPointsFromHomogeneous(src, dst);

    bool tflag = dst0.channels() == 1 && dst0.cols > d1;
    dst = dst.reshape(dst0.channels(), (tflag ? dst0.cols : dst0.rows));

    if( tflag )
    {
        CV_Assert( dst.rows == dst0.cols && dst.cols == dst0.rows );
        if( dst0.type() == dst.type() )
            transpose( dst, dst0 );
        else
        {
            transpose( dst, dst );
            dst.convertTo( dst0, dst0.type() );
        }
    }
    else
    {
        CV_Assert( dst.size() == dst0.size() );
        if( dst.data != dst0.data )
            dst.convertTo(dst0, dst0.type());
    }
}
