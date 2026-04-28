#include "apilib.h"

void HariMain(void)
{
	char buf[128];
	int n;

	n = api_getcwd(buf, sizeof buf);
	if (n < 0) {
		api_putstr0("pwd failed.\n");
		api_end();
	}
	api_putstr0(buf);
	api_putchar('\n');
	api_end();
}
