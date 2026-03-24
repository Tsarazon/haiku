/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVICE_KEEPER_LIFECYCLE_H
#define DEVICE_KEEPER_LIFECYCLE_H

#include <SupportDefs.h>
#include <lock.h>
#include <util/DoublyLinkedList.h>


class DkMatcher;
class DkNode;
class DkPublisher;


struct DkDeferredEntry : DoublyLinkedListLinkImpl<DkDeferredEntry> {
	DkNode*		node;
	int32		retries;
};


class DkLifecycle {
public:
						DkLifecycle(DkMatcher& matcher);
						~DkLifecycle();

	void				SetPublisher(DkPublisher* publisher);

	// probe and attach best driver for node.
	// Handles KOSM_DEFER_PROBE: queues the node for later retry.
	status_t			ProbeAndAttach(DkNode* node);

	// retry all deferred nodes (called after each successful attach)
	void				RetryDeferred();

	// detach driver from node and all children (bottom-up)
	// also unpublishes all devices for affected nodes.
	// Called WITHOUT the tree lock held.
	void				Detach(DkNode* node);

	// rescan: ask driver to re-enumerate children
	status_t			Rescan(DkNode* node);

	// suspend subtree bottom-up (children first)
	status_t			Suspend(DkNode* node, int32 state);

	// resume subtree top-down (node first)
	status_t			Resume(DkNode* node);

	// notify device_removed callback on all published devices in subtree
	// (called before detach to unblock pending I/O on surprise removal)
	void				NotifyRemoved(DkNode* node);

private:
	static const int32	kMaxDeferRetries = 10;

	void				_DetachRecursive(DkNode* node);
	void				_NotifyRemovedRecursive(DkNode* node);
	void				_AddDeferred(DkNode* node);
	void				_RemoveDeferred(DkNode* node);

	DkMatcher&			fMatcher;
	DkPublisher*		fPublisher;

	typedef DoublyLinkedList<DkDeferredEntry> DeferredList;
	mutex				fDeferredLock;
	DeferredList		fDeferred;
};


#endif // DEVICE_KEEPER_LIFECYCLE_H
