/*=========================================================================
 *
 *  Copyright UMC Utrecht and contributors
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef __itkRecursiveBSplineInterpolateImageFunction_hxx
#define __itkRecursiveBSplineInterpolateImageFunction_hxx

#include "itkRecursiveBSplineInterpolateImageFunction.h"
#include "itkRecursiveBSplineImplementation.h"

#include "itkImageLinearIteratorWithIndex.h"
#include "itkImageRegionConstIteratorWithIndex.h"
#include "itkImageRegionIterator.h"
#include "itkVector.h"
#include "itkMatrix.h"
//#include "emm_vec.hxx"

namespace itk
{

/**
 * ******************* Constructor ***********************
 */

template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::RecursiveBSplineInterpolateImageFunction()
{
  this->m_UseImageDirection = true;

  /** Setup coefficient filter. */
  this->m_CoefficientFilter = CoefficientFilter::New();
  this->m_CoefficientFilter->SetSplineOrder( SplineOrder );
  this->m_Coefficients = CoefficientImageType::New();

  if( SplineOrder > 5 )
  {
    itkExceptionMacro( << "SplineOrder must be between 0 and 5. Requested spline order has not been implemented yet." );
  }

  this->m_RecursiveBSplineWeightFunction = RecursiveBSplineWeightFunctionType::New();
  this->m_Kernel = KernelType::New();
  this->m_DerivativeKernel = DerivativeKernelType::New();
  this->m_SecondOrderDerivativeKernel = SecondOrderDerivativeKernelType::New();

} // end Constructor()


/**
 * ******************* PrintSelf ***********************
 */

template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
void
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::PrintSelf(std::ostream & os,Indent indent) const
{
  Superclass::PrintSelf(os, indent);
  os << indent << "Spline Order: " << SplineOrder << std::endl;
  os << indent << "UseImageDirection = "
    << ( this->m_UseImageDirection ? "On" : "Off" ) << std::endl;
} // end PrintSelf()


/**
 * ******************* SetInputImage ***********************
 */

template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
void
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::SetInputImage( const TImageType *inputData )
{
  if( inputData )
  {
    Superclass::SetInputImage( inputData );

    this->m_CoefficientFilter->SetInput( inputData );
    this->m_CoefficientFilter->Update();
    this->m_Coefficients = m_CoefficientFilter->GetOutput();
    this->m_DataLength = inputData->GetBufferedRegion().GetSize();

    for( unsigned int n = 0; n < ImageDimension; ++n )
    {
      this->m_OffsetTable[n] = this->m_Coefficients->GetOffsetTable()[n];
    }

    this->m_Spacing = inputData->GetSpacing();
  }
  else
  {
    this->m_Coefficients = NULL;
  }
} // end SetInputImage()


/**
 * ******************* Evaluate ***********************
 */

template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
typename RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >::OutputType
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::Evaluate( const PointType & point ) const
{
  ContinuousIndexType cindex;
  this->GetInputImage()->TransformPhysicalPointToContinuousIndex( point, cindex );
  return this->EvaluateAtContinuousIndex( cindex );
} // end Evaluate()


/**
 * ******************* EvaluateDerivative ***********************
 */

template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
typename RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >::CovariantVectorType
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::EvaluateDerivative( const PointType & point ) const
{
  ContinuousIndexType cindex;
  this->GetInputImage()->TransformPhysicalPointToContinuousIndex( point, cindex );
  return this->EvaluateDerivativeAtContinuousIndex( cindex );
} // end EvaluateDerivative()


/**
 * ******************* EvaluateValueAndDerivative ***********************
 */

template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
void
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::EvaluateValueAndDerivative( const PointType & point, OutputType & value, CovariantVectorType & deriv ) const
{
  ContinuousIndexType cindex;
  this->GetInputImage()->TransformPhysicalPointToContinuousIndex( point, cindex );
  this->EvaluateValueAndDerivativeAtContinuousIndex( cindex, value, deriv );
} // end EvaluateValueAndDerivative()


/**
 * ******************* EvaluateAtContinuousIndex ***********************
 */

template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
typename
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::OutputType
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::EvaluateAtContinuousIndex( const ContinuousIndexType & x ) const
{
  // Allocate memory on the stack
    /** Define some constants. */
   const unsigned int numberOfWeights = RecursiveBSplineWeightFunctionType::NumberOfWeights;

  // MS: use MaxNumberInterpolationPoints??
  //of: const unsigned int helper = ( SplineOrder + 1 ) * ImageDimension;
  long evaluateIndexData[(SplineOrder+1)*ImageDimension];
  long stepsData[(SplineOrder+1)*ImageDimension];
  vnl_matrix_ref<long> evaluateIndex(ImageDimension,SplineOrder+1,evaluateIndexData);
  long * steps = &(stepsData[0]);

  typename WeightsType::ValueType weightsArray1D[ numberOfWeights ];
  WeightsType weights1D( weightsArray1D, numberOfWeights, false );
  typename WeightsType::ValueType * weightsPtr = &weights1D[0];

  IndexType supportIndex;
  this->m_RecursiveBSplineWeightFunction->Evaluate( x, weights1D, supportIndex );

  // Compute the interpolation indexes
  this->DetermineRegionOfSupport( evaluateIndex, x );

  // Modify evaluateIndex at the boundaries using mirror boundary conditions
  this->ApplyMirrorBoundaryConditions( evaluateIndex );

  // MS: should we store steps in a member variable for later use?
  //Calculate steps for image pointer
  for( unsigned int n = 0; n < ImageDimension; ++n )
  {
    for( unsigned int k = 0; k <= SplineOrder; ++k )
    {
      steps[ ( SplineOrder + 1 ) * n + k ] = evaluateIndex[ n ][ k ] * this->m_OffsetTable[ n ];
    }
  }

  TCoefficientType * coefficientPointer = const_cast< TCoefficientType* >( this->m_Coefficients->GetBufferPointer() );
  OutputType interpolated = RecursiveBSplineImplementation_GetSample< OutputType, ImageDimension, SplineOrder, OutputType*, USE_STEPS >
                ::GetSample(coefficientPointer, steps, weightsPtr );

  return interpolated;
} // end EvaluateAtContinuousIndex()


/**
 * ******************* EvaluateValueAndDerivativeAtContinuousIndex ***********************
 */

template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
void
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::EvaluateValueAndDerivativeAtContinuousIndex(
  const ContinuousIndexType & x,
  OutputType & value,
  CovariantVectorType & derivative ) const
{
  // MS: use MaxNumberInterpolationPoints??
  // Allocate memory on the stack
  long evaluateIndexData[(SplineOrder+1)*ImageDimension];
  long stepsData[(SplineOrder+1)*ImageDimension];

  vnl_matrix_ref<long> evaluateIndex( ImageDimension, SplineOrder + 1, evaluateIndexData );
  long * steps = &(stepsData[0]);

  /** Create storage for the B-spline interpolation weights. */
  const unsigned int numberOfWeights = RecursiveBSplineWeightFunctionType::NumberOfWeights;
  typename WeightsType::ValueType weightsArray1D[ numberOfWeights ];
  WeightsType weights1D( weightsArray1D, numberOfWeights, false );
  typename WeightsType::ValueType derivativeWeightsArray1D[ numberOfWeights ];
  WeightsType derivativeWeights1D( derivativeWeightsArray1D, numberOfWeights, false );

  double * weightsPointer = &(weights1D[0]);
  double * derivativeWeightsPointer = &(derivativeWeights1D[0]);

  // Compute the interpolation indexes
  this->DetermineRegionOfSupport( evaluateIndex, x );

  IndexType supportIndex;
  this->m_RecursiveBSplineWeightFunction->Evaluate( x, weights1D, supportIndex );
  this->m_RecursiveBSplineWeightFunction->EvaluateDerivative( x, derivativeWeights1D, supportIndex );

  // Modify EvaluateIndex at the boundaries using mirror boundary conditions
  this->ApplyMirrorBoundaryConditions( evaluateIndex );

  // Calculate steps for coefficients pointer
  for( unsigned int n = 0; n < ImageDimension; ++n )
  {
    for( unsigned int k = 0; k <= SplineOrder; ++k )
    {
      steps[ ( SplineOrder + 1 ) * n + k ] = evaluateIndex[ n ][ k ] * this->m_OffsetTable[ n ];
    }
  }

  // Call recursive sampling function
  OutputType derivativeValue[ ImageDimension + 1 ];

  /** Recursively compute the spatial Jacobian. */
  TCoefficientType * coefficientPointer = const_cast< TCoefficientType* >( this->m_Coefficients->GetBufferPointer() );
  RecursiveBSplineImplementation_GetSpatialJacobian< OutputType*, ImageDimension, SplineOrder, OutputType*, USE_STEPS >
            ::GetSpatialJacobian( &derivativeValue[0], coefficientPointer, steps, weightsPointer, derivativeWeightsPointer  );


  // Extract the interpolated value and the derivative from the derivativeValue
  // vector. Element 0 contains the value, element 1 to ImageDimension+1 contains
  // the derivative in each dimension.
  for( unsigned int n = 0; n < ImageDimension; ++n )
  {
    derivative[ n ] = derivativeValue[ n + 1 ] / this->m_Spacing[ n ];
  }

  /** Assign value and derivative. */
  value = derivativeValue[ 0 ];
  const InputImageType *inputImage = this->GetInputImage();
  if( this->m_UseImageDirection )
  {
    CovariantVectorType orientedDerivative;
    inputImage->TransformLocalVectorToPhysicalVector( derivative, orientedDerivative );
    derivative = orientedDerivative;
  }
} // end EvaluateValueAndDerivativeAtContinuousIndex()


/**
 * ******************* EvaluateDerivativeAtContinuousIndex ***********************
 */

template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
typename
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::CovariantVectorType
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::EvaluateDerivativeAtContinuousIndex( const ContinuousIndexType & x ) const
{
  // MS: We can avoid code duplication by letting this function call
  // EvaluateValueAndDerivativeAtContinuousIndex and then ignore the value
  // Would the performance penalty be large?

    OutputType value;
    CovariantVectorType derivative;
    this->EvaluateValueAndDerivativeAtContinuousIndex( x, value, derivative );

  // MS: use MaxNumberInterpolationPoints??
  // Allocate memory on the stack
//  long evaluateIndexData[(SplineOrder+1)*ImageDimension];
//  long stepsData[(SplineOrder+1)*ImageDimension];
//  double weightsData[(SplineOrder+1)*ImageDimension];
//  double derivativeWeightsData[(SplineOrder+1)*ImageDimension];

//  vnl_matrix_ref<long> evaluateIndex( ImageDimension, SplineOrder + 1, evaluateIndexData );
//  double * weights = &(weightsData[0]);
//  double * derivativeWeights = &(derivativeWeightsData[0]);
//  long * steps = &(stepsData[0]);

//  // Compute the interpolation indexes
//  this->DetermineRegionOfSupport( evaluateIndex, x );

//  // Compute the B-spline weights
//  this->SetInterpolationWeights( x, evaluateIndex, weights );

//  // Compute the B-spline derivative weights
//  this->SetDerivativeWeights( x, evaluateIndex, derivativeWeights );

//  // Modify EvaluateIndex at the boundaries using mirror boundary conditions
//  this->ApplyMirrorBoundaryConditions( evaluateIndex );

//  const InputImageType *inputImage = this->GetInputImage();

//  //Calculate steps for coefficients pointer
//  for( unsigned int n = 0; n < ImageDimension; ++n )
//  {
//    for( unsigned int k = 0; k <= SplineOrder; ++k )
//    {
//      steps[ ( SplineOrder + 1 ) * n + k ] = evaluateIndex[ n ][ k ] * m_OffsetTable[ n ];
//    }
//  }

//  // Call recursive sampling function. Since the value is computed almost for
//  // free, both value and derivative are calculated.
//  TCoordRep derivativeValue[ ImageDimension + 1 ];
//  SampleFunction< ImageDimension, SplineOrder, TCoordRep >
//    ::SampleValueAndDerivative( derivativeValue,
//    this->m_Coefficients->GetBufferPointer(),
//    steps,
//    weights,
//    derivativeWeights );

//  CovariantVectorType derivative;

//  // Extract the interpolated value and the derivative from the derivativeValue
//  // vector. Element 0 contains the value, element 1 to ImageDimension+1 contains
//  // the derivative in each dimension.
//  for( unsigned int n = 0; n < ImageDimension; ++n )
//  {
//    derivative[ n ] = derivativeValue[ n + 1 ] / this->m_Spacing[ n ];
//  }

//  if( this->m_UseImageDirection )
//  {
//    CovariantVectorType orientedDerivative;
//    inputImage->TransformLocalVectorToPhysicalVector( derivative, orientedDerivative );
//    return orientedDerivative;
//  }

  return derivative;
} // end EvaluateDerivativeAtContinuousIndex()

/**
 * ******************* EvaluateValueAndDerivativeAndHessianAtContinuousIndex ***********************
 */

template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
void
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::EvaluateValueAndDerivativeAndHessianAtContinuousIndex(
  const ContinuousIndexType & x,
  OutputType & value,
  CovariantVectorType & derivative,
  MatrixType & sh) const
{
  // MS: use MaxNumberInterpolationPoints??
  // Allocate memory on the stack
  long evaluateIndexData[(SplineOrder+1)*ImageDimension];
  long stepsData[(SplineOrder+1)*ImageDimension];

  vnl_matrix_ref<long> evaluateIndex( ImageDimension, SplineOrder + 1, evaluateIndexData );
  long * steps = &(stepsData[0]);

  /** Create storage for the B-spline interpolation weights. */
  const unsigned int numberOfWeights = RecursiveBSplineWeightFunctionType::NumberOfWeights;
  typename WeightsType::ValueType weightsArray1D[ numberOfWeights ];
  WeightsType weights1D( weightsArray1D, numberOfWeights, false );
  typename WeightsType::ValueType derivativeWeightsArray1D[ numberOfWeights ];
  WeightsType derivativeWeights1D( derivativeWeightsArray1D, numberOfWeights, false );
  typename WeightsType::ValueType hessianWeightsArray1D[ numberOfWeights ];
  WeightsType hessianWeights1D( hessianWeightsArray1D, numberOfWeights, false);

  double * weightsPointer = &(weights1D[0]);
  double * derivativeWeightsPointer = &(derivativeWeights1D[0]);
  double * hessianWeightsPointer = &(hessianWeights1D[0]);

  // Compute the interpolation indexes
  this->DetermineRegionOfSupport( evaluateIndex, x );

  IndexType supportIndex;
  this->m_RecursiveBSplineWeightFunction->Evaluate( x, weights1D, supportIndex );
  this->m_RecursiveBSplineWeightFunction->EvaluateDerivative( x, derivativeWeights1D, supportIndex );
  this->m_RecursiveBSplineWeightFunction->EvaluateSecondOrderDerivative(x, hessianWeights1D, supportIndex );

  // Modify EvaluateIndex at the boundaries using mirror boundary conditions
  this->ApplyMirrorBoundaryConditions( evaluateIndex );

  // Calculate steps for coefficients pointer
  for( unsigned int n = 0; n < ImageDimension; ++n )
  {
    for( unsigned int k = 0; k <= SplineOrder; ++k )
    {
      steps[ ( SplineOrder + 1 ) * n + k ] = evaluateIndex[ n ][ k ] * this->m_OffsetTable[ n ];
    }
  }

  // Call recursive sampling function
  OutputType hessian[ ImageDimension * ( ImageDimension + 1 ) * ( ImageDimension + 2 ) / 2 ];

  /** Recursively compute the spatial Jacobian. */
  TCoefficientType * coefficientPointer = const_cast< TCoefficientType* >( this->m_Coefficients->GetBufferPointer() );
  RecursiveBSplineImplementation_GetSpatialHessian< OutputType*, ImageDimension, SplineOrder, OutputType*, USE_STEPS >
            ::GetSpatialHessian( &hessian[0], coefficientPointer, steps, weightsPointer, derivativeWeightsPointer, hessianWeightsPointer );

  /** Copy the correct elements to the spatial Hessian.
 * The first SpaceDimension elements are actually the displacement, i.e. the recursive
 * function GetSpatialHessian() has the TransformPoint as a free by-product.
 * In addition, the spatial Jacobian is a by-product.
 */

//  unsigned int k = 2 * ImageDimension;
//  for( unsigned int i = 0; i < ImageDimension; ++i )
//  {
//      for( unsigned int j = 0; j < ( i + 1 ) * ImageDimension; ++j )
//      {
//          sh[ i ][ j / ImageDimension ] = hessian[ k + j ];
//      }
//      k += ( i + 2 ) * ImageDimension;
//  }

//  /** Mirror, as only the lower triangle is now filled. */
//  for( unsigned int j = 0; j < ImageDimension - 1; ++j )
//  {
//      for( unsigned int k = 1; k < ImageDimension; ++k )
//      {
//          sh[ j ][ k ] = sh[ k ][ j ];
//      }
//  }

//  const InputImageType *inputImage = this->GetInputImage();
//  if( this->m_UseImageDirection )
//  {
//    MatrixType orientedHessian;
//    inputImage->TransformLocalVectorToPhysicalVector( hessian, orientedHessian );
//    sh = orientedHessian;
//  }

} // end EvaluateValueAndDerivativeAndHessianAtContinuousIndex()


/**
  * ******************* SetInterpolationWeights ***********************
  */

//template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
//void
//RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
//::SetInterpolationWeights(
//  const ContinuousIndexType & x,
//  const vnl_matrix< long > & evaluateIndex,
//  double * weights ) const
//{
//  Vector< double, SplineOrder + 1 > weightsvec;
//  const int idx = Math::Floor<int>( SplineOrder / 2.0 );

//  for( unsigned int n = 0; n < ImageDimension; ++n )
//  {
//    weightsvec.Fill( 0.0 );

//    double w = x[ n ] - (double)evaluateIndex[ n ][ idx ];
//    BSplineWeights< SplineOrder, TCoefficientType >::GetWeights( weightsvec, w );
//    for( unsigned int k = 0; k <= SplineOrder; ++k )
//    {
//      weights[ ( SplineOrder + 1 ) * n + k ] = weightsvec[ k ];
//    }
//  }
//} // end SetInterpolationWeights()


/**
 * ******************* SetDerivativeWeights ***********************
 */

//template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
//void
//RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
//::SetDerivativeWeights(
//  const ContinuousIndexType & x,
//  const vnl_matrix< long > & evaluateIndex,
//  double * weights ) const
//{
//  Vector< double, SplineOrder + 1 > weightsvec;
//  const int idx = Math::Floor<int>( ( SplineOrder + 1 ) / 2.0 );

//  for( unsigned int n = 0; n < ImageDimension; ++n )
//  {
//    weightsvec.Fill( 0.0 );
//    const double w = x[ n ] - (double)evaluateIndex[ n ][ idx ] + 0.5;
//    BSplineWeights< SplineOrder, TCoefficientType >::GetDerivativeWeights( weightsvec, w );

//    for( unsigned int k = 0; k <= SplineOrder; ++k )
//    {
//      weights[ ( SplineOrder + 1 ) * n + k ] = weightsvec[ k ];
//    }
//  }
//} // end SetDerivativeWeights()


/**
 * ******************* SetHessianWeights ***********************
 */

//template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int splineOrder >
//void
//RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, splineOrder >
//::SetHessianWeights(const ContinuousIndexType & x,
//                    const vnl_matrix< long > & evaluateIndex,
//                    double * weights) const
//{
//    itk::Vector<double, splineOrder+1> weightsvec;
//    weightsvec.Fill( 0.0 );

//    for ( unsigned int n = 0; n < ImageDimension; n++ )
//    {
//        int idx = floor( splineOrder / 2.0 );//FIX
//        double w = x[n] - (double)evaluateIndex[n][idx];
//        this->m_BSplineWeightInstance->getHessianWeights(weightsvec, w);
//        for(unsigned int k = 0; k <= splineOrder; ++k)
//        {
//            weights[(splineOrder+1)*n+k] = weightsvec[k];
//        }
//        weightsvec.Fill( 0.0 );
//    }
//}


/**
 * ******************* DetermineRegionOfSupport ***********************
 */

template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
void
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::DetermineRegionOfSupport(vnl_matrix< long > & evaluateIndex, const ContinuousIndexType & x) const
{
  const float halfOffset = SplineOrder & 1 ? 0.0 : 0.5;
  for( unsigned int n = 0; n < ImageDimension; ++n )
  {
    long indx = Math::Floor<long>( (float)x[ n ] + halfOffset ) - SplineOrder / 2;
    for( unsigned int k = 0; k <= SplineOrder; ++k )
    {
      evaluateIndex[ n ][ k ] = indx++;
    }
  }
} // end DetermineRegionOfSupport()


/**
 * ******************* ApplyMirrorBoundaryConditions ***********************
 */

template< class TImageType, class TCoordRep, class TCoefficientType, unsigned int SplineOrder >
void
RecursiveBSplineInterpolateImageFunction< TImageType, TCoordRep, TCoefficientType, SplineOrder >
::ApplyMirrorBoundaryConditions( vnl_matrix< long > & evaluateIndex ) const
{
  const IndexType startIndex = this->GetStartIndex();
  const IndexType endIndex = this->GetEndIndex();

  for( unsigned int n = 0; n < ImageDimension; ++n )
  {
    // apply the mirror boundary conditions
    // TODO:  We could implement other boundary options beside mirror
    if( m_DataLength[n] == 1 )
    {
      for( unsigned int k = 0; k <= SplineOrder; ++k )
      {
        evaluateIndex[ n ][ k ] = 0;
      }
    }
    else
    {
      for( unsigned int k = 0; k <= SplineOrder; ++k )
      {
        if( evaluateIndex[n][k] < startIndex[n] )
        {
          evaluateIndex[n][k] = startIndex[n] +
            ( startIndex[n] - evaluateIndex[n][k] );
        }
        if( evaluateIndex[n][k] >= endIndex[n] )
        {
          evaluateIndex[n][k] = endIndex[n] -
            ( evaluateIndex[n][k] - endIndex[n] );
        }
      }
    }
  }
} // end ApplyMirrorBoundaryConditions()


} // namespace itk

#endif