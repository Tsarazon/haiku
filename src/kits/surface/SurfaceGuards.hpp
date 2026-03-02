/*
 * Copyright 2025 KosmOS Project. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Simple RAII guards for Surface Kit resource cleanup.
 */
#ifndef _KOSM_SURFACE_GUARDS_HPP
#define _KOSM_SURFACE_GUARDS_HPP

#include <OS.h>


struct KosmAreaGuard {
	area_id id;

	KosmAreaGuard(area_id area)
		: id(area) {}

	~KosmAreaGuard()
	{
		if (id >= 0)
			delete_area(id);
	}

	void Release() { id = -1; }

private:
	KosmAreaGuard(const KosmAreaGuard&);
	KosmAreaGuard& operator=(const KosmAreaGuard&);
};


#endif
