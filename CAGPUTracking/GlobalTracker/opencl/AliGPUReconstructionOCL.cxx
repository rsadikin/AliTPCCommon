#define GPUCA_GPUTYPE_RADEON

#include "AliGPUReconstructionOCL.h"
#include "AliGPUReconstructionOCLInternals.h"
#include "AliGPUReconstructionCommon.h"

#include <string.h>
#include <unistd.h>

#include "../makefiles/opencl_obtain_program.h"
extern "C" char _makefile_opencl_program_GlobalTracker_opencl_AliGPUReconstructionOCL_cl[];

#define quit(msg) {CAGPUError(msg);return(1);}

#include "AliGPUReconstructionOCLWorkarounds.h"

template <class T, int I, typename... Args> int AliGPUReconstructionOCLBackend::runKernelBackend(const krnlExec& x, const krnlRunRange& y, const krnlEvent& z, const Args&... args)
{
	if (x.device == krnlDeviceType::CPU) return AliGPUReconstructionCPU::runKernelBackend<T, I>(x, y, z, args...);
	cl_kernel k = getKernelObject<cl_kernel, T, I>();
	if (y.num == (unsigned int) -1)
	{
		if (OCLsetKernelParameters(k, mInternals->mem_gpu, mInternals->mem_constant, args...)) return 1;
	}
	else if (y.num == 0)
	{
		if (OCLsetKernelParameters(k, mInternals->mem_gpu, mInternals->mem_constant, y.start, args...)) return 1;
	}
	else
	{
		if (OCLsetKernelParameters(k, mInternals->mem_gpu, mInternals->mem_constant, y.start, y.num, args...)) return 1;
	}
	return clExecuteKernelA(mInternals->command_queue[x.stream], k, x.nThreads, x.nThreads * x.nBlocks, (cl_event*) z.ev, (cl_event*) z.evList, z.nEvents);
}

AliGPUReconstructionOCLBackend::AliGPUReconstructionOCLBackend(const AliGPUCASettingsProcessing& cfg) : AliGPUReconstructionDeviceBase(cfg)
{
	mInternals = new AliGPUReconstructionOCLInternals;
	mProcessingSettings.deviceType = OCL;
	mITSTrackerTraits.reset(new o2::ITS::TrackerTraitsCPU);

	mHostMemoryBase = nullptr;
	mInternals->devices = nullptr;
}

AliGPUReconstructionOCLBackend::~AliGPUReconstructionOCLBackend()
{
	delete mInternals;
}

AliGPUReconstruction* AliGPUReconstruction_Create_OCL(const AliGPUCASettingsProcessing& cfg)
{
	return new AliGPUReconstructionOCL(cfg);
}

int AliGPUReconstructionOCLBackend::InitDevice_Runtime()
{
	//Find best OPENCL device, initialize and allocate memory

	cl_int ocl_error;
	cl_uint num_platforms;
	if (clGetPlatformIDs(0, nullptr, &num_platforms) != CL_SUCCESS) quit("Error getting OpenCL Platform Count");
	if (num_platforms == 0) quit("No OpenCL Platform found");
	if (mDeviceProcessingSettings.debugLevel >= 2) CAGPUInfo("%d OpenCL Platforms found", num_platforms);
	
	//Query platforms
	cl_platform_id* platforms = new cl_platform_id[num_platforms];
	if (platforms == nullptr) quit("Memory allocation error");
	if (clGetPlatformIDs(num_platforms, platforms, nullptr) != CL_SUCCESS) quit("Error getting OpenCL Platforms");

	cl_platform_id platform;
	bool found = false;
	if (mDeviceProcessingSettings.platformNum >= 0)
	{
		if (mDeviceProcessingSettings.platformNum >= (int) num_platforms)
		{
			CAGPUError("Invalid platform specified");
			return(1);
		}
		platform = platforms[mDeviceProcessingSettings.platformNum];
		found = true;
	}
	else
	{
		for (unsigned int i_platform = 0;i_platform < num_platforms;i_platform++)
		{
			char platform_profile[64], platform_version[64], platform_name[64], platform_vendor[64];
			clGetPlatformInfo(platforms[i_platform], CL_PLATFORM_PROFILE, 64, platform_profile, nullptr);
			clGetPlatformInfo(platforms[i_platform], CL_PLATFORM_VERSION, 64, platform_version, nullptr);
			clGetPlatformInfo(platforms[i_platform], CL_PLATFORM_NAME, 64, platform_name, nullptr);
			clGetPlatformInfo(platforms[i_platform], CL_PLATFORM_VENDOR, 64, platform_vendor, nullptr);
			if (mDeviceProcessingSettings.debugLevel >= 2) {CAGPUDebug("Available Platform %d: (%s %s) %s %s\n", i_platform, platform_profile, platform_version, platform_vendor, platform_name);}
			if (strcmp(platform_vendor, "Advanced Micro Devices, Inc.") == 0)
			{
				found = true;
				if (mDeviceProcessingSettings.debugLevel >= 2) CAGPUInfo("AMD OpenCL Platform found");
				platform = platforms[i_platform];
				break;
			}
		}
	}
	delete[] platforms;
	if (found == false)
	{
		CAGPUError("Did not find AMD OpenCL Platform");
		return(1);
	}

	cl_uint count, bestDevice = (cl_uint) -1;
	double bestDeviceSpeed = -1, deviceSpeed;
	if (GPUFailedMsgI(clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, nullptr, &count)))
	{
		CAGPUError("Error getting OPENCL Device Count");
		return(1);
	}

	//Query devices
	mInternals->devices = new cl_device_id[count];
	if (mInternals->devices == nullptr) quit("Memory allocation error");
	if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, count, mInternals->devices, nullptr) != CL_SUCCESS) quit("Error getting OpenCL devices");

	char device_vendor[64], device_name[64];
	cl_device_type device_type;
	cl_uint freq, shaders;

	if (mDeviceProcessingSettings.debugLevel >= 2) CAGPUInfo("Available OPENCL devices:");
	for (unsigned int i = 0;i < count;i++)
	{
		if (mDeviceProcessingSettings.debugLevel >= 3) {CAGPUInfo("Examining device %d\n", i);}
		cl_uint nbits;

		clGetDeviceInfo(mInternals->devices[i], CL_DEVICE_NAME, 64, device_name, nullptr);
		clGetDeviceInfo(mInternals->devices[i], CL_DEVICE_VENDOR, 64, device_vendor, nullptr);
		clGetDeviceInfo(mInternals->devices[i], CL_DEVICE_TYPE, sizeof(cl_device_type), &device_type, nullptr);
		clGetDeviceInfo(mInternals->devices[i], CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(freq), &freq, nullptr);
		clGetDeviceInfo(mInternals->devices[i], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(shaders), &shaders, nullptr);
		clGetDeviceInfo(mInternals->devices[i], CL_DEVICE_ADDRESS_BITS, sizeof(nbits), &nbits, nullptr);
		int deviceOK = true;
		const char* deviceFailure = "";
		if (mDeviceProcessingSettings.gpuDeviceOnly && ((device_type & CL_DEVICE_TYPE_CPU) || !(device_type & CL_DEVICE_TYPE_GPU))) {deviceOK = false; deviceFailure = "No GPU device";}
		if (nbits / 8 != sizeof(void*)) {deviceOK = false; deviceFailure = "No 64 bit device";}

		deviceSpeed = (double) freq * (double) shaders;
		if (mDeviceProcessingSettings.debugLevel >= 2) CAGPUImportant("Device %s%2d: %s %s (Frequency %d, Shaders %d, %d bit) (Speed Value: %lld)%s %s\n", deviceOK ? " " : "[", i, device_vendor, device_name, (int) freq, (int) shaders, (int) nbits, (long long int) deviceSpeed, deviceOK ? " " : " ]", deviceOK ? "" : deviceFailure);
		if (!deviceOK) continue;
		if (deviceSpeed > bestDeviceSpeed)
		{
			bestDevice = i;
			bestDeviceSpeed = deviceSpeed;
		}
		else
		{
			if (mDeviceProcessingSettings.debugLevel >= 0) CAGPUInfo("Skipping: Speed %f < %f\n", deviceSpeed, bestDeviceSpeed);
		}
	}
	if (bestDevice == (cl_uint) -1)
	{
		CAGPUWarning("No %sOPENCL Device available, aborting OPENCL Initialisation", count ? "appropriate " : "");
		return(1);
	}

	if (mDeviceProcessingSettings.deviceNum > -1)
	{
		if (mDeviceProcessingSettings.deviceNum < (signed) count)
		{
			bestDevice = mDeviceProcessingSettings.deviceNum;
		}
		else
		{
			CAGPUWarning("Requested device ID %d non existend, falling back to default device id %d", mDeviceProcessingSettings.deviceNum, bestDevice);
		}
	}
	mInternals->device = mInternals->devices[bestDevice];

	clGetDeviceInfo(mInternals->device, CL_DEVICE_NAME, 64, device_name, nullptr);
	clGetDeviceInfo(mInternals->device, CL_DEVICE_VENDOR, 64, device_vendor, nullptr);
	clGetDeviceInfo(mInternals->device, CL_DEVICE_TYPE, sizeof(cl_device_type), &device_type, nullptr);
	clGetDeviceInfo(mInternals->device, CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(freq), &freq, nullptr);
	clGetDeviceInfo(mInternals->device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(shaders), &shaders, nullptr);
	if (mDeviceProcessingSettings.debugLevel >= 2) {CAGPUDebug("Using OpenCL device %d: %s %s (Frequency %d, Shaders %d)\n", bestDevice, device_vendor, device_name, (int) freq, (int) shaders);}

	cl_uint compute_units;
	clGetDeviceInfo(mInternals->device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cl_uint), &compute_units, nullptr);

	fBlockCount = compute_units;
	fThreadCount = GPUCA_GPU_THREAD_COUNT;
	fConstructorBlockCount = compute_units * GPUCA_GPU_BLOCK_COUNT_CONSTRUCTOR_MULTIPLIER;
	fSelectorBlockCount = compute_units * GPUCA_GPU_BLOCK_COUNT_SELECTOR_MULTIPLIER;
	fConstructorThreadCount = GPUCA_GPU_THREAD_COUNT_CONSTRUCTOR;
	fSelectorThreadCount = GPUCA_GPU_THREAD_COUNT_SELECTOR;
	fFinderThreadCount = GPUCA_GPU_THREAD_COUNT_FINDER;
	fTRDThreadCount = GPUCA_GPU_THREAD_COUNT_TRD;

	mInternals->context = clCreateContext(nullptr, count, mInternals->devices, nullptr, nullptr, &ocl_error);
	if (ocl_error != CL_SUCCESS)
	{
		CAGPUError("Could not create OPENCL Device Context!");
		return(1);
	}

	//Workaround to compile CL kernel during tracker initialization
	/*{
		char* file = "GlobalTracker/opencl/AliGPUReconstructionOCL.cl";
		CAGPUDebug("Reading source file %s\n", file);
		FILE* fp = fopen(file, "rb");
		if (fp == nullptr)
		{
			CAGPUDebug("Cannot open %s\n", file);
			return(1);
		}
		fseek(fp, 0, SEEK_END);
		size_t file_size = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		char* buffer = (char*) malloc(file_size + 1);
		if (buffer == nullptr)
		{
			quit("Memory allocation error");
		}
		if (fread(buffer, 1, file_size, fp) != file_size)
		{
			quit("Error reading file");
		}
		buffer[file_size] = 0;
		fclose(fp);

		CAGPUDebug("Creating OpenCL Program Object\n");
		//Create OpenCL program object
		mInternals->program = clCreateProgramWithSource(mInternals->context, (cl_uint) 1, (const char**) &buffer, nullptr, &ocl_error);
		if (ocl_error != CL_SUCCESS) quit("Error creating program object");

		CAGPUDebug("Compiling OpenCL Program\n");
		//Compile program
		ocl_error = clBuildProgram(mInternals->program, count, mInternals->devices, "-I. -Iinclude -ISliceTracker -IHLTHeaders -IMerger -IGlobalTracker -I/home/qon/AMD-APP-SDK-v2.8.1.0-RC-lnx64/include -DGPUCA_STANDALONE -DBUILD_GPU -D_64BIT -x clc++", nullptr, nullptr);
		if (ocl_error != CL_SUCCESS)
		{
			CAGPUDebug("OpenCL Error while building program: %d (Compiler options: %s)\n", ocl_error, "");

			for (unsigned int i = 0;i < count;i++)
			{
				cl_build_status status;
				clGetProgramBuildInfo(mInternals->program, mInternals->devices[i], CL_PROGRAM_BUILD_STATUS, sizeof(status), &status, nullptr);
				if (status == CL_BUILD_ERROR)
				{
					size_t log_size;
					clGetProgramBuildInfo(mInternals->program, mInternals->devices[i], CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
					char* build_log = (char*) malloc(log_size + 1);
					if (build_log == nullptr) quit("Memory allocation error");
					clGetProgramBuildInfo(mInternals->program, mInternals->devices[i], CL_PROGRAM_BUILD_LOG, log_size, build_log, nullptr);
					CAGPUDebug("Build Log (device %d):\n\n%s\n\n", i, build_log);
					free(build_log);
				}
			}
		}
	}*/

	if (_makefiles_opencl_obtain_program_helper(mInternals->context, count, mInternals->devices, &mInternals->program, _makefile_opencl_program_GlobalTracker_opencl_AliGPUReconstructionOCL_cl))
	{
		clReleaseContext(mInternals->context);
		CAGPUError("Could not obtain OpenCL progarm");
		return(1);
	}
	if (mDeviceProcessingSettings.debugLevel >= 2) CAGPUInfo("OpenCL program loaded successfully");

	mInternals->kernel_memclean16 = clCreateKernel(mInternals->program, "KernelMemClean16", &ocl_error); if (ocl_error != CL_SUCCESS) {CAGPUError("OPENCL Kernel Error 0");return(1);}
	mInternals->kernel_neighbours_finder = clCreateKernel(mInternals->program, "AliGPUTPCProcess_AliGPUTPCNeighboursFinder", &ocl_error); if (ocl_error != CL_SUCCESS) {CAGPUError("OPENCL Kernel Error 1");return(1);}
	mInternals->kernel_neighbours_cleaner = clCreateKernel(mInternals->program, "AliGPUTPCProcess_AliGPUTPCNeighboursCleaner", &ocl_error); if (ocl_error != CL_SUCCESS) {CAGPUError("OPENCL Kernel Error 2");return(1);}
	mInternals->kernel_start_hits_finder = clCreateKernel(mInternals->program, "AliGPUTPCProcess_AliGPUTPCStartHitsFinder", &ocl_error); if (ocl_error != CL_SUCCESS) {CAGPUError("OPENCL Kernel Error 3");return(1);}
	mInternals->kernel_start_hits_sorter = clCreateKernel(mInternals->program, "AliGPUTPCProcess_AliGPUTPCStartHitsSorter", &ocl_error); if (ocl_error != CL_SUCCESS) {CAGPUError("OPENCL Kernel Error 4");return(1);}
	mInternals->kernel_tracklet_constructor0 = clCreateKernel(mInternals->program, "AliGPUTPCProcess_AliGPUTPCTrackletConstructor0", &ocl_error); if (ocl_error != CL_SUCCESS) {CAGPUError("OPENCL Kernel Error 5");return(1);}
	mInternals->kernel_tracklet_constructor1 = clCreateKernel(mInternals->program, "AliGPUTPCProcess_AliGPUTPCTrackletConstructor1", &ocl_error); if (ocl_error != CL_SUCCESS) {CAGPUError("OPENCL Kernel Error 7");return(1);}
	mInternals->kernel_tracklet_selector = clCreateKernel(mInternals->program, "AliGPUTPCProcessMulti_AliGPUTPCTrackletSelector", &ocl_error); if (ocl_error != CL_SUCCESS) {CAGPUError("OPENCL Kernel Error 6");return(1);}
	if (mDeviceProcessingSettings.debugLevel >= 2) CAGPUInfo("OpenCL kernels created successfully");

	mInternals->mem_gpu = clCreateBuffer(mInternals->context, CL_MEM_READ_WRITE, mDeviceMemorySize, nullptr, &ocl_error);
	if (ocl_error != CL_SUCCESS)
	{
		CAGPUError("OPENCL Memory Allocation Error");
		clReleaseContext(mInternals->context);
		return(1);
	}

	mInternals->mem_constant = clCreateBuffer(mInternals->context, CL_MEM_READ_ONLY, sizeof(AliGPUCAConstantMem), nullptr, &ocl_error);
	if (ocl_error != CL_SUCCESS)
	{
		CAGPUError("OPENCL Constant Memory Allocation Error");
		clReleaseMemObject(mInternals->mem_gpu);
		clReleaseContext(mInternals->context);
		return(1);
	}

	mNStreams = std::max(mDeviceProcessingSettings.nStreams, 3);

	for (int i = 0;i < mNStreams;i++)
	{
#ifdef CL_VERSION_2_0
		mInternals->command_queue[i] = clCreateCommandQueueWithProperties(mInternals->context, mInternals->device, nullptr, &ocl_error);
#else
		mInternals->command_queue[i] = clCreateCommandQueue(mInternals->context, mInternals->device, 0, &ocl_error);
#endif
		if (ocl_error != CL_SUCCESS) quit("Error creating OpenCL command queue");
	}
	if (clEnqueueMigrateMemObjects(mInternals->command_queue[0], 1, &mInternals->mem_gpu, 0, 0, nullptr, nullptr) != CL_SUCCESS) quit("Error migrating buffer");
	if (clEnqueueMigrateMemObjects(mInternals->command_queue[0], 1, &mInternals->mem_constant, 0, 0, nullptr, nullptr) != CL_SUCCESS) quit("Error migrating buffer");

	if (mDeviceProcessingSettings.debugLevel >= 1) CAGPUInfo("GPU Memory used: %lld", (long long int) mDeviceMemorySize);

	mInternals->mem_host = clCreateBuffer(mInternals->context, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, mHostMemorySize, nullptr, &ocl_error);
	if (ocl_error != CL_SUCCESS) quit("Error allocating pinned host memory");

	const char* krnlGetPtr = "__kernel void krnlGetPtr(__global char* gpu_mem, __global char* constant_mem, __global size_t* host_mem) {if (get_global_id(0) == 0) {host_mem[0] = (size_t) gpu_mem; host_mem[1] = (size_t) constant_mem;}}";
	cl_program program = clCreateProgramWithSource(mInternals->context, 1, (const char**) &krnlGetPtr, nullptr, &ocl_error);
	if (ocl_error != CL_SUCCESS) quit("Error creating program object");
	ocl_error = clBuildProgram(program, 1, &mInternals->device, "", nullptr, nullptr);
	if (ocl_error != CL_SUCCESS)
	{
		char build_log[16384];
		clGetProgramBuildInfo(program, mInternals->device, CL_PROGRAM_BUILD_LOG, 16384, build_log, nullptr);
		CAGPUImportant("Build Log:\n\n%s\n\n", build_log);
		quit("Error compiling program");
	}
	cl_kernel kernel = clCreateKernel(program, "krnlGetPtr", &ocl_error);
	if (ocl_error != CL_SUCCESS) quit("Error creating kernel");
	OCLsetKernelParameters(kernel, mInternals->mem_gpu, mInternals->mem_constant, mInternals->mem_host);
	clExecuteKernelA(mInternals->command_queue[0], kernel, 16, 16, nullptr);
	clFinish(mInternals->command_queue[0]);
	clReleaseKernel(kernel);
	clReleaseProgram(program);

	if (mDeviceProcessingSettings.debugLevel >= 2) CAGPUInfo("Mapping hostmemory");
	mHostMemoryBase = clEnqueueMapBuffer(mInternals->command_queue[0], mInternals->mem_host, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE, 0, mHostMemorySize, 0, nullptr, nullptr, &ocl_error);
	if (ocl_error != CL_SUCCESS)
	{
		CAGPUError("Error allocating Page Locked Host Memory");
		return(1);
	}
	if (mDeviceProcessingSettings.debugLevel >= 1) CAGPUInfo("Host Memory used: %lld", (long long int) mHostMemorySize);

	if (mDeviceProcessingSettings.debugLevel >= 2) CAGPUInfo("Obtained Pointer to GPU Memory: %p", *((void**) mHostMemoryBase));
	mDeviceMemoryBase = ((void**) mHostMemoryBase)[0];
	mDeviceConstantMem = (AliGPUCAConstantMem*) ((void**) mHostMemoryBase)[1];

	if (mDeviceProcessingSettings.debugLevel >= 1)
	{
		memset(mHostMemoryBase, 0, mHostMemorySize);
	}

	CAGPUInfo("OPENCL Initialisation successfull (%d: %s %s (Frequency %d, Shaders %d) Thread %d, %lld bytes used)", bestDevice, device_vendor, device_name, (int) freq, (int) shaders, fThreadId, (long long int) mDeviceMemorySize);

	return(0);
}
 
int AliGPUReconstructionOCLBackend::ExitDevice_Runtime()
{
	//Uninitialize OPENCL
	SynchronizeGPU();

	if (mDeviceMemoryBase)
	{
		clReleaseMemObject(mInternals->mem_gpu);
		clReleaseMemObject(mInternals->mem_constant);
		mDeviceMemoryBase = nullptr;

		clReleaseKernel(mInternals->kernel_neighbours_finder);
		clReleaseKernel(mInternals->kernel_neighbours_cleaner);
		clReleaseKernel(mInternals->kernel_start_hits_finder);
		clReleaseKernel(mInternals->kernel_start_hits_sorter);
		clReleaseKernel(mInternals->kernel_tracklet_constructor0);
		clReleaseKernel(mInternals->kernel_tracklet_constructor1);
		clReleaseKernel(mInternals->kernel_tracklet_selector);
		clReleaseKernel(mInternals->kernel_memclean16);
	}
	if (mHostMemoryBase)
	{
		clEnqueueUnmapMemObject(mInternals->command_queue[0], mInternals->mem_host, mHostMemoryBase, 0, nullptr, nullptr);
		mHostMemoryBase = nullptr;
		for (int i = 0;i < mNStreams;i++)
		{
			clReleaseCommandQueue(mInternals->command_queue[i]);
		}
		clReleaseMemObject(mInternals->mem_host);
		mHostMemoryBase = nullptr;
	}

	if (mInternals->devices)
	{
		delete[] mInternals->devices;
		mInternals->devices = nullptr;
	}

	clReleaseProgram(mInternals->program);
	clReleaseContext(mInternals->context);

	CAGPUInfo("OPENCL Uninitialized");
	return(0);
}

void AliGPUReconstructionOCLBackend::TransferMemoryResourceToGPU(AliGPUMemoryResource* res, int stream, deviceEvent* ev, deviceEvent* evList, int nEvents)
{
	if (evList == nullptr) nEvents = 0;
	if (mDeviceProcessingSettings.debugLevel >= 3) stream = -1;
	if (mDeviceProcessingSettings.debugLevel >= 3) printf("Copying to GPU: %s\n", res->Name());
	if (stream == -1) SynchronizeGPU();
	GPUFailedMsg(clEnqueueWriteBuffer(mInternals->command_queue[stream == -1 ? 0 : stream], mInternals->mem_gpu, stream == -1, (char*) res->PtrDevice() - (char*) mDeviceMemoryBase, res->Size(), res->Ptr(), nEvents, (cl_event*) evList, (cl_event*) ev));
}

void AliGPUReconstructionOCLBackend::TransferMemoryResourceToHost(AliGPUMemoryResource* res, int stream, deviceEvent* ev, deviceEvent* evList, int nEvents)
{
	if (evList == nullptr) nEvents = 0;
	if (mDeviceProcessingSettings.debugLevel >= 3) stream = -1;
	if (mDeviceProcessingSettings.debugLevel >= 3) printf("Copying to Host: %s\n", res->Name());
	if (stream == -1) SynchronizeGPU();
	GPUFailedMsg(clEnqueueReadBuffer(mInternals->command_queue[stream == -1 ? 0 : stream], mInternals->mem_gpu, stream == -1, (char*) res->PtrDevice() - (char*) mDeviceMemoryBase, res->Size(), res->Ptr(), nEvents, (cl_event*) evList, (cl_event*) ev));
}

void AliGPUReconstructionOCLBackend::WriteToConstantMemory(size_t offset, const void* src, size_t size, int stream, deviceEvent* ev)
{
	if (stream == -1) SynchronizeGPU();
	GPUFailedMsg(clEnqueueWriteBuffer(mInternals->command_queue[stream == -1 ? 0 : stream], mInternals->mem_constant, stream == -1, offset, size, src, 0, nullptr, (cl_event*) ev));
}

void AliGPUReconstructionOCLBackend::ReleaseEvent(deviceEvent* ev)
{
	GPUFailedMsg(clReleaseEvent(*(cl_event*) ev));
}

void AliGPUReconstructionOCLBackend::RecordMarker(deviceEvent* ev, int stream)
{
	GPUFailedMsg(clEnqueueMarkerWithWaitList(mInternals->command_queue[stream], 0, nullptr, (cl_event*) ev));
}

int AliGPUReconstructionOCLBackend::RefitMergedTracks(AliGPUTPCGMMerger* Merger, bool resetTimers)
{
	CAGPUFatal("Not implemented in OpenCL (Merger)");
	return(1);
}

void AliGPUReconstructionOCLBackend::ActivateThreadContext()
{
}

void AliGPUReconstructionOCLBackend::ReleaseThreadContext()
{
}

int AliGPUReconstructionOCLBackend::DoStuckProtection(int stream, void* event)
{
	if (mDeviceProcessingSettings.stuckProtection)
	{
		cl_int tmp = 0;
		for (int i = 0;i <= mDeviceProcessingSettings.stuckProtection / 50;i++)
		{
			usleep(50);
			clGetEventInfo(* (cl_event*) event, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(tmp), &tmp, nullptr);
			if (tmp == CL_COMPLETE) break;
		}
		if (tmp != CL_COMPLETE)
		{
			CAGPUError("GPU Stuck, future processing in this component is disabled, skipping event (GPU Event State %d)", (int) tmp);
			fGPUStuck = 1;
			return(1);
		}
	}
	else
	{
		clFinish(mInternals->command_queue[stream]);
	}
	return 0;
}

void AliGPUReconstructionOCLBackend::SynchronizeGPU()
{
	for (int i = 0;i < mNStreams;i++) GPUFailedMsg(clFinish(mInternals->command_queue[i]));
}

void AliGPUReconstructionOCLBackend::SynchronizeStream(int stream)
{
	GPUFailedMsg(clFinish(mInternals->command_queue[stream]));
}

void AliGPUReconstructionOCLBackend::SynchronizeEvents(deviceEvent* evList, int nEvents)
{
	GPUFailedMsg(clWaitForEvents(nEvents, (cl_event*) evList));
}

int AliGPUReconstructionOCLBackend::IsEventDone(deviceEvent* evList, int nEvents)
{
	cl_int eventdone;
	for (int i = 0;i < nEvents;i++)
	{
		GPUFailedMsg(clGetEventInfo(((cl_event*) evList)[i] , CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(eventdone), &eventdone, nullptr));
		if (eventdone != CL_COMPLETE) return 0;
	}
	return 1;
}

int AliGPUReconstructionOCLBackend::GPUDebug(const char* state, int stream, int slice)
{
	//Wait for OPENCL-Kernel to finish and check for OPENCL errors afterwards, in case of debugmode
	if (mDeviceProcessingSettings.debugLevel == 0) return(0);
	for (int i = 0;i < mNStreams;i++)
	{
		if (GPUFailedMsgI(clFinish(mInternals->command_queue[i])))
		{
			CAGPUError("OpenCL Error while synchronizing (%s) (Stream %d/%d; Slice %d/%d)", state, stream, i, slice, NSLICES);
		}
	}
	if (mDeviceProcessingSettings.debugLevel >= 3) CAGPUInfo("GPU Sync Done");
	return(0);
}
