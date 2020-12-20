#include "cachelab.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include <string.h>

int VERBOSE = 0;

typedef struct {
	int hits;
	int misses;
	int evictions;
} Result;

typedef struct {
	int s;
	int E;
	int b;
	const char *trace_file_path;
} Input;

typedef struct {
	int S;  // number of sets in the cache
	int s;  // number of set selector bits
	int E;  // number of cache lines per set
	int b;  // number of block offset bits
} Config;

typedef struct {
	int valid;
	unsigned long long tag;
} Line;

typedef struct {
	Line *lines;
	int E;
	int *lru_queue;
} Set;

typedef struct {
	Set *sets;
	int s;
	int S;
	int b;
} Cache;

int parse_int(char *str)
{
	char *end;
	errno = 0;
	long i = strtol(str, &end, 10);
	if (str == end)
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
	while ((opt = getopt(argc, argv, "+vs:E:b:t:")) != -1)
		switch (opt) {
		case 'v':
			VERBOSE = 1;
			break;
		case 's':
			if ((input->s = parse_int(optarg)) < 0)
				return -1;
			break;
		case 'E':
			if ((input->E = parse_int(optarg)) < 0)
				return -1;
			break;
		case 'b':
			if ((input->b = parse_int(optarg)) < 0)
				return -1;
			break;
		case 't':
			input->trace_file_path = optarg;
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
	return 0;
}

int init_lru_queue(Set *set, int E)
{
	if ((set->lru_queue = (int *) malloc(E * sizeof(int))) == NULL)
		return -1;
	for (int i = 0; i < E; ++i)
		set->lru_queue[i] = i;
	return 0;
}

int allocate_cache(Cache *cache, Config *config)
{
	cache->s = config->s;
	cache->S = config->S;
	cache->b = config->b;
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
		if ((init_lru_queue(&cache->sets[i], config->E)) == -1)
			return -1;
	}

	return 0;
}

int index_of(int *lru_queue, int line)
{
	int i = 0;
	while (*lru_queue != line) {
		lru_queue++;
		++i;
	}
	return i;
}

void update_lru_queue(int *lru_queue, int line, int len)
{
	int index = index_of(lru_queue, line);
	if (index == (len-1))
		return;
	int tmp = lru_queue[len-1];
	lru_queue[len-1] = lru_queue[index];
	while (index < len-2) {
		lru_queue[index] = lru_queue[index+1];
		++index;
	}
	lru_queue[len-2] = tmp;
}

void evict_lru(Set *set, unsigned long long tag)
{
	int line_index = set->lru_queue[0];
	set->lines[line_index].tag = tag;
	update_lru_queue(set->lru_queue, line_index, set->E);
}

int update(Set *set, unsigned long long tag)
{
	int added = 0;
	for (int i = 0; i < set->E; ++i) {
		if (set->lines[i].valid == 0) {
			set->lines[i].valid = 1;
			set->lines[i].tag = tag;
			added = 1;
			update_lru_queue(set->lru_queue, i, set->E);
			break;
		}
	}
	if (!added) {
		evict_lru(set, tag);
		return 1;
	}
	return 0;
}

void ref_mem(Cache *cache, unsigned long long address, Result *result)
{
	// don't need the b bits
	address >>= cache->b;
	int index = address & (pow2(cache->s)-1);
	unsigned long long tag = address >> cache->s;
	int hit = 0;
	for (int i = 0; i < cache->sets[index].E; ++i) {
		if (cache->sets[index].lines[i].valid &&
                    cache->sets[index].lines[i].tag == tag) {
			result->hits++;
			hit = 1;
			update_lru_queue(cache->sets[index].lru_queue, i, cache->sets[index].E);
			if (VERBOSE)
				printf("hit ");
			break;
		}
	}
	if (!hit) {
		result->misses++;
		if (VERBOSE)
			printf("miss ");
		int e = update(&cache->sets[index], tag);
		result->evictions += e;
		if (VERBOSE && e)
			printf("eviction ");
	}
}

int simulate(Cache *cache, Result *result, const char *trace_file_path)
{
	FILE *trace_file;
	if ((trace_file = fopen(trace_file_path, "r")) == NULL)
		return -1;

	// parsing the Valgrind memory trace (csapp.cs.cmu.edu/3e/cachelab.pdf page 2)
	const int STR_SIZE = 32;
	char line_str[STR_SIZE];
	while (fgets(line_str, STR_SIZE, trace_file) != NULL) {
		// can ignore instruction operations
		if (line_str[0] == 'I')
			continue;
		if (VERBOSE) {
			char *newline = strstr(line_str, "\n");
			*newline = '\0';
			printf("%s ", &line_str[1]);
		}
		char *end;
		unsigned long long address = strtoull(&line_str[3], &end, 16);
		if (line_str[1] == 'M') {
			ref_mem(cache, address, result);
			ref_mem(cache, address, result);
		} else
			ref_mem(cache, address, result);
		if (VERBOSE)
			printf("\n");
	}
	fclose(trace_file);
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
	// then derive S value
	Config config;
	if ((build_config(&config, &input)) == -1) {
		fprintf(stderr, "%s: error: input parameters are invalid.\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	// Cache = array of Set; Set = array of Line; Line = struct {int,int}
	Cache cache;
	if (allocate_cache(&cache, &config) == -1) {
		fprintf(stderr, "%s: error: failed to allocate cache structure.\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	Result result = {0, 0, 0};
	if (simulate(&cache, &result, input.trace_file_path) == -1) {
		fprintf(stderr, "%s: error: cache simulation failed.\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	printSummary(result.hits, result.misses, result.evictions);
	return 0;
}
