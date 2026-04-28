#include "apilib.h"

static char *skip_token(char *p)
{
	while (*p > ' ') p++;
	while (*p == ' ') p++;
	return p;
}

void HariMain(void)
{
	char cmdline[30], *name;

	api_cmdline(cmdline, 30);
	name = skip_token(cmdline);
	if (*name == 0) {
		api_putstr0("usage: fdel.he2 <file>\n");
		api_end();
	}
	if (api_fdelete(name) != 0) {
		api_putstr0("delete failed.\n");
	}
	api_end();
}
