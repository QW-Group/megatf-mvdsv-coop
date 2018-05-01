#ifndef _ExceptionHandler
#define _ExceptionHandler

#if defined (_WIN32)

#include <ImageHlp.h>

class Win32StructuredException
{
public :
	static void RegisterWin32ExceptionHandler();
	static void UnRegisterWin32ExceptionHandler();

	LPTSTR GetExceptionString();
	const char *GetExceptionMessage();

	void DumpExceptionContext();

	Win32StructuredException(struct _EXCEPTION_POINTERS *pep) : 
		m_exceptRec(*pep->ExceptionRecord), m_context(*pep->ContextRecord) 
		{
		}

protected:
	static LPTOP_LEVEL_EXCEPTION_FILTER m_oldExceptionHandler;
	EXCEPTION_RECORD	m_exceptRec;
	CONTEXT				m_context;

	// Make typedefs for some IMAGEHLP.DLL functions so that we can use them
	// with GetProcAddress
	typedef BOOL (__stdcall * SYMINITIALIZEPROC)(HANDLE, LPSTR, BOOL);
	typedef BOOL (__stdcall *SYMCLEANUPPROC)(HANDLE);
	typedef BOOL (__stdcall * STACKWALKPROC)(DWORD, HANDLE, HANDLE, LPSTACKFRAME, LPVOID, PREAD_PROCESS_MEMORY_ROUTINE, PFUNCTION_TABLE_ACCESS_ROUTINE, PGET_MODULE_BASE_ROUTINE,  PTRANSLATE_ADDRESS_ROUTINE);
	typedef LPVOID (__stdcall *SYMFUNCTIONTABLEACCESSPROC)(HANDLE, DWORD);
	typedef DWORD (__stdcall *SYMGETMODULEBASEPROC)(HANDLE, DWORD);
	typedef BOOL (__stdcall *SYMGETSYMFROMADDRPROC)(HANDLE, DWORD, PDWORD, PIMAGEHLP_SYMBOL);

	static SYMINITIALIZEPROC _SymInitialize;
	static SYMCLEANUPPROC _SymCleanup;
	static STACKWALKPROC _StackWalk;
	static SYMFUNCTIONTABLEACCESSPROC _SymFunctionTableAccess;
	static SYMGETMODULEBASEPROC _SymGetModuleBase;
	static SYMGETSYMFROMADDRPROC _SymGetSymFromAddr;


	bool GetLogicalAddress(PVOID addr, PTSTR szModule, DWORD len, DWORD& section, DWORD& offset);

	void IntelStackWalk();
	void ImagehlpStackWalk();
	bool InitImagehlpFunctions();

	void GenerateStackDump(unsigned char *lStackPtr);
};

#endif 


#if defined(__linux__)

bool RegisterLinuxSegvHandler();
bool UnRegisterLinuxSegvHandler();

#endif

#endif
