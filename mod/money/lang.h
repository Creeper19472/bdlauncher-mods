#pragma once
#include <lang_detect.h>
#if LANG==CN
	#include "./lang/LANG_CN.h"
#else
	#include "./lang/LANG_EN.h"
#endif