//
// tsxBST.cpp
//
// Copyright (C) 2013 - 2018 jones@scss.tcd.ie
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
// 11/12/13 first version
// 06/11/18 CS4012/4521 template
//

//
// gcc needs flags -mrtm -mrdrnd
//

//
// volatile int *p;                             // object pointed to by p is volatile
// int* volatile p;                             // p is volatile
//

#include "helper.h"                             //

#include <iostream>                             //
#include <sstream>                              // stringstream
#include <iomanip>                              // setprecision

using namespace std;                            // cout

#define K                   1024LL              // NB: 64 bit
#define M                   (K*K)               //
#define G                   (K*K*K)             //

//
// METHOD 0: no lock single thread
// METHOD 1: testAndTestAndSet lock
//
// TODO TODO TODO TODO TODO TODO TODO TODO TODO
//
// METHOD 2: HLE testAndTestAndSet lock
// METHOD 3: RTM
//

// Method = 2 - HLE testAndTestAndSet
// Method = 3 - RTM
#define METHOD              3                   // METHOD
//#define MOVENODE                              // move node rather than content
#define PREFILL             0                   // pre-fill with odd integers 0 .. maxKey-1 => 0: perfect 1: right list 2: left list

//
// key
//
#define MINKEY              (16)                // min key
#define MAXKEY              (1*M)               // max key
#define SCALEKEY            (4)                 // scale key by SCALEKEY

//
// number of threads
//
#define MINNT               1                   // min number of threads

//
// run each test for NSECONDS
//
#define NSECONDS            2                   // run each test for NSECONDS

//
// statistics
//
#define STATS               0x17                // STATS bit MASK 1:commit 2:abort 4:depth 8:tsxStatus 16:times

//
// memory management
//
#define ALIGNED                               // Node cache aligned
#define RECYCLENODES                          // recycle nodes

//
// work load
//
//#define CONTAINS          100                 // % contains (100-contains)/2 adds and removes
#define NOP                 1000                // number of operations between tests for exceeding runtime

#if CONTAINS < 0 || CONTAINS > 100
#error CONTAINS sould be in the range 0 to 100
#endif

//
// useful MACROs
//
#if STATS & 1                                   //
#define STAT1(x)    x                           //
#else
#define STAT1(x)                                //
#endif

#if STATS & 2                                   //
#define STAT2(x)    x                           //
#else
#define STAT2(x)                                //
#endif


#if STATS & 4                                   //
#define STAT4(x)    x                           //
#else
#define STAT4(x)                                //
#endif

#if STATS & 8                                   //
#define STAT8(x)    x                           //
#else
#define STAT8(x)                                //
#endif

#if STATS & 16                                  //
#define STAT16(x)   x                           //
#else
#define STAT16(x)                               //
#endif

#define DSUM    pt->avgD += d;      \
                if (d > pt->maxD)   \
                    pt->maxD = d                //

#define PT(bst, thread)     ((PerThreadData*) ((BYTE*) ((bst)->perThreadData) + (thread)*ptDataSz))

//
// global variables
//
UINT ntMin;                                     //
UINT nt;                                        // # threads
UINT maxThread;                                 //
UINT lineSz;                                    // cache line sz
UINT ptDataSz;                                  // per thread data size
UINT64 tStart;                                  // start time
UINT64 t0;                                      // start time of test
INT64 maxKey;                                   // 0 .. keyMax-1
UINT64 totalOps = 0;                            // cumulative ops

THREADH *threadH;                               // thread handles

TLSINDEX tlsPtIndx;                             // {joj 25/11/15}

//
// NB: ALL results
//
typedef struct {
    INT64 maxKey;                               // 0 .. maxKey
    UINT nt;                                    // # threads
    UINT64 pft;                                 // pre fill time (ms)
    UINT64 rt;                                  // test run time (ms)
    UINT64 nop;                                 // nop
    UINT64 nmalloc;                             // nmalloc
    UINT64 nfree;                               // nfree
    UINT64 avgD;                                // used to calculate average search depth of tree
    UINT64 maxD;                                // max depth of tree
    size_t vmUse;                               // vmUse
    size_t memUse;                              // memUse
    UINT64 ntree;                               // nodes in tree
    UINT64 tt;                                  // total time (ms) [fill time] + test run time + free memory time
} Result;

Result *r, *ravg;                               // for results
UINT rindx;                                     // results index
UINT64 errs = 0;                                // errors

class Node;                                     // forward declaration

//
// PerThreadData
//
typedef struct {
    UINT thread;                                // thread #
    UINT64 nop;                                 // nop
    UINT64 nmalloc;                             // nmalloc
    UINT64 nfree;                               // nfree
    UINT64 avgD;                                // average tree depth
    UINT64 maxD;                                // max tree depth
    Node *free;                                 // head of free node list (RECYLENODES)
} PerThreadData;

//
// derive from ALIGNEDMA for aligned memory allocation
//
class ALIGNEDMA {

public:

    void* operator new(size_t);                     // override new
    void operator delete(void*);                    // override delete

};

//
// new
//
void* ALIGNEDMA::operator new(size_t sz) {
    return AMALLOC(sz, lineSz);                     // allocate on a lineSz boundary
}

//
// delete
//
void ALIGNEDMA::operator delete(void *p) {
    AFREE(p);                                       // free memory
}

//
// Node
//
// Why are the Node members declared volatile? Consider the following code sequence:
//
//  .. = n->key                                     (1)
//
//  while(1)
//
//      UNIT status = _xbegin();
//
//      if (status == _XBEGIN_STARTED) {
//
//          .. = n->key;                            (2)
//
//          .. = n->key                             (3)
//
//          _xend();
//
//      } else {
//
//          // abort handler
//
//      }
//
// }
//
// It is important that (2) reads the key value from memory as its address needs to be added
// to the transaction read set. A compiler may optimise away the memory read (2) because it
// has already been read by (1). Interestingly, it doesn't matter if (3) is read from memory
// or not as long as (2) is a memory read. A write to the key by another thread will be caught
// by the hardware and the transaction aborted. Clearly, it is more efficient to read the key
// in (3) from a register copy.
//
#if defined(ALIGNED)
class Node : public ALIGNEDMA {
#else
class Node {
#endif

public:

    INT64 volatile key;                                     // volatile
    Node* volatile left;                                    // volatile
    Node* volatile right;                                   // volatile

    Node() {key = 0; right = left = NULL;}                  // default constructor
    Node(INT64 k) {key = k; right = left = NULL;}           // constructor

};

//
// BST
//
class BST : public ALIGNEDMA {

public:

    PerThreadData *perThreadData;                           // per thread data
    Node* volatile root;                                    //

    BST(UINT);                                              // constructor
    ~BST();                                                 // destructor

    int contains(INT64);                                    // return 1 if key in tree {joj 25/11/15}
    int add(INT64);                                         // add key to tree {joj 25/11/15}
    int remove(INT64);                                      // remove key from tree {joj 25/11/15}

    INT64 checkBST(Node*, UINT64& errBST);                  // {joj 13/12/15}

#ifdef PREFILL
    void preFill();                                         //
#endif

private:                                                    // private

#if METHOD == 1 || METHOD == 2
    ALIGN(64) volatile long lock;                           // lock
#elif METHOD == 2

#elif METHOD == 3


#endif

    int addTSX(Node*);                                      // add key into tree {joj 25/11/15}
    Node* removeTSX(INT64);                                 // remove key from tree {joj 25/11/15}

#if defined(RECYCLENODES)
    Node* alloc(INT64, PerThreadData*);                     // alloc
    void dealloc(Node*, PerThreadData*);                    // dealloc
#endif

    INT64 checkHelper(Node*, INT64, INT64, UINT64&);        // {joj 13/12/15}

};

//
// binary search tree
//
BST *bst;                                                   // binary search tree

//
// BST constructor
//
BST::BST(UINT nt)  {                                                    //
    perThreadData = (PerThreadData*) AMALLOC(nt*ptDataSz, lineSz);      // for per thread data
    memset(perThreadData, 0, nt*ptDataSz);                              // zero
    for (UINT thread = 0; thread < nt; thread++)                        //
        PT(this, thread)->thread = thread;                              //
    root = NULL;                                                        //

#if METHOD == 1 || METHOD == 2
    lock = 0;
#elif METHOD == 3


#endif

}

#if defined(RECYCLENODES)

//
// alloc
//
inline Node* BST::alloc(INT64 key, PerThreadData *pt) {
    Node *n;
    if (pt->free) {
        n = pt->free;
        pt->free = n->right;
        n->key = key;
        n->left = n->right = NULL;
    } else {
        n = new Node(key);
        pt->nmalloc++;
    }
    return n;
}

//
// dealloc
//
inline void BST::dealloc(Node *n, PerThreadData *pt) {
    n->right = pt->free;
    pt->free = n;
}

#endif

//
// checkHelper
//
// will not work if key VINTMIN or VINTMAX added
//
INT64 BST:: checkHelper(Node *n, INT64 min, INT64 max, UINT64& errBST) {
    if (n == NULL)
        return 0;
    if (n->key <= min || n->key >= max)
        errBST += 1;
    return checkHelper(n->left, min, n->key, errBST) + checkHelper(n->right, n->key, max, errBST) + 1;
}

//
//
// checkBST
//
// check tree structure
// in-order traversal of binary tree making sure keys in correct range
// return number of nodes in BST
// count errors
//
INT64 BST::checkBST(Node* n, UINT64& errBST) {
    errBST = 0;
    return checkHelper(n, INT64MIN, INT64MAX, errBST);
}

//
// BST destructor
//
BST::~BST() {
    AFREE(perThreadData);
}

//
// contains - search for key in tree
//
// METHOD 0: NO lock single thread
// METHOD 1: testAndTestAndSet
//
// return 1 if key in tree
//
int BST::contains(INT64 key) {
    
    PerThreadData *pt = (PerThreadData*)TLSGETVALUE(tlsPtIndx);
    Node *p = root;
    STAT4(UINT64 d = 0);

#if METHOD == 1
    while (_InterlockedExchange(&lock, 1)) {
        do {
            _mm_pause();
        } while (lock);
    }

#elif METHOD == 2
    while (_InterlockedExchange_HLEAcquire(&lock, 1)) {
        do {
            _mm_pause();
        } while (lock);
    }

#elif METHOD == 3
    while(1){
        int status = _xbegin();
        if(status == _XBEGIN_STARTED){
#endif
    while (p) {
        STAT4(d++);
        if (key < p->key) {
            p = p->left;
        } else if (key > p->key) {
            p = p->right;
        } else {
#if METHOD == 1
            lock = 0;
#elif METHOD == 2
            _Store_HLERelease(&lock, 0);
#elif METHOD == 3
            _xend();
#endif
            STAT4(DSUM);
            return 1;
#if METHOD == 3
        }
        }
    _xend();
    break;
#endif   
        }
    }

#if METHOD == 1
    lock = 0;
#elif METHOD == 2
    _Store_HLERelease(&lock, 0);
#elif METHOD == 3

#endif
    STAT4(DSUM);
    return 0;
}

//
// addTSX - add key to tree
//
// METHOD 0: NO lock single thread
// METHOD 1: testAndTestAndSet
//
// return 1 if key found
//
int BST::addTSX(Node *n) {

    PerThreadData *pt = (PerThreadData*)TLSGETVALUE(tlsPtIndx);
    Node* volatile *pp = &root;
    Node *p = root;
    STAT4(UINT64 d = 0);

#if METHOD == 1
    while (_InterlockedExchange(&lock, 1)) {
        do {
            _mm_pause();
        } while (lock);
    }

#elif METHOD == 2
    while (_InterlockedExchange_HLEAcquire(&lock, 1)) {
        do {
            _mm_pause();
        } while (lock);
    }
#elif METHOD == 3
    while(1){
    int status = _xbegin();
    if(status == _XBEGIN_STARTED){

#endif

    while (p) {
        STAT4(d++);
        if (n->key < p->key) {
            pp = &p->left;
        } else if (n->key > p->key) {
            pp = &p->right;
        } else {
#if METHOD == 1
            lock = 0;

#elif METHOD == 2
            _Store_HLERelease(&lock, 0); 
#elif METHOD == 3
            _xend();
#endif
            STAT4(DSUM);
            return 0;
        }
        p = *pp;
    } 

    *pp = n;
#if METHOD == 1
    lock = 0;
    _Store_HLERelease(&lock, 0); 

#elif METHOD == 2

#elif METHOD == 3
    _xend();
    break;
    }
    }
    

#endif
    STAT4(DSUM);
    return 1;
}

//
// removeTSX - remove key from tree
//
// METHOD 0: NO lock single thread
// METHOD 1: testAndTestAndSet
//
// return pointer to removed node, otherwise NULL
//
Node* BST::removeTSX(INT64 key) {

    PerThreadData *pt = (PerThreadData*)TLSGETVALUE(tlsPtIndx);
    Node* volatile *pp = &root;
    Node *p = root;
    STAT4(UINT64 d = 0);

#if METHOD == 1
    while (_InterlockedExchange(&lock, 1)) {
        do {
            _mm_pause();
        } while (lock);
    }


#elif METHOD == 2
    while (_InterlockedExchange_HLEAcquire(&lock, 1)) {
        do {
            _mm_pause();
        } while (lock);
    }
#elif METHOD == 3
    while(1){
    int status = _xbegin();
    if(status == _XBEGIN_STARTED){

#endif

    while (p) {
        STAT4(d++);
        if (key < p->key) {
            pp = &p->left;
        } else if (key > p->key) {
            pp = &p->right;
        } else {
            break;
        }
        p = *pp;
    }

    if (p == NULL) {
#if METHOD == 1
        lock = 0;
#elif METHOD == 2
        _Store_HLERelease(&lock, 0);
#elif METHOD == 3
        _xend();
#endif
        STAT4(DSUM);
        return NULL;
    }

    Node *left = p->left;
    Node *right = p->right;
    if (left == NULL && right == NULL) {
        *pp = NULL;
    } else if (left == NULL) {
        *pp = right;
    } else if (right == NULL) {
        *pp = left;
    } else {
        Node* volatile *ppr = &p->right;
        Node *r = right;
        while (r->left) {
            ppr = &r->left;
            r = r->left;
        }
#ifdef MOVENODE
        *ppr = r->right;
        r->left = p->left;
        r->right = p->right;
        *pp = r;
#else
        p->key = r->key;
        p = r;
        *ppr = r->right;
#endif
    }

#if METHOD == 1
    lock = 0;
#elif METHOD == 2
    _Store_HLERelease(&lock, 0);
#elif METHOD == 3
    _xend();
    break;
    }
    }

#endif
    STAT4(DSUM);
    return  p;

}

//
// add
//
// add key to tree
//
int BST::add(INT64 key) {

#if defined(RECYCLENODES)
    PerThreadData *pt = (PerThreadData*)TLSGETVALUE(tlsPtIndx); // {joj 6/11/18}
    Node *n = alloc(key, pt);
#else
    Node *n = new Node(key);
    pt->nmalloc++;
#endif
    if (addTSX(n) == 0) {
#if defined(RECYCLENODES)
        dealloc(n, pt);
#else
        delete n;
        pt->nfree++;
#endif
        return 0;
    }
    return 1;
}

//
// remove
//
// remove key from tree
//
int BST::remove(INT64 key) {

    if (Node volatile *n = removeTSX(key)) {
#if defined(RECYCLENODES)
        PerThreadData *pt = (PerThreadData*)TLSGETVALUE(tlsPtIndx); // {joj 6/11/18}
        dealloc((Node *) n, pt);
#else
        delete n;
        pt->nfree++;
#endif
        return 1;
    }
    return 0;
}

#if defined(PREFILL) && PREFILL == 0

//
// preFillHelper
//
void preFillHelper(Node* volatile &root, INT64 minKey, INT64 maxKey, INT64 diff, PerThreadData *pt) {
    if (maxKey - minKey <= diff)
        return;
    INT64 key = minKey + (maxKey - minKey) / 2;
    root = new Node(key);
    pt->nmalloc++;
    preFillHelper(root->left, minKey, key, diff, pt);
    preFillHelper(root->right, key, maxKey, diff, pt);
}

//
// preFillWorker
//
// NB: use thread to set up values for preFillHelper
//
WORKER preFillWorker(void* vthread) {

    UINT64 thread = (UINT64) vthread;

    INT64 minK = 0;
    INT64 maxK = maxKey - 1;
    INT64 key = 0;
    for (UINT64 n = ncpu / 2; n; n /= 2) {
        key = minK + (maxK - minK) / 2;
        if (thread & n) {
            minK = key;
        } else {
            maxK = key;
        }
    }
    Node *np = bst->root;
    while (np) {
        if (key < np->key) {
            np = np->left;
        } else if (key > np->key) {
            np = np->right;
        } else {
            break;
        }
    }

    PerThreadData *pt = PT(bst, thread);
    preFillHelper((key == maxK) ? np->left : np->right, minK, maxK, 2, pt);

    return 0;
}

//
// preFill
//
// NB: single thread for small trees, ALL threads for large trees
//
void BST::preFill() {

    //
    // for small lists use a single thread
    //
    if (maxKey <= 1*M) {
        preFillHelper(bst->root, 0, maxKey - 1, 2, PT(this, 0));
        return;
    }

    //
    // fill top ncpu-1 tree nodes
    //
    preFillHelper(bst->root, 0, maxKey - 1, maxKey / ncpu, PT(this, 0));

    //
    // create worker threads and wait to finish
    //
    for (UINT thread = 0; thread < ncpu; thread++)
        createThread(&threadH[thread], preFillWorker, (void*) (UINT64) thread);
    waitForThreadsToFinish(ncpu, threadH);

    for (UINT thread = 0; thread < ncpu; thread++)
        closeThread(threadH[thread]);

    //
    // accumulate nmalloc stats on thread 0 for threads >= nt
    //
    for (UINT cpu = 1; cpu < ncpu; cpu++) {
        if (cpu >= nt) {
            PT(this, 0)->nmalloc += PT(this, cpu)->nmalloc;
            PT(this, cpu)->nmalloc = 0;
        }
    }

}

#elif defined(PREFILL) && (PREFILL == 1 || PREFILL == 2)

struct PARAMS {
    PerThreadData *pt;
    INT64 minKey;
    INT64 maxKey;
    Node *first;
    Node *last;
};

//
// preFillWorker
//
// NB: worker for single and multi-threaded prefill
// NB: makes an ordered list containing the odd numbers in the range params->minKey to params->maxKey
// NB: assumes params->minKey and params->maxKey are both even
// NB: assumes params->first is NULL
//
WORKER preFillWorker(void *_params) {

    PARAMS *params = (PARAMS*) _params;     // coercion
    INT64 key = params->minKey + 1;         // insert odd integers

    while (key < params->maxKey) {
        Node *nn = new Node(key);
        params->pt->nmalloc++;
#if PREFILL == 1
        if (params->first == NULL) {        // in ascending order
            params->first = nn;
        } else {
            params->last->right = nn;
        }
        params->last = nn;
#else
        if (params->first == NULL) {        // in descending order
            params->last = nn;
        } else {
            nn->left = params->first;
        }
        params->first = nn;
#endif
        key += 2;
    }

    return 0;
}

//
// preFill
//
// NB: single thread for small trees, ALL threads for large trees
//
void BST::preFill() {

    //
    // for small lists use a single thread
    //
    if (maxKey <= 1*M) {
        PARAMS params;
        params.pt = PT(this, 0);                // use thread 0
        params.minKey = 0;
        params.maxKey = maxKey;
        params.first = NULL;
        params.last = NULL;
        preFillWorker(&params);
        root = params.first;
        //params.last->left = new (PT(this, 0)->thread) Node(3);        // error
        return;
    }

    //
    // following code is future proofing!
    // on current CPUs, searching very long ordered lists is slow
    //
    // set up parameters
    //
    PARAMS *params = new PARAMS[ncpu];
    for (UINT cpu = 0; cpu < ncpu; cpu++) {
        params[cpu].pt = PT(this, cpu);
        params[cpu].minKey = maxKey / ncpu * cpu;
        params[cpu].maxKey = maxKey / ncpu * (cpu + 1);
        params[cpu].first = NULL;
        params[cpu].last = NULL;
    }

    //
    // create worker threads and wait to finish
    //
    for (UINT cpu = 0; cpu < ncpu; cpu++)
        createThread(&threadH[cpu], preFillWorker, (void*) &params[cpu]);
    waitForThreadsToFinish(ncpu, threadH);


    //
    // concatenate individual lists in ascending or descending order
    //
    for (UINT cpu = 0; cpu < ncpu; cpu++) {
#if PREFILL == 1
        if (cpu == 0) {
            root = params[0].first;
        } else {
            params[cpu-1].last->right = params[cpu].first;
        }
#else
        if (cpu == ncpu - 1) {
            root = params[cpu].first;
        } else {
            params[cpu+1].last->left = params[cpu].first;
        }
#endif
    }

    //
    // tidy up
    //
    for (UINT cpu = 0; cpu < ncpu; cpu++)
        closeThread(threadH[cpu]);

    delete params;

    //
    // accumulate nmalloc stats on thread 0 for threads >= nt
    //
    for (UINT cpu = 1; cpu < ncpu; cpu++) {
        if (cpu >= nt) {
            PT(this, 0)->nmalloc += PT(this, cpu)->nmalloc;
            PT(this, cpu)->nmalloc = 0;
        }
    }

}

#endif

//
// worker thread
//
WORKER worker(void* vthread) {

    PerThreadData *pt = PT(bst, (UINT64)vthread);       // {joj 25/11/15}
    TLSSETVALUE(tlsPtIndx, pt);                         // {joj 25/11/15}

    UINT64 r;                                           // local variable for pseudo random number generator
    while (_rdrand64_step(&r) == 0);                    // random seed for rand

    while (1) {

        //
        // do some work
        //
        for (int i = 0; i < NOP; i++) {

            UINT64 k = rand(r);

#if CONTAINS == 0
            INT64 key = k & (maxKey - 1);
            if (k >> 63) {
                bst->add(key);
            } else {
                bst->remove(key);
            }
#else
            UINT op = k % 100;
            INT64 key = (k / 100) & (maxKey - 1);
            if (op < CONTAINS) {
                bst->contains(key);
            } else if (op < CONTAINS + (100 - CONTAINS) / 2) {
                bst->add(key);
            } else {
                bst->remove(key);
            }
#endif
        }
        pt->nop += NOP;

        //
        // check if runtime exceeded
        //
        if (getWallClockMS() - t0 > NSECONDS*1000)
            break;

    }

    return 0;

}

//
// header
//
void header() {

    char date[256];

    getDateAndTime(date, sizeof(date));

    cout << getHostName() << " " << getOSName() << (is64bitExe() ? " 64" : " 32") << " bit exe";

#ifdef _DEBUG
    cout << " DEBUG";
#else
    cout << " RELEASE";
#endif
#if METHOD == 0
    cout << " BST [NO lock single thread ONLY]";
#elif METHOD == 1
    cout << " BST [testAndTestAndSet lock]";

#elif METHOD == 2
    cout << " BST [HLE testAndTestAndSet lock]";
#elif METHOD == 3
    cout << " BST [RTM]";
#endif

    cout << " NCPUS=" << ncpu << " RAM=" << (getPhysicalMemSz() + G - 1) / G << "GB ";

    cout << endl;

    cout << "METHOD=" << METHOD << " ";
#ifdef ALIGNED
    cout << "ALIGNED ";
#endif
#ifdef CONTAINS
    cout << "CONTAINS=" << CONTAINS << "% ADD=" << (100 - CONTAINS) / 2 << "% REMOVE=" << (100 - CONTAINS) / 2 << "% ";
#endif
#ifdef MOVENODE
    cout << "MOVENODE ";
#endif
    cout << "NOP=" << NOP << " ";
    cout << "NSECONDS=" << NSECONDS << " ";
#ifdef PREFILL
    cout << "PREFILL=" << PREFILL << " ";
#endif
#if defined(RECYCLENODES)
    cout << "RECYCLENODES ";
#endif
#ifdef STATS
    cout << "STATS=0x" << setfill('0') << hex << setw(2) << STATS << dec << setfill(' ') << " ";
#endif

    cout << "sizeof(Node)=" << sizeof(Node) << endl;;

    cout << "Intel" << (cpu64bit() ? "64" : "32") << " family " << cpuFamily() << " model " << cpuModel() << " stepping " << cpuStepping() << " " << cpuBrandString() << endl;

}

//
// main
//
int main(int argc, char* argv[]) {

    ncpu = getNumberOfCPUs();           // get number of CPUs
    if (ncpu > 32)                      // {joj 6/11/18}
        ncpu = 32;                      //
    maxThread = 2 * ncpu;               //

    TLSALLOC(tlsPtIndx);                // {joj 25/11/15}

    header();                           // output header

    tStart = time(NULL);                // start time

    lineSz = getCacheLineSz();          // get cache line size and output cache organisation
    ptDataSz = (UINT) (sizeof(PerThreadData) + lineSz - 1) / lineSz * lineSz;          // {joj 12/2/18}

    //cout << "sizeof(PerThreadData)=" << sizeof(PerThreadData) << " ptDataSz=" << ptDataSz << " sizeof(Result)=" << sizeof(Result);

    cout << endl;

#if METHOD == 2
    //
    // check if HLE supported
    //
    if (!hleSupported()) {
        cout << "HLE (hardware lock elision) NOT supported by this CPU" << endl;
        quit();
        return 1;
    }
#endif

#if METHOD == 3
    //
    // check if RTM supported
    //
    if (!rtmSupported()) {
        cout << "RTM (restricted transactional memory) NOT supported by this CPU" << endl;
        quit();
        return 1;
    }
#endif

#ifdef WIN32
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif

    INT64 keyMin = MINKEY;                                                  // set min key
    INT64 keyMax = MAXKEY;                                                  // set max key
#if METHOD == 0                                 
    ntMin = 1;
    UINT ntMax = 1;
#else
    ntMin = MINNT;
    UINT ntMax = maxThread;                                                 //
#endif

    int c0 = 1, c1 = 1;                                                     //
    for (maxKey = keyMin; maxKey < keyMax; maxKey *= SCALEKEY)              //
        c0++;                                                               //
    for (nt = ntMin; nt < ntMax; nt *= 2)                                   //
        c1++;                                                               //

    threadH = (THREADH*) AMALLOC(maxThread*sizeof(THREADH), lineSz);        // thread handles
    r = (Result*) AMALLOC(c0*c1*sizeof(Result), lineSz);                    // for results
    ravg = (Result*) AMALLOC(c0*c1*sizeof(Result), lineSz);                 // for averages
    memset(ravg, 0, c0*c1*sizeof(Result));                                  // clear results

        //
        // results
        //
        setCommaLocale();
        int keyw = (int) log10((double) keyMax) + (int) (log10((double) keyMax) / log10(1000)) + 2;
        keyw = (keyw < 7) ? 7 : keyw;
        cout << setw(keyw - 1) << "maxKey" << setw(3) << "nt";
#ifdef PREFILL
        STAT16(cout << setw(7) << "pft");
#endif
        cout << setw(7) << "rt";
        cout << setw(16) << "ops" << setw(12) << "ops/s";
#if METHOD > 0
        if (ntMin == 1)
            cout << setw(8) << "rel";
#endif
        cout << setw(14) << "nMalloc" << setw(14) << "nFree";
        cout << setw(keyw) << "ntree";
        cout << setw(11) << "vmUse" << setw(11) << "memUse";
        STAT4(cout << setw(keyw) << "avgD" << setw(keyw) << "maxD");
#if METHOD > 1
        STAT1(cout << setw(8) << "commit");
#endif
        STAT16(cout << setw(7) << "tt");
        cout << endl;

        cout << setw(keyw - 1) << "------" << setw(3) << "--";          // maxKey nt
#ifdef PREFILL
        STAT16(cout << setw(7) << "---");                               // pft
#endif
        cout << setw(7) << "--";                                        // rt
        cout << setw(16) << "---" << setw(12) << "-----";               // ops ops/s
#if METHOD > 0
        if (ntMin == 1)
            cout << setw(8) << "---";                                   // rel
#endif
        cout << setw(14) << "-------" << setw(14) << "-----";           // nMalloc nFree
        cout << setw(keyw) << "-----";                                  // ntree
        cout << setw(11) << "-----" << setw(11) << "------";            // vmUse memUse
        STAT4(cout << setw(keyw) << "----" << setw(keyw) << "----");    // avgD maxD

        STAT16(cout << setw(7) << "--");                                // tt
        cout << endl;

        rindx = 0;                                                      // zero results index

        for (maxKey = keyMin; maxKey <= keyMax; maxKey *= SCALEKEY) {

#if METHOD > 0
            double noppersec1 = 1;
#endif

            for (nt = ntMin; nt <= ntMax; nt *= 2) {

                bst = new BST(maxThread);                               // create an empty binary search tree

                t0 = getWallClockMS();                                  // get start time

#ifdef PREFILL
                bst->preFill();
                UINT64 pft = getWallClockMS() - t0;
                t0 = getWallClockMS();                                  // get start time
#else
                UINT64 pft = 0;                                         //
#endif

                //
                // create worker threads
                //
                for (UINT thread = 0; thread < nt; thread++)
                    createThread(&threadH[thread], worker, (void*) (UINT64) thread);

                //
                // wait for ALL worker threads to finish
                //
                waitForThreadsToFinish(nt, threadH);
                UINT64 rt = getWallClockMS() - t0;

                //
                // calculate results
                //
                UINT64 nop = 0, nmalloc = 0, nfree = 0, ntree;
                UINT64 avgD = 0, maxD = 0;
                size_t vmUse, memUse;

                for (UINT thread = 0; thread < nt; thread++) {
                    PerThreadData *pt = PT(bst, thread);
                    nop += pt->nop;
                    nmalloc += pt->nmalloc;

#if defined(RECYCLENODES)
                    while (pt->free) {
                        Node *tmp = pt->free->right;
                        delete pt->free;
                        pt->free = tmp;
                        pt->nfree++;
                    }
#endif
                    nfree += pt->nfree;
                    avgD += pt->avgD;
                    maxD = (pt->maxD > maxD) ? pt->maxD : maxD;
                }

#if METHOD > 0
                if (nt == 1)
                    noppersec1 = (double) nop / (double) rt / 1000;     // {joj 12/1/18}
#endif

                totalOps += nop;

                //
                // save results
                //
                r[rindx].maxKey = maxKey;
                r[rindx].nt = nt;
                r[rindx].pft = pft;
                r[rindx].rt = rt;
                r[rindx].nop = nop;
                r[rindx].nmalloc = nmalloc;
                r[rindx].nfree = nfree;
                r[rindx].avgD = avgD;
                r[rindx].maxD = maxD;

                //
                // get vmUse and memUse before deleting BST
                //
                vmUse = r[rindx].vmUse = getVMUse();
                memUse = r[rindx].memUse = getMemUse();

                //
                // output results
                //
                cout << setw(keyw - 1) << maxKey << setw(3) << nt;
#ifdef PREFILL
                STAT16(cout << setw(7) << fixed << setprecision(pft < 100*1000 ? 2 : 0) << (double) pft / 1000);
#endif
                cout << setw(7) << fixed << setprecision(rt < 100*1000 ? 2 : 0) << (double) rt / 1000;
                cout << setw(16) << nop << setw(12) << nop * 1000 / rt;
#if METHOD > 0
                if (ntMin == 1)
                    cout << " [" << fixed << setprecision(2) << setw(5) << (double) nop / (double) rt / 1000 / noppersec1 << "]";   // {joj 12/1/18}
#endif
                cout << setw(14) << nmalloc << setw(14) << nfree;

                cout << flush;  // useful on linux

                UINT64 errBST;
                ntree = bst->checkBST(bst->root, errBST);
                r[rindx].ntree = ntree;
                cout << setw(keyw) << ntree;
                delete bst;

                if (vmUse / G) {
                    cout << fixed << setprecision(2) << setw(9) << (double) vmUse / G << "GB";
                } else {
                    cout << fixed << setprecision(2) << setw(9) << (double) vmUse / M << "MB";
                }

                if (memUse / G) {
                    cout << fixed << setprecision(2) << setw(9) << (double) memUse / G << "GB";
                } else {
                    cout << fixed << setprecision(2) << setw(9) << (double) memUse / M << "MB";
                }

                STAT4(double davgD = (double) avgD / (double) nop); //  {joj 12/1/18}
                STAT4(cout << fixed << setprecision(2) << setw(keyw) << setprecision(davgD < 1000 ? 2 : 0) << davgD << setw(keyw) << maxD);

                UINT64 tt = getWallClockMS() - t0;
#ifdef PREFILL
                tt += pft;
#endif
                STAT16(cout << setw(7) << fixed << setprecision(tt < 100*1000 ? 2 : 0) << (double) tt / 1000);

                //
                // tidy up
                //
                for (UINT thread = 0; thread < nt; thread++)
                    closeThread(threadH[thread]);

                //
                // check for errors
                //
                if (errBST || (nmalloc != ntree + nfree)) {

                    cout << " ERROR:";

                    //
                    // BST NOT correct
                    //
                    if (errBST) {
                        errs++;
                        cout << " BST inccorrect";
                    }

                    //
                    // nodes missing
                    //
                    if (nmalloc != ntree + nfree) {
                        errs++;
                        cout << " diff= " << nmalloc - ntree - nfree;
                    }

                    cout << " [errs=" << errs << "]";

                }

                //
                // accumlate averages
                //
                ravg[rindx].pft += r[rindx].pft;
                ravg[rindx].rt += r[rindx].rt;
                ravg[rindx].nop += r[rindx].nop;
                ravg[rindx].nmalloc += r[rindx].nmalloc;
                ravg[rindx].nfree += r[rindx].nfree;
                ravg[rindx].avgD += r[rindx].avgD;
                ravg[rindx].maxD += r[rindx].maxD;
                ravg[rindx].vmUse += r[rindx].vmUse;
                ravg[rindx].memUse += r[rindx].memUse;
                ravg[rindx].ntree += r[rindx].ntree;
                ravg[rindx].tt += tt;

                rindx++;
                cout << endl;

        } // nt

    } // maxkey

    pressKeyToContinue();

    return 0;

}

// eof
