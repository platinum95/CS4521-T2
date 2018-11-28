//
// helper.cpp
//
// Copyright (C) 2011 - 2018 jones@scss.tcd.ie
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software Foundation;
// either version 2 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//

//
// 12/01/11 first version
// 15/07/13 added performance monitoring support
// 14/09/13 linux support (needs g++ 4.8 or later)
// 09/02/14 added setCommaLocale and setLocale
// 10/05/14 added getVMUse and getMemUse
// 08/06/14 allocated commaLocale only once
// 26/07/14 added getPageSz()
// 26/10/14 added thread_local definitions
// 26/11/14 added AMALLOC and AFREE
//

//
// 05-08-15 enabling TSX instructions
//
// TSX was first supported by Haswell CPUs released Q2 2013.
// Not all Haswell or newer CPUs support TSX, you need to consult the Intel Ark database.
// In Aug-14, a bug was reported in the TSX implementation (occurs very rarely).
// Although this bug has been fixed in more recent CPUs (eg Broadwell and Skylake), many systems
// disable the buggy TSX instructions at boot time by loading microcode into the CPU.
// This is done by the BIOS or OS or both.
// On Windows the file C:/Windows/System32/mcupdate_GenuineIntel.dll is used to load the microcode into the CPU.
// Ubuntu doesn't update the microcode by default
//

//
// NB: gcc needs flags -mrtm -mrdrnd
//

#include <iostream>         // cout
#include <iomanip>          // setprecision
#include "helper.h"         //

#ifdef WIN32
#include <conio.h>          // _getch()
#include <psapi.h>          // GetProcessMemoryInfo
#elif __linux__
#include <termios.h>        //
#include <unistd.h>         //
#include <limits.h>         // HOST_NAME_MAX
#include <sys/utsname.h>    //
#include <fcntl.h>          // O_RDWR
#endif

using namespace std;        // cout. ...

//
// for data returned by cpuid instruction
//
struct _cd {
    UINT eax;
    UINT ebx;
    UINT ecx;
    UINT edx;
} cd;

UINT ncpu;                  // # logical CPUs
char *hostName = NULL;      // host name
char *osName = NULL;        // os name
char *brandString = NULL;   // cpu brand string

//
// getDateAndTime
//
void getDateAndTime(char *dateAndTime, int sz, time_t t) {
    t = (t == 0) ? time(NULL) : 0;
#ifdef WIN32
    struct tm now;
    localtime_s(&now, &t);
    strftime(dateAndTime, sz, "%d-%b-%Y %H:%M:%S", &now);
#elif __linux__
    struct tm *now = localtime(&t);
    strftime(dateAndTime, sz, "%d-%b-%Y %H:%M:%S", now);
#endif
}

//
// getHostName
//
char* getHostName() {
    if (hostName == NULL) {

#ifdef WIN32
        DWORD sz = (MAX_COMPUTERNAME_LENGTH + 1) * sizeof(char);
        hostName = (char*) malloc(sz);
        GetComputerNameA(hostName, &sz);
#elif __linux__
        size_t sz = (HOST_NAME_MAX + 1) * sizeof(char);
        hostName = (char*) malloc(sz);
        gethostname(hostName, sz);
#endif

    }
    return hostName;
}

//
// getOSName
//
char* getOSName() {
    if (osName == NULL) {

        osName = (char*) malloc(256);   // should be large enough

#ifdef WIN32
        DWORD sz = 256;
        RegGetValueA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows NT\\CurrentVersion", "ProductName", RRF_RT_ANY, NULL, (LPBYTE) osName, &sz);
#ifdef _WIN64
        strcat_s(osName, 256, " (64 bit)");
#else
        int win64;
        IsWow64Process(GetCurrentProcess(), &win64);
        strcat_s(osName, 256, win64 ? " (64 bit)" : " (32 bit)");
#endif
#elif __linux__
        struct utsname utsName;
        uname(&utsName);
        strcpy(osName, utsName.sysname);
        strcat(osName,  " ");
        strcat(osName, utsName.release);
#endif

    }
    return osName;
}

//
// is64bitExe
//
// return 1 if a 64 bit .exe
// return 0 if a 32 bit .exe
//
int is64bitExe() {
    return sizeof(size_t) == 8;
}

//
// getPhysicalMemSz
//
UINT64 getPhysicalMemSz() {
#ifdef WIN32
    UINT64 v;
    GetPhysicallyInstalledSystemMemory(&v);                         // returns KB
    return v * 1024;                                                // now bytes
#elif __linux__
    return (UINT64) sysconf(_SC_PHYS_PAGES)* sysconf(_SC_PAGESIZE); // NB: returns bytes
#endif
}

//
// getNumberOfCPUs
//
int getNumberOfCPUs() {
#ifdef WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo );
    return sysinfo.dwNumberOfProcessors;
#elif __linux__
    return (int) sysconf(_SC_NPROCESSORS_ONLN);                      // {joj 12/1/18}
#endif
}

//
// cpu64bit
//
int cpu64bit() {
    CPUID(cd, 0x80000001);
    return (cd.edx >> 29) & 0x01;
}

//
// cpuFamily
//
int cpuFamily() {
    CPUID(cd, 0x01);
    return (cd.eax >> 8) & 0xff;
}

//
// cpuModel
//
int cpuModel() {
    CPUID(cd, 0x01);
    if (((cd.eax >> 8) & 0xff) == 0x06)
        return (cd.eax >> 12 & 0xf0) + ((cd.eax >> 4) & 0x0f);
    return (cd.eax >> 4) & 0x0f;
}

//
// cpuStepping
//
int cpuStepping() {
    CPUID(cd, 0x01);
    return cd.eax & 0x0f;
}

//
// cpuBrandString
//
char *cpuBrandString() {
    if (brandString)
        return brandString;

    brandString = (char*) calloc(16*3, sizeof(char));

    CPUID(cd, 0x80000000);

    if (cd.eax < 0x80000004) {
        strcpy_s(brandString, 16*3, "unknown");
        return brandString;
    }

    for (int i = 0; i < 3; i++) {
        CPUID(cd, 0x80000002 + i);
        UINT *p = &cd.eax;
        for (int j = 0; j < 4; j++, p++) {
            for (int k = 0; k < 4; k++ ) {
                brandString[i*16 + j*4 + k] = (char) ((*p >> (k * 8)) & 0xff);      // {joj 12/1/18}
            }
        }
    }
    return brandString;
}

//
// rtmSupported (restricted transactional memory)
//
// NB: VirtualBox returns 0 even if CPU supports RTM?
//
int rtmSupported() {
    CPUIDEX(cd, 0x07, 0);
    return (cd.ebx >> 11) & 1;      // test bit 11 in ebx
}

//
// hleSupported (hardware lock elision)
//
// NB: VirtualBox returns 0 even if CPU supports HLE??
//
int hleSupported() {
    CPUIDEX(cd, 0x07, 0);
    return (cd.ebx >> 4) & 1;       // test bit 4 in ebx
}

//
// look for L1 cache line size (see Intel Application note on CPUID instruction)
//
int lookForL1DataCacheInfo(int v) {
    if (v & 0x80000000)
        return 0;

    for (int i = 0; i < 4; i++) {
        switch (v & 0xff) {
        case 0x0a:
        case 0x0c:
        case 0x10:
            return 32;
        case 0x0e:
        case 0x2c:
        case 0x60:
        case 0x66:
        case 0x67:
        case 0x68:
            return 64;
        }
        v >>= 8;
    }
    return 0;
}

//
// getL1DataCacheInfo
//
int getL1DataCacheInfo() {
    CPUID(cd, 2);

    if ((cd.eax & 0xff) != 1) {
        cout << "unrecognised cache type: default L 64" << endl;
        return 64;
    }

    int sz;

    if ((sz = lookForL1DataCacheInfo(cd.eax & ~0xff)))
        return sz;
    if ((sz = lookForL1DataCacheInfo(cd.ebx)))
        return sz;
    if ((sz = lookForL1DataCacheInfo(cd.ecx)))
        return sz;
    if ((sz = lookForL1DataCacheInfo(cd.edx)))
        return sz;

    cout << "unrecognised cache type: default L 64" << endl;
    return 64;
}

//
// getCacheInfo
//
int getCacheInfo(int level, int data, int &l, int &k, int&n) {
    CPUID(cd, 0x00);
    if (cd.eax < 4)
        return 0;
    int i = 0;
    while (1) {
        CPUIDEX(cd, 0x04, i);
        int type = cd.eax & 0x1f;
        if (type == 0)
            return 0;
        int lev = ((cd.eax >> 5) & 0x07);
        if ((lev == level) && (((data == 0) && (type = 2)) || ((data == 1) && (type == 1))))
            break;
        i++;
    }
    k = ((cd.ebx >> 22) & 0x03ff) + 1;
    int partitions = ((cd.ebx) >> 12 & 0x03ff) + 1;
    n = cd.ecx + 1;
    l = (cd.ebx & 0x0fff) + 1;
    return partitions == 1;
}

//
// getDeterministicCacheInfo
//
int getDeterministicCacheInfo() {
    int type, ways, partitions, lineSz = 0, sets;
    int i = 0;
    while (1) {
        CPUIDEX(cd, 0x04, i);
        type = cd.eax & 0x1f;
        if (type == 0)
            break;
        cout << "L" << ((cd.eax >> 5) & 0x07);
        cout << ((type == 1) ? " D" : (type == 2) ? " I" : " U");
        ways = ((cd.ebx >> 22) & 0x03ff) + 1;
        partitions = ((cd.ebx) >> 12 & 0x03ff) + 1;
        sets = cd.ecx + 1;
        lineSz = (cd.ebx & 0x0fff) + 1;
        cout << " " << setw(5) << ways*partitions*lineSz*sets/1024 << "K" << " L" << setw(3) << lineSz << " K" << setw(3) << ways << " N" << setw(6) << sets;
        cout << endl;
        i++;
    }
    return lineSz;
}

//
// getCacheLineSz
//
int getCacheLineSz() {
    CPUID(cd, 0x00);
    if (cd.eax >= 4)
        return getDeterministicCacheInfo();
    return getL1DataCacheInfo();
}

//
// getPageSz
//
UINT getPageSz() {
#ifdef WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#elif __linux__
    return (UINT) sysconf(_SC_PAGESIZE);    // {joj 12/1/18}
#endif 
}

//
// getWallClockMS
//
UINT64 getWallClockMS() {
#ifdef WIN32
    return (UINT64) clock() * 1000 / CLOCKS_PER_SEC;
#elif __linux__
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec*1000 + t.tv_nsec / 1000000;
#endif
}

//
// setThreadCPU
//
void createThread(THREADH *threadH, WORKERF, void *arg) {
#ifdef WIN32
    *threadH = CreateThread(NULL, 0, worker, arg, 0, NULL);
#elif __linux__
    pthread_create(threadH, NULL, worker, arg);
#endif
}

//
// runThreadOnCPU
//
void runThreadOnCPU(UINT cpu) {
#ifdef WIN32
    SetThreadAffinityMask(GetCurrentThread(), 1ULL << cpu);
#elif __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

//
// closeThread
//
void closeThread(THREADH threadH) {
#ifdef WIN32
    CloseHandle(threadH);
#elif __linux__
    // nothing to do
#endif
}

//
// waitForThreadsToFinish
//
void waitForThreadsToFinish(UINT nt, THREADH *threadH) {
#ifdef WIN32
    WaitForMultipleObjects(nt, threadH, true, INFINITE);
#elif __linux__
    for (UINT thread = 0; thread < nt; thread++)
        pthread_join(threadH[thread], NULL);
#endif
}

//
// pauseIfKeyPressed
//
void pauseIfKeyPressed() {
#ifdef WIN32
    if (_kbhit()) {
        if (_getch() == ' ') {
            cout << endl << endl << "PAUSED - press key to continue";
            _getch();
            cout << endl;
        }
    }
#elif __linux__

#endif
};

//
// pressKeyToContinue
//
void pressKeyToContinue() {
#ifdef WIN32
    cout << endl << "Press any key to continue...";
    _getch();
#elif __linux__
    termios old, input;
    tcgetattr(fileno(stdin), &old);             // save settings
    input = old;                                // make new settings same as old settings
    input.c_lflag &= ~(ICANON | ECHO);          // disable buffered i/o and echo
    tcsetattr(fileno(stdin), TCSANOW, &input);  // use these new terminal i/o settings now
    puts("Press any key to continue...");
    getchar();
    tcsetattr(fileno(stdin), TCSANOW, &old);
#endif
}

//
// quit
//
void quit(int r) {
#ifdef WIN32
    cout << endl << "Press key to quit...";
    _getch();   // stop DOS window disappearing prematurely
#endif
    exit(r);
}

//
// rand
//
// due to George Marsaglia (google "xorshift wiki")
// NB: initial seed must NOT be 0
//
UINT64 rand(UINT64 &r) {
    r ^= r >> 12;   // a
    r ^= r << 25;   // b
    r ^= r >> 27;   // c
    return r * 2685821657736338717LL;
}

locale *commaLocale = NULL;

//
// setCommaLocale
//
void setCommaLocale() {
    if (commaLocale == NULL)
        commaLocale = new locale(locale(), new CommaLocale());
    cout.imbue(*commaLocale);
}

//
// setLocale
//
void setLocale() {
    cout.imbue(locale());
}

//
// getVMUse
//
size_t getVMUse() {
    size_t r = 0;

#ifdef WIN32

    HANDLE hProcess;
    PROCESS_MEMORY_COUNTERS pmc;

    if ((hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, GetCurrentProcessId()))) {
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
            r = pmc.PagefileUsage;
        CloseHandle(hProcess);
    }

#elif __linux__

    UINT64 vmuse;
    FILE* fp;

    if ((fp = fopen("/proc/self/statm", "r")) != NULL) {
        if (fscanf(fp, "%llu", &vmuse) == 1)
            r = vmuse * sysconf(_SC_PAGESIZE);
        fclose(fp);
    }

#endif

    return r;
}

//
// getMemUse
//
size_t getMemUse() {
    size_t r = 0;

#ifdef WIN32

    HANDLE hProcess;
    PROCESS_MEMORY_COUNTERS pmc;

    if ((hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, GetCurrentProcessId()))) {
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
            r = pmc.WorkingSetSize;
        CloseHandle(hProcess);
    }

#elif __linux__

    UINT64 memuse;
    FILE* fp;

    if ((fp = fopen("/proc/self/statm", "r")) != NULL) {
        if (fscanf(fp, "%*s%llu", &memuse) == 1)
            r = memuse * sysconf(_SC_PAGESIZE);
        fclose(fp);
    }

#endif

    return r;
}

// eof
