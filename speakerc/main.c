/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 Meindert Kempe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     1. Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *     2. Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *     3. Neither the name of the copyright holder nor the names of its
 *        contributors may be used to endorse or promote products derived from
 *        this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>

static CURL *curl;
static char err_buff[CURL_ERROR_SIZE];
static char *network_interface = NULL;
static char *network_address   = NULL;
static int interactive         = 0;
static char *command           = NULL;

static int init(void) {
	CURLcode res;
	res = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (res != CURLE_OK) {
		fprintf(stderr, "curl_global_init error: %s\n",
		        curl_easy_strerror(res));
		goto error;
	}
	curl = curl_easy_init();
	if (!curl) {
		fprintf(stderr, "curl_easy_init error\n");
		goto error;
	}

	res = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err_buff);
	if (res != CURLE_OK) {
		fprintf(stderr, "failed to set CURLOPT_ERRORBUFFER\n");
	}

	return 0;

error:
	curl_easy_cleanup(curl);
	curl_global_cleanup();
	return 1;
}

static int deinit(void) {
	curl_easy_cleanup(curl);
	curl_global_cleanup();
	return 0;
}

static int check_argc(int argc, char **argv, int i) {
	if (i >= argc - 1) {
		fprintf(stderr, "Missing argument for option: %s\n", argv[i]);
		return 0;
	}
	return 1;
}

static void help(void) {
	printf("Usage: speakerc [options...] <command>\n");
	printf("  -h Show help\n");
	printf("  -n Specify network interface\n");
	printf("  -a Specify speaker address\n");
	// printf("  -i\n");
}

int main(int argc, char **argv) {
	for (int i = 1; i < argc; ++i) {
		if (*argv[i] == '-') {
			switch (argv[i][1]) {
				case 'n':
					if (!check_argc(argc, argv, i)) return EXIT_FAILURE;
					++i;
					network_interface = argv[i];
					break;
				case 'a':
					if (!check_argc(argc, argv, i)) return EXIT_FAILURE;
					++i;
					network_address = argv[i];
					break;
				case 'h':
					help();
					return EXIT_SUCCESS;
					break;
				/* TODO: add interactive mode */
				case 'i': interactive = 1; break;
			}
			continue;
		}
		if (!command) command = argv[i];
	}

	if (init()) return EXIT_FAILURE;

	CURLcode res;

	if (!network_address) {
		fprintf(stderr, "missing network address (e.g. -a 192.168.178.10)\n");
		help();
		return EXIT_FAILURE;
	}
	if (!command) {
		fprintf(stderr, "missing command\n");
		help();
		return EXIT_FAILURE;
	}

	char *url = NULL;
	int url_size =
	    snprintf(url, 0, "http://%s/cmd/%s", network_address, command) + 1;
	url = malloc(url_size);
	snprintf(url, url_size, "http://%s/cmd/%s", network_address, command);

	res = curl_easy_setopt(curl, CURLOPT_URL, url);
	if (res != CURLE_OK) {
		fprintf(stderr, "error CURLOPT_URL (%d): %s\n", res,
		        curl_easy_strerror(res));
		fprintf(stderr, "error CURLOPT_URL %s\n", err_buff);
		return EXIT_FAILURE;
	}
	res = curl_easy_setopt(curl, CURLOPT_INTERFACE, network_interface);
	if (res != CURLE_OK) {
		fprintf(stderr, "error CURLOPT_INTERFACE (%d): %s\n", res,
		        curl_easy_strerror(res));
		fprintf(stderr, "error CURLOPT_INTERFACE %s\n", err_buff);
	}

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, "curl_easy_perform error: %s\n",
		        curl_easy_strerror(res));
		fprintf(stderr, "curl_easy_perform error: %s\n", err_buff);
	}

	/*long code;
	res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code); */

	free(url);

	deinit();

	return EXIT_SUCCESS;
}
