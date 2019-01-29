#include "AliGPUReconstructionImpl.h"
#include "AliGPUReconstructionCommon.h"

#include "AliGPUTPCClusterData.h"
#include "AliGPUTPCSliceOutput.h"
#include "AliGPUTPCSliceOutTrack.h"
#include "AliGPUTPCSliceOutCluster.h"
#include "AliGPUTPCGMMergedTrack.h"
#include "AliGPUTPCGMMergedTrackHit.h"
#include "AliGPUTRDTrackletWord.h"
#include "AliHLTTPCClusterMCData.h"
#include "AliGPUTPCMCInfo.h"
#include "AliGPUTRDTrack.h"
#include "AliGPUTRDTracker.h"
#include "AliHLTTPCRawCluster.h"
#include "ClusterNativeAccessExt.h"
#include "AliGPUTRDTrackletLabels.h"
#include "AliGPUCADisplay.h"
#include "AliGPUCAQA.h"
#include "AliGPUMemoryResource.h"

#define GPUCA_LOGGING_PRINTF
#include "AliCAGPULogging.h"

int AliGPUReconstructionCPU::RunTPCTrackingSlices()
{
	bool error = false;
	//int nLocalTracks = 0, nGlobalTracks = 0, nOutputTracks = 0, nLocalHits = 0, nGlobalHits = 0;

	if (mOutputControl.OutputType != AliGPUCAOutputControl::AllocateInternal && mDeviceProcessingSettings.nThreads > 1)
	{
		CAGPUError("fOutputPtr must not be used with multiple threads\n");
		return(1);
	}
	int offset = 0;
	for (unsigned int iSlice = 0;iSlice < NSLICES;iSlice++)
	{
		if (error) continue;
		mTPCSliceTrackersCPU[iSlice].Data().SetClusterData(mIOPtrs.clusterData[iSlice], mIOPtrs.nClusterData[iSlice], offset);
		offset += mIOPtrs.nClusterData[iSlice];
	}
	PrepareEvent();
#ifdef GPUCA_HAVE_OPENMP
#pragma omp parallel for num_threads(mDeviceProcessingSettings.nThreads)
#endif
	for (unsigned int iSlice = 0;iSlice < NSLICES;iSlice++)
	{
		if (mTPCSliceTrackersCPU[iSlice].ReadEvent())
		{
			CAGPUError("Error initializing cluster data\n");
			error = true;
			continue;
		}
		mTPCSliceTrackersCPU[iSlice].SetOutput(&mSliceOutput[iSlice]);
		mTPCSliceTrackersCPU[iSlice].Reconstruct();
		mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNLocalTracks = mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNTracks;
		mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNLocalTrackHits = mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNTrackHits;
		if (!mParam.rec.GlobalTracking)
		{
			mTPCSliceTrackersCPU[iSlice].ReconstructOutput();
			//nOutputTracks += (*mTPCSliceTrackersCPU[iSlice].Output())->NTracks();
			//nLocalTracks += mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNTracks;
			if (!mDeviceProcessingSettings.eventDisplay)
			{
				mTPCSliceTrackersCPU[iSlice].SetupCommonMemory();
			}
		}
	}
	if (error) return(1);

	if (mParam.rec.GlobalTracking)
	{
		for (unsigned int iSlice = 0;iSlice < NSLICES;iSlice++)
		{
			int sliceLeft = (iSlice + (NSLICES / 2 - 1)) % (NSLICES / 2);
			int sliceRight = (iSlice + 1) % (NSLICES / 2);
			if (iSlice >= NSLICES / 2)
			{
				sliceLeft += NSLICES / 2;
				sliceRight += NSLICES / 2;
			}
			mTPCSliceTrackersCPU[iSlice].PerformGlobalTracking(mTPCSliceTrackersCPU[sliceLeft], mTPCSliceTrackersCPU[sliceRight], mTPCSliceTrackersCPU[sliceLeft].NMaxTracks(), mTPCSliceTrackersCPU[sliceRight].NMaxTracks());
		}
		for (unsigned int iSlice = 0;iSlice < NSLICES;iSlice++)
		{
			mTPCSliceTrackersCPU[iSlice].ReconstructOutput();
			//printf("Slice %d - Tracks: Local %d Global %d - Hits: Local %d Global %d\n", iSlice, mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNLocalTracks, mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNTracks, mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNLocalTrackHits, mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNTrackHits);
			//nLocalTracks += mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNLocalTracks;
			//nGlobalTracks += mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNTracks;
			//nLocalHits += mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNLocalTrackHits;
			//nGlobalHits += mTPCSliceTrackersCPU[iSlice].CommonMemory()->fNTrackHits;
			//nOutputTracks += (*mTPCSliceTrackersCPU[iSlice].Output())->NTracks();
			if (!mDeviceProcessingSettings.eventDisplay)
			{
				mTPCSliceTrackersCPU[iSlice].SetupCommonMemory();
			}
		}
	}
	for (unsigned int iSlice = 0;iSlice < NSLICES;iSlice++)
	{
		if (mTPCSliceTrackersCPU[iSlice].GPUParameters()->fGPUError != 0)
		{
			const char* errorMsgs[] = GPUCA_GPU_ERROR_STRINGS;
			const char* errorMsg = (unsigned) mTPCSliceTrackersCPU[iSlice].GPUParameters()->fGPUError >= sizeof(errorMsgs) / sizeof(errorMsgs[0]) ? "UNKNOWN" : errorMsgs[mTPCSliceTrackersCPU[iSlice].GPUParameters()->fGPUError];
			CAGPUError("Error during tracking: %s\n", errorMsg);
			return(1);
		}
	}
	//printf("Slice Tracks Output %d: - Tracks: %d local, %d global -  Hits: %d local, %d global\n", nOutputTracks, nLocalTracks, nGlobalTracks, nLocalHits, nGlobalHits);
	if (mDeviceProcessingSettings.debugMask & 1024)
	{
		for (unsigned int i = 0;i < NSLICES;i++)
		{
			mTPCSliceTrackersCPU[i].DumpOutput(stdout);
		}
	}
	return 0;
}

int AliGPUReconstructionCPU::RunTPCTrackingMerger()
{
	mTPCMergerCPU.Reconstruct();
	mIOPtrs.mergedTracks = mTPCMergerCPU.OutputTracks();
	mIOPtrs.nMergedTracks = mTPCMergerCPU.NOutputTracks();
	mIOPtrs.mergedTrackHits = mTPCMergerCPU.Clusters();
	mIOPtrs.nMergedTrackHits = mTPCMergerCPU.NOutputTrackClusters();
	return 0;
}

int AliGPUReconstructionCPU::RunTRDTracking()
{
	HighResTimer timer;
	timer.Start();
	
	if (!mTRDTracker->IsInitialized()) return 1;
	std::vector<GPUTRDTrack> tracksTPC;
	std::vector<int> tracksTPCLab;
	std::vector<int> tracksTPCId;

	for (unsigned int i = 0;i < mIOPtrs.nMergedTracks;i++)
	{
		const AliGPUTPCGMMergedTrack& trk = mIOPtrs.mergedTracks[i];
		if (!trk.OK()) continue;
		if (trk.Looper()) continue;
		if (mParam.rec.NWaysOuter) tracksTPC.emplace_back(trk.OuterParam());
		else tracksTPC.emplace_back(trk);
		tracksTPCId.push_back(i);
		tracksTPCLab.push_back(-1);
	}

	mTRDTracker->Reset();

	mTRDTracker->SetMaxData();
	if (GetDeviceProcessingSettings().memoryAllocationStrategy == AliGPUMemoryResource::ALLOCATION_INDIVIDUAL)
	{
		AllocateRegisteredMemory(mTRDTracker->MemoryTracks());
		AllocateRegisteredMemory(mTRDTracker->MemoryTracklets());
	}

	for (unsigned int iTracklet = 0;iTracklet < mIOPtrs.nTRDTracklets;++iTracklet)
	{
		if (mIOPtrs.trdTrackletsMC) mTRDTracker->LoadTracklet(mIOPtrs.trdTracklets[iTracklet], mIOPtrs.trdTrackletsMC[iTracklet].fLabel);
		else mTRDTracker->LoadTracklet(mIOPtrs.trdTracklets[iTracklet]);
	}

	for (unsigned int iTrack = 0; iTrack < tracksTPC.size(); ++iTrack)
	{
		mTRDTracker->LoadTrack(tracksTPC[iTrack], tracksTPCLab[iTrack]);
	}

	mTRDTracker->DoTracking();
	
	printf("TRD Tracker reconstructed %d tracks\n", mTRDTracker->NTracks());
	if (mDeviceProcessingSettings.debugLevel >= 1)
	{
		printf("TRD tracking time: %'d us\n", (int) (1000000 * timer.GetCurrentElapsedTime()));
	}
	
	return 0;
}