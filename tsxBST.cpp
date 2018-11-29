//
// addTSX - add key to tree
//
// METHOD 0: NO lock single thread
// METHOD 1: testAndTestAndSet
//
// return 1 if key found
//
int BST::addTSX(Node *n)
{

    PerThreadData *pt = (PerThreadData *)TLSGETVALUE(tlsPtIndx);
    STAT4(UINT64 d = 0);
    uint16_t numAttempts = 0;
    // Retry RTM infinitely
    while (1){
        Node *volatile *pp = &root;
        Node *p = root;
        int status = 0;
        // Use RTM if we haven't exceeded max attempts
        if (numAttempts <= MAXATTEMPTS){
            status = _xbegin();
        }
        else{
            // Fallback to t&t&s lock
            while (_InterlockedExchange(&lock, 1)){
                do{
                    _mm_pause();
                } while (lock);
            }
            // Set status so following `if` is passed
            status = _XBEGIN_STARTED;
        }
        if (status == _XBEGIN_STARTED){
            while (p){
                STAT4(d++);
                if (n->key < p->key){
                    pp = &p->left;
                }
                else if (n->key > p->key)
                {
                    pp = &p->right;
                }
                else
                {
                    // Return path, so end TX or unset lock
                    if (numAttempts <= MAXATTEMPTS){
                        _xend();
                    }
                    else{
                        lock = 0;
                    }
                    STAT4(DSUM);
                    return 0;
                }
                p = *pp;
            }

            *pp = n;
            // Node added, end TX and break to exit
            _xend();
            break;
        }
        else{
            numAttempts++;
        }
    }
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
Node *BST::removeTSX(INT64 key){

    PerThreadData *pt = (PerThreadData *)TLSGETVALUE(tlsPtIndx);
    Node *volatile *pp = &root;
    Node *p = root;
    STAT4(UINT64 d = 0);

    uint16_t numAttempts = 0;
    while (1){
        int status = 0;
        if (numAttempts <= MAXATTEMPTS){
            status = _xbegin();
        }
        else{
            // Fallback to t&t&s lock
            while (_InterlockedExchange(&lock, 1)){
                do{
                    _mm_pause();
                } while (lock);
            }
            // Set status so following if is passed
            status = _XBEGIN_STARTED;
        }
        if (status == _XBEGIN_STARTED){

            while (p){
                STAT4(d++);
                if (key < p->key){
                    pp = &p->left;
                }
                else if (key > p->key){
                    pp = &p->right;
                }
                else{
                    break;
                }
                p = *pp;
            }

            if (p == NULL){

                if (numAttempts <= MAXATTEMPTS){
                    _xend();
                }
                else{
                    lock = 0;
                }

                STAT4(DSUM);
                return NULL;
            }

            Node *left = p->left;
            Node *right = p->right;
            if (left == NULL && right == NULL){
                *pp = NULL;
            }
            else if (left == NULL){
                *pp = right;
            }
            else if (right == NULL){
                *pp = left;
            }
            else{
                Node *volatile *ppr = &p->right;
                Node *r = right;
                while (r->left){
                    ppr = &r->left;
                    r = r->left;
                }
                p->key = r->key;
                p = r;
                *ppr = r->right;
            }

            _xend();
            break;
        }
        else
        {
            numAttempts += 1;
        }
    }

    STAT4(DSUM);
    return p;
}