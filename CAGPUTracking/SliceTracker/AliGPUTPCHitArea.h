//-*- Mode: C++ -*-
// @(#) $Id: AliGPUTPCRow.h 27042 2008-07-02 12:06:02Z richterm $
// ************************************************************************
// This file is property of and copyright by the ALICE HLT Project        *
// ALICE Experiment at CERN, All rights reserved.                         *
// See cxx source for full Copyright notice                               *
//                                                                        *
//*************************************************************************

#ifndef ALIHLTTPCCAHITAREA_H
#define ALIHLTTPCCAHITAREA_H

#include "AliGPUTPCDef.h"

class AliGPUTPCHit;
class AliGPUTPCGrid;
MEM_CLASS_PRE()
class AliGPUTPCTracker;
MEM_CLASS_PRE()
class AliGPUTPCRow;
MEM_CLASS_PRE()
class AliGPUTPCSliceData;

/**
 * @class ALIHLTTPCCAHitArea
 *
 * This class is used to _iterate_ over the hit data via GetNext
 */
class AliGPUTPCHitArea
{
  public:
	MEM_TEMPLATE()
	GPUd() void Init(const MEM_TYPE(AliGPUTPCRow) & row, GPUglobalref() const MEM_GLOBAL(AliGPUTPCSliceData) & slice, float y, float z, float dy, float dz);

	/**
     * look up the next hit in the requested area.
     * Sets h to the coordinates and returns the index for the hit data
     */
	MEM_TEMPLATE()
	GPUd() int GetNext(GPUconstant() const MEM_CONSTANT(AliGPUTPCTracker) & tracker, const MEM_TYPE(AliGPUTPCRow) & row,
	                   GPUglobalref() const MEM_GLOBAL(AliGPUTPCSliceData) & slice, AliGPUTPCHit *h);

	float Y() const { return fY; }
	float Z() const { return fZ; }
	float MinZ() const { return fMinZ; }
	float MaxZ() const { return fMaxZ; }
	float MinY() const { return fMinY; }
	float MaxY() const { return fMaxY; }
	int BZmax() const { return fBZmax; }
	int BDY() const { return fBDY; }
	int IndYmin() const { return fIndYmin; }
	int Iz() const { return fIz; }
	int HitYfst() const { return fHitYfst; }
	int HitYlst() const { return fHitYlst; }
	int Ih() const { return fIh; }
	int Ny() const { return fNy; }
	int HitOffset() const { return fHitOffset; }

  protected:
	float fY;       // search coordinates
	float fZ;       // search coordinates
	float fMinZ;    // search coordinates
	float fMaxZ;    // search coordinates
	float fMinY;    // search coordinates
	float fMaxY;    // search coordinates
	int fBZmax;     // maximal Z bin index
	int fBDY;       // Y distance of bin indexes
	int fIndYmin;   // minimum index for
	int fIz;        // current Z bin index (incremented while iterating)
	int fHitYfst;   //
	int fHitYlst;   //
	int fIh;        // some XXX index in the hit data
	int fNy;        // Number of bins in Y direction
	int fHitOffset; // global hit offset XXX what's that?
};

#endif //ALIHLTTPCCAHITAREA_H
