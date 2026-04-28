#include "apilib.h"

static char *skip_app(char *p)
{
	while (*p > ' ') p++;
	while (*p == ' ') p++;
	return p;
}

static int len(char *p)
{
	int n = 0;
	while (p[n] != 0) n++;
	return n;
}

void HariMain(void)
{
	char cmdline[30], *args, *gt, *text, *name;
	int i, n, fh;

	api_cmdline(cmdline, 30);
	args = skip_app(cmdline);
	if (*args == 0) {
		api_putstr0("usage: echo.he2 <text> > <file>\n");
		api_end();
	}

	gt = 0;
	for (i = 0; args[i] != 0; i++) {
		if (args[i] == '>') {
			gt = args + i;
			break;
		}
	}
	if (gt != 0) {
		name = gt + 1;
		while (*name == ' ') name++;
		while (gt > args && gt[-1] == ' ') gt--;
		*gt = 0;
		text = args;
	} else {
		name = args;
		while (*args > ' ') args++;
		if (*args != 0) *args++ = 0;
		while (*args == ' ') args++;
		text = args;
	}
	if (*name == 0) {
		api_putstr0("usage: echo.he2 <text> > <file>\n");
		api_end();
	}

	fh = api_fopen_w(name);
	if (fh == 0) {
		api_putstr0("echo failed.\n");
		api_end();
	}
	n = len(text);
	if (n > 0 && api_fwrite(text, n, fh) != n) {
		api_putstr0("echo failed.\n");
		api_fclose(fh);
		api_end();
	}
	api_fwrite("\n", 1, fh);
	api_fclose(fh);
	api_end();
}
