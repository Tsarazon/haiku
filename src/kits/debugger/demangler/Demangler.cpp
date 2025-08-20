/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "Demangler.h"

#include <TypeConstants.h>

#include "demangle.h"


/*static*/ BString
Demangler::Demangle(const BString& mangledName)
{
	char buffer[1024];

	if (mangledName.Compare("_Z", 2) == 0) {
		// probably a gcc3+ mangled symbol
		const char* demangled = demangle_name_gcc3(mangledName.String(), buffer,
			sizeof(buffer));
		if (demangled != NULL)
			return demangled;
	}

	// name not mangled (GCC2 demangling removed)
	return mangledName;
}
