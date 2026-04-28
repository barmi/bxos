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
	int fh;

	api_cmdline(cmdline, 30);
	name = skip_token(cmdline);
	if (*name == 0) {
		api_putstr0("usage: touch.he2 <file>\n");
		api_end();
	}

	fh = api_fopen(name);
	if (fh != 0) {
		api_fclose(fh);
		api_end();
	}
	fh = api_fopen_w(name);
	if (fh == 0) {
		api_putstr0("touch failed.\n");
		api_end();
	}
	api_fclose(fh);
	api_end();
}
