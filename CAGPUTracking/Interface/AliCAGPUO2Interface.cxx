// **************************************************************************
// This file is property of and copyright by the ALICE HLT Project          *
// ALICE Experiment at CERN, All rights reserved.                           *
//                                                                          *
// Primary Authors: Sergey Gorbunov <sergey.gorbunov@kip.uni-heidelberg.de> *
//                  Ivan Kisel <kisel@kip.uni-heidelberg.de>                *
//                  for The ALICE HLT Project.                              *
//                                                                          *
// Permission to use, copy, modify and distribute this software and its     *
// documentation strictly for non-commercial purposes is hereby granted     *
// without fee, provided that the above copyright notice appears in all     *
// copies and that both the copyright notice and this permission notice     *
// appear in the supporting documentation. The authors make no claims       *
// about the suitability of this software for any purpose. It is            *
// provided "as is" without express or implied warranty.                    *
//                                                                          *
//***************************************************************************

#include "AliCAGPUO2Interface.h"
#include "AliGPUReconstruction.h"
#include "AliGPUCAConfiguration.h"
#include "TPCFastTransform.h"
#include <iostream>
#include <fstream>
#ifdef GPUCA_HAVE_OPENMP
#include <omp.h>
#endif

#ifdef BUILD_EVENT_DISPLAY
#include "AliGPUCADisplayBackendGlfw.h"
#else
#include "AliGPUCADisplayBackend.h"
class AliGPUCADisplayBackendGlfw : public AliGPUCADisplayBackend {};
#endif

#include "DataFormatsTPC/ClusterNative.h"
#include "ClusterNativeAccessExt.h"

AliGPUTPCO2Interface::AliGPUTPCO2Interface() : fInitialized(false), fDumpEvents(false), fContinuous(false), mRec(nullptr), mConfig()
{
}

AliGPUTPCO2Interface::~AliGPUTPCO2Interface()
{
	Deinitialize();
}

int AliGPUTPCO2Interface::Initialize(const AliGPUCAConfiguration& config, std::unique_ptr<TPCFastTransform>&& fastTrans)
{
	if (fInitialized) return(1);
	mConfig.reset(new AliGPUCAConfiguration(config));
	fDumpEvents = mConfig->configInterface.dumpEvents;
	fContinuous = mConfig->configEvent.continuousMaxTimeBin != 0;
	mRec.reset(AliGPUReconstruction::CreateInstance(mConfig->configProcessing));
	mRec->mConfigDisplay = &mConfig->configDisplay;
	mRec->mConfigQA = &mConfig->configQA;
	mRec->SetSettings(&mConfig->configEvent, &mConfig->configReconstruction, &mConfig->configDeviceProcessing);
	mRec->SetTPCFastTransform(std::move(fastTrans));
	if (mRec->Init()) return(1);
	fInitialized = true;
	return(0);
}

int AliGPUTPCO2Interface::Initialize(const char* options, std::unique_ptr<TPCFastTransform>&& fastTrans)
{
	if (fInitialized) return(1);
	float solenoidBz = -5.00668;
	float refX = 1000.;
	int nThreads = 1;
	bool useGPU = false;
	char gpuType[1024];

	if (options && *options)
	{
		printf("Received options %s\n", options);
		const char* optPtr = options;
		while (optPtr && *optPtr)
		{
			while (*optPtr == ' ') optPtr++;
			const char* nextPtr = strstr(optPtr, " ");
			const int optLen = nextPtr ? nextPtr - optPtr : strlen(optPtr);
			if (strncmp(optPtr, "cont", optLen) == 0)
			{
				fContinuous = true;
				printf("Continuous tracking mode enabled\n");
			}
			else if (strncmp(optPtr, "dump", optLen) == 0)
			{
				fDumpEvents = true;
				printf("Dumping of input events enabled\n");
			}
#ifdef BUILD_EVENT_DISPLAY
			else if (strncmp(optPtr, "display", optLen) == 0)
			{
				mDisplayBackend.reset(new AliGPUCADisplayBackendGlfw);
				printf("Event display enabled\n");
			}
#endif
			else if (optLen > 3 && strncmp(optPtr, "bz=", 3) == 0)
			{
				sscanf(optPtr + 3, "%f", &solenoidBz);
				printf("Using solenoid field %f\n", solenoidBz);
			}
			else if (optLen > 5 && strncmp(optPtr, "refX=", 5) == 0)
			{
				sscanf(optPtr + 5, "%f", &refX);
				printf("Propagating to reference X %f\n", refX);
			}
			else if (optLen > 8 && strncmp(optPtr, "threads=", 8) == 0)
			{
				sscanf(optPtr + 8, "%d", &nThreads);
				printf("Using %d threads\n", nThreads);
			}
			else if (optLen > 8 && strncmp(optPtr, "gpuType=", 8) == 0)
			{
				int len = std::min(optLen - 8, 1023);
				memcpy(gpuType, optPtr + 8, len);
				gpuType[len] = 0;
				useGPU = true;
				printf("Using GPU Type %s\n", gpuType);
			}
			else
			{
				printf("Unknown option: %s\n", optPtr);
				return 1;
			}
			optPtr = nextPtr;
		}
	}

#ifdef GPUCA_HAVE_OPENMP
	omp_set_num_threads(nThreads);
#else
	if (nThreads != 1) printf("ERROR: Compiled without OpenMP. Cannot set number of threads!\n");
#endif
	mRec.reset(AliGPUReconstruction::CreateInstance(useGPU ? gpuType : "CPU", true));
	if (mRec == nullptr) return 1;
	
	AliGPUCASettingsRec rec;
	AliGPUCASettingsEvent ev;
	AliGPUCASettingsDeviceProcessing devProc;
	
	rec.SetDefaults();
	ev.SetDefaults();
	devProc.SetDefaults();
	
	ev.solenoidBz = solenoidBz;
	ev.continuousMaxTimeBin = fContinuous ? 0.023 * 5e6 : 0;

	rec.NWays = 3;
	rec.NWaysOuter = true;
	rec.SearchWindowDZDR = 2.5f;
	rec.TrackReferenceX = refX;
	
	devProc.eventDisplay = mDisplayBackend.get();
	
	mRec->SetSettings(&ev, &rec, &devProc);
	mRec->SetTPCFastTransform(std::move(fastTrans));
	if (mRec->Init()) return 1;

	fInitialized = true;
	return(0);
}

void AliGPUTPCO2Interface::Deinitialize()
{
	if (fInitialized)
	{
		mRec->Finalize();
		mRec.reset();
	}
	fInitialized = false;
}

int AliGPUTPCO2Interface::RunTracking(const o2::TPC::ClusterNativeAccessFullTPC* inputClusters, const AliGPUTPCGMMergedTrack* &outputTracks, int &nOutputTracks, const AliGPUTPCGMMergedTrackHit* &outputTrackClusters)
{
	if (!fInitialized) return(1);
	static int nEvent = 0;
	if (fDumpEvents)
	{
		mRec->ClearIOPointers();
		mRec->mIOPtrs.clustersNative = inputClusters;
		
		char fname[1024];
		sprintf(fname, "event.%d.dump", nEvent);
		mRec->DumpData(fname);
		if (nEvent == 0)
		{
			mRec->DumpSettings();
		}
	}
	
	mRec->mIOPtrs.clustersNative = inputClusters;
	mRec->ConvertNativeToClusterData();
	mRec->RunStandalone();

	outputTracks = mRec->mIOPtrs.mergedTracks;
	nOutputTracks = mRec->mIOPtrs.nMergedTracks;
	outputTrackClusters = mRec->mIOPtrs.mergedTrackHits;
	const ClusterNativeAccessExt* ext = mRec->GetClusterNativeAccessExt();
	for (int i = 0;i < mRec->mIOPtrs.nMergedTrackHits;i++)
	{
		AliGPUTPCGMMergedTrackHit& cl = (AliGPUTPCGMMergedTrackHit&) mRec->mIOPtrs.mergedTrackHits[i];
		cl.fNum -= ext->clusterOffset[cl.fSlice][cl.fRow];
	}
	nEvent++;
	return(0);
}

void AliGPUTPCO2Interface::GetClusterErrors2( int row, float z, float sinPhi, float DzDs, float &ErrY2, float &ErrZ2 ) const
{
	if (!fInitialized) return;
	mRec->GetParam().GetClusterErrors2(row, z, sinPhi, DzDs, ErrY2, ErrZ2);
}

void AliGPUTPCO2Interface::Cleanup()
{

}
