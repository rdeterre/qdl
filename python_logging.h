///
/// Uses Python's logging module to log messages
///

#pragma once

enum logtypes { log_info, log_warning, log_error, log_debug };

void log_msg(int type, char *format, ...);
