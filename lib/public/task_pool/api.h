#pragma once

#ifdef _WIN32
#    ifdef _MSC_VER
#        define TASKPOOL_EXPORT __declspec( dllexport )
#    else
#        define TASKPOOL_EXPORT __attribute__( ( dllexport ) )
#    endif
#else
#    define TASKPOOL_EXPORT __attribute__( ( visibility( "default" ) ) )
#endif

#ifdef _WIN32
#    ifdef _MSC_VER
#        define TASKPOOL_IMPORT __declspec( dllimport )
#    else
#        define TASKPOOL_IMPORT __attribute__( ( dllimport ) )
#    endif
#else
#    define TASKPOOL_IMPORT
#endif

#ifdef _WIN32
#    define TASKPOOL_HIDDEN
#else
#    define TASKPOOL_HIDDEN __attribute__( ( visibility( "hidden" ) ) )
#endif
#ifdef task_pool_EXPORTS
#    define TASK_POOL_EXPORTS
#endif
#ifdef TASK_POOL_EXPORTS
#    define TASKPOOL_API TASKPOOL_EXPORT
#else
#    define TASKPOOL_API TASKPOOL_IMPORT
#endif
