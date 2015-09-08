#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <stdbool.h>
#include "int.h"
#include "clog.h"
#include "snapshot.h"
#include "limits.h"

typedef struct Transaction {
	// true if the transaction was started on the node
	bool active;

	int node;
	int vote;

	xid_t xid;
	Snapshot snapshot;

	// if this is equal to seqno, we need to generate a new snapshot (for each node)
	int sent_seqno;
} Transaction;

typedef struct GlobalTransaction {
	Transaction participants[MAX_NODES];
} GlobalTransaction;

int global_transaction_status(GlobalTransaction *gt);
bool global_transaction_mark(clog_t clg, GlobalTransaction *gt, int status);

#endif
