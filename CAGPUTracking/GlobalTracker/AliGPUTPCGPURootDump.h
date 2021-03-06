#ifndef ALIHLTTPCCAGPUROOTDUMP_H
#define ALIHLTTPCCAGPUROOTDUMP_H

#if (!defined(GPUCA_STANDALONE) || defined(BUILD_QA)) && !defined(GPUCA_GPUCODE)
#include <TTree.h>
#include <TFile.h>
#include <TNtuple.h>

namespace {
template <class S> struct internal_Branch
{
	template <typename... Args> static void Branch(S* p, Args... args) {}
};
template <> struct internal_Branch<TTree>
{
	template <typename... Args> static void Branch(TTree* p, Args... args) {p->Branch(args...);}
};
}

template <class T> class AliGPUTPCGPURootDump
{
public:
	AliGPUTPCGPURootDump() = delete;
	AliGPUTPCGPURootDump(const AliGPUTPCGPURootDump<T>&) = delete;
	AliGPUTPCGPURootDump<T> operator = (const AliGPUTPCGPURootDump<T>&) = delete;
	template <typename... Args> AliGPUTPCGPURootDump(const char* filename, Args... args)
	{
		fFile = new TFile(filename, "recreate");
		fTree = new T(args...);
	}
	
	~AliGPUTPCGPURootDump()
	{
		fTree->Write();
		fFile->Write();
		fFile->Close();
		delete fFile;
	}
	
	template <typename... Args> void Fill(Args... args) {fTree->Fill(args...);}
	template <typename... Args> void Branch(Args... args) {internal_Branch<T>::Branch(fTree, args...);}
private:

	TFile* fFile = nullptr;
	T* fTree = nullptr;
};
#else
template <class T> class AliGPUTPCGPURootDump
{
public:
	AliGPUTPCGPURootDump() = delete;
	template <typename... Args> AliGPUTPCGPURootDump(const char* filename, Args... args) {}
	template <typename... Args> void Fill(Args... args) {}
	template <typename... Args> void Branch(Args... args) {}
private:
	void *a, *b;
};
#endif

#endif
