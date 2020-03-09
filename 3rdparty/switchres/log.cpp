#include "log.h"

void log_dummy(const char *, ...) {}

LOG_VERBOSE log_verbose = &log_dummy;
LOG_INFO log_info = &log_dummy;
LOG_ERROR log_error = &log_dummy;

void set_log_verbose(void *func_ptr)
{
	log_verbose = (LOG_VERBOSE)func_ptr;
}

void set_log_info(void *func_ptr)
{
	log_info = (LOG_INFO)func_ptr;
}

void set_log_error(void *func_ptr)
{
	log_error = (LOG_ERROR)func_ptr;
}
