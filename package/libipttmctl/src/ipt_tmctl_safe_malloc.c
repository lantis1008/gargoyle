#include "ipt_tmctl.h"

void *ipt_tmctl_safe_malloc(size_t size)
{
	void* val = malloc(size);
	if(val == NULL)
	{
		fprintf(stderr, "ERROR: MALLOC FAILURE!\n");
		exit(1);
	}
	return val;
}

char* ipt_tmctl_safe_strdup(const char* str)
{
	char* new_str = strdup(str);
	if(new_str == NULL)
	{
		fprintf(stderr, "ERROR: MALLOC FAILURE!\n");
		exit(1);
	}
	return new_str;
}