#include "cachelab.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include <string.h>

typedef struct {
	int s;
	int E;
	int b;
	char *file_name;
} Input;

typedef struct {
	int S;  // number of sets in the cache
	int s;  // number of set selector bits
	int E;  // number of cache lines per set
	int B;  // number of bytes per block
	int b;  // number of block offset bits
} Config;

typedef struct {
	int valid;
	int tag;
	int offset;
} Line;

typedef struct {
	Line *lines;
	int E;
} Set;

typedef struct {
	Set *sets;
	int S;
} Cache;

int parse_arg(char *arg)
{
	char *end;
	errno = 0;
	long i = strtol(arg, &end, 10);
	if (arg == end)
		return -1;
	if (errno != 0 && i == 0)
		return -1;
	if (i <= INT_MAX && i >= INT_MIN)
		return (int) i;
	return -1;
}

int parse_input(Input *input, int argc, char *argv[])
{
	opterr = 0;
	char opt;
	while ((opt = getopt(argc, argv, "+s:E:b:t:")) != -1)
		switch (opt) {
		case 's':
			if ((input->s = parse_arg(optarg)) < 0)
				return -1;
			break;
		case 'E':
			if ((input->E = parse_arg(optarg)) < 0)
				return -1;
			break;
		case 'b':
			if ((input->b = parse_arg(optarg)) < 0)
				return -1;
			break;
		case 't':
			input->file_name = optarg;
			break;
		default:
			return -1;
		}
	return 0;
}

int pow2(int i)
{
	if (i > 30)
		return -1;
	return 1<<i;
}

int build_config(Config *config, Input *input)
{
	config->s = input->s;
	config->E = input->E;
	config->b = input->b;
	if ((config->S = pow2(input->s)) == -1)
		return -1;
	if ((config->B = pow2(input->b)) == -1)
		return -1;
	return 0;
}

int allocate_cache(Cache *cache, Config *config)
{
	cache->S = config->S;
	// allocate array of sets
	if ((cache->sets = (Set *) malloc(config->S * sizeof(Set))) == NULL)
		return -1;

	for (int i = 0; i < config->S; ++i) {
		// allocate array of lines for each set
		if ((cache->sets[i].lines = (Line *) malloc(config->E * sizeof(Line))) == NULL)
			return -1;
		cache->sets[i].E = config->E;
		// set valid bits to 0 for each line
		for (int j = 0; j < config->E; ++j)
			cache->sets[i].lines[j].valid = 0;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	// user supplies 3 cache parameters and a memory trace file
	Input input;
	if ((parse_input(&input, argc, argv)) == -1) {
		fprintf(stderr, "usage: %s -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	// build the Config object with s, E, and b values from user input
	// then derive S and B values
	Config config;
	if ((build_config(&config, &input)) == -1) {
		fprintf(stderr,
			"%s: error: input parameters are invalid.\n",
			argv[0]);
		exit(EXIT_FAILURE);
	}

	Cache cache;
	allocate_cache(&cache, &config);

	printSummary(0, 0, 0);
	return 0;
}
