/**
 * @file general.cpp
 *
 * @brief Contains general static methods
 * @author Jan Wilmans <jw@dds.nl>  Copyright (C) 2009 Nowind
 *
 */

#define GEN_API_EXPORT
#include "general.h"
#include "Util.h"

// this method should be called at startup, before
// any other method in the general library is used.

void general::initialize()
{
	static bool lInitialized = false;
	if (lInitialized) return;
    Util::_startTicks();
	lInitialized = true;
}
