#ifndef SNTP_H
#define SNTP_H


/// @brief Sets settings for sntp
void sntp_mod_init(void);

/// @brief Fetches the current time from the ntp server
void fetch_current_time(void);

/// @brief Prints system time
void print_system_time(void);

#endif