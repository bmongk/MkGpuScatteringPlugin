#pragma once


#define MK_GPUSCATTERING_ONLY 1
#if MK_GPUSCATTERING_ONLY
	#if UE_BUILD_SHIPPING
		#ifndef MK_OPTIMIZATION_OFF
		#define MK_OPTIMIZATION_OFF
		#endif
		#ifndef MK_OPTIMIZATION_ON
		#define MK_OPTIMIZATION_ON
		#endif
		#ifndef MK_ENSURE_DEBUG
		#define MK_ENSURE_DEBUG(condition)
		#endif
	#else
		#ifndef MK_OPTIMIZATION_OFF
		#define MK_OPTIMIZATION_OFF UE_DISABLE_OPTIMIZATION
		#endif
		#ifndef MK_OPTIMIZATION_ON
		#define MK_OPTIMIZATION_ON UE_ENABLE_OPTIMIZATION
		#endif
		#ifndef MK_ENSURE_DEBUG
		#define MK_ENSURE_DEBUG(condition) ensure(condition)
		#endif
	#endif

	#ifndef MK_DEFINE_CONSOLE_REF
		#define MK_DEFINE_CONSOLE_REF(Variable, Command, Description)\
						FAutoConsoleVariableRef CVar_##Variable(TEXT(Command), Variable, TEXT(Description), ECVF_Default)
	#endif
#endif

