#ifndef __MULTIMASTER_H__
#define __MULTIMASTER_H__

#include "bytebuf.h"
#include "bgwpool.h"
#include "bkb.h"

#define MTM_INFO(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__) 
#define MTM_TRACE(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__) 
#define MTM_TUPLE_TRACE(fmt, ...)
/*
#define MTM_TRACE(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__) 
#define MTM_TUPLE_TRACE(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__) 
*/

#define MULTIMASTER_NAME                "multimaster"
#define MULTIMASTER_SCHEMA_NAME         "mtm"
#define MULTIMASTER_DDL_TABLE           "ddl_log"
#define MULTIMASTER_SLOT_PATTERN        "mtm_slot_%d"
#define MULTIMASTER_MIN_PROTO_VERSION   1
#define MULTIMASTER_MAX_PROTO_VERSION   1
#define MULTIMASTER_MAX_GID_SIZE        32

#define USEC 1000000

#define Natts_mtm_ddl_log 2
#define Anum_mtm_ddl_log_issued		1
#define Anum_mtm_ddl_log_query		2

typedef uint64 csn_t; /* commit serial number */
#define INVALID_CSN  ((csn_t)-1)

#define PGLOGICAL_COMMIT			0x00
#define PGLOGICAL_PREPARE			0x01
#define PGLOGICAL_COMMIT_PREPARED	0x02
#define PGLOGICAL_ABORT_PREPARED	0x03

#define PGLOGICAL_XACT_EVENT(flags)	(flags & 0x03)

typedef uint64 timestamp_t;

/* Identifier of global transaction */
typedef struct 
{
	int node;          /* Zero based index of node initiating transaction */
	TransactionId xid; /* Transaction ID at this node */
} GlobalTransactionId;

#define EQUAL_GTID(x,y) ((x).node == (y).node && (x).xid == (y).xid)

typedef enum
{ 
	MSG_INVALID,
	MSG_HANDSHAKE,
	MSG_PREPARED,
	MSG_ABORTED,
	MSG_STATUS
} MtmMessageCode;

typedef enum
{
	MTM_INITIALIZATION, /* Initial status */
	MTM_OFFLINE,        /* Node is out of quorum */
	MTM_CONNECTED,      /* Arbiter is established connections with other nodes */
	MTM_ONLINE,         /* Ready to receive client's queries */
	MTM_RECOVERY        /* Node is in recovery process */
} MtmNodeStatus;

typedef enum
{
	SLOT_CREATE_NEW,   /* create new slot (drop existed) */
	SLOT_OPEN_EXISTED, /* open existed slot */
	SLOT_OPEN_ALWAYS,  /* open existed slot or create new if noty exists */
} MtmSlotMode;

typedef struct MtmTransState
{
    TransactionId  xid;
    XidStatus      status; 
	GlobalTransactionId gtid;
    csn_t          csn;                /* commit serial number */
    csn_t          snapshot;           /* transaction snapshot, or INVALID_CSN for local transactions */
	int            nVotes;             /* number of votes received from replcas for this transaction: 
							              finally should be nNodes-1 */
	int            procno;             /* pgprocno of transaction coordinator waiting for responses from replicas, 
							              used to notify coordinator by arbiter */
	int            nSubxids;           /* Number of subtransanctions */
	struct MtmTransState* nextVoting;  /* Next element in L1-list of voting transactions. */
    struct MtmTransState* next;        /* Next element in L1 list of all finished transaction present in xid2state hash */
} MtmTransState;

typedef struct
{
	MtmNodeStatus status;              /* Status of this node */
	int recoverySlot;                  /* NodeId of recovery slot or 0 if none */
	volatile slock_t spinlock;         /* spinlock used to protect access to hash table */
	PGSemaphoreData votingSemaphore;   /* semaphore used to notify mtm-sender about new responses to coordinator */
	LWLockPadded *locks;               /* multimaster lock tranche */
	TransactionId oldestXid;           /* XID of oldest transaction visible by any active transaction (local or global) */
	nodemask_t disabledNodeMask;       /* bitmask of disabled nodes */
	nodemask_t connectivityMask;       /* bitmask of dicconnected nodes */
	nodemask_t pglogicalNodeMask;      /* bitmask of started pglogic receivers */
	nodemask_t walSenderLockerMask;    /* Mask of WAL-senders IDs locking the cluster */
	nodemask_t nodeLockerMask;         /* Mask of node IDs which WAL-senders are locking the cluster */
    int    nNodes;                     /* number of active nodes */
    int    nReceivers;                 /* number of initialized logical receivers (used to determine moment when Mtm intialization is completed */
	int    nLockers;                   /* number of lockers */
	long   timeShift;                  /* local time correction */
	csn_t  csn;                        /* last obtained CSN: used to provide unique acending CSNs based on system time */
	MtmTransState* votingTransactions; /* L1-list of replicated transactions sendings notifications to coordinator.
									 	 This list is used to pass information to mtm-sender BGW */
    MtmTransState* transListHead;      /* L1 list of all finished transactions present in xid2state hash.
									 	  It is cleanup by MtmGetOldestXmin */
    MtmTransState** transListTail;     /* Tail of L1 list of all finished transactionds, used to append new elements.
								  		  This list is expected to be in CSN ascending order, by strict order may be violated */
	uint64 transCount;                 /* Counter of transactions perfromed by this node */
	time_t nodeTransDelay[MAX_NODES];  /* Time of waiting transaction acknowledgment from node */
    BgwPool pool;                      /* Pool of background workers for applying logical replication patches */
} MtmState;

#define MtmIsCoordinator(ts) (ts->gtid.node == MtmNodeId)

extern char const* const MtmNodeStatusMnem[];

extern char* MtmConnStrs;
extern int   MtmNodeId;
extern int   MtmNodes;
extern int   MtmArbiterPort;
extern char* MtmDatabaseName;
extern int   MtmConnectAttempts;
extern int   MtmConnectTimeout;
extern int   MtmReconnectAttempts;
extern int   MtmKeepaliveTimeout;
extern HTAB* MtmXid2State;

extern void  MtmArbiterInitialize(void);
extern int   MtmStartReceivers(char* nodes, int nodeId);
extern csn_t MtmTransactionSnapshot(TransactionId xid);
extern csn_t MtmAssignCSN(void);
extern csn_t MtmSyncClock(csn_t csn);
extern void  MtmJoinTransaction(GlobalTransactionId* gtid, csn_t snapshot);
extern void  MtmReceiverStarted(int nodeId);
extern MtmSlotMode MtmReceiverSlotMode(int nodeId);
extern void  MtmExecute(void* work, int size);
extern void  MtmExecutor(int id, void* work, size_t size);
extern void  MtmSendNotificationMessage(MtmTransState* ts);
extern void  MtmAdjustSubtransactions(MtmTransState* ts);
extern void  MtmLock(LWLockMode mode);
extern void  MtmUnlock(void);
extern void  MtmDropNode(int nodeId, bool dropSlot);
extern void  MtmRecoverNode(int nodeId);
extern void  MtmOnNodeDisconnect(int nodeId);
extern void  MtmOnNodeConnect(int nodeId);
extern MtmState* MtmGetState(void);
extern timestamp_t MtmGetCurrentTime(void);
extern void  MtmSleep(timestamp_t interval);
extern void  MtmSetCurrentTransactionGID(char const* gid);
extern void  MtmSetCurrentTransactionCSN(csn_t csn);
extern csn_t MtmGetTransactionCSN(TransactionId xid);
extern TransactionId MtmGetCurrentTransactionId(void);
extern bool  MtmIsRecoveredNode(int nodeId);
extern void  MtmRefreshClusterStatus(bool nowait);
extern void  MtmSwitchClusterMode(MtmNodeStatus mode);
#endif
