/*
 * Copyright 2025, KosmOS Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVICE_KEEPER_PUBLISHER_H
#define DEVICE_KEEPER_PUBLISHER_H

#include <device_keeper.h>

#include "BaseDevice.h"


class DkNode;


// adapter: wraps dk_device_ops into BaseDevice for devfs
class DkPublishedDevice : public BaseDevice {
public:
						DkPublishedDevice(DkNode* node, dk_device_ops* ops);
	virtual				~DkPublishedDevice();

	status_t			InitCheck() const;

	DkNode*				GetNode() const { return fNode; }

	virtual	bool		HasSelect() const;
	virtual	bool		HasDeselect() const;
	virtual	bool		HasRead() const;
	virtual	bool		HasWrite() const;
	virtual	bool		HasIO() const;

	virtual	status_t	Open(const char* path, int openMode, void** _cookie);
	virtual	status_t	Read(void* cookie, off_t pos, void* buffer,
							size_t* _length);
	virtual	status_t	Write(void* cookie, off_t pos, const void* buffer,
							size_t* _length);
	virtual	status_t	IO(void* cookie, io_request* request);
	virtual	status_t	Control(void* cookie, int32 op, void* buffer,
							size_t length);
	virtual	status_t	Select(void* cookie, uint8 event, selectsync* sync);
	virtual	status_t	Deselect(void* cookie, uint8 event, selectsync* sync);
	virtual	status_t	Close(void* cookie);
	virtual	status_t	Free(void* cookie);

	virtual	status_t	InitDevice();
	virtual	void		UninitDevice();
	virtual	void		Removed();

	// hotplug: call dk_device_ops::device_removed
	virtual	void		NotifyRemoved();

private:
	DkNode*				fNode;
	dk_device_ops*		fOps;
};


class DkPublisher {
public:
						DkPublisher();
						~DkPublisher();

	status_t			Publish(DkNode* node, const char* path,
							dk_device_ops* ops);
	status_t			Unpublish(DkNode* node, const char* path);

	// unpublish all devices for a node using its tracked paths
	void				UnpublishAll(DkNode* node);

	// notify device_removed callback on all published devices for a node
	// (called before detach on hotplug removal to unblock pending I/O)
	void				NotifyDeviceRemoved(DkNode* node);
};


#endif // DEVICE_KEEPER_PUBLISHER_H
