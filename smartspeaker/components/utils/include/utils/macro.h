#ifndef UTILS_MACRO_H
#define UTILS_MACRO_H
#pragma once

#define ARRAY_SIZE(a) ((sizeof a) / (sizeof a[0]))

#define SEND_CMD(command, type, d, evt)                                        \
	audio_event_iface_sendout(evt, &(audio_event_iface_msg_t){                 \
	                                   .cmd         = command,                 \
	                                   .source_type = type,                    \
	                                   .data        = (void *)(d),             \
	                               });

#ifdef __GNUC__
#	define UNUSED __attribute__((__unused__))
#else
#	define UNUSED
#endif

#endif /* UTILS_MACRO_H */
