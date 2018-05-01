#include "../../idlib/precompiled.h"
#pragma hdrstop

//#include "../Game_local.h"

#if defined (_WIN32)

#define STACKDUMP_VALUES_PER_LINE	8
#define STACKDUMP_NUMBER_OF_LINES	20

//#include "Win32StructuredException.h"
#include "getmsg.h"


Win32StructuredException::SYMINITIALIZEPROC				Win32StructuredException::_SymInitialize = 0;
Win32StructuredException::SYMCLEANUPPROC				Win32StructuredException::_SymCleanup = 0;
Win32StructuredException::STACKWALKPROC					Win32StructuredException::_StackWalk = 0;
Win32StructuredException::SYMFUNCTIONTABLEACCESSPROC	Win32StructuredException::_SymFunctionTableAccess = 0;
Win32StructuredException::SYMGETMODULEBASEPROC			Win32StructuredException::_SymGetModuleBase = 0;
Win32StructuredException::SYMGETSYMFROMADDRPROC			Win32StructuredException::_SymGetSymFromAddr = 0;

//_se_translator_function Win32StructuredException::m_oldTranslateFunc = 0;
LPTOP_LEVEL_EXCEPTION_FILTER Win32StructuredException::m_oldExceptionHandler = 0;


LONG WINAPI HandleException(struct _EXCEPTION_POINTERS *pep)
{
	_try 
	{
		Win32StructuredException except(pep);
		except.DumpExceptionContext();

	//	Win32StructuredException::UnRegisterWin32ExceptionHandler();

		gameLocal.Error(except.GetExceptionMessage());

		return EXCEPTION_EXECUTE_HANDLER;
	}
	 _except( EXCEPTION_CONTINUE_SEARCH )
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

LPTSTR Win32StructuredException::GetExceptionString()
{
	// Given an exception code, returns a pointer to a static 
	// string with a description of the exception                                         

	#define EXCEPTION(x) case EXCEPTION_##x: return #x;

	switch (m_exceptRec.ExceptionCode)
	{
		EXCEPTION(ACCESS_VIOLATION)
		EXCEPTION(DATATYPE_MISALIGNMENT)
		EXCEPTION(BREAKPOINT)
		EXCEPTION(SINGLE_STEP)
		EXCEPTION(ARRAY_BOUNDS_EXCEEDED)
		EXCEPTION(FLT_DENORMAL_OPERAND)
		EXCEPTION(FLT_DIVIDE_BY_ZERO)
		EXCEPTION(FLT_INEXACT_RESULT)
		EXCEPTION(FLT_INVALID_OPERATION)
		EXCEPTION(FLT_OVERFLOW)
		EXCEPTION(FLT_STACK_CHECK)
		EXCEPTION(FLT_UNDERFLOW)
		EXCEPTION(INT_DIVIDE_BY_ZERO)
		EXCEPTION(INT_OVERFLOW)
		EXCEPTION(PRIV_INSTRUCTION)
		EXCEPTION(IN_PAGE_ERROR)
		EXCEPTION(ILLEGAL_INSTRUCTION)
		EXCEPTION(NONCONTINUABLE_EXCEPTION)
		EXCEPTION(STACK_OVERFLOW)
		EXCEPTION(INVALID_DISPOSITION)
		EXCEPTION(GUARD_PAGE)
		EXCEPTION(INVALID_HANDLE)
	}

	// If not one of the "known" exceptions, try to get the string from NTDLL.DLL's message table.

	static TCHAR szBuffer[512] = { 0 };

	FormatMessage( FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_HMODULE, GetModuleHandle("NTDLL.DLL"),
					m_exceptRec.ExceptionCode, 0, szBuffer, sizeof(szBuffer), 0);

	return szBuffer;
}

const char *Win32StructuredException::GetExceptionMessage()
{
    TCHAR szFaultingModule[MAX_PATH];
    DWORD section, offset;
    GetLogicalAddress(  m_exceptRec.ExceptionAddress,
                        szFaultingModule,
                        sizeof( szFaultingModule ),
                        section, offset );

	void *baseAddress = (void *)0x20000000;
    MEMORY_BASIC_INFORMATION mbi;
	BOOL mbiValid = VirtualQuery(HandleException, &mbi, sizeof(mbi));

	if (mbiValid)
		baseAddress = mbi.AllocationBase;

	return va("Exception code: %08X %s occurred at %08X %02X:%08X %s - base address %08x\n", 
						m_exceptRec.ExceptionCode, GetExceptionString(), 
						m_exceptRec.ExceptionAddress, section, offset, szFaultingModule, baseAddress);
}

void Win32StructuredException::DumpExceptionContext()
{  
	gameLocal.Printf(S_COLOR_RED "8<- - - - Error message starts here - - - -\n");
    // Print information about where the fault occured.
    gameLocal.Printf(GetExceptionMessage());

    // Show the register data.
	gameLocal.Printf("\nRegisters:\n");
	gameLocal.Printf("eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x\n", m_context.Eax, m_context.Ebx, m_context.Ecx, m_context.Edx, m_context.Esi, m_context.Edi);
	gameLocal.Printf("eip=%08x esp=%08x ebp=%08x\n", m_context.Eip, m_context.Esp, m_context.Ebp);
    gameLocal.Printf("cs=%04x ss=%04x ds=%04x es=%04x fs=%04x gs=%04x efl=%08x\n", m_context.SegCs, m_context.SegSs, m_context.SegDs, m_context.SegEs, m_context.SegFs, m_context.SegGs, m_context.EFlags);

	// Write stack data.

	_try
	{
		GenerateStackDump((unsigned char *)m_context.Esp);
	}
	_finally
	{
	}

	_try
	{
		if (!InitImagehlpFunctions())
		{
			gameLocal.Printf("Warning: IMAGEHLP.DLL or its exported procs not found\n");

			// Walk the stack using x86 specific code
			IntelStackWalk();
		}
		else
		{
			ImagehlpStackWalk();
			_SymCleanup(GetCurrentProcess());
		}	
	}
	_finally
	{
	}

	gameLocal.Printf(S_COLOR_RED "8<- - - - Error message ends here - - - -\n");
	gameLocal.Printf(S_COLOR_YELLOW "--- Please supply this error message in full to the q4max team. Thanks. ---\n");
	gameLocal.Printf(S_COLOR_YELLOW "--- Type 'condump somefilename.txt' at the console to dump the error text to a file.  ---\n");
	gameLocal.Printf(S_COLOR_YELLOW "--- If you want to keep on playing, quit then restart Quake 4 + Q4Max.  ---\n");
}

bool Win32StructuredException::GetLogicalAddress(PVOID addr, PTSTR szModule, DWORD len, DWORD& section, DWORD& offset)
{
	// Given a linear address, this method locates the module, section, and 
	// offset containing that address.
	// Note: the szModule paramater buffer is an output buffer of length specified 
	// by the len parameter (in characters!)                                       

    MEMORY_BASIC_INFORMATION mbi;

    if (!VirtualQuery(addr, &mbi, sizeof(mbi)))
        return false;

    DWORD hMod = (DWORD)mbi.AllocationBase;

    if (!GetModuleFileName((HMODULE)hMod, szModule, len) || !hMod)
        return false;

    // Point to the DOS header in memory
    PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)hMod;

    // From the DOS header, find the NT (PE) header
    PIMAGE_NT_HEADERS pNtHdr = (PIMAGE_NT_HEADERS)(hMod + pDosHdr->e_lfanew);

    PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNtHdr);

    DWORD rva = (DWORD)addr - hMod; // RVA is offset from module load address

    // Iterate through the section table, looking for the one that encompasses
    // the linear address.
    for (  unsigned i = 0;
            i < pNtHdr->FileHeader.NumberOfSections;
            i++, pSection++)
    {
        DWORD sectionStart = pSection->VirtualAddress;
        DWORD sectionEnd = sectionStart
                    + max(pSection->SizeOfRawData, pSection->Misc.VirtualSize);

        // Is the address in this section???
        if ((rva >= sectionStart) && (rva <= sectionEnd))
        {
            // Yes, address is in the section.  Calculate section and offset,
            // and store in the "section" & "offset" params, which were
            // passed by reference.
            section = i+1;
            offset = rva - sectionStart;
            return true;
        }
    }

    return false;   // Should never get here!
}

void Win32StructuredException::IntelStackWalk()
{
	gameLocal.Printf("\nCall stack:\n");
    gameLocal.Printf("Address   Frame     Logical addr  Module\n");

    DWORD pc = m_context.Eip;   
    PDWORD pFrame = (PDWORD)m_context.Ebp;
	PDWORD pPrevFrame = NULL;
    do
    {
        TCHAR szModule[MAX_PATH] = "";
        DWORD section = 0, offset = 0;

        GetLogicalAddress((PVOID)pc, szModule,sizeof(szModule),section,offset);

        gameLocal.Printf("%08X  %08X  %04X:%08X %s\n", pc, pFrame, section, offset, szModule);

		if (pFrame && !IsBadReadPtr(pFrame, sizeof(void*) * 2))
		{
			pc = pFrame[1];
	        pPrevFrame = pFrame;
			pFrame = (PDWORD)pFrame[0]; // precede to next higher frame on stack
		}

        if ((DWORD)pFrame & 3)    // Frame pointer must be aligned on a
            break;                  // DWORD boundary.  Bail if not so.

        if (pFrame <= pPrevFrame)
            break;

        // Can two DWORDs be read from the supposed frame address?          
        if (IsBadReadPtr(pFrame, sizeof(PVOID)*2))
            break;

    } while (1);     
}

void Win32StructuredException::ImagehlpStackWalk()
{
    gameLocal.Printf("\nCall stack:\n");
    gameLocal.Printf("Address   Frame\n");

    // Could use SymSetOptions here to add the SYMOPT_DEFERRED_LOADS flag

    STACKFRAME sf;
    memset(&sf, 0, sizeof(sf));

    // Initialize the STACKFRAME structure for the first call.  This is only
    // necessary for Intel CPUs, and isn't mentioned in the documentation.
    sf.AddrPC.Offset       = m_context.Eip;
    sf.AddrPC.Mode         = AddrModeFlat;
    sf.AddrStack.Offset    = m_context.Esp;
    sf.AddrStack.Mode      = AddrModeFlat;
    sf.AddrFrame.Offset    = m_context.Ebp;
    sf.AddrFrame.Mode      = AddrModeFlat;

    while (1)
    {
        if (! _StackWalk( IMAGE_FILE_MACHINE_I386,
                            GetCurrentProcess(),
                            GetCurrentThread(),
                            &sf,
                            &m_context,
                            0,
                            _SymFunctionTableAccess,
                            _SymGetModuleBase,
                            0))
            break;

        if (0 == sf.AddrFrame.Offset) // Basic sanity check to make sure
            break;                      // the frame is OK.  Bail if not.

        gameLocal.Printf("%08X  %08X  ", sf.AddrPC.Offset, sf.AddrFrame.Offset);

        // IMAGEHLP is wacky, and requires you to pass in a pointer to a
        // IMAGEHLP_SYMBOL structure.  The problem is that this structure is
        // variable length.  That is, you determine how big the structure is
        // at runtime.  This means that you can't use sizeof(struct).
        // So...make a buffer that's big enough, and make a pointer
        // to the buffer.  We also need to initialize not one, but TWO
        // members of the structure before it can be used.

        BYTE symbolBuffer[ sizeof(IMAGEHLP_SYMBOL) + 512 ];
        PIMAGEHLP_SYMBOL pSymbol = (PIMAGEHLP_SYMBOL)symbolBuffer;
        pSymbol->SizeOfStruct = sizeof(symbolBuffer);
        pSymbol->MaxNameLength = 512;
                        
        DWORD symDisplacement = 0;  // Displacement of the input address,
                                    // relative to the start of the symbol

        if (_SymGetSymFromAddr(GetCurrentProcess(), sf.AddrPC.Offset,
                                &symDisplacement, pSymbol))
        {
            gameLocal.Printf("%hs+%X\n", pSymbol->Name, symDisplacement);
        }
        else    // No symbol found.  Print out the logical address instead.
        {
            TCHAR szModule[MAX_PATH] = "";
            DWORD section = 0, offset = 0;

            GetLogicalAddress( (PVOID)sf.AddrPC.Offset, szModule, sizeof(szModule), section, offset);

            gameLocal.Printf("%04X:%08X %s\n", section, offset, szModule);
        }
    }
}

bool Win32StructuredException::InitImagehlpFunctions()
{
	// Load IMAGEHLP.DLL and get the address of functions in it that we'll use.

    HMODULE hModImagehlp = LoadLibrary("IMAGEHLP.DLL");
	if (!hModImagehlp)
		return false;

    _SymInitialize = (SYMINITIALIZEPROC)GetProcAddress(hModImagehlp, "SymInitialize");
    _SymCleanup = (SYMCLEANUPPROC)GetProcAddress(hModImagehlp, "SymCleanup");
    _StackWalk = (STACKWALKPROC)GetProcAddress(hModImagehlp, "StackWalk");
    _SymFunctionTableAccess = (SYMFUNCTIONTABLEACCESSPROC)GetProcAddress(hModImagehlp, "SymFunctionTableAccess");
    _SymGetModuleBase = (SYMGETMODULEBASEPROC)GetProcAddress(hModImagehlp, "SymGetModuleBase");
    _SymGetSymFromAddr = (SYMGETSYMFROMADDRPROC)GetProcAddress(hModImagehlp, "SymGetSymFromAddr");

    if (!_SymInitialize || !_SymCleanup || !_StackWalk || !_SymFunctionTableAccess || !_SymGetModuleBase || !_SymGetSymFromAddr)
        return false;

    if (!_SymInitialize(GetCurrentProcess(), 0, TRUE))
        return false;

    return true;        
}

void Win32StructuredException::RegisterWin32ExceptionHandler()
{
	m_oldExceptionHandler = SetUnhandledExceptionFilter(HandleException);
}

void Win32StructuredException::UnRegisterWin32ExceptionHandler()
{
	 SetUnhandledExceptionFilter(m_oldExceptionHandler);
}

BYTE GetMemoryContents(unsigned char *aAddress, bool &aIsValid)
{
	aIsValid = false;
	int lRetVal = 0;

	_try
	{
		if (!IsBadReadPtr(aAddress, 1))
		{
			lRetVal = *aAddress;
			aIsValid = true;
		}
	}
	_finally
	{
	}

	return lRetVal;
}

void Win32StructuredException::GenerateStackDump(unsigned char *lStackPtr)
{
	gameLocal.Printf("  Raw stack dump:\n");

	for (int lLine=0; lLine<STACKDUMP_NUMBER_OF_LINES; lLine++)
	{
		char lLineHex[STACKDUMP_VALUES_PER_LINE * 3 + 1];
		char lLineAscii[STACKDUMP_VALUES_PER_LINE + 1];
		char lLineFull[(STACKDUMP_VALUES_PER_LINE * 3) + STACKDUMP_VALUES_PER_LINE + 40];

		sprintf(lLineFull, "    %08X  ", lStackPtr);

		lLineHex[0] = '\0';
		for (int i=0; i<STACKDUMP_VALUES_PER_LINE; i++)
		{
			bool lIsValid = false;
			unsigned char lThisValue = GetMemoryContents(lStackPtr++, lIsValid);

			char lHexVal[4];
			if (lIsValid)
				sprintf(lHexVal, "%02x ", lThisValue);
			else
				sprintf(lHexVal, "?? ");

			strcat(lLineHex, lHexVal);

			if (lThisValue < 32 || lThisValue > 127)
				lLineAscii[i] = '.';
			else
				lLineAscii[i] = lThisValue;
		}
		lLineAscii[STACKDUMP_VALUES_PER_LINE] = '\0';

		strcat(lLineFull, lLineHex);
		strcat(lLineFull, "  ");
		strcat(lLineFull, lLineAscii);
		strcat(lLineFull, "\n");

		gameLocal.Printf(lLineFull);
	}
}

#endif



#if defined( __linux__ )

#define NO_CPP_DEMANGLE

/**
* This source file is used to print out a stack-trace when your program
* segfaults. It is relatively reliable and spot-on accurate.
*
* Author: Jaco Kroon <jaco@kroon.co.za>
*
* Copyright (C) 2005 - 2006 Jaco Kroon
*/
#define _GNU_SOURCE
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <ucontext.h>
#include <dlfcn.h>
#include <execinfo.h>
#ifndef NO_CPP_DEMANGLE
#include <cxxabi.h>
#endif

#if defined(REG_RIP)
# define SIGSEGV_STACK_IA64
# define REGFORMAT "%016lx"
#elif defined(REG_EIP)
# define SIGSEGV_STACK_X86
# define REGFORMAT "%08x"
#else
# define SIGSEGV_STACK_GENERIC
# define REGFORMAT "%x"
#endif


static void signal_segv(int signum, siginfo_t* info, void*ptr)
{
	static const char *si_codes[3] = {"", "SEGV_MAPERR", "SEGV_ACCERR"};

	size_t i;
	ucontext_t *ucontext = (ucontext_t*)ptr;

#if defined(SIGSEGV_STACK_X86) || defined(SIGSEGV_STACK_IA64)
	int f = 0;
	Dl_info dlinfo;
	unsigned char **bp = 0;
	unsigned char *ip = 0;
#else
	unsigned char *bt[20];
	char **strings;
	size_t sz;
#endif

	gameLocal.Printf("Segmentation Fault!\n");
	gameLocal.Printf("info.si_signo = %d\n", signum);
	gameLocal.Printf("info.si_errno = %d\n", info->si_errno);
	gameLocal.Printf("info.si_code = %d (%s)\n", info->si_code, si_codes[info->si_code]);
	gameLocal.Printf("info.si_addr = %p\n", info->si_addr);

	for(i = 0; i < NGREG; i++)
		gameLocal.Printf("reg[%02d] = 0x" REGFORMAT "\n", i, ucontext->uc_mcontext.gregs[i]);

#if defined(SIGSEGV_STACK_X86) || defined(SIGSEGV_STACK_IA64)
# if defined(SIGSEGV_STACK_IA64)
	ip = (unsigned char*)ucontext->uc_mcontext.gregs[REG_RIP];
	bp = (unsigned char**)ucontext->uc_mcontext.gregs[REG_RBP];
# elif defined(SIGSEGV_STACK_X86)
	ip = (unsigned char*)ucontext->uc_mcontext.gregs[REG_EIP];
	bp = (unsigned char**)ucontext->uc_mcontext.gregs[REG_EBP];
# endif

	gameLocal.Printf("Stack trace:\n");
	while(bp && ip) 
	{
		if(!dladdr(ip, &dlinfo))
			break;

		const char *symname = dlinfo.dli_sname;

		if (!dlinfo.dli_saddr)
			gameLocal.Printf("% 2d: %p <+0x%x> (%s)\n", ++f, ip, (unsigned)(ip - (unsigned char *)dlinfo.dli_fbase), dlinfo.dli_fname);
		else
			gameLocal.Printf("% 2d: %p <%s+0x%x> (%s)\n", ++f, ip, symname, (unsigned)(ip - (unsigned char *)dlinfo.dli_saddr), dlinfo.dli_fname);

		if(dlinfo.dli_sname && !strcmp(dlinfo.dli_sname, "main"))
			break;	

		ip = bp[1];
		bp = (unsigned char**)bp[0];
	}
#else
	gameLocal.Printf("Stack trace (non-dedicated):\n");
	sz = backtrace(bt, 20);
	strings = backtrace_symbols(bt, sz);

	for(i = 0; i < sz; ++i)
		gameLocal.Printf("%s\n", strings[i]);
#endif
	gameLocal.Printf("End of stack trace\n");

	gameLocal.Error("Segfault - signo=%d, code=%s, addr=%p", signum, si_codes[info->si_code], info->si_addr);
}

static struct sigaction oldSegvHandler;

bool RegisterLinuxSegvHandler()
{
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = signal_segv;
	action.sa_flags = SA_SIGINFO;
	if(sigaction(SIGSEGV, &action, &oldSegvHandler) < 0)
		return false;

	return true;
}

bool UnRegisterLinuxSegvHandler()
{
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = signal_segv;
	action.sa_flags = SA_SIGINFO;
	if(sigaction(SIGSEGV, &oldSegvHandler, NULL) < 0)
		return false;

	return true;
}


#endif
