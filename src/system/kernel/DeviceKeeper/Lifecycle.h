/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVICE_KEEPER_LIFECYCLE_H
#define DEVICE_KEEPER_LIFECYCLE_H

#include <SupportDefs.h>
#include <KernelExport.h>
#include <lock.h>
#include <util/DoublyLinkedList.h>


class DkMatcher;
class DkModuleRef;
class DkNode;
class DkPublisher;


struct DkDeferredEntry : DoublyLinkedListLinkImpl<DkDeferredEntry> {
	DkNode*		node;
	int32		retries;
	bigtime_t	enqueue_time;	// watchdog: warn if stuck for too long
};


class DkLifecycle {
public:
						DkLifecycle(DkMatcher& matcher);
						~DkLifecycle();

	void				SetPublisher(DkPublisher* publisher);

	// probe and attach best driver for node.
	// Handles KOSM_DEFER_PROBE: queues the node for later retry.
	status_t			ProbeAndAttach(DkNode* node);

	// probe ALL matching drivers for a node with
	// KOSM_FIND_MULTIPLE_CHILDREN. Each matching driver gets
	// attach() called — it registers its own child node(s).
	status_t			ProbeAndAttachAll(DkNode* node);

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

	// Unified attach path: takes ownership of module ref in _ref,
	// calls driver->attach() without any DK locks held, updates node
	// state atomically, and handles KOSM_DEFER_PROBE.
	//
	// On B_OK:   node owns the module ref (driver set, cookie set).
	// On error:  module ref released via DkModuleRef dtor.
	// On KOSM_DEFER_PROBE: node queued in deferred list, ref released
	//            (a fresh ref will be taken on retry).
	//
	// The caller is responsible for setting fAttaching=true before
	// calling this helper, and the helper clears it.
	status_t			_AttachDriverToNode(DkNode* node,
							DkModuleRef& _ref);

public:
	// Called from DkKeeper::_PostRegisterProbe to perform auto-attach-
	// by-name (when a node's module itself is a dk_driver_info).
	// Takes ownership of _ref on success. Exposed so that the keeper
	// can share the same attach state machine as ProbeAndAttach.
	status_t			AutoAttachByName(DkNode* node,
							DkModuleRef& _ref);

private:
	static const int32	kMaxDeferRetries = 10;
	// Warn if a deferred entry sits in the queue longer than this
	// (in microseconds). 60 seconds — long enough to avoid noise on
	// slow boot, short enough to catch real stuck probes.
	static const bigtime_t kDeferWatchdogTimeout = 60 * 1000000LL;

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
