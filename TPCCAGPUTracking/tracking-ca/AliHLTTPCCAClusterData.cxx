// **************************************************************************
// * This file is property of and copyright by the ALICE HLT Project        *
// * All rights reserved.                                                   *
// *                                                                        *
// * Primary Authors:                                                       *
// *     Copyright 2009       Matthias Kretz <kretz@kde.org>                *
// *                                                                        *
// * Permission to use, copy, modify and distribute this software and its   *
// * documentation strictly for non-commercial purposes is hereby granted   *
// * without fee, provided that the above copyright notice appears in all   *
// * copies and that both the copyright notice and this permission notice   *
// * appear in the supporting documentation. The authors make no claims     *
// * about the suitability of this software for any purpose. It is          *
// * provided "as is" without express or implied warranty.                  *
// **************************************************************************

#include "AliHLTTPCCAClusterData.h"
#include "AliHLTTPCCAMath.h"

void AliHLTTPCCAClusterData::StartReading( int sliceIndex, int guessForNumberOfClusters )
{
  // Start reading of event - initialisation

  fSliceIndex = sliceIndex;
  fFirstRow = 0;
  fLastRow = 0;
  fData.clear();
  fNumberOfClusters.reserve( 160 );
  fRowOffset.reserve( 160 );
  fData.reserve( CAMath::Max( 64, guessForNumberOfClusters ) );
}


void AliHLTTPCCAClusterData::FinishReading()
{
  // finish event reading - data sorting, etc.

  std::sort( fData.begin(), fData.end(), CompareClusters );
  if ( fData.size() ) fFirstRow = fData[0].fRow;

  fNumberOfClusters.clear();
  fRowOffset.clear();

  int row = fFirstRow;
  for ( int i = 0; i < row; ++i ) {
    fNumberOfClusters.push_back( 0 );
    fRowOffset.push_back( 0 );
  }
  fRowOffset.push_back( 0 );
  for ( unsigned int ic = 0; ic < fData.size(); ++ic ) {
    Data &cluster = fData[ic];
    while ( row < cluster.fRow ) {
      fNumberOfClusters.push_back( ic - fRowOffset.back() );
      fRowOffset.push_back( ic );
      ++row;
    }
  }
  fNumberOfClusters.push_back( fData.size() - fRowOffset.back() );
  fLastRow = row; // the last seen row is the last row in this slice
}

