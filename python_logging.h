///
/// Uses Python's logging module to log messages
///

#pragma once

void begin_allow_threads();

void end_allow_threads();

enum logtypes { log_info, log_warning, log_error, log_debug };

void log_msg(int type, char *format, ...);
