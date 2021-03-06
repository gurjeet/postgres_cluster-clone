#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/time.h>
#include <pthread.h>

#include <string>
#include <vector>

#include <pqxx/connection>
#include <pqxx/transaction>
#include <pqxx/nontransaction>

using namespace std;
using namespace pqxx;

template<class T>
class my_unique_ptr
{
    T* ptr;
    
  public:
    my_unique_ptr(T* p = NULL) : ptr(p) {}
    ~my_unique_ptr() { delete ptr; }
    T& operator*() { return *ptr; }
    T* operator->() { return ptr; }
    void operator=(T* p) { ptr = p; }
    void operator=(my_unique_ptr& other) {
        ptr = other.ptr;
        other.ptr = NULL;
    }        
};

typedef void* (*thread_proc_t)(void*);
typedef int64_t csn_t;

struct thread
{
    pthread_t t;
    size_t proceeded;
    size_t aborts;
    time_t max_trans_duration;
    int id;

    void start(int tid, thread_proc_t proc) { 
        id = tid;
        proceeded = 0;
        aborts = 0;
        max_trans_duration = 0;
        pthread_create(&t, NULL, proc, this);
    }

    void wait() { 
        pthread_join(t, NULL);
    }
};

struct config
{
    int nReaders;
    int nWriters;
    int nIterations;
    int nAccounts;
    int startId;
    int diapason;
    bool deadlockFree;
    bool maxSnapshot;
    bool makeSavepoints;
    vector<string> connections;

    config() {
        nReaders = 1;
        nWriters = 10;
        nIterations = 1000;
        nAccounts = 100000;  
        startId = 0;
        diapason = 100000;
        deadlockFree = false;
        makeSavepoints = false;
        maxSnapshot = false;
    }
};

config cfg;
bool running;

#define USEC 1000000

static time_t getCurrentTime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (time_t)tv.tv_sec*USEC + tv.tv_usec;
}

inline csn_t max(csn_t t1, csn_t t2) { 
    return t1 < t2 ? t2 : t1;
}

void exec(transaction_base& txn, char const* sql, ...)
{
    va_list args;
    va_start(args, sql);
    char buf[1024];
    vsprintf(buf, sql, args);
    va_end(args);
    txn.exec(buf);
}

int64_t execQuery( transaction_base& txn, char const* sql, ...)
{
    va_list args;
    va_start(args, sql);
    char buf[1024];
    vsprintf(buf, sql, args);
    va_end(args);
    result r = txn.exec(buf);
    return r[0][0].as(int64_t());
}  

void* reader(void* arg)
{
    thread& t = *(thread*)arg;
    vector< my_unique_ptr<connection> > conns(cfg.connections.size());
    for (size_t i = 0; i < conns.size(); i++) {
        conns[i] = new connection(cfg.connections[i]);
    }
    int64_t prevSum = 0;

    while (running) {
        csn_t snapshot = 0;
        vector< my_unique_ptr<work> > txns(conns.size());
        time_t start = getCurrentTime();
        for (size_t i = 0; i < conns.size(); i++) {        
            txns[i] = new work(*conns[i]);
        }
        if (cfg.maxSnapshot) { 
            for (size_t i = 0; i < txns.size(); i++) {        
                snapshot = max(snapshot, execQuery(*txns[i], "select dtm_extend()"));
            }
            for (size_t i = 0; i < txns.size(); i++) {                    
                execQuery(*txns[i], "select dtm_access(%ld)", snapshot);
            }
        } else {             
            for (size_t i = 0; i < txns.size(); i++) {        
                if (i == 0) {
                    snapshot = execQuery(*txns[i], "select dtm_extend()");
                } else {
                    snapshot = execQuery(*txns[i], "select dtm_access(%ld)", snapshot);
                }
            }
        }
        int64_t sum = 0;
        for (size_t i = 0; i < txns.size(); i++) {        
            sum += execQuery(*txns[i], "select sum(v) from t");
        }
        if (sum != prevSum) {
            printf("Total=%ld snapshot=%ld, delta=%ld usec\n", sum, snapshot, getCurrentTime()-snapshot);
            prevSum = sum;
        }
        t.proceeded += 1;
        time_t elapsed = getCurrentTime() - start;
        if (elapsed > t.max_trans_duration) { 
            t.max_trans_duration = elapsed;
        }
    }
    return NULL;
}
 
void* writer(void* arg)
{
    thread& t = *(thread*)arg;
    connection *srcCon, *dstCon;

    srcCon = new connection(cfg.connections[t.id % cfg.connections.size()]);
    dstCon = new connection(cfg.connections[(t.id + 1) % cfg.connections.size()]);

    for (int i = 0; i < cfg.nIterations; i++)
    { 
        char gtid[32];

        int srcAcc = cfg.startId + random() % cfg.diapason;
        int dstAcc = cfg.startId + random() % cfg.diapason;

        if (cfg.deadlockFree && srcAcc > dstAcc) { // avoid deadlocks
            int tmpAcc = dstAcc;
            dstAcc = srcAcc;
            srcAcc = tmpAcc;
        }
        sprintf(gtid, "%d.%d.%d", cfg.startId, t.id, i);

        nontransaction srcTx(*srcCon);
        nontransaction dstTx(*dstCon);
        
        time_t start = getCurrentTime();

        exec(srcTx, "begin transaction");
        exec(dstTx, "begin transaction");

        if (cfg.maxSnapshot) {
            csn_t snapshot = execQuery(srcTx, "select dtm_extend('%s')", gtid);
            snapshot = max(snapshot, execQuery(dstTx, "select dtm_extend('%s')", gtid));
            execQuery(srcTx, "select dtm_access(%ld, '%s')", snapshot, gtid);
            execQuery(dstTx, "select dtm_access(%ld, '%s')", snapshot, gtid);
        } else { 
            csn_t snapshot = execQuery(srcTx, "select dtm_extend('%s')", gtid);
            snapshot = execQuery(dstTx, "select dtm_access(%ld, '%s')", snapshot, gtid);
        }
        if (cfg.makeSavepoints) { 
            exec(srcTx, "savepoint c1");        
            exec(dstTx, "savepoint c2");        
        }
        try { 
            exec(srcTx, "update t set v = v - 1 where u=%d", srcAcc);        
            exec(dstTx, "update t set v = v + 1 where u=%d", dstAcc);
        } catch (pqxx_exception const& x) { 
            exec(srcTx, "rollback");
            exec(dstTx, "rollback");
            t.aborts += 1;
            i -= 1;
            continue;
        }

        exec(srcTx, "prepare transaction '%s'", gtid);
        exec(dstTx, "prepare transaction '%s'", gtid);
        exec(srcTx, "select dtm_begin_prepare('%s')", gtid);
        exec(dstTx, "select dtm_begin_prepare('%s')", gtid);
        csn_t csn = execQuery(srcTx, "select dtm_prepare('%s', 0)", gtid);
        csn = execQuery(dstTx, "select dtm_prepare('%s', %ld)", gtid, csn);
        exec(srcTx, "select dtm_end_prepare('%s', %ld)", gtid, csn);
        exec(dstTx, "select dtm_end_prepare('%s', %ld)", gtid, csn);
        exec(srcTx, "commit prepared '%s'", gtid);
        exec(dstTx, "commit prepared '%s'", gtid);
 
        time_t elapsed = getCurrentTime() - start;
        if (elapsed > t.max_trans_duration) { 
            t.max_trans_duration = elapsed;
        }
 
        t.proceeded += 1;
    }
    return NULL;
}
      
void initializeDatabase()
{
    for (size_t i = 0; i < cfg.connections.size(); i++) { 
        connection conn(cfg.connections[i]);
        work txn(conn);
        exec(txn, "drop extension if exists pg_dtm");
        exec(txn, "create extension pg_dtm");
        exec(txn, "drop table if exists t");
        exec(txn, "create table t(u int primary key, v int)");
        exec(txn, "insert into t (select generate_series(0,%d), %d)", cfg.nAccounts-1, 0);
        txn.commit();
    }        
}

int main (int argc, char* argv[])
{
    bool initialize = false;

    if (argc == 1){
        printf("Use -h to show usage options\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) { 
        if (argv[i][0] == '-') { 
            switch (argv[i][1]) { 
            case 'r':
                cfg.nReaders = atoi(argv[++i]);
                continue;
            case 'w':
                cfg.nWriters = atoi(argv[++i]);
                continue;                
            case 'a':
                cfg.nAccounts = atoi(argv[++i]);
                continue;
            case 'n':
                cfg.nIterations = atoi(argv[++i]);
                continue;
            case 's':
                cfg.startId = atoi(argv[++i]);
                continue;
            case 'd':
                cfg.diapason = atoi(argv[++i]);
                continue;
            case 'C':
            case 'c':
                cfg.connections.push_back(string(argv[++i]));
                continue;
            case 'f':
                cfg.deadlockFree = true;
                continue;
            case 'm':
                cfg.maxSnapshot = true;
                continue;
            case 'x':
                cfg.makeSavepoints = true;
                continue;
            case 'i':
                initialize = true;
                continue;
            }
        }
        printf("Options:\n"
               "\t-r N\tnumber of readers (1)\n"
               "\t-w N\tnumber of writers (10)\n"
               "\t-a N\tnumber of accounts (100000)\n"
               "\t-s N\tperform updates starting from this id (0)\n"
               "\t-d N\tperform updates in this diapason (#accounts)\n"
               "\t-n N\tnumber of iterations (1000)\n"
               "\t-c STR\tdatabase connection string\n"
               "\t-f\tavoid deadlocks by ordering accounts\n"
               "\t-m\tchoose maximal snapshot\n"
               "\t-x\tmake savepoints\n"
               "\t-i\tinitialize datanase\n");
        return 1;
    }

    if (cfg.startId + cfg.diapason > cfg.nAccounts) {
        cfg.diapason = cfg.nAccounts - cfg.startId;
        return 1;
    }

    if (initialize) { 
        initializeDatabase();
        printf("%d account inserted\n", cfg.nAccounts);
        return 0;
    }

    time_t start = getCurrentTime();
    running = true;

    vector<thread> readers(cfg.nReaders);
    vector<thread> writers(cfg.nWriters);
    size_t nReads = 0;
    size_t nWrites = 0;
    size_t nAborts = 0;
    time_t maxReadDuration = 0;
    time_t maxWriteDuration = 0;

    for (int i = 0; i < cfg.nReaders; i++) { 
        readers[i].start(i, reader);
    }
    for (int i = 0; i < cfg.nWriters; i++) { 
        writers[i].start(i, writer);
    }
    
    for (int i = 0; i < cfg.nWriters; i++) { 
        writers[i].wait();
        nWrites += writers[i].proceeded;
        nAborts += writers[i].aborts;
        if (writers[i].max_trans_duration > maxWriteDuration) { 
            maxWriteDuration = writers[i].max_trans_duration;
        }
    }
    
    running = false;

    for (int i = 0; i < cfg.nReaders; i++) { 
        readers[i].wait();
        nReads += readers[i].proceeded;
        if (readers[i].max_trans_duration > maxReadDuration) { 
            maxReadDuration = readers[i].max_trans_duration;
        }
     }
 
    time_t elapsed = getCurrentTime() - start;



    printf(
        "{\"update_tps\":%f, \"read_tps\":%f,"
        " \"readers\":%d, \"writers\":%d, \"aborts\":%ld, \"abort_percent\": %d,"
        " \"max_read_duration\":%ld, \"max_write_duration\":%ld,"
        " \"accounts\":%d, \"iterations\":%d, \"hosts\":%ld}\n",
        (double)(nWrites*USEC)/elapsed,
        (double)(nReads*USEC)/elapsed,
        cfg.nReaders,
        cfg.nWriters,
        nAborts,
        (int)(nAborts*100/nWrites),
        maxReadDuration, maxWriteDuration,
        cfg.nAccounts,
        cfg.nIterations,
        cfg.connections.size()
        );

    return 0;
}
